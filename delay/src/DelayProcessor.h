#pragma once

#include "DelayEngine.h"
#include "EffectPresets.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkdelay
{
/**
 * Tonwerk Delay: ping-pong, stereo or mono echoes in the host's tempo, filters and saturation in the feedback,
 * ducking (`DelayEngine.h`). Mono in is played as stereo. Parameter ids are part of saved Logic projects: never rename
 * or remove one, and append new ones with the version hint of the release they first appear in. Menu orders are saved
 * too: append only.
 */
class DelayProcessor : public juce::AudioProcessor
{
public:
    DelayProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** Long repeats at a high feedback. */
    double getTailLengthSeconds() const override { return 20.0; }

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
    /** For the display: the tempo the time follows and the repeats' gain from ducking. */
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> duckGain { 1.0f };

private:
    Engine engine;
    std::vector<float> spare;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayProcessor)
};
} // namespace tonwerkdelay
