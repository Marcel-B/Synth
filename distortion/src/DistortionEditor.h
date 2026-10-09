#pragma once

#include "DistortionProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkdistortion
{
/** Black knobs with a white line, as on the pedal. */
class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float start, float end,
                          juce::Slider&) override;
};

/** A knob with its name above and its value below, tied to a parameter. */
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label);
    /** For a knob on the orange instead of the black field. */
    void setTextColour(juce::Colour);
    void resized() override;

private:
    juce::Label name;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** The pedal: orange, with LEVEL, TONE and DIST in a row in the black field at the top, as on the DS-1. */
class DistortionEditor : public juce::AudioProcessorEditor
{
public:
    explicit DistortionEditor(DistortionProcessor&);
    ~DistortionEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PedalLookAndFeel lookAndFeel;
    Knob level, tone, distortion, input;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionEditor)
};
} // namespace tonwerkdistortion
