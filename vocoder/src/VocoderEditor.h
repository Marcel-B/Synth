#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "VocoderProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkvocoder
{
/**
 * The filter bank as a row of bars, one per band from low to high, each as high as the voice's level there (in dB,
 * 60 dB across), the notes the carrier plays above them.
 */
class BandView : public juce::Component, private juce::Timer
{
public:
    explicit BandView(VocoderProcessor&);
    void paint(juce::Graphics&) override;
    /** Reads the processor's levels and repaints; the timer does this while on screen. */
    void refresh();
    juce::String notesText() const { return notes; }

private:
    void timerCallback() override { refresh(); }

    VocoderProcessor& processor;
    std::array<float, kMaxBands> shown {};
    int bands = 16;
    juce::String notes;
};

class BankSection : public tonwerkui::Section
{
public:
    explicit BankSection(VocoderProcessor&);
    void resized() override;
    BandView view;
};

/** Night City, as the other Tonwerk plugins: the bank with its bars, the carrier, the output, the oscilloscope. */
class VocoderEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocoderEditor(VocoderProcessor&);
    ~VocoderEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    BankSection bank;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section carrier, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderEditor)
};
} // namespace tonwerkvocoder
