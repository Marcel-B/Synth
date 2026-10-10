#pragma once

#include "Ds1Circuit.h"
#include "ScopeBuffer.h"

#include "EffectPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace tonwerkdistortion
{
/**
 * Tonwerk Distortion: the DS-1's circuit (`Ds1Circuit.h`) as an audio effect. The clipping stages run four times
 * oversampled with JUCE's polyphase IIR filters, the cheapest of its oversamplers, so the hard clip does not fold
 * back into the audible band; tone and level run at the host's rate. On a mono track only one channel is computed. Parameter ids are part of saved Logic projects:
 * never rename or remove one, and append new ones with the version hint of the release they first appear in.
 */
class DistortionProcessor : public juce::AudioProcessor
{
public:
    DistortionProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Distortion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    /** The factory sounds as the host's programs. */
    int getNumPrograms() override { return programs.count(); }
    int getCurrentProgram() override { return programs.current(); }
    void setCurrentProgram(int index) override { programs.choose(index); }
    const juce::String getProgramName(int index) override { return programs.name(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the circuit reads them; lock-free, for the audio thread. */
    Settings readSettings() const;

    juce::AudioProcessorValueTreeState state;
    tonwerkui::EffectPrograms programs;
    /** The first channel before and after, for the editor's oscilloscope. */
    tonwerkui::ScopeBuffer scopeIn, scopeOut;

private:
    /** Two stages of 2x: 4x. 2x lets a high note fold back 20 dB more; 8x halves the speed for little more. */
    static constexpr int kOversamplingStages = 2;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<Drive, 2> drives;
    std::array<Voice, 2> voices;
    int maxBlock = 0;

    struct Values
    {
        std::atomic<float>* input;
        std::atomic<float>* distortion;
        std::atomic<float>* tone;
        std::atomic<float>* level;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionProcessor)
};
} // namespace tonwerkdistortion
