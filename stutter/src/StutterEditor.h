#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "StutterProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkstutter
{
/**
 * The interval in sixteenths, the stretch that repeats marked (the live slice and its repeats), and where the song
 * is now: lit while a repeat sounds.
 */
class PatternView : public juce::Component, private juce::Timer
{
public:
    explicit PatternView(StutterProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();

private:
    void timerCallback() override { refresh(); }

    StutterProcessor& processor;
    Settings shown;
    float position = 0.0f;
    int repeat = -1;
};

class PatternSection : public tonwerkui::Section
{
public:
    explicit PatternSection(StutterProcessor&);
    void resized() override;
    PatternView view;
};

class StutterEditor : public juce::AudioProcessorEditor
{
public:
    explicit StutterEditor(StutterProcessor&);
    ~StutterEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    PatternSection pattern;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section grid, sound, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StutterEditor)
};
} // namespace tonwerkstutter
