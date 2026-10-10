#pragma once

#include "EffectsChain.h"
#include "PhysicalEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkphys
{
/**
 * Tonwerk Physical: instruments built from their physics, a string, a tube or a struck body set going by a pluck,
 * a strike, a bow or breath. The engine (`PhysicalEngine.h`) plays the notes; this class gives it the host's
 * parameters, MIDI and tempo and adds Tonwerk's delay and hall and the master level. Parameter ids are part of saved
 * Logic projects: never rename or remove one, and append new ones with the version hint of the release they first
 * appear in.
 */
class PhysicalProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PhysicalProcessor();
    ~PhysicalProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Physical"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest ring and the longest hall. */
    double getTailLengthSeconds() const override { return 20.0 + 8.0; }

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

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;

    /** The newest note as the display draws it: the resonator (-1 while nothing sounds), its wave or its modes. */
    std::atomic<int> shownResonator { -1 };
    std::atomic<float> shownFrequency { 0.0f };
    std::atomic<float> shownPosition { 0.2f };
    std::atomic<int> shownPoints { 0 };
    std::array<std::atomic<float>, Voice::kShape> shownShape {};
    std::atomic<int> shownModes { 0 };
    std::array<std::atomic<float>, kModes> shownRatios {};
    std::array<std::atomic<float>, kModes> shownLevels {};

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicalProcessor)
};
} // namespace tonwerkphys
