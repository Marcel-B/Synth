#pragma once

#include "EffectPresets.h"
#include "FilterProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkfilter
{
/**
 * The filter's response as the knobs set it (cyan, dim) and where the envelope and the LFO have moved it now (yellow),
 * with the cutoff marked: the curve breathes with the music.
 */
class ResponseView : public juce::Component, private juce::Timer
{
public:
    explicit ResponseView(FilterProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The gain of `type` at `hz` with the cutoff at `cutoff`, in dB (the analog prototype; enough for a picture). */
    static float responseDb(const Settings& s, float cutoff, float hz);

private:
    void timerCallback() override { refresh(); }

    FilterProcessor& processor;
    Settings shown;
    float cutoffNow = 1000.0f, envelope = 0.0f;
};

class FilterSection : public tonwerkui::Section
{
public:
    explicit FilterSection(FilterProcessor&);
    void resized() override;
    ResponseView view;
};

class FilterEditor : public juce::AudioProcessorEditor
{
public:
    explicit FilterEditor(FilterProcessor&);
    ~FilterEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    FilterSection filter;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section envelope, lfo, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterEditor)
};
} // namespace tonwerkfilter
