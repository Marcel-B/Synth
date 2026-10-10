#pragma once

#include "EffectPresets.h"
#include "GlitchProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace chromeglitch
{
/** Night City: near black, the yellow, cyan and red of Cyberpunk 2077's interface. */
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

/**
 * A small screen that shows the chrome's state: a calm scanline picture while the voice is clean, torn bars, colour
 * split text and noise while it glitches, fading out over a few frames so even a short glitch is seen.
 */
class GlitchMonitor : public juce::Component
{
public:
    /** Called at the editor's frame rate with what the processor reports. */
    void update(bool glitching, bool started, int event);
    /** 1 while a glitch shows, falling to 0 after it. */
    float level() const { return intensity; }
    void paint(juce::Graphics&) override;

private:
    float intensity = 0.0f;
    int event = 0;
    int frame = 0;
    juce::Random random;
};

class ChromeGlitchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChromeGlitchEditor(ChromeGlitchProcessor&);
    ~ChromeGlitchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    GlitchMonitor& getMonitor() { return monitor; }

private:
    void timerCallback() override;

    ChromeGlitchProcessor& processor;
    GlitchLookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    Section main;
    Section stutter;
    Section dropout;
    Section damage;
    Section timing;
    GlitchMonitor monitor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromeGlitchEditor)
};
} // namespace chromeglitch
