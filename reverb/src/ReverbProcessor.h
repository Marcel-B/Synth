#pragma once

#include "ReverbEngine.h"
#include "EffectPresets.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkreverb
{
/**
 * Tonwerk Reverb: an algorithmic reverb of four spaces (room, hall, plate, cathedral; `ReverbEngine.h`). Mono in is
 * played as stereo. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in. Menu orders are saved too: append only.
 */
class ReverbProcessor : public juce::AudioProcessor
{
public:
    ReverbProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest decay with the bass stretched; a frozen tail goes on until freeze is released. */
    double getTailLengthSeconds() const override { return 30.0; }

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
    /** For the display: the wet's level in the last block, its gain from ducking and the host's tempo. */
    std::atomic<float> tailLevel { 0.0f }, duckGain { 1.0f };
    std::atomic<double> bpm { 120.0 };

private:
    Engine engine;
    std::vector<float> spare;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbProcessor)
};
} // namespace tonwerkreverb
