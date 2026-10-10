#include "EffectPresets.h"

namespace tonwerkui
{
namespace
{
const juce::Identifier kPresetName { "presetName" };
} // namespace

EffectPrograms::EffectPrograms(juce::AudioProcessorValueTreeState& s, const std::vector<EffectPreset>& list)
    : state(s), presets(list)
{
    if (! presets.empty())
        state.state.setProperty(kPresetName, juce::String::fromUTF8(presets.front().name), nullptr);
}

void EffectPrograms::choose(int index)
{
    if (! juce::isPositiveAndBelow(index, count()))
        return;
    currentIndex = index;
    const auto& preset = presets[(std::size_t) index];
    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    for (const auto& [parameterId, value] : preset.values)
        if (auto* parameter = state.getParameter(parameterId))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse; // a preset names a parameter that does not exist
    state.state.setProperty(kPresetName, juce::String::fromUTF8(preset.name), nullptr);
}

int EffectPrograms::current() const
{
    const auto chosen = chosenName();
    if (name(currentIndex) == chosen)
        return currentIndex;
    for (int i = 0; i < count(); ++i)
        if (name(i) == chosen)
            return i;
    return currentIndex;
}

juce::String EffectPrograms::name(int index) const
{
    return juce::isPositiveAndBelow(index, count()) ? juce::String::fromUTF8(presets[(std::size_t) index].name)
                                                     : juce::String();
}

juce::String EffectPrograms::chosenName() const { return state.state.getProperty(kPresetName, {}).toString(); }

PresetBar::PresetBar(juce::AudioProcessor& p, EffectPrograms& list, const std::vector<EffectPreset>& presets,
                     std::vector<int> order)
    : processor(p), programs(list), menu(std::move(order))
{
    // Grouped under headings; an item's id stays its program number + 1.
    const char* group = "";
    for (const int i : menu)
    {
        const auto& preset = presets[(std::size_t) i];
        if (std::strcmp(preset.category, group) != 0 && *preset.category != 0)
            box.addSectionHeading(juce::String::fromUTF8(preset.category));
        group = preset.category;
        box.addItem(juce::String::fromUTF8(preset.name), i + 1);
    }
    box.setTextWhenNothingSelected("Preset");
    box.onChange = [this] {
        if (box.getSelectedId() > 0)
            choose(box.getSelectedId() - 1);
    };
    // The arrows step through the menu as it reads, group by group.
    auto step = [this](int direction) {
        const auto at = std::find(menu.begin(), menu.end(), processor.getCurrentProgram());
        const auto from = at == menu.end() ? -1 : (int) std::distance(menu.begin(), at);
        const int to = from + direction;
        if (juce::isPositiveAndBelow(to, (int) menu.size()))
            choose(menu[(std::size_t) to]);
    };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
    for (auto* c : std::initializer_list<juce::Component*> { &previous, &box, &next })
        addAndMakeVisible(*c);
    timerCallback();
    startTimerHz(10);
}

void PresetBar::choose(int index)
{
    processor.setCurrentProgram(index);
    box.setSelectedId(index + 1, juce::dontSendNotification);
    shown = programs.chosenName();
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous.setBounds(area.removeFromLeft(28));
    next.setBounds(area.removeFromRight(28));
    box.setBounds(area.reduced(4, 0));
}

void PresetBar::timerCallback()
{
    // The host may choose a program, or a project bring its sound back.
    if (programs.chosenName() == shown)
        return;
    shown = programs.chosenName();
    box.setSelectedId(0, juce::dontSendNotification);
    for (int i = 0; i < box.getNumItems(); ++i)
        if (box.getItemText(i) == shown)
            box.setSelectedId(box.getItemId(i), juce::dontSendNotification);
    if (box.getSelectedId() == 0)
        box.setText(shown, juce::dontSendNotification);
}
} // namespace tonwerkui
