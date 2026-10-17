#pragma once

#include "EffectPresets.h"
#include "LfoProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerklfo
{
/**
 * One cycle of the LFO as the controller will see it: the curve between 0 and 127 in yellow, the swing's range shaded
 * in cyan, a dot where it is now; the caption names what is sent where.
 */
class WaveView : public juce::Component, private juce::Timer
{
public:
    explicit WaveView(LfoProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "CC 20 · 14 BIT · KANAL 1 · 1/4". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    LfoProcessor& processor;
    Settings shown;
    float phase = 0.0f, value = 0.5f;
};

class WaveSection : public tonwerkui::Section
{
public:
    explicit WaveSection(LfoProcessor&);
    void resized() override;
    WaveView view;
};

class LfoEditor : public juce::AudioProcessorEditor
{
public:
    explicit LfoEditor(LfoProcessor&);
    ~LfoEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    WaveSection wave;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoEditor)
};
} // namespace tonwerklfo
