#pragma once

#include "Compact.h"
#include "NeuroProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkneuro
{
using tonwerkui::Section;

/**
 * The same controls on pages for a phone's screen (`tonwerkui::CompactEditor`), at most ten places a page; no display,
 * since the app's own scope beside the keys stands in for the editor's.
 */
std::vector<tonwerkui::CompactPage> neuroPages();

/** The scope over the panels, its caption naming the note and the cutoff the filter stands at. */
class NeuroEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeuroEditor(NeuroProcessor&);
    ~NeuroEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    NeuroProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    tonwerkui::ScopeView scope;
    Section oscillator, fm, filter;
    Section ampEnvelope, modEnvelope, output;
    Section lfo, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset, shownCaption;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuroEditor)
};
} // namespace tonwerkneuro
