#pragma once

#include "ExpanderEngine.h"
#include "EffectsChain.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkexpander
{
/**
 * Tonwerk Expander: an eight-voice synth in the manner of the classic two-oscillator expander modules, its 12 dB filter
 * fading from lowpass through notch to highpass. The engine (`ExpanderEngine.h`) plays the notes; this class gives it
 * the host's parameters, MIDI and tempo and adds Tonwerk's delay and hall and the master level. Parameter ids are part of saved Logic projects: never rename or remove one, and
 * append new ones with the version hint of the release they first appear in.
 */
class ExpanderProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ExpanderProcessor();
    ~ExpanderProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Expander"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest release and the longest hall. */
    double getTailLengthSeconds() const override { return 5.0 + 8.0; }

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
    /** A MIDI note's name as Logic writes it (C3 = 60). */
    static juce::String noteName(int note);

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    tonwerkui::ScopeBuffer scope;

    /** For the display: the newest note sounding (-1 none), its filter's cutoff and how many voices sound. */
    std::atomic<int> shownNote { -1 }, shownVoices { 0 };
    std::atomic<float> shownCutoff { 1200.0f };

private:
    void timerCallback() override;
    void handle(const juce::MidiMessage& message, const Settings& s);
    tonwerk::Effects effectsFor() const;

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> voices;
    int currentProgram = 0;
    int wheel = 8192;
    std::atomic<double> bpm { 120.0 };

    /** The fx parameters' values, read apart from `readSettings` since the engine has no use for them. */
    struct FxValues
    {
        std::atomic<float>*delayMix, *delaySync, *delayTime, *delayDivision, *delayFeedback, *delayTone, *reverbMix,
            *reverbDecay;
    } fx {};
    std::vector<std::atomic<float>*> values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpanderProcessor)
};
} // namespace tonwerkexpander
