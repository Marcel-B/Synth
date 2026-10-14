#pragma once

#include "EffectsChain.h"
#include "SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace tonwerksampler
{
/** A key as Logic names it: C3 is 60. */
juce::String noteName(int note);

/**
 * Tonwerk Sampler: plays a sample chromatically or one slice per key (`SamplerEngine.h`), from the sounds built in or
 * the user's own file, then Tonwerk's delay and hall and the master level. The file's path is in the state and read
 * again on load; the samples themselves are not saved. Parameter ids are part of saved Logic projects: never rename
 * or remove one, and append new ones with the version hint of the release they first appear in.
 */
class SamplerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SamplerProcessor();
    ~SamplerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest release and the longest hall. */
    double getTailLengthSeconds() const override { return 10.0 + 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;
    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }
    bool roomPlaying() const { return effects.roomPlaying(); }

    /**
     * Reads an audio file (the first 60 s, one or two channels) as the user's sample and switches the source to it;
     * false if it cannot be read, keeping the old one. On the message thread.
     */
    bool loadFile(const juce::File& file);
    juce::File userFile() const { return juce::File(state.state.getProperty("sampleFile", {}).toString()); }
    /** The sample the source parameter names, for the editor; an empty one for the user's file before one is loaded. */
    std::shared_ptr<const Sample> shownSample() const;

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    /** The newest voices' places as shares of the sample, -1 where none, for the editor's play heads. */
    std::array<std::atomic<float>, 8> playheads {};

private:
    void timerCallback() override;
    void handle(const juce::MidiMessage& message, const Settings& s, const Sample& sample);
    tonwerk::Effects effectsFor() const;

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> voices;
    int currentProgram = 0;
    int wheel = 8192;
    std::atomic<double> bpm { 120.0 };

    /**
     * The user's sample. The message thread swaps it under the lock; the audio thread takes it with a try-lock, and a
     * replaced one waits in `retired` until only that list holds it, so the audio thread never frees one.
     */
    std::shared_ptr<const Sample> userSample;
    std::shared_ptr<const Sample> audioUserSample;
    std::vector<std::shared_ptr<const Sample>> retired;
    mutable juce::SpinLock sampleLock;

    struct FxValues
    {
        std::atomic<float>*delayMix, *delaySync, *delayTime, *delayDivision, *delayFeedback, *delayTone, *reverbMix,
            *reverbDecay;
    } fx {};
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerProcessor)
};
} // namespace tonwerksampler
