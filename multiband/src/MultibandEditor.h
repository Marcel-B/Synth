#pragma once

#include "EffectPresets.h"
#include "MultibandProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tonwerkmultiband
{
/**
 * The three bands across the spectrum (20 Hz to 20 kHz, the crossovers as yellow lines): in each, the detector's level
 * rising from the bottom in cyan, the threshold in red and, in yellow, where the band ends up after its gain change,
 * below the level when it is pulled down and above when it is lifted.
 */
class BandView : public juce::Component, private juce::Timer
{
public:
    explicit BandView(MultibandProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The caption, e.g. "TIEF -3.5 DB · MITTE +1.2 DB · HOCH 0.0 DB". */
    juce::String caption() const;

private:
    void timerCallback() override { refresh(); }

    MultibandProcessor& processor;
    Settings shown;
    std::array<float, kBands> levels {}, changes {};
};

class BandsSection : public tonwerkui::Section
{
public:
    explicit BandsSection(MultibandProcessor&);
    void resized() override;
    BandView view;
};

class MultibandEditor : public juce::AudioProcessorEditor
{
public:
    explicit MultibandEditor(MultibandProcessor&);
    ~MultibandEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    BandsSection bands;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    std::array<std::unique_ptr<tonwerkui::Section>, kBands> bandSections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandEditor)
};
} // namespace tonwerkmultiband
