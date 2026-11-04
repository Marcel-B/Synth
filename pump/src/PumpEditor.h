#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "PumpProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tonwerkpump
{
/**
 * The pump at work: the last four seconds of reduction hanging from the top in red over the key's level in cyan (the
 * threshold across it), a flash on each hit, and on the right the shape of one duck (in compressor mode the static
 * curve) with the threshold or the step it fits in.
 */
class MeterView : public juce::Component, private juce::Timer
{
public:
    explicit MeterView(PumpProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "GR 17.5 DB · AUSLÖSER · SIDECHAIN". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }
    void paintShape(juce::Graphics&, juce::Rectangle<float>) const;

    static constexpr int kHistory = 120;
    PumpProcessor& processor;
    Settings shown;
    std::array<float, kHistory> reductions {}, levels {};
    std::array<bool, kHistory> hit {};
    int newest = 0;
    bool keyConnected = false;
};

class MeterSection : public tonwerkui::Section
{
public:
    explicit MeterSection(PumpProcessor&);
    void resized() override;
    MeterView view;
};

class PumpEditor : public juce::AudioProcessorEditor
{
public:
    explicit PumpEditor(PumpProcessor&);
    ~PumpEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    MeterSection meter;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section curve, key, band, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PumpEditor)
};
} // namespace tonwerkpump
