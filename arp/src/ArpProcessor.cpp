#include "ArpProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "ArpEditor.h"
#include "ArpPresets.h"

namespace tonwerkarp
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;

const juce::StringArray kNoteNames { "C", "Cis", "D", "Dis", "E", "F", "Fis", "G", "Gis", "A", "B", "H" };

std::unique_ptr<juce::AudioParameterFloat> number(const juce::String& id, const juce::String& name,
                                                  juce::NormalisableRange<float> range, float initial, const char* unit,
                                                  float shown = 1.0f)
{
    const juce::String suffix = juce::String(" ") + juce::String::fromUTF8(unit);
    auto toText = [suffix, shown](float value, int) { return juce::String(juce::roundToInt(value * shown)) + suffix; };
    auto fromText = [shown](const juce::String& text) { return text.getFloatValue() / shown; };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText));
}

std::unique_ptr<juce::AudioParameterInt> whole(const juce::String& id, const juce::String& name, int low, int high,
                                               int initial, const char* unit)
{
    const juce::String suffix = *unit == 0 ? juce::String() : juce::String(" ") + juce::String::fromUTF8(unit);
    return std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), low, high, initial,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction(
                tonwerkui::translated([suffix](int value, int) { return juce::String(value) + suffix; }))
            .withValueFromStringFunction([](const juce::String& text) { return text.getIntValue(); }));
}

std::unique_ptr<juce::AudioParameterChoice> menu(const char* id, const char* name, juce::StringArray items, int initial)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 },
                                                        tonwerkui::tr(juce::String::fromUTF8(name)), tonwerkui::tr(items),
                                                        initial);
}

std::unique_ptr<juce::AudioParameterBool> toggle(const juce::String& id, const juce::String& name, bool initial)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial);
}

juce::StringArray utf8Items(std::initializer_list<const char*> items)
{
    juce::StringArray out;
    for (const auto* item : items)
        out.add(juce::String::fromUTF8(item));
    return out;
}
} // namespace

juce::String noteName(int midi)
{
    if (midi < 0 || midi > 127)
        return "-";
    return kNoteNames[midi % 12] + juce::String(midi / 12 - 2);
}

juce::String ArpProcessor::stepId(int step, const char* field) { return "step" + juce::String(step + 1) + field; }

juce::AudioProcessorValueTreeState::ParameterLayout ArpProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(menu("mode", "Modus", { "Arpeggiator", "Sequencer" }, 0));
    layout.add(menu("order", "Reihenfolge",
                    utf8Items({ "Hoch", "Runter", "Hoch-Runter", "Runter-Hoch", "Gespielt", "Zufall" }), 0));
    layout.add(whole("octaves", "Oktaven", 1, 4, 1, "Okt"));
    // T for triplets, a dot for dotted notes, as Logic writes them.
    layout.add(menu("division", "Notenwert",
                    { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/4.", "1/8.", "1/16." },
                    4));
    layout.add(number("gate", juce::String::fromUTF8("Notenlänge"), { 0.05f, 2.0f }, 0.5f, "%", 100.0f));
    layout.add(number("swing", "Swing", { 0.5f, 0.75f }, 0.5f, "%", 100.0f));
    layout.add(whole("length", juce::String::fromUTF8("Länge"), 1, kSteps, kSteps, "Schritte"));
    layout.add(toggle("latch", "Halten", false));
    layout.add(menu("velocitySource", "Anschlag", { "Gespielt", "Fest" }, 0));
    layout.add(whole("fixedVelocity", "Fester Anschlag", 1, 127, 100, ""));
    layout.add(whole("transpose", "Transponieren", -24, 24, 0, "HT"));
    for (int i = 0; i < kSteps; ++i)
    {
        const auto name = "Schritt " + juce::String(i + 1) + " ";
        layout.add(toggle(stepId(i, "On"), name + "an", true));
        layout.add(whole(stepId(i, "Note"), name + "Tonh" + juce::String::fromUTF8("ö") + "he", -24, 24, 0, "HT"));
        layout.add(number(stepId(i, "Velocity"), name + "Anschlag", { 0.0f, 1.0f }, 1.0f, "%", 100.0f));
        layout.add(toggle(stepId(i, "Tie"), name + "gebunden", false));
    }
    return layout;
}

ArpProcessor::ArpProcessor()
    : AudioProcessor(BusesProperties()), // a MIDI effect: no audio in or out
      state(*this, nullptr, "TonwerkArp", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const juce::String& id) { return state.getRawParameterValue(id); };
    values.mode = get("mode");
    values.order = get("order");
    values.octaves = get("octaves");
    values.division = get("division");
    values.gate = get("gate");
    values.swing = get("swing");
    values.length = get("length");
    values.latch = get("latch");
    values.velocitySource = get("velocitySource");
    values.fixedVelocity = get("fixedVelocity");
    values.transpose = get("transpose");
    for (int i = 0; i < kSteps; ++i)
        values.steps[(std::size_t) i] = { get(stepId(i, "On")), get(stepId(i, "Note")), get(stepId(i, "Velocity")),
                                          get(stepId(i, "Tie")) };
    events.reserve(1024);
}

Settings ArpProcessor::readSettings() const
{
    auto index = [](const std::atomic<float>* value) { return juce::roundToInt(value->load()); };
    Settings s;
    s.mode = (Mode) index(values.mode);
    s.order = (Order) index(values.order);
    s.octaves = index(values.octaves);
    s.division = (Division) index(values.division);
    s.gate = values.gate->load();
    s.swing = values.swing->load();
    s.length = index(values.length);
    s.latch = values.latch->load() > 0.5f;
    s.velocitySource = (VelocitySource) index(values.velocitySource);
    s.fixedVelocity = index(values.fixedVelocity);
    s.transpose = index(values.transpose);
    for (std::size_t i = 0; i < (std::size_t) kSteps; ++i)
    {
        const auto& v = values.steps[i];
        s.steps[i] = { v.on->load() > 0.5f, index(v.note), v.velocity->load(), v.tie->load() > 0.5f };
    }
    return s;
}

void ArpProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    engine.update(readSettings());
    out.ensureSize(4096);
}

bool ArpProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
}

void ArpProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midiLearn.process(midi);
    const int count = buffer.getNumSamples();
    buffer.clear();
    engine.update(readSettings());

    Position position;
    if (auto* head = getPlayHead())
        if (const auto info = head->getPosition())
        {
            if (const auto bpm = info->getBpm())
                position.bpm = *bpm;
            // Without a position there is no grid to follow: run free as when stopped.
            if (const auto ppq = info->getPpqPosition(); ppq && info->getIsPlaying())
            {
                position.playing = true;
                position.ppq = *ppq;
            }
        }

    out.clear();
    auto emit = [this](int offset, int note, int velocity, int channel) {
        out.addEvent(velocity > 0 ? juce::MidiMessage::noteOn(channel, note, (juce::uint8) velocity)
                                  : juce::MidiMessage::noteOff(channel, note),
                     offset);
    };

    // The keys go to the arpeggiator; everything else (controllers, pitch bend, ...) passes at its place.
    events.clear();
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        const int offset = std::clamp(event.samplePosition, 0, std::max(0, count - 1));
        if (message.isNoteOn())
        {
            if (events.size() < events.capacity())
                events.push_back({ offset, message.getNoteNumber(), message.getVelocity(), message.getChannel() });
        }
        else if (message.isNoteOff())
        {
            if (events.size() < events.capacity())
                events.push_back({ offset, message.getNoteNumber(), 0, message.getChannel() });
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            // The arpeggiator stops its notes and forgets its keys at that sample, latched ones too.
            if (events.size() < events.capacity())
                events.push_back({ offset, -1, 0, message.getChannel() });
            out.addEvent(message, event.samplePosition);
        }
        else
            out.addEvent(message, event.samplePosition);
    }
    engine.process(count, position, events.data(), (int) events.size(), emit);
    midi.swapWith(out);

    playingStep = engine.currentStep();
    lastNote = engine.lastNote();
    const int held = std::min(engine.heldCount(), (int) keys.size());
    for (int i = 0; i < held; ++i)
        keys[(std::size_t) i] = engine.heldNote(i);
    keyCount = held;
}

juce::AudioProcessorEditor* ArpProcessor::createEditor() { return tonwerkui::forHost(new ArpEditor(*this)); }

void ArpProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ArpProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkarp

#ifndef TONWERK_ARP_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkarp::ArpProcessor();
}
#endif
