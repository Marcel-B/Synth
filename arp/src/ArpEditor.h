#pragma once

#include "ArpProcessor.h"
#include "EffectPresets.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>

namespace tonwerkarp
{
/** The keys held and the note just played, so it shows what the arpeggiator works with. */
class NotesView : public juce::Component, private juce::Timer
{
public:
    explicit NotesView(ArpProcessor&);
    void paint(juce::Graphics&) override;
    /** Reads the processor's notes and repaints when they changed; the timer does this while on screen. */
    void refresh();

private:
    void timerCallback() override { refresh(); }

    ArpProcessor& processor;
    std::vector<int> keys;
    int note = -1;
};

class NotesSection : public tonwerkui::Section
{
public:
    explicit NotesSection(ArpProcessor&);
    void resized() override;
    NotesView view;
};

/**
 * The sixteen steps in lanes, as on a hardware sequencer: on or off, the note in semitones (drag up and down,
 * double-click for 0), the velocity (drag, also across steps), tied over from the step before. Steps past the
 * pattern's length are dimmed; the step playing is lit.
 */
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid(ArpProcessor&);
    ~StepGrid() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    static constexpr int kHeight = 236;

private:
    enum class Lane
    {
        none,
        on,
        note,
        velocity,
        tie
    };

    void timerCallback() override;
    juce::Rectangle<int> laneArea(Lane) const;
    juce::Rectangle<int> cell(Lane, int step) const;
    int stepAt(int x) const;
    Lane laneAt(int y) const;
    float value(int step, const char* field) const;
    void set(int step, const char* field, float plainValue);
    juce::RangedAudioParameter* parameter(int step, const char* field) const;

    ArpProcessor& processor;
    /** The parameters a drag has touched, whose gestures end with it. */
    std::set<juce::RangedAudioParameter*> touched;
    Lane dragLane = Lane::none;
    int dragStep = -1;
    float dragStart = 0.0f;
    float paintValue = 0.0f;
    std::size_t shown = 0;
};

/**
 * Night City, as the other Tonwerk plugins: how the keys are walked (with the keys and the note on a display), the
 * grid's timing, the velocity, and the sixteen steps under them.
 */
class ArpEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 780;

    explicit ArpEditor(ArpProcessor&);
    ~ArpEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    NotesSection notes;
    tonwerkui::Section timing, velocity;
    StepGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpEditor)
};
} // namespace tonwerkarp
