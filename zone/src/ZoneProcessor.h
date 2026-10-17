#pragma once

#include "ZoneEngine.h"
#include "EffectPresets.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace tonwerkzone
{
/** A ratio as the knob shows it: "1.5:1" below two, "4:1" from there. */
juce::String ratioText(float ratio);

/**
 * Tonwerk Zone: a compressor on one range of the spectrum, with a side chain input (`ZoneEngine.h`). Mono in is played
 * as stereo; the side chain is a second input bus, off until the host connects one. Parameter ids are part of saved
 * Logic projects: never rename or remove one, and append new ones with the version hint of the release they first
 * appear in. Menu orders are saved too: append only.
 */
class ZoneProcessor : public juce::AudioProcessor
{
public:
    ZoneProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Zone"; }
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
    /** For the meter: the deepest reduction (dB) and the detector's peak (dBFS) since the editor last took them. */
    std::atomic<float> reduction { 0.0f }, level { -100.0f };
    /** Whether the host feeds the side chain, for the editor's hint. */
    std::atomic<bool> keyConnected { false };

private:
    Engine engine;
    std::vector<float> spare, keyCopy;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoneProcessor)
};
} // namespace tonwerkzone
