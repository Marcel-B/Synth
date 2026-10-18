#pragma once

#include "ExpanderProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkexpander
{
using tonwerkui::Section;

/**
 * The filter's response as the knobs set it (cyan, dim) and where the newest voice's envelope has moved it now
 * (yellow): turning the mode knob shows the lowpass bending through the notch into the highpass.
 */
class ResponseView : public juce::Component, private juce::Timer
{
public:
    explicit ResponseView(ExpanderProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The filter's gain at `hz` with the cutoff at `cutoff`, in dB (the analog prototype; enough for a picture). */
    static float responseDb(const Settings& s, float cutoff, float hz);

private:
    void timerCallback() override { refresh(); }

    ExpanderProcessor& processor;
    Settings shown;
    float cutoffNow = 1200.0f;
    bool sounding = false;
};

class FilterSection : public Section
{
public:
    explicit FilterSection(ExpanderProcessor&);
    void resized() override;
    ResponseView view;
};

/** The scope over the panels, its caption naming the newest note and how many voices sound. */
class ExpanderEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ExpanderEditor(ExpanderProcessor&);
    ~ExpanderEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);
    /** Saves the patch as a picture of the hardware module's knob positions (`drawHardwareSheet`). */
    void exportHardwareSheet();

    ExpanderProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" }, hardwareSheet { "Hardware-Blatt" };
    std::unique_ptr<juce::FileChooser> chooser;
    tonwerkui::ScopeView scope;
    Section osc1, osc2;
    FilterSection filter;
    Section filterEnvelope, ampEnvelope;
    Section lfo, voices, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset, shownCaption;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpanderEditor)
};
} // namespace tonwerkexpander
