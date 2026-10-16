#pragma once

#include "EffectPresets.h"
#include "StutterEngine.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkstutter
{
/**
 * Tonwerk Stutter: a beat repeat on the song's grid (`StutterEngine.h`). Parameter ids are part of saved Logic
 * projects: never rename or remove one, and append new ones with the version hint of the release they first appear
 * in. Menu orders are saved too: append only.
 */
class StutterProcessor : public juce::AudioProcessor
{
public:
    StutterProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Stutter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** A repeat may run on after the input stops: up to four bars at 60 BPM. */
    double getTailLengthSeconds() const override { return 16.0; }

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
    /** For the display: where the song is in the interval (0 to 1), and the repeat sounding (-1 none, 0 the live slice). */
    std::atomic<float> positionNow { 0.0f };
    std::atomic<int> repeatNow { -1 };

private:
    Engine engine;
    double sampleRate = 48000.0;
    /** The beat, run on at the tempo while the song stands still. */
    double running = 0.0;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StutterProcessor)
};
} // namespace tonwerkstutter
