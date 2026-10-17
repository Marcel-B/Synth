#pragma once

#include "EffectPresets.h"
#include "ScopeEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkscope
{
/**
 * Tonwerk Scope: an oscilloscope that passes the audio through untouched and keeps what the editor draws
 * (`ScopeEngine.h`). Parameter ids are part of saved Logic projects: never rename or remove one, and append new ones
 * with the version hint of the release they first appear in. Menu orders are saved too: append only.
 */
class ScopeProcessor : public juce::AudioProcessor
{
public:
    ScopeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Scope"; }
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
    /** The knobs as the editor reads them. */
    Settings readSettings() const;

    juce::AudioProcessorValueTreeState state;
    tonwerkui::EffectPrograms programs;
    Ring ring;
    BeatTrace bars;
    /** The rate the samples in `ring` were played at, and the tempo, for the editor's scales. */
    std::atomic<double> rate { 48000.0 }, tempo { 120.0 };
    /** Whether the host's transport runs, so the bars mode can say whether it follows the song or its own clock. */
    std::atomic<bool> playing { false };

private:
    std::vector<float> spare;
    std::vector<std::atomic<float>*> values;
    /** The bars mode's own beat while the host stands still. */
    double freeBeat = 0.0;
    int lastWindow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeProcessor)
};
} // namespace tonwerkscope
