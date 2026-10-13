#pragma once

#include "AdditiveEngine.h"
#include "EffectsChain.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkadd
{
/**
 * Tonwerk Additiv: sounds built from up to 64 sine partials, each with its own level, place and envelope. The engine
 * (`AdditiveEngine.h`) plays the notes; this class gives it the host's parameters, MIDI and tempo and adds Tonwerk's
 * delay and hall and the master level. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in.
 */
class AdditiveProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    AdditiveProcessor();
    ~AdditiveProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Additiv"; }
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
    /** The knobs as the engine reads them; lock-free, for the audio thread (and the display). */
    Settings readSettings() const;
    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }
    bool roomPlaying() const { return effects.roomPlaying(); }

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;

    /** The newest note as the display draws it: its frequency (0 while nothing sounds) and its partials. */
    std::atomic<float> shownFrequency { 0.0f };
    std::array<std::atomic<float>, kPartials> shownLevels {};
    std::array<std::atomic<float>, kPartials> shownRatios {};

private:
    void timerCallback() override;
    void handle(const juce::MidiMessage& message, const Settings& s);
    tonwerk::Effects effectsFor() const;
    void publishPicture();

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> voices;
    Voice::Picture picture;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveProcessor)
};
} // namespace tonwerkadd
