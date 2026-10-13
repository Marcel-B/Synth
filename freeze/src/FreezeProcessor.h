#pragma once

#include "FreezeEngine.h"

#include "EffectPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tonwerkfreeze
{
/**
 * Tonwerk Freeze: the spectral freeze (`FreezeEngine.h`) as an audio effect. `freeze` holds the sound (automatable,
 * so a project can freeze on a beat); with the trigger on tempo it freezes anew on every grid line of the host's
 * song position while the transport plays. Parameter ids are part of saved Logic projects: never rename or remove
 * one, and append new ones with the version hint of the release they first appear in. Menu orders are saved too:
 * append only.
 */
class FreezeProcessor : public juce::AudioProcessor
{
public:
    FreezeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Freeze"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** A frozen pad goes on after the input stops; a host should not stop the effect when the track falls silent. */
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return programs.count(); }
    int getCurrentProgram() override { return programs.current(); }
    void setCurrentProgram(int index) override { programs.choose(index); }
    const juce::String getProgramName(int index) override { return programs.name(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;
    /** The trigger's grid in beats, from the `interval` menu. */
    static double intervalBeats(int index);

    juce::AudioProcessorValueTreeState state;
    tonwerkui::EffectPrograms programs;
    /** For the editor's display: both spectra in dB and how far the frozen layer is faded in. */
    std::array<std::atomic<float>, kDisplayBands> liveBands, frozenBands;
    std::atomic<float> frozenLevel { 0.0f };
    /** Counts every freeze taken, so the editor can flash on one. */
    std::atomic<int> freezes { 0 };

private:
    void run(const float* inL, const float* inR, float* outL, float* outR, int from, int to);

    Engine engine;
    std::vector<float> spareRight;
    double lastGridLine = -1.0;
    bool wasFrozen = false;

    struct Values
    {
        std::atomic<float>* freeze;
        std::atomic<float>* trigger;
        std::atomic<float>* interval;
        std::atomic<float>* capture;
        std::atomic<float>* fade;
        std::atomic<float>* stack;
        std::atomic<float>* pitch;
        std::atomic<float>* shimmer;
        std::atomic<float>* motion;
        std::atomic<float>* drift;
        std::atomic<float>* blur;
        std::atomic<float>* width;
        std::atomic<float>* lowCut;
        std::atomic<float>* highCut;
        std::atomic<float>* mix;
        std::atomic<float>* level;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreezeProcessor)
};
} // namespace tonwerkfreeze
