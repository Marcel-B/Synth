#pragma once

#include "EffectPresets.h"
#include "FilterEngine.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkfilter
{
/**
 * Tonwerk Filter: a multimode filter moved by an envelope follower and an LFO (`FilterEngine.h`). Parameter ids are
 * part of saved Logic projects: never rename or remove one, and append new ones with the version hint of the release
 * they first appear in. Menu orders are saved too: append only.
 */
class FilterProcessor : public juce::AudioProcessor
{
public:
    FilterProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Filter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** A resonant comb or ladder rings on a little after the input stops. */
    double getTailLengthSeconds() const override { return 0.5; }

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
    /** For the display: where the left filter's cutoff stands, and the follower's level. */
    std::atomic<float> cutoffNow { 1000.0f }, envelopeNow { 0.0f };

private:
    Engine engine;
    double sampleRate = 48000.0;
    /** The LFO's phase in cycles, carried from block to block when it is not locked to the song. */
    double running = 0.0;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterProcessor)
};
} // namespace tonwerkfilter
