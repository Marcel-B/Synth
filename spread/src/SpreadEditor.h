#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "SpreadProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkspread
{
/**
 * The stereo image: on the left a goniometer (a mono sound stands upright, a wide one spreads sideways; the input
 * faint behind the output), on the right which way a sound from the middle leans at each frequency (above the line
 * left, below right; the bass's crossover in red), under it the correlation before and after.
 */
class StereoView : public juce::Component, private juce::Timer
{
public:
    explicit StereoView(SpreadProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "DIFFUS · 12 MS · BASS MONO 120 HZ". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }
    void paintGoniometer(juce::Graphics&, juce::Rectangle<float> area);
    void paintLean(juce::Graphics&, juce::Rectangle<float> area);
    void paintCorrelation(juce::Graphics&, juce::Rectangle<float> area, const juce::String& name, float value,
                          juce::Colour colour);

    SpreadProcessor& processor;
    Settings shown;
    float correlationIn = 1.0f, correlationOut = 1.0f, side = 0.0f;
    static constexpr int kPoints = 1024;
    std::array<float, kPoints> inL {}, inR {}, outL {}, outR {};
};

class StereoSection : public tonwerkui::Section
{
public:
    explicit StereoSection(SpreadProcessor&);
    void resized() override;
    StereoView view;
};

class SpreadEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpreadEditor(SpreadProcessor&);
    ~SpreadEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    StereoSection stereo;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section tone, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpreadEditor)
};
} // namespace tonwerkspread
