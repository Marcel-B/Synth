#pragma once

#include "DrumsEngine.h"
#include "EffectsChain.h"

#include "MidiLearn.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkdrums
{
/**
 * Tonwerk Drums: Tonwerk Groovebox's synthesised drums as an instrument to play, without a sequencer. Sixteen pads on
 * General MIDI's drum notes (each pad's note can be moved), delay and hall on sends, drive and master on the sum.
 * Parameter ids are part of saved Logic projects: never rename or remove one, and append new ones with the version
 * hint of the release they first appear in.
 */
class DrumsProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DrumsProcessor();
    ~DrumsProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Drums"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest cymbal and the longest hall. */
    double getTailLengthSeconds() const override { return 8.0 + 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;
    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }
    bool roomPlaying() const { return effects.roomPlaying(); }

    /** Plays a pad at once, as a click on it does. */
    void audition(int pad, float velocity = 0.9f);

    juce::AudioProcessorValueTreeState state;
    /** CC to knob, learnt in the editor (right click on a knob), kept in the state. */
    tonwerkui::MidiLearn midiLearn { state };

    /** Each pad's loudest sample since the editor last looked; the editor swaps in 0. */
    std::array<std::atomic<float>, kPads> padPeaks {};
    /** The pad whose knobs the editor shows; kept here so it survives closing the window. */
    std::atomic<int> selectedPad { 0 };

    /** Hits per pad since `prepareToPlay`, for the tests. */
    int hitCount(int pad) const { return engine.hitCount(pad); }

private:
    void timerCallback() override;
    tonwerk::Effects effectsFor() const;

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> work;
    int currentProgram = 0;
    std::atomic<double> bpm { 120.0 };
    /** Pads clicked in the editor (bit per pad) and their velocity in 1/127. */
    std::atomic<std::uint32_t> auditions { 0 };
    std::atomic<int> auditionVelocity { 114 };

    struct PadValues
    {
        std::atomic<float>*tune, *decay, *tone, *character, *level, *pan, *delay, *reverb, *mute;
        std::atomic<float>*attack, *hold, *pitchMod, *modDecay, *filter, *cutoff, *resonance, *filterMod, *drive, *bits,
            *crush, *velocity, *random, *machine, *note;
    };
    std::array<PadValues, kPads> padValues {};
    std::atomic<float>*drive = nullptr, *master = nullptr;
    struct FxValues
    {
        std::atomic<float>*delayMix, *delaySync, *delayTime, *delayDivision, *delayFeedback, *delayTone, *reverbMix,
            *reverbDecay, *on;
    } fx {};
    /** How long switching the effects off or on fades what they add. */
    static constexpr double kBypassSeconds = 0.02;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetGain { 1.0f };
    /** Whether the delay and hall were emptied since the effects went off. */
    bool effectsCleared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumsProcessor)
};
} // namespace tonwerkdrums
