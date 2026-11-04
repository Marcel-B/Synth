#pragma once

#include "DrumsProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkdrums
{
using tonwerkui::Section;

/**
 * The sixteen pads in four rows of four: name and note on each, a light that flashes with every hit, yellow for the
 * pad whose knobs are shown, red letters for a muted one. A click plays the pad (harder the higher up it lands) and
 * shows its knobs.
 */
class PadGrid : public juce::Component
{
public:
    explicit PadGrid(DrumsProcessor& processor);
    /** Reads the lights; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int kCell = 104;
    int preferredWidth() const { return 2 * tonwerkui::kPad + 4 * kCell; }
    /** The pad at a place in the grid (0 to 15, row by row). */
    juce::Rectangle<int> cellArea(int place) const;

private:
    DrumsProcessor& synth;
    std::array<float, kPads> lights {};
    std::uint64_t shown = 0;
};

class DrumsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DrumsEditor(DrumsProcessor&);
    ~DrumsEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    /** Shows a pad's knobs. */
    void select(int pad);

private:
    void timerCallback() override;
    void choosePreset(int index);

    DrumsProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    PadGrid grid;
    std::vector<std::unique_ptr<Section>> pads;
    Section effects, master;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumsEditor)
};
} // namespace tonwerkdrums
