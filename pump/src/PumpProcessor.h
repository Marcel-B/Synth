#pragma once

#include "PumpEngine.h"
#include "EffectPresets.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkpump
{
/**
 * Tonwerk Pump: a sidechain ducker made for pumping (`PumpEngine.h`). The side chain is a second input bus, off until
 * the host connects one (Logic's side chain menu); mono in is played as stereo. Parameter ids are part of saved Logic
 * projects: never rename or remove one, and append new ones with the version hint of the release they first appear
 * in. Menu orders are saved too: append only.
 */
class PumpProcessor : public juce::AudioProcessor
{
public:
    PumpProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Pump"; }
    bool acceptsMidi() const override { return false; }
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
    /** The left side before and after, for the editor's oscilloscope. */
    tonwerkui::ScopeBuffer scopeIn, scopeOut;
    /**
     * For the meter: the deepest reduction (dB) and the key's loudest level (dBFS) since the editor last took them
     * (`exchange`), so a duck shorter than a frame still shows.
     */
    std::atomic<float> reduction { 0.0f }, level { -100.0f };
    /** Hits heard in trigger mode since the editor last took them. */
    std::atomic<int> hits { 0 };
    /** Whether the host feeds the side chain, for the editor's hint. */
    std::atomic<bool> keyConnected { false };

private:
    Engine engine;
    std::vector<float> spare, keyCopy;
    std::vector<std::atomic<float>*> values;
    int hitsBefore = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PumpProcessor)
};
} // namespace tonwerkpump
