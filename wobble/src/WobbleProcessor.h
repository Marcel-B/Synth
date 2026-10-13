#pragma once

#include "EffectPresets.h"
#include "WobbleEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace tonwerkwobble
{
/** The two LFOs: one moves the filter, one the volume. */
enum class Lane
{
    filter,
    volume,
};

/**
 * Tonwerk Wobble: a filter and a volume shaper, each moved by a drawn LFO curve locked to the host's tempo
 * (`WobbleEngine.h`). While the host plays, an LFO's phase comes from the song position, so a wobble sits on the
 * beat wherever playback starts; stopped, it runs on at the tempo. The curves are not parameters: they live in the
 * state as text (`filterCurve`, `volumeCurve`) and in tables the audio thread reads. Parameter ids are part of saved
 * Logic projects: never rename or remove one, and append new ones with the version hint of the release they first
 * appear in.
 */
class WobbleProcessor : public juce::AudioProcessor
{
public:
    WobbleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Wobble"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    /** The factory sounds as the host's programs; choosing one also draws its curves. */
    int getNumPrograms() override { return programs.count(); }
    int getCurrentProgram() override { return programs.current(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override { return programs.name(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;

    /** A lane's curve, as last drawn; message thread. */
    const Curve& curve(Lane lane) const { return curves[index(lane)]; }
    /** Draws a lane's curve: into the table the audio thread reads and into the state. Message thread. */
    void setCurve(Lane lane, const Curve& curve);

    /** Where each LFO is (left channel, 0 to 1) and the value it gives, for the editor's playheads. */
    std::array<std::atomic<float>, 2> phase {}, value {};

    juce::AudioProcessorValueTreeState state;
    tonwerkui::EffectPrograms programs;

private:
    static std::size_t index(Lane lane) { return lane == Lane::filter ? 0 : 1; }
    /** Reads the curves back from the state after a project or preset brought it. */
    void loadCurves();

    Engine engine;
    std::array<Curve, 2> curves;
    std::array<CurveTable, 2> tables;
    /** The LFOs' phases while they run free (stopped, or not in sync), in cycles. */
    std::array<double, 2> running {};
    double sampleRate = 48000.0;

    struct LaneValues
    {
        std::atomic<float>* sync;
        std::atomic<float>* division;
        std::atomic<float>* rate;
        std::atomic<float>* phase;
    };
    std::array<LaneValues, 2> lanes {};

    struct Values
    {
        std::atomic<float>* filterOn;
        std::atomic<float>* filterType;
        std::atomic<float>* filterSlope;
        std::atomic<float>* filterLow;
        std::atomic<float>* filterHigh;
        std::atomic<float>* resonance;
        std::atomic<float>* drive;
        std::atomic<float>* volumeOn;
        std::atomic<float>* volumeDepth;
        std::atomic<float>* smooth;
        std::atomic<float>* spread;
        std::atomic<float>* mix;
        std::atomic<float>* output;
    } values {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WobbleProcessor)
};
} // namespace tonwerkwobble
