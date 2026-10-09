#pragma once

#include "NightCity.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerk
{
using tonwerkui::ScopeView;
using tonwerkui::Section;

/** An envelope's shape on a screen, from its four parameters; redrawn when one of them moves. */
class EnvelopeView : public juce::Component
{
public:
    EnvelopeView(juce::AudioProcessorValueTreeState& state, const juce::String& prefix);
    /** Small text in the corner, e.g. what an FM operator does in the current algorithm. */
    void setCaption(const juce::String& text, bool lit);
    /** Called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;

private:
    std::array<std::atomic<float>*, 4> values {};
    std::array<float, 4> shown { -1.0f, -1.0f, -1.0f, -1.0f };
    juce::String caption;
    bool captionLit = false;
};

/** A section with an envelope drawn on the left of its knobs, or above them for an FM operator. */
class EnvelopeSection : public Section
{
public:
    EnvelopeSection(juce::AudioProcessorValueTreeState& state, const juce::String& heading, const juce::String& prefix,
                    std::vector<Row> rows, Display room);
    void resized() override;
    EnvelopeView view;
};

/** The FM algorithm as the TX81Z prints it: the operators as boxes, modulators above what they modulate. */
class AlgorithmView : public juce::Component
{
public:
    explicit AlgorithmView(juce::AudioProcessorValueTreeState& state);
    void update();
    void paint(juce::Graphics&) override;
    /** 0 to 7, as the parameter counts. */
    int algorithm() const { return shown; }

private:
    std::atomic<float>* value;
    int shown = -1;
};

class AlgorithmSection : public Section
{
public:
    explicit AlgorithmSection(juce::AudioProcessorValueTreeState& state);
    void resized() override;
    AlgorithmView view;
};

class TonwerkSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 1270;

    explicit TonwerkSynthEditor(TonwerkSynthProcessor&);
    ~TonwerkSynthEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** The height the engine's panels need. */
    int heightFor(Engine engine) const;
    /** For the tests: the presets in the menu, by item id - 1. */
    const juce::Array<PresetLibrary::Preset>& menu() const { return menuPresets; }

private:
    using Row = std::vector<Section*>;

    template <typename SectionType, typename... Args>
    SectionType& add(std::vector<std::unique_ptr<Section>>& owner, Args&&... args);
    void showEngine(Engine engine);
    void timerCallback() override;
    void refreshPresets();
    void presetChosen();
    void fetchFromTonwerk();
    void importFile();
    void exportFile();
    void deletePreset();
    void setMessage(const juce::String& text, bool error = false);
    /** The other plugin, for a sound this one does not play. */
    juce::String otherPlugin() const;

    TonwerkSynthProcessor& processor;
    tonwerkui::LookAndFeel lookAndFeel;

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
    std::vector<Row> analogRows;
    std::vector<Row> fmRows;
    std::vector<EnvelopeView*> envelopes;
    std::vector<std::pair<EnvelopeView*, int>> operatorViews;
    AlgorithmSection* algorithmSection = nullptr;

    juce::MidiKeyboardComponent keyboard;
    ScopeView scope;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<PresetLibrary::Preset> menuPresets;
    Engine engine = Engine::analog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonwerkSynthEditor)
};
} // namespace tonwerk
