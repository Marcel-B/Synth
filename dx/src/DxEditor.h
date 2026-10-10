#pragma once

#include "DxProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkdx
{
using tonwerkui::Section;

/**
 * An operator's four-rate envelope on a screen: from L4 up to L1, on to L2 and L3, held, then back to L4, each stage
 * as long as its rate makes it. Clicking it chooses the operator for the detail panel.
 */
class DxEnvelopeView : public juce::Component
{
public:
    DxEnvelopeView(juce::AudioProcessorValueTreeState& state, int op);
    /** Small text in the corner: what the operator does in the current algorithm. */
    void setCaption(const juce::String& text, bool lit);
    void setSelected(bool selected);
    void update();
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    std::array<std::atomic<float>*, 8> values {};
    std::array<float, 8> shown {};
    bool first = true;
    juce::String caption;
    bool captionLit = false;
    bool selected = false;
};

/** A section with a `DxEnvelopeView` in its display room. */
class OperatorSection : public Section
{
public:
    OperatorSection(juce::AudioProcessorValueTreeState& state, const juce::String& heading, int op,
                    std::vector<Row> rows, const juce::String& powerId, Display room);
    void resized() override;
    DxEnvelopeView view;
};

/** The DX7's algorithm chart for the current algorithm: carriers along the bottom, modulators above what they feed. */
class DxAlgorithmView : public juce::Component
{
public:
    explicit DxAlgorithmView(juce::AudioProcessorValueTreeState& state);
    void update();
    void paint(juce::Graphics&) override;
    int algorithm() const { return shown; }

    /**
     * Where each operator's box sits: x in box widths from the left (0 for the leftmost), y the row counted up from
     * the carriers' (0). For the drawing and the tests.
     */
    static std::array<juce::Point<float>, kOperators> placesFor(const Algorithm& algorithm);

private:
    std::atomic<float>* value;
    int shown = -1;
};

class DxAlgorithmSection : public Section
{
public:
    explicit DxAlgorithmSection(juce::AudioProcessorValueTreeState& state);
    void resized() override;
    DxAlgorithmView view;
};

class DxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 1270;

    explicit DxEditor(DxProcessor&);
    ~DxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** 0 to 5: the operator the detail panel shows. */
    void selectOperator(int op);
    int selectedOperator() const { return selected; }
    /** For the tests: the sounds in the menu, by item id - 1. */
    const std::vector<NamedDxPatch>& menu() const { return menuPatches; }
    int preferredHeight() const;

private:
    void timerCallback() override;
    void refreshPresets();
    void choose(int index);
    void step(int direction);
    void importSysex();
    void setMessage(const juce::String& text, bool error = false);

    DxProcessor& processor;
    tonwerkui::LookAndFeel lookAndFeel;

    juce::ComboBox presets;
    juce::TextButton previous { "<" };
    juce::TextButton next { ">" };
    juce::TextButton sysexButton;
    juce::Label message;

    std::unique_ptr<DxAlgorithmSection> algorithmSection;
    std::unique_ptr<Section> lfoSection;
    std::unique_ptr<Section> fxSection;
    std::array<std::unique_ptr<OperatorSection>, kOperators> operators;
    std::array<std::unique_ptr<OperatorSection>, kOperators> details;
    int selected = 0;

    juce::MidiKeyboardComponent keyboard;
    tonwerkui::ScopeView scope;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<NamedDxPatch> menuPatches;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DxEditor)
};
} // namespace tonwerkdx
