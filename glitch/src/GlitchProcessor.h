#pragma once

#include "GlitchEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace chromeglitch
{
/**
 * Chrome Glitch: an audio effect for a voice whose speech chrome fails. The engine (`GlitchEngine.h`) does the sound;
 * this class gives it the host's parameters and tempo. Parameter ids are part of saved Logic projects: never rename or
 * remove one, and append new ones with the version hint of the release they first appear in.
 */
class ChromeGlitchProcessor : public juce::AudioProcessor
{
public:
    ChromeGlitchProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chrome Glitch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;

    juce::AudioProcessorValueTreeState state;
    /** Whether a glitch sounded in the last block, for the editor's monitor. */
    std::atomic<bool> glitching { false };
    /** Set when a glitch starts; the monitor takes it back, so even one shorter than its frame flickers. */
    std::atomic<bool> glitchStarted { false };
    /** The latest glitch's `GlitchEngine::Event`, as an int. */
    std::atomic<int> lastEvent { 0 };

private:
    GlitchEngine engine;
    std::uint32_t eventsSeen = 0;

    struct Values
    {
        std::atomic<float>* amount;
        std::atomic<float>* stutterChance;
        std::atomic<float>* stutterLength;
        std::atomic<float>* repeats;
        std::atomic<float>* accelerate;
        std::atomic<float>* dropoutChance;
        std::atomic<float>* dropoutLength;
        std::atomic<float>* crush;
        std::atomic<float>* pitchChance;
        std::atomic<float>* pitchRange;
        std::atomic<float>* sync;
        std::atomic<float>* division;
        std::atomic<float>* chaos;
        std::atomic<float>* freeze;
        std::atomic<float>* mix;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromeGlitchProcessor)
};
} // namespace chromeglitch
