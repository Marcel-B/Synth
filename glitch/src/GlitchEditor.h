#pragma once

#include "GlitchProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace chromeglitch
{
/** Dark like Tonwerk Synth, with a cold cyan for the chrome. */
class GlitchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlitchLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float start, float end,
                          juce::Slider&) override;
};

/** A knob, menu or switch with its name above, tied to a parameter. */
class Control : public juce::Component
{
public:
    Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label);
    void resized() override;

private:
    juce::Label name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
};

/** A titled group of controls in one row. */
class Section : public juce::Component
{
public:
    Section(juce::AudioProcessorValueTreeState& state, const juce::String& title,
            std::initializer_list<std::pair<const char*, const char*>> controls);
    int count() const { return (int) controls.size(); }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    std::vector<std::unique_ptr<Control>> controls;
};

class ChromeGlitchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChromeGlitchEditor(ChromeGlitchProcessor&);
    ~ChromeGlitchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ChromeGlitchProcessor& processor;
    GlitchLookAndFeel lookAndFeel;
    Section main;
    Section stutter;
    Section dropout;
    Section damage;
    Section timing;
    /** The light in the title, on while a glitch sounds; it falls off slowly so short ones are seen. */
    float light = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromeGlitchEditor)
};
} // namespace chromeglitch
