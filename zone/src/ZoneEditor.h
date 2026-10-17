#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "ZoneProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tonwerkzone
{
/**
 * The range at work: across the spectrum (20 Hz to 20 kHz) the range shaded in cyan and, in yellow, the response as
 * it is right now, dipping as far as the range is turned down; on the right the detector's level against the
 * threshold and the reduction hanging from the top in red.
 */
class ZoneView : public juce::Component, private juce::Timer
{
public:
    explicit ZoneView(ZoneProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "GLOCKE 1000 HZ · 12 HT · BAND · GR 3.5 DB". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    ZoneProcessor& processor;
    Settings shown;
    float level = -100.0f, reduction = 0.0f;
    bool keyConnected = false;
};

class ZoneSection : public tonwerkui::Section
{
public:
    explicit ZoneSection(ZoneProcessor&);
    void resized() override;
    ZoneView view;
};

class ZoneEditor : public juce::AudioProcessorEditor
{
public:
    explicit ZoneEditor(ZoneProcessor&);
    ~ZoneEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    ZoneSection zone;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section compressor, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoneEditor)
};
} // namespace tonwerkzone
