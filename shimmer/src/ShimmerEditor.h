#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "ShimmerProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkshimmer
{
/**
 * The tail as the knobs shape it: after the predelay's gap its level in dB falling over the decay (cyan), the share the
 * shimmer has lifted growing inside it (yellow), and a red line where the tail stands now.
 */
class TailView : public juce::Component, private juce::Timer
{
public:
    explicit TailView(ShimmerProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "DECAY 5.0 S · PREDELAY 20 MS · OKTAVE 50 %". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    ShimmerProcessor& processor;
    Settings shown;
    float level = 0.0f;
};

class TailSection : public tonwerkui::Section
{
public:
    explicit TailSection(ShimmerProcessor&);
    void resized() override;
    TailView view;
};

class ShimmerEditor : public juce::AudioProcessorEditor
{
public:
    explicit ShimmerEditor(ShimmerProcessor&);
    ~ShimmerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    TailSection tail;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section shimmer, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShimmerEditor)
};
} // namespace tonwerkshimmer
