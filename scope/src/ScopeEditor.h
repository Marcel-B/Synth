#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "ScopeProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace tonwerkscope
{
/**
 * The oscilloscope's screen: a graticule of ten by eight divisions, the trace drawn with a glow onto a layer that fades
 * by the afterglow each frame (the phosphor of an old tube), the readouts (time per division, pitch and note, peak and
 * RMS) and on the right the level meters of both sides with the stereo correlation under them.
 */
class ScopeScreen : public juce::Component, private juce::Timer
{
public:
    explicit ScopeScreen(ScopeProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    /** Reads the newest samples and draws them onto the layer; the timer does this while the screen is showing. */
    void refresh();
    /** The caption on the top left, e.g. "WELLE · 20 MS · 2 MS/DIV · STEIGEND 0 %". */
    juce::String caption() const;
    /** The pitch read out on the top right in Hz, 0 when there is none. */
    float pitchHz() const { return pitch; }
    /** What the tuner shows: its note (-1 while it has none) and the needle's cents, eased from frame to frame. */
    Tuning tuning() const { return { tunerNote, tunerCents }; }

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    juce::Colour traceColour() const;
    void drawWave(juce::Graphics&, juce::Rectangle<float> plot);
    void drawBars(juce::Graphics&, juce::Rectangle<float> plot);
    void drawXy(juce::Graphics&, juce::Rectangle<float> plot);
    /** The tuner fills the screen itself, not the afterglow layer: a needle has no business leaving a trail. */
    void paintTuner(juce::Graphics&, juce::Rectangle<float> plot);
    void updateTuner(float hz);
    /** The zoom in effect for a signal whose peak is `peak`: the manual gain, or auto, easing towards filling 90 %. */
    float zoomFor(float peak);

    ScopeProcessor& processor;
    Settings shown;
    /** The newest samples of both sides, the signal the channel chooses, and room for the pitch's mono copy. */
    std::vector<float> left, right, signal, second;
    PitchEstimator estimator;
    double estimatorRate = 0.0;
    juce::Image layer;
    float scale = 1.0f;
    float zoom = 1.0f;
    float pitch = 0.0f;
    int frame = 0;
    std::array<float, 2> peakDb { -100.0f, -100.0f }, rmsDb { -100.0f, -100.0f };
    float correlation = 0.0f;
    int tunerNote = -1;
    float tunerCents = 0.0f, tunerHz = 0.0f;
    /** The strobe's stripes move by the cents each frame, so they stand still when the note is in tune. */
    float strobe = 0.0f;
    int framesWithoutPitch = 0;
    std::uint64_t lastWritten = 0;
    bool silent = true;
};

class ScopeEditor : public juce::AudioProcessorEditor
{
public:
    explicit ScopeEditor(ScopeProcessor&);
    ~ScopeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    ScopeScreen screen;

private:
    ScopeProcessor& processor;
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section view, trigger, picture, tuner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeEditor)
};
} // namespace tonwerkscope
