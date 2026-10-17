#pragma once

#include "EffectPresets.h"
#include "ScopeBuffer.h"
#include "VocoderEngine.h"

#include "MidiLearn.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkvocoder
{
/**
 * Tonwerk Vocoder: the classic channel vocoder (`VocoderEngine.h`), the voice on the input and an eight-voice synth
 * played by MIDI as the carrier. An Audio Unit music effect (`aumf`) like Tonwerk Robovox, so in Logic it sits in an
 * instrument slot with the voice's track as its side chain and the track's notes playing the carrier. Mono voice in
 * (a stereo take summed), stereo out. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in. Menu orders are saved too: append only.
 */
class VocoderProcessor : public juce::AudioProcessor
{
public:
    VocoderProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Vocoder"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest carrier release. */
    double getTailLengthSeconds() const override { return 5.0; }

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
    /** A MIDI note's name as Logic writes it (C3 = 60). */
    static juce::String noteName(int note);

    juce::AudioProcessorValueTreeState state;
    /** CC to knob, learnt in the editor (right click on a knob), kept in the state. */
    tonwerkui::MidiLearn midiLearn { state };
    tonwerkui::EffectPrograms programs;
    /** The voice in and the vocoder out (left), for the editor's oscilloscope. */
    tonwerkui::ScopeBuffer scopeIn, scopeOut;
    /** For the display: each band's voice level, how many bands there are, and the notes the carrier plays. */
    std::array<std::atomic<float>, kMaxBands> bandLevels {};
    std::atomic<int> bandCount { 16 };
    std::array<std::atomic<int>, kVoices> carrierNotes {};

private:
    void handleMidi(const juce::MidiMessage&, const Settings&);

    Engine engine;
    std::vector<float> mono, side;
    std::vector<std::atomic<float>*> values;
    int wheel = 8192;
    bool latched = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderProcessor)
};
} // namespace tonwerkvocoder
