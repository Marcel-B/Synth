#pragma once

#include "WavetableProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkwave
{
/** Night City, as in Chrome Glitch: near black, the yellow, cyan and red of Cyberpunk 2077's interface. */
class WaveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WaveLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position, float minPosition,
                          float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

/** A knob, menu or switch with its name above, tied to a parameter. */
class Control : public juce::Component
{
public:
    Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label);
    void resized() override;
    /** Menus take two slots, so their entries fit. */
    int slots() const { return menu != nullptr ? 2 : 1; }

private:
    juce::Label name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
};

using ControlList = std::vector<std::pair<juce::String, const char*>>;

/**
 * A titled panel of controls in rows, with an optional power switch in its title bar and room on the left for a
 * display (`inset` pixels).
 */
class Section : public juce::Component
{
public:
    Section(juce::AudioProcessorValueTreeState& state, const juce::String& title, std::vector<ControlList> rows,
            const juce::String& powerId = {}, int inset = 0);
    /** The width the rows need. */
    int preferredWidth() const;
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    juce::Rectangle<int> insetArea() const;

private:
    juce::String title;
    int inset;
    std::vector<std::vector<std::unique_ptr<Control>>> rows;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
};

/**
 * The oscillator's table drawn as Serum draws it: its frames stacked in depth, the one being played lit. While a note
 * sounds it follows the modulated position, so a wobble on the position is seen moving.
 */
class WavetableView : public juce::Component
{
public:
    void setTable(int index);
    /** `position` in frames, 0 to kFrames - 1. */
    void setPosition(float position);
    void paint(juce::Graphics&) override;

private:
    /** Each frame's cycle at a fixed number of points, made when the table changes. */
    static constexpr int kPoints = 96;
    std::vector<float> frames;
    int table = -1;
    float position = 0.0f;
};

class OscillatorSection : public Section
{
public:
    OscillatorSection(WavetableProcessor& processor, const juce::String& heading, const juce::String& prefix);
    void resized() override;
    /** Called at the editor's frame rate. */
    void update(float playedPosition);

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String prefix;
    WavetableView view;
};

/** One slot of the modulation matrix in a line: source, target, amount, bipolar. */
class ModRow : public juce::Component
{
public:
    ModRow(juce::AudioProcessorValueTreeState& state, int slot);
    void resized() override;

private:
    juce::Label number;
    juce::ComboBox source, target;
    juce::Slider amount;
    juce::ToggleButton bipolar;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, targetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bipolarAttachment;
};

class MatrixSection : public juce::Component
{
public:
    explicit MatrixSection(juce::AudioProcessorValueTreeState& state);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::vector<std::unique_ptr<ModRow>> rows;
};

class WavetableEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WavetableEditor(WavetableProcessor&);
    ~WavetableEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    WavetableProcessor& synth;
    WaveLookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    OscillatorSection oscA, oscB;
    Section sub, noise, filter, distortion, master;
    Section env1, env2, env3, macros;
    Section lfo1, lfo2, voice;
    MatrixSection matrix;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableEditor)
};
} // namespace tonwerkwave
