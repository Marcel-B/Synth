#pragma once

#include "GrooveProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkgroove
{
using tonwerkui::Section;

/**
 * The pattern as a grid: a row per track with its name (a click plays it and shows its knobs, a light flashes on each
 * hit), a mute switch and the steps. A click on a step turns it on, again makes it an accent, a third time off;
 * dragging along the row sets the steps passed to the same. The step that sounds is lit; steps past the pattern's
 * length are dimmed.
 */
class StepGrid : public juce::Component
{
public:
    explicit StepGrid(GrooveProcessor& processor);
    /** Reads the running step and the lights; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    static constexpr int kName = 112;
    static constexpr int kMute = 30;
    static constexpr int kCell = 26;
    static constexpr int kLine = 27;
    int preferredWidth() const { return 2 * tonwerkui::kPad + kName + kMute + kSteps * kCell + 4; }
    int preferredHeight() const { return tonwerkui::kTitle + kTracks * kLine + 2 * tonwerkui::kPad; }

private:
    juce::Rectangle<int> rowArea(int track) const;
    juce::Rectangle<int> stepArea(int track, int step) const;
    juce::Rectangle<int> nameArea(int track) const;
    juce::Rectangle<int> muteArea(int track) const;
    void copyPattern();
    void pastePattern();

    GrooveProcessor& synth;
    juce::TextButton copy { tonwerkui::tr("Kopieren") }, paste { tonwerkui::tr("Einfügen") }, clear { tonwerkui::tr("Leeren") };
    std::optional<Pattern> clipboard;
    int playing = -1;
    std::uint64_t shown = 0;
    std::array<float, kTracks> lights {};
    int dragTrack = -1, dragValue = 0;

    static juce::String utf8Text(const char* text) { return juce::String::fromUTF8(text); }
};

/**
 * The machines under the grid: which classic each track's instrument is built like, a menu per track in the grid's
 * order, and in front of them one that sets all eight at once (it reads "Gemischt" while they differ).
 */
class MachinePanel : public juce::Component
{
public:
    explicit MachinePanel(GrooveProcessor& processor);
    /** Shows the machine all tracks share; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kMenu = 2 * tonwerkui::kSlot;
    int preferredWidth() const { return 2 * tonwerkui::kPad + (kTracks + 1) * kMenu + tonwerkui::kGap; }
    /** A row of menus needs less room than a row of knobs. */
    static constexpr int kLine = 50;
    int preferredHeight() const { return tonwerkui::kTitle + kLine + tonwerkui::kPad; }

private:
    void setAll(int machine);

    GrooveProcessor& synth;
    juce::Label allName;
    juce::ComboBox all;
    std::vector<std::unique_ptr<tonwerkui::Control>> menus;
    int shownCommon = -2;
};

class GrooveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GrooveEditor(GrooveProcessor&);
    ~GrooveEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    /** Shows a track's knobs. */
    void select(int track);

private:
    void timerCallback() override;
    void choosePreset(int index);

    GrooveProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    Section sequencer, master;
    StepGrid grid;
    MachinePanel machines;
    std::vector<std::unique_ptr<Section>> tracks;
    Section effects;
    juce::String shownPreset;
    std::vector<int> menu;
    int shownTrack = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveEditor)
};
} // namespace tonwerkgroove
