#pragma once

#include "EffectPresets.h"
#include "ScopeBuffer.h"
#include "SpreadEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkspread
{
/**
 * Tonwerk Spread: a stereo widener that keeps the mono sum (`SpreadEngine.h`). Mono in is played as stereo, which is
 * where it does the most. Parameter ids are part of saved Logic projects: never rename or remove one, and append new
 * ones with the version hint of the release they first appear in. Menu orders are saved too: append only.
 */
class SpreadProcessor : public juce::AudioProcessor
{
public:
    SpreadProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Spread"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest delay plus the allpasses' ring. */
    double getTailLengthSeconds() const override { return 0.2; }

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
    /** Both sides before and after, for the editor's goniometer. */
    tonwerkui::ScopeBuffer inLeft, inRight, outLeft, outRight;
    /**
     * For the meters: the correlation of the two sides (1 mono, 0 unrelated, -1 opposite) before and after, and the
     * side's share of the output's level (0 mono, 0.5 as much side as middle), each smoothed over about 300 ms.
     */
    std::atomic<float> correlationIn { 1.0f }, correlationOut { 1.0f }, sideShare { 0.0f };

private:
    Engine engine;
    std::vector<float> spare;
    std::vector<std::atomic<float>*> values;
    float smoothing = 0.1f;
    double inLR = 0.0, inLL = 0.0, inRR = 0.0, outLR = 0.0, outLL = 0.0, outRR = 0.0, outMM = 0.0, outSS = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpreadProcessor)
};
} // namespace tonwerkspread
