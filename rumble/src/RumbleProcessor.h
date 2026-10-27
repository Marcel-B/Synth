#pragma once

#include "EffectPresets.h"
#include "RumbleEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tonwerkrumble
{
/**
 * Tonwerk Rumble: a techno rumble made from the kick it sits on (`RumbleEngine.h`). Parameter ids are part of saved
 * Logic projects: never rename or remove one, and append new ones with the version hint of the release they first
 * appear in. Menu orders are saved too: append only.
 */
class RumbleProcessor : public juce::AudioProcessor
{
public:
    /** The editor's picture: this many columns of `kColumnSeconds` each, the newest at `written - 1`. */
    static constexpr int kColumns = 240;
    static constexpr double kColumnSeconds = 0.01;

    RumbleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Rumble"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest hall rings this long before it is gone. */
    double getTailLengthSeconds() const override { return 10.0; }

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

    /** The last `kColumns` columns: the kick's peak, the rumble's peak and the ducking's lowest gain in each. */
    std::array<std::atomic<float>, kColumns> kickHistory {}, rumbleHistory {}, duckHistory {};
    std::atomic<int> written { 0 };
    /** Where the lowpass stands now (ducking included), Hz. */
    std::atomic<float> cutoffNow { 140.0f };

private:
    void addColumn();

    Engine engine;
    double sampleRate = 48000.0;
    /** The song's beat when the transport stands, carried on at its tempo. */
    double running = 0.0;
    int columnSamples = 480, columnFilled = 0;
    float columnKick = 0.0f, columnRumble = 0.0f, columnDuck = 1.0f;
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleProcessor)
};
} // namespace tonwerkrumble
