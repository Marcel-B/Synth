#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "RobovoxProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkrobovox
{
/** The carrier's note large and the voice's pitch under it, so the steps of the quantiser can be watched. */
class PitchView : public juce::Component, private juce::Timer
{
public:
    explicit PitchView(RobovoxProcessor&);
    void paint(juce::Graphics&) override;
    /** Reads the processor's notes and repaints when they changed; the timer does this while on screen. */
    void refresh();
    juce::String carrierText() const { return noteName(carrier); }

private:
    void timerCallback() override { refresh(); }

    RobovoxProcessor& processor;
    int carrier = -1;
    float sung = -1.0f;
};

class PitchSection : public tonwerkui::Section
{
public:
    explicit PitchSection(RobovoxProcessor&);
    void resized() override;
    PitchView view;
};

/**
 * Night City, as the other Tonwerk plugins: the robot over the voice in the oscilloscope, then where the carrier's
 * note comes from (with the note it plays), the carrier, what is taken from the voice, and the output.
 */
class RobovoxEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 744;

    explicit RobovoxEditor(RobovoxProcessor&);
    ~RobovoxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    PitchSection pitch;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section carrier, voice, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RobovoxEditor)
};
} // namespace tonwerkrobovox
