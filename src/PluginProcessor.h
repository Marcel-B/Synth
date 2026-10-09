#pragma once

#include "EffectsChain.h"
#include "Parameters.h"
#include "PresetLibrary.h"
#include "ScopeBuffer.h"
#include "dsp/AnalogVoice.h"
#include "dsp/FmVoice.h"
#include "dsp/Tempo.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>

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

using tonwerkui::ScopeBuffer;

/**
 * Tonwerk Analog, Tonwerk FM or Tonwerk Synth, by `edition`: the same voices, effects and preset library, with the
 * edition's parameters and factory sounds. The one-engine editions always play their engine, whatever a sound says.
 */
class TonwerkSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    /** First, so the parameters below are made for it. */
    const Edition edition;

    static constexpr int kVoices = 16;
    /** The pitch wheel's reach in semitones, either way. */
    static constexpr double kBendRange = 2.0;

    explicit TonwerkSynthProcessor(Edition edition = Edition::combined);
    ~TonwerkSynthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return editionName(edition); }
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

    /** The plugin's name for an edition. */
    static juce::String editionName(Edition edition);
    /** The engine the edition plays, or none for Tonwerk Synth, which plays the one the sound names. */
    std::optional<Engine> fixedEngine() const;
    /** Whether a sound in Tonwerk's JSON is one this edition plays: Tonwerk Analog takes no FM sounds. */
    bool plays(const juce::var& json) const;

    /** Takes over a sound in Tonwerk's JSON, unless the edition does not play it; on the message thread. */
    bool applyPatch(const juce::var& json, const juce::String& name);
    /** The current sound in Tonwerk's JSON, for saving to a file. */
    juce::var currentPatchJson() const;

    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }

    /** The host's tempo as last seen, for the sync display. */
    double tempo() const { return bpm.load(); }

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    ScopeBuffer scope;
    PresetLibrary library;

private:
    void timerCallback() override;
    /** The parameters' patch, on the edition's engine. */
    Patch readPatch() const;

    ParameterReader reader;
    SharedState shared;
    juce::Synthesiser synth;
    EffectsChain effects;
    juce::AudioBuffer<float> voiceBuffer;
    int currentProgram = 0;
    std::atomic<double> bpm { kDefaultBpm };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonwerkSynthProcessor)
};
} // namespace tonwerk
