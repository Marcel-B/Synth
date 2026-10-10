#pragma once

#include "DxSysex.h"
#include "EffectsChain.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

namespace tonwerkdx
{
/**
 * Every field of a `DxPatch` as a host parameter, in one table: the layout, reading the patch each block and writing
 * a preset or a SysEx voice all come from it. Ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in (they start at 1).
 */
struct DxParameter
{
    enum class Kind { integer, number, choice, toggle };

    juce::String id;
    juce::String name;
    Kind kind;
    float min = 0.0f;
    float max = 99.0f;
    juce::StringArray choices;
    std::function<juce::String(float)> text;
    std::function<float(const DxPatch&)> get;
    std::function<void(DxPatch&, float)> set;
};

const std::vector<DxParameter>& parameters();

/** One note on a `DxVoice`; switching patches while it sounds changes it at once, as on the other Tonwerk synths. */
class DxSynthVoice : public juce::SynthesiserVoice
{
public:
    explicit DxSynthVoice(const DxPatch& shared, const double& bend) : patch(shared), bendSemitones(bend) {}

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void setCurrentPlaybackSampleRate(double rate) override;
    void renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count) override;
    using SynthesiserVoice::renderNextBlock;

private:
    const DxPatch& patch;
    const double& bendSemitones;
    DxVoice voice;
    std::uint32_t seed = 0x9e3779b9u;
};

/**
 * Tonwerk DX: six operators, the DX7's 32 algorithms and four-rate envelopes, sixteen voices, then Tonwerk's delay
 * and hall. Factory sounds are this plugin's own; DX7 SysEx banks the user has are read with `importSysex` and kept
 * under Application Support, so every instance lists them.
 */
class DxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kVoices = 16;
    static constexpr double kBendRange = 2.0;

    struct Bank
    {
        juce::String name;
        std::vector<NamedDxPatch> voices;
    };

    DxProcessor();
    ~DxProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk DX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0 + 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The parameters' patch; lock-free, for the audio thread. */
    DxPatch readPatch() const;
    /** Sets every parameter to the patch and names it; on the message thread. */
    void applyPatch(const DxPatch& patch, const juce::String& name);
    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }

    /** Where imported banks live: ~/Library/Application Support/Tonwerk DX/SysEx on the Mac. */
    static juce::File sysexFolder();
    /** Reads a DX7 file and keeps a copy in `folder`; returns how many voices it has (0 if none). */
    static int importSysex(const juce::File& file, const juce::File& folder = sysexFolder());
    static std::vector<Bank> banks(const juce::File& folder = sysexFolder());

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    tonwerkui::ScopeBuffer scope;

private:
    void timerCallback() override;
    tonwerk::Effects effectsFor(const DxPatch& p) const;

    std::vector<std::atomic<float>*> values;
    DxPatch shared;
    double bend = 0.0;
    juce::Synthesiser synth;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> voiceBuffer;
    int currentProgram = 0;
    std::atomic<double> bpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DxProcessor)
};
} // namespace tonwerkdx
