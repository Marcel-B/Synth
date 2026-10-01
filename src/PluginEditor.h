#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerk
{
/** Dark, with Tonwerk's green as the accent. */
class TonwerkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TonwerkLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float start, float end,
                          juce::Slider&) override;
};

/** One knob, menu or switch with its short name above, tied to a parameter. */
class Control : public juce::Component
{
public:
    Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label, int slots);
    void resized() override;
    int slots() const { return width; }

private:
    juce::Label name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    int width;
};

/** A titled group of controls in one row. */
class Section : public juce::Component
{
public:
    struct Item
    {
        juce::String id;
        juce::String label;
        int slots = 1;
        /**
         * A switch parameter this control depends on: it is shown only while the switch is `whenOn`. Two neighbours
         * with the same switch, one for each state, take turns in one place (a rate knob and its note value).
         */
        juce::String when = {};
        bool whenOn = false;
    };

    Section(juce::AudioProcessorValueTreeState& state, const juce::String& title, std::initializer_list<Item> items);
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    /** Where each control goes, in slots from the left, and how wide; a pair taking turns shares one place. */
    std::vector<std::pair<int, int>> places() const;

    juce::String title;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> switches;
};

/**
 * An oscilloscope of what the plugin plays, like Tonwerk's on its instruments page: triggered on a rising zero
 * crossing so a held note stands still, quiet sounds scaled up to be seen.
 */
class ScopeView : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView(const ScopeBuffer& source);
    void paint(juce::Graphics&) override;
    /** Takes the newest samples and draws them; the timer does this while the view is on screen. */
    void refresh();

private:
    void timerCallback() override;

    const ScopeBuffer& source;
    std::array<float, 2048> samples {};
};

class TonwerkSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit TonwerkSynthEditor(TonwerkSynthProcessor&);
    ~TonwerkSynthEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Row = std::vector<Section*>;

    Section& add(std::vector<std::unique_ptr<Section>>& owner, Section* section);
    void showEngine(Engine engine);
    void layoutRows(const std::vector<Row>& rows, juce::Rectangle<int> area);
    void refreshPresets();
    void presetChosen();
    void fetchFromTonwerk();
    void importFile();
    void exportFile();
    void deletePreset();
    void setMessage(const juce::String& text, bool error = false);

    TonwerkSynthProcessor& processor;
    TonwerkLookAndFeel lookAndFeel;

    juce::Label title;
    juce::TextButton analogButton { "Analog" };
    juce::TextButton fmButton { "FM" };
    std::unique_ptr<juce::ParameterAttachment> engineAttachment;
    juce::ComboBox presets;
    juce::TextButton tonwerkButton;
    juce::TextButton importButton;
    juce::TextButton exportButton;
    juce::TextButton deleteButton;
    juce::Label message;

    std::vector<std::unique_ptr<Section>> analogSections;
    std::vector<std::unique_ptr<Section>> fmSections;
    std::unique_ptr<Section> fxSection;
    std::vector<Row> analogRows;
    std::vector<Row> fmRows;

    juce::MidiKeyboardComponent keyboard;
    ScopeView scope;
    std::unique_ptr<juce::FileChooser> chooser;
    /** The presets in the menu, by item id - 1. */
    juce::Array<PresetLibrary::Preset> menuPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonwerkSynthEditor)
};
} // namespace tonwerk
