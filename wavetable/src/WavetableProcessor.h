#pragma once

#include "WaveEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace tonwerkwave
{
/**
 * Tonwerk Wavetable: a wavetable synthesizer for basses and leads, Serum's idea in this repository's own code. The
 * engine (`WaveEngine.h`) plays the notes; this class gives it the host's parameters, MIDI and tempo, and adds the
 * distortion and the master level. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in.
 */
class WavetableProcessor : public juce::AudioProcessor
{
public:
    WavetableProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Wavetable"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest release. */
    double getTailLengthSeconds() const override { return 10.0; }

    /** The factory sounds as the host's programs. */
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

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;

    /** Where in their tables the newest note's oscillators play, in frames, or -1 when nothing sounds. */
    std::array<std::atomic<float>, 2> playedPosition { -1.0f, -1.0f };

private:
    void handle(const juce::MidiMessage& message, const Settings& s);
    void applyDistortion(juce::AudioBuffer<float>& buffer, const DistortionSettings& d);

    Engine engine;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    /** The second channel when the host gives a mono bus, and the dry signal the distortion mixes against. */
    juce::AudioBuffer<float> monoScratch, dry;
    std::array<float, 2> dcIn {}, dcOut {};
    float dcPole = 0.999f;
    int maxBlock = 512;
    int currentProgram = 0;
    int wheel = 8192;

    /** The raw parameter values, in the order of `createParameterLayout`. */
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableProcessor)
};
} // namespace tonwerkwave
