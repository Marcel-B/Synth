#pragma once

#include "RobovoxEngine.h"
#include "ScopeBuffer.h"

#include "EffectPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkrobovox
{
/**
 * Tonwerk Robovox: the robot voice (`RobovoxEngine.h`) as an effect that also takes MIDI (an Audio Unit music effect,
 * `aumf`), so Logic can send it a track's notes for the carrier through the plugin's MIDI side chain. Without MIDI the
 * carrier follows the voice. Mono inside: a stereo voice is summed and the robot goes to both sides. Parameter ids are
 * part of saved Logic projects: never rename or remove one, and append new ones with the version hint of the release
 * they first appear in. Menu orders are saved too: append only.
 */
class RobovoxProcessor : public juce::AudioProcessor
{
public:
    RobovoxProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Robovox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    /** The voice in and the robot out, for the editor's oscilloscope. */
    tonwerkui::ScopeBuffer scopeIn, scopeOut;
    /** For the editor's display: the carrier's note (MIDI, -1 for none) and the voice's pitch (-1 while silent). */
    std::atomic<int> carrierNote { -1 };
    std::atomic<float> sungPitch { -1.0f };

private:
    void handleMidi(const juce::MidiMessage&);

    Engine engine;
    std::vector<float> mono;
    /** The keys held down, newest last: the carrier plays the newest, and the one before it when that is let go. */
    std::vector<int> held;

    struct Values
    {
        std::atomic<float>* mode;
        std::atomic<float>* key;
        std::atomic<float>* scale;
        std::atomic<float>* octave;
        std::atomic<float>* glide;
        std::atomic<float>* wave;
        std::atomic<float>* formant;
        std::atomic<float>* bandwidth;
        std::atomic<float>* sibilance;
        std::atomic<float>* bass;
        std::atomic<float>* mix;
        std::atomic<float>* output;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RobovoxProcessor)
};

/** A note's name as Logic shows it: C3 is MIDI note 60; German names, H for B natural. */
juce::String noteName(int midi);
} // namespace tonwerkrobovox
