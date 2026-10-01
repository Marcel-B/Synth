#pragma once

#include "EffectsChain.h"
#include "Parameters.h"
#include "PresetLibrary.h"
#include "dsp/AnalogVoice.h"
#include "dsp/FmVoice.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace tonwerk
{
/** What every voice reads on each block: the patch from the parameters and the pitch wheel. */
struct SharedState
{
    Patch patch;
    double bend = 0.0;
};

/** One note, on whichever engine the patch named when it started; switching engines does not cut it off. */
class SynthVoice : public juce::SynthesiserVoice
{
public:
    explicit SynthVoice(const SharedState& state) : shared(state) {}

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void setCurrentPlaybackSampleRate(double rate) override;
    void renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count) override;
    using SynthesiserVoice::renderNextBlock;

private:
    const SharedState& shared;
    Engine engine = Engine::analog;
    AnalogVoice analog;
    FmVoice fm;
};

class TonwerkSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kVoices = 16;
    /** The pitch wheel's reach in semitones, either way. */
    static constexpr double kBendRange = 2.0;

    TonwerkSynthProcessor();
    ~TonwerkSynthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest a note can still ring: the slowest release plus the room. */
    double getTailLengthSeconds() const override { return 6.0 + 8.0; }

    /** The factory sounds as the host's programs; imported ones are in the editor's menu only. */
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /** Takes over a sound in Tonwerk's JSON; on the message thread. */
    void applyPatch(const juce::var& json, const juce::String& name);
    /** The current sound in Tonwerk's JSON, for saving to a file. */
    juce::var currentPatchJson() const;

    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    PresetLibrary library;

private:
    void timerCallback() override;

    ParameterReader reader;
    SharedState shared;
    juce::Synthesiser synth;
    EffectsChain effects;
    juce::AudioBuffer<float> voiceBuffer;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonwerkSynthProcessor)
};
} // namespace tonwerk
