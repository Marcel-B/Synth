#include "MidiLearn.h"

#include <map>

namespace tonwerkui
{
namespace
{
const juce::Identifier kMap { "MidiMap" };

juce::Identifier propertyFor(int controller) { return juce::Identifier("cc" + juce::String(controller)); }

/** Which instance works on which state, so a knob in the editor finds the plugin's MIDI learn. */
struct Registry
{
    juce::CriticalSection lock;
    std::map<const juce::AudioProcessorValueTreeState*, MidiLearn*> instances;
};

Registry& registry()
{
    static Registry r;
    return r;
}
} // namespace

MidiLearn::MidiLearn(juce::AudioProcessorValueTreeState& s) : state(s)
{
    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            parameters.push_back(ranged);
            ids.add(ranged->getParameterID());
        }
    for (auto& t : target)
        t.store(-1);
    rebuild();
    state.state.addListener(this);
    const juce::ScopedLock locked(registry().lock);
    registry().instances[&state] = this;
}

MidiLearn::~MidiLearn()
{
    {
        const juce::ScopedLock locked(registry().lock);
        registry().instances.erase(&state);
    }
    stopTimer();
    state.state.removeListener(this);
}

MidiLearn* MidiLearn::of(const juce::AudioProcessorValueTreeState& s)
{
    const juce::ScopedLock locked(registry().lock);
    const auto found = registry().instances.find(&s);
    return found != registry().instances.end() ? found->second : nullptr;
}

void MidiLearn::process(const juce::MidiBuffer& midi)
{
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        if (! message.isController())
            continue;
        const int controller = message.getControllerNumber();
        const int value = message.getControllerValue();

        // Learning takes the first controller that may move a knob: not a fine half, not a channel mode message.
        if (const int learner = learnIndex.load(std::memory_order_relaxed);
            learner >= 0 && controller <= kLastLearnable && (controller < 32 || controller >= 64))
        {
            for (auto& t : target)
                if (t.load(std::memory_order_relaxed) == learner)
                    t.store(-1, std::memory_order_relaxed);
            target[(std::size_t) controller].store(learner, std::memory_order_relaxed);
            learntIndex.store(learner, std::memory_order_relaxed);
            learntController.store(controller, std::memory_order_release);
            learnIndex.store(-1, std::memory_order_relaxed);
        }

        float normalised = -1.0f;
        int index = target[(std::size_t) controller].load(std::memory_order_relaxed);
        if (index >= 0)
        {
            if (controller < 32)
            {
                coarse[(std::size_t) controller] = value;
                // With a fine half to come, the coarse one is the upper seven bits; without, it spans the whole knob.
                normalised = fine[(std::size_t) controller] ? (float) (value * 128) / 16383.0f : (float) value / 127.0f;
            }
            else
                normalised = (float) value / 127.0f;
        }
        else if (controller >= 32 && controller < 64)
        {
            const int pair = controller - 32;
            index = target[(std::size_t) pair].load(std::memory_order_relaxed);
            if (index >= 0)
            {
                fine[(std::size_t) pair] = true;
                normalised = (float) (coarse[(std::size_t) pair] * 128 + value) / 16383.0f;
            }
        }
        if (index < 0 || index >= (int) parameters.size() || normalised < 0.0f)
            continue;
        auto* parameter = parameters[(std::size_t) index];
        if (std::abs(parameter->getValue() - normalised) > 1.0e-6f)
            parameter->setValueNotifyingHost(normalised);
    }
}

void MidiLearn::learn(const juce::String& parameterId)
{
    learnIndex.store(ids.indexOf(parameterId));
    if (learnIndex.load() >= 0)
        startTimerHz(10);
    else
        stopTimer();
    sendChangeMessage();
}

juce::String MidiLearn::learning() const
{
    const int index = learnIndex.load();
    return index >= 0 ? ids[index] : juce::String();
}

void MidiLearn::forget(const juce::String& parameterId)
{
    auto tree = map();
    for (int i = tree.getNumProperties(); --i >= 0;)
    {
        const auto name = tree.getPropertyName(i);
        if (tree[name].toString() == parameterId)
            tree.removeProperty(name, nullptr);
    }
}

int MidiLearn::controllerFor(const juce::String& parameterId) const
{
    const int index = ids.indexOf(parameterId);
    if (index < 0)
        return -1;
    for (int c = 0; c < (int) target.size(); ++c)
        if (target[(std::size_t) c].load() == index)
            return c;
    return -1;
}

void MidiLearn::assign(int controller, const juce::String& parameterId)
{
    if (controller < 0 || controller > kLastLearnable || ! ids.contains(parameterId))
        return;
    forget(parameterId);
    map().setProperty(propertyFor(controller), parameterId, nullptr);
}

void MidiLearn::commitLearnt()
{
    const int controller = learntController.exchange(-1, std::memory_order_acquire);
    if (controller >= 0)
    {
        const int index = learntIndex.load(std::memory_order_relaxed);
        if (index >= 0 && index < ids.size())
            assign(controller, ids[index]);
    }
    if (learnIndex.load() < 0)
    {
        stopTimer();
        sendChangeMessage();
    }
}

juce::ValueTree MidiLearn::map() { return state.state.getOrCreateChildWithName(kMap, nullptr); }

void MidiLearn::rebuild()
{
    const juce::ScopedLock locked(rebuilding);
    std::array<int, 128> fresh {};
    fresh.fill(-1);
    const auto tree = state.state.getChildWithName(kMap);
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName(i).toString();
        const int controller = name.startsWith("cc") ? name.substring(2).getIntValue() : -1;
        const int index = ids.indexOf(tree[tree.getPropertyName(i)].toString());
        if (controller >= 0 && controller <= kLastLearnable && index >= 0)
            fresh[(std::size_t) controller] = index;
    }
    for (std::size_t c = 0; c < fresh.size(); ++c)
        target[c].store(fresh[c], std::memory_order_relaxed);
    // Asynchronous, so a project loaded on another thread tells the knobs on the message thread.
    sendChangeMessage();
}

void MidiLearn::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType(kMap))
        rebuild();
}

void MidiLearn::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType(kMap))
        rebuild();
}

void MidiLearn::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType(kMap))
        rebuild();
}
} // namespace tonwerkui
