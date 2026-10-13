#pragma once

#include "ScopeBuffer.h"
#include "TapeEngine.h"

#include "EffectPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace tonwerktape
{
/**
 * Tonwerk Tape: the tape machine (`TapeEngine.h`) as an audio effect. The wobble, dropouts and hiss start from the same
 * seed whenever the host's transport starts, so a bounce sounds like the playback before it. The playback point sits
 * `kCentreSeconds` behind, which the processor reports as latency. Parameter ids are part of saved Logic projects:
 * never rename or remove one, and append new ones with the version hint of the release they first appear in. Menu
 * orders are saved too: append only.
 */
class TapeProcessor : public juce::AudioProcessor
{
public:
    TapeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Tape"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0 * kCentreSeconds; }

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

    juce::AudioProcessorValueTreeState state;
    tonwerkui::EffectPrograms programs;
    /** The first channel before and after, for the editor's oscilloscope. */
    tonwerkui::ScopeBuffer scopeIn, scopeOut;
    /** For the editor's cassette: the tape's speed against nominal (0.01 = 1 % fast) and the samples run so far. */
    std::atomic<float> speed { 0.0f };
    std::atomic<std::int64_t> samplesRun { 0 };

private:
    TapeEngine engine;
    bool wasPlaying = false;

    struct Values
    {
        std::atomic<float>* input;
        std::atomic<float>* drive;
        std::atomic<float>* type;
        std::atomic<float>* tone;
        std::atomic<float>* wow;
        std::atomic<float>* flutter;
        std::atomic<float>* noise;
        std::atomic<float>* dropouts;
        std::atomic<float>* mix;
        std::atomic<float>* output;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeProcessor)
};
} // namespace tonwerktape
