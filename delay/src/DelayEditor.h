#pragma once

#include "DelayProcessor.h"
#include "EffectPresets.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkdelay
{
/**
 * The repeats as they will come: a line for each, as far along as its time and as tall as the feedback leaves it,
 * left above the middle in cyan and right below it in yellow, so ping-pong bounces on screen too. The ducking's pull
 * on the right.
 */
class EchoView : public juce::Component, private juce::Timer
{
public:
    explicit EchoView(DelayProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The time as the caption writes it, e.g. "1/8. · 375 MS". */
    juce::String timeText() const;

private:
    void timerCallback() override { refresh(); }

    DelayProcessor& processor;
    Settings shown;
    double bpm = 120.0;
    float duck = 1.0f;
};

class EchoSection : public tonwerkui::Section
{
public:
    explicit EchoSection(DelayProcessor&);
    void resized() override;
    EchoView view;
};

class DelayEditor : public juce::AudioProcessorEditor
{
public:
    explicit DelayEditor(DelayProcessor&);
    ~DelayEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    EchoSection echo;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section tone, ducking, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayEditor)
};
} // namespace tonwerkdelay
