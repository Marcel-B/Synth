#pragma once

#include "BassEngine.h"
#include "EffectsChain.h"
#include "ScopeBuffer.h"

#include "MidiLearn.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkbass
{
/**
 * Tonwerk Bass: an acid bass in the manner of Roland's TB-303, one voice with slide and accent. The engine
 * (`BassEngine.h`) plays the notes; this class gives it the host's parameters, MIDI and tempo and adds Tonwerk's delay
 * and hall and the master level. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in.
 */
class BassProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BassProcessor();
    ~BassProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Bass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest amp decay and the longest hall. */
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
    /** A MIDI note's name as Logic writes it (C3 = 60). */
    static juce::String noteName(int note);

    juce::AudioProcessorValueTreeState state;
    /** CC to knob, learnt in the editor (right click on a knob), kept in the state. */
    tonwerkui::MidiLearn midiLearn { state };
    juce::MidiKeyboardState keyboardState;
    tonwerkui::ScopeBuffer scope;

    /** For the display: the note sounding (-1 none), and whether it is accented or sliding. */
    std::atomic<int> shownNote { -1 };
    std::atomic<bool> shownAccent { false };
    std::atomic<bool> shownSlide { false };

private:
    void timerCallback() override;
    void handle(const juce::MidiMessage& message, const Settings& s);
    tonwerk::Effects effectsFor() const;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassProcessor)
};
} // namespace tonwerkbass
