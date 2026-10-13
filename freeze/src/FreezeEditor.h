#pragma once

#include "EffectPresets.h"
#include "FreezeProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkfreeze
{
/**
 * The input's spectrum in cyan and the frozen layer's in yellow over it, so what a freeze took can be seen. A click
 * freezes or lets go, as the switch does.
 */
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView(FreezeProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { toggleFreeze(); }
    void toggleFreeze();
    /** Reads the processor's spectra and repaints; the timer does this while on screen. */
    void refresh();

private:
    void timerCallback() override { refresh(); }

    FreezeProcessor& processor;
    std::array<float, kDisplayBands> live {}, held {};
    float level = 0.0f;
    int lastFreezes = 0;
    /** Flashes on a new freeze, then fades. */
    float flash = 0.0f;
};

/** Night City, as the other Tonwerk plugins: the spectra, then the freeze, its sound, its motion and the output. */
class FreezeEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 744;

    explicit FreezeEditor(FreezeProcessor&);
    ~FreezeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SpectrumView spectrum;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section freeze, sound, motion, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreezeEditor)
};
} // namespace tonwerkfreeze
