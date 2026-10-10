#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>
#include <vector>

namespace tonwerkui
{
/**
 * An effect's factory sound: the parameters it changes from their defaults, in the parameters' own units (percent as
 * 0 to 1, decibels, milliseconds, semitones, menus as the index of their entry). Names and groups are UI strings, UTF-8.
 */
struct EffectPreset
{
    const char* name;
    /** The menu's group; empty for Init, which comes first. */
    const char* category;
    std::vector<std::pair<const char*, float>> values;
};

/**
 * The factory sounds of an effect as the host's programs. Choosing one sets every parameter to its default first, so a
 * sound only names what it changes; its name is kept in the state, so a project shows it again when opened.
 */
class EffectPrograms
{
public:
    EffectPrograms(juce::AudioProcessorValueTreeState& state, const std::vector<EffectPreset>& presets);

    int count() const { return (int) presets.size(); }
    /** The chosen sound's program number, found by its name after a project was loaded. */
    int current() const;
    void choose(int index);
    juce::String name(int index) const;
    /** The sound last chosen, also after a project was loaded. */
    juce::String chosenName() const;

private:
    juce::AudioProcessorValueTreeState& state;
    const std::vector<EffectPreset>& presets;
    int currentIndex = 0;
};

/**
 * The preset menu with arrows either side, for an effect's header: the sounds under their groups' headings in `menu`'s
 * order, which the arrows step through. Follows a program the host chose.
 */
class PresetBar : public juce::Component, private juce::Timer
{
public:
    /** `menu` lists the program numbers in the order the menu shows them. */
    PresetBar(juce::AudioProcessor& processor, EffectPrograms& programs, const std::vector<EffectPreset>& presets,
              std::vector<int> menu);
    void resized() override;

private:
    void timerCallback() override;
    void choose(int index);

    juce::AudioProcessor& processor;
    EffectPrograms& programs;
    std::vector<int> menu;
    juce::ComboBox box;
    juce::TextButton previous { "<" }, next { ">" };
    juce::String shown;
};
} // namespace tonwerkui
