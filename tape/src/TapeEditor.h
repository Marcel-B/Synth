#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "TapeProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerktape
{
/**
 * A cassette whose reels turn while audio runs through, faster and slower with the wobble (exaggerated, the real
 * percent would not be seen), and the tape's pitch against nominal in cents under it.
 */
class CassetteView : public juce::Component, private juce::Timer
{
public:
    explicit CassetteView(TapeProcessor&);
    void paint(juce::Graphics&) override;
    /** Reads the processor and turns the reels; the timer does this while on screen. */
    void refresh();
    int cents() const { return shownCents; }
    bool running() const { return isRunning; }

private:
    void timerCallback() override { refresh(); }
    void paintReel(juce::Graphics&, juce::Point<float> centre, float hub, float pack, float angle) const;

    TapeProcessor& processor;
    std::int64_t lastSamples = 0;
    float leftAngle = 0.0f, rightAngle = 0.0f;
    int shownCents = 0;
    bool isRunning = false;
};

class TransportSection : public tonwerkui::Section
{
public:
    explicit TransportSection(TapeProcessor&);
    void resized() override;
    CassetteView cassette;
};

/**
 * Night City, as the other Tonwerk plugins: the output over the input in the oscilloscope, then the drive with its
 * cassette, the tape, the playback head's tone and hiss, and the output.
 */
class TapeEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 760;

    explicit TapeEditor(TapeProcessor&);
    ~TapeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    TransportSection transport;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::ScopeView scope;
    tonwerkui::Section tape, head, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEditor)
};
} // namespace tonwerktape
