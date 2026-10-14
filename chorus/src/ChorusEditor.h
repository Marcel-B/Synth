#pragma once

#include "ChorusProcessor.h"
#include "EffectPresets.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkchorus
{
/**
 * The sweep as it will run: two cycles of what the LFO moves (the lines' delays, the phaser's frequency), left in cyan
 * and right in yellow, a chorus's further voices fainter, a marker where the LFO is now.
 */
class SweepView : public juce::Component, private juce::Timer
{
public:
    explicit SweepView(ChorusProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "CHORUS · 3 STIMMEN · 11.3-16.8 MS · 0.6 HZ". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    ChorusProcessor& processor;
    Settings shown;
    double bpm = 120.0, phase = 0.0;
};

class SweepSection : public tonwerkui::Section
{
public:
    explicit SweepSection(ChorusProcessor&);
    void resized() override;
    SweepView view;
};

class ChorusEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChorusEditor(ChorusProcessor&);
    ~ChorusEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SweepSection sweep;
    /** The lines' knobs (chorus, ensemble, flanger) and the phaser's take turns in one place. */
    tonwerkui::Section line, phaser;

private:
    void modeChanged(float value);

    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section tone, output;
    juce::ParameterAttachment modeAttachment;
    bool phaserShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusEditor)
};
} // namespace tonwerkchorus
