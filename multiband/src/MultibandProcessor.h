#pragma once

#include "EffectPresets.h"
#include "MultibandEngine.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tonwerkmultiband
{
/** The bands' names in the UI and their parameter ids' prefixes, low to high. */
inline constexpr std::array<const char*, kBands> kBandNames { "Tief", "Mitte", "Hoch" };
inline constexpr std::array<const char*, kBands> kBandIds { "low", "mid", "high" };

/** A ratio as the knob shows it: "1.5:1" below two, "4:1" from there. */
juce::String ratioText(float ratio);

/**
 * Tonwerk Multiband: a three-band compressor, downward and upward (`MultibandEngine.h`). Mono in is played as stereo.
 * Parameter ids are part of saved Logic projects: never rename or remove one, and append new ones with the version
 * hint of the release they first appear in.
 */
class MultibandProcessor : public juce::AudioProcessor
{
public:
    MultibandProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Multiband"; }
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
    /** For the meter, per band: the detector's peak (dBFS) and the largest gain change (dB) since the editor last looked. */
    std::array<std::atomic<float>, kBands> levels {}, changes {};

private:
    Engine engine;
    std::vector<float> spare;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandProcessor)
};
} // namespace tonwerkmultiband
