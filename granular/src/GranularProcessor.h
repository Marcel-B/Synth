#pragma once

#include "EffectsChain.h"
#include "GranularEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace tonwerkgrain
{
/**
 * Tonwerk Granular: clouds of short grains cut from a source, one of the plugin's own or a file of the user's. The
 * engine (`GranularEngine.h`) plays the notes; this class gives it the host's parameters, MIDI and tempo, holds the
 * user's file and adds Tonwerk's delay and hall and the master level. Parameter ids are part of saved Logic projects:
 * never rename or remove one, and append new ones with the version hint of the release they first appear in.
 */
class GranularProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GranularProcessor();
    ~GranularProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Granular"; }
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
     * Reads an audio file (WAV, AIFF, FLAC and what the system decodes; the first 30 seconds, mixed to mono) as the
     * source "Eigene Datei" and selects it. The path is kept in the state, so a project finds it again. On the message
     * thread; false if the file could not be read.
     */
    bool loadFile(const juce::File& file);
    juce::File userFile() const { return juce::File(state.state.getProperty("sampleFile", {}).toString()); }
    /** The source the editor shows: the user's file or one of the built-in ones; never null. On the message thread. */
    std::shared_ptr<const GrainSource> shownSource() const;

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;

    /** The newest note's grains (place 0 to 1, or -1) and where its cloud is centred, for the display. */
    std::array<std::atomic<float>, kMaxGrains> grainPlaces {};
    std::array<std::atomic<float>, kMaxGrains> grainPhases {};
    std::atomic<float> shownCentre { -1.0f };
    std::atomic<float> shownSpray { 0.0f };

private:
    void timerCallback() override;
    void handle(const juce::MidiMessage& message, const Settings& s);
    tonwerk::Effects effectsFor(const Settings& s) const;
    void publishGrains();

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> voices;
    int currentProgram = 0;
    int wheel = 8192;
    std::atomic<double> bpm { 120.0 };

    /** The fx parameters' values, read apart from `readSettings` since the engine has no use for them. */
    struct FxValues
    {
        std::atomic<float>*delayMix, *delaySync, *delayTime, *delayDivision, *delayFeedback, *delayTone, *reverbMix,
            *reverbDecay;
    } fx {};
    std::vector<std::atomic<float>*> values;

    /**
     * The user's file. The message thread swaps it under the lock; the audio thread takes a copy when it gets the lock
     * and keeps its old one otherwise. A replaced source waits in `retired` until only that list holds it, so the
     * audio thread never frees one.
     */
    std::shared_ptr<const GrainSource> userSource;
    std::shared_ptr<const GrainSource> audioUserSource;
    std::vector<std::shared_ptr<const GrainSource>> retired;
    mutable juce::SpinLock sourceLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularProcessor)
};
} // namespace tonwerkgrain
