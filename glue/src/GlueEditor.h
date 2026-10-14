#pragma once

#include "EffectPresets.h"
#include "GlueProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tonwerkglue
{
/**
 * The compressor at work: the last four seconds of reduction hanging from the top (the compressor's red, the pump's
 * yellow under it) over the detector's level in cyan, and on the right the static curve with the threshold and where
 * the level sits on it now.
 */
class MeterView : public juce::Component, private juce::Timer
{
public:
    explicit MeterView(GlueProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "GR 3.5 DB · 4:1 · 10 MS · AUTO". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    static constexpr int kHistory = 120;
    GlueProcessor& processor;
    Settings shown;
    std::array<float, kHistory> reductions {}, pumps {}, levels {};
    int newest = 0;
    bool keyConnected = false;
};

class MeterSection : public tonwerkui::Section
{
public:
    explicit MeterSection(GlueProcessor&);
    void resized() override;
    MeterView view;
};

class GlueEditor : public juce::AudioProcessorEditor
{
public:
    explicit GlueEditor(GlueProcessor&);
    ~GlueEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    MeterSection meter;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section key, pump, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlueEditor)
};
} // namespace tonwerkglue
