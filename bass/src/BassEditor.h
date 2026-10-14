#pragma once

#include "BassProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkbass
{
using tonwerkui::Section;

/** The scope over the panels, its caption naming the note and whether it is accented or sliding. */
class BassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassEditor(BassProcessor&);
    ~BassEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    BassProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    tonwerkui::ScopeView scope;
    Section oscillator, filter;
    Section accent, envelope, output;
    Section lfo, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset, shownCaption;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEditor)
};
} // namespace tonwerkbass
