#pragma once

#include "LfoEngine.h"

#include "EffectPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerklfo
{
/**
 * Tonwerk LFO: an LFO as a MIDI effect (an Audio Unit `aumi`, Logic's MIDI FX slot) that sends a controller to the
 * instrument after it (`LfoEngine.h`); the instrument moves whichever knob learnt that controller (right click on a
 * knob in a Tonwerk instrument). No audio; everything on the MIDI input passes through. Parameter ids are part of
 * saved Logic projects: never rename or remove one, and append new ones with the version hint of the release they
 * first appear in. Menu orders are saved too: append only.
 */
class LfoProcessor : public juce::AudioProcessor
{
public:
    LfoProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk LFO"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
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

    /** For the editor: the phase (cycles, 0 to 1) and the value (0 to 1) at the end of the last block. */
    std::atomic<float> phaseNow { 0.0f }, valueNow { 0.5f };

private:
    Engine engine;
    double rate = 48000.0;
    /** The phase in cycles, carried from block to block when it is not locked to the song. */
    double running = 0.0;
    /** Keys held, for Start note: the first key of a phrase restarts the cycle. */
    int held = 0;
    int lastController = -1, lastChannel = -1;
    bool lastFine = false;
    std::vector<int> restarts;
    juce::MidiBuffer out;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoProcessor)
};
} // namespace tonwerklfo
