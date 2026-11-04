#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "ReverbProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkreverb
{
/**
 * The reverb as the knobs shape it: after the predelay's gap the early reflections as yellow lines, then the tail's
 * level in dB falling over the decay (cyan) with the bass's own decay drawn over it, a red line where the wet stands
 * now and the ducking's pull as a bar on the right.
 */
class SpaceView : public juce::Component, private juce::Timer
{
public:
    explicit SpaceView(ReverbProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "SAAL · DECAY 2.5 S · PREDELAY 20 MS". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    ReverbProcessor& processor;
    Settings shown;
    double bpm = 120.0;
    float level = 0.0f, duck = 1.0f;
};

class SpaceSection : public tonwerkui::Section
{
public:
    explicit SpaceSection(ReverbProcessor&);
    void resized() override;
    SpaceView view;
};

class ReverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReverbEditor(ReverbProcessor&);
    ~ReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SpaceSection space;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section tone, filter, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbEditor)
};
} // namespace tonwerkreverb
