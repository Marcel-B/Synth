#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "WobbleProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkwobble
{
/**
 * A lane's curve to draw on, with the LFO's playhead running over it. A click adds a corner (snapped to sixteenths of
 * the cycle unless Shift is held), a drag moves it, a double click removes it; the dot in the middle of a segment
 * bends it. The FORM menu starts from a shape or turns the curve around.
 */
class CurveView : public juce::Component, private juce::Timer
{
public:
    CurveView(WobbleProcessor&, Lane);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    /** Reads the playhead and repaints when it moved; the timer does this while on screen. */
    void refresh();
    /** Applies the FORM menu's entry `id` (1-based shapes, then the actions). */
    void applyMenu(int id);

private:
    void timerCallback() override { refresh(); }
    juce::Rectangle<float> plot() const;
    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> fromScreen(juce::Point<float> at) const;
    /** The corner or bend handle under the mouse: index, or -1. */
    int cornerAt(juce::Point<float> at) const;
    int handleAt(juce::Point<float> at) const;
    juce::Point<float> handlePosition(std::size_t segment) const;
    void moveCorner(int index, juce::Point<float> at, bool snap);
    void showMenu();
    juce::Colour colour() const;

    WobbleProcessor& processor;
    Lane lane;
    Curve curve;
    juce::TextButton form { "FORM" };
    int dragging = -1;
    bool bending = false;
    int hovered = -1;
    bool hoveredHandle = false;
    float playhead = 0.0f;
};

/** A lane's panel: the curve on the left, its clock and what it moves on the right. */
class LaneSection : public tonwerkui::Section
{
public:
    LaneSection(WobbleProcessor&, Lane, const juce::String& name, std::vector<Row> rows);
    void resized() override;
    CurveView view;
};

/**
 * Night City, as the other Tonwerk effects: the filter's curve and knobs across the top, the volume's below with the
 * output beside it. The presets are in the header.
 */
class WobbleEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 760;

    explicit WobbleEditor(WobbleProcessor&);
    ~WobbleEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    LaneSection filter, volume;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WobbleEditor)
};
} // namespace tonwerkwobble
