#pragma once

#include "ArpEngine.h"

#include "EffectPresets.h"

#include "MidiLearn.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tonwerkarp
{
/**
 * Tonwerk Arp: the arpeggiator and step sequencer (`ArpEngine.h`) as a MIDI effect (an Audio Unit `aumi`, Logic's
 * MIDI FX slot). No audio: keys come in, notes go out; everything else on the MIDI input passes through. Parameter ids
 * are part of saved Logic projects: never rename or remove one, and append new ones with the version hint of the
 * release they first appear in. Menu orders are saved too: append only.
 */
class ArpProcessor : public juce::AudioProcessor
{
public:
    ArpProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Arp"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return programs.count(); }
    int getCurrentProgram() override { return programs.current(); }
    void setCurrentProgram(int index) override { programs.choose(index); }
    const juce::String getProgramName(int index) override { return programs.name(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The parameters as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;

    /** A step's parameter id: `step1On`, `step16Note`, ... (`field` On, Note, Velocity or Tie). */
    static juce::String stepId(int step, const char* field);

    juce::AudioProcessorValueTreeState state;
    /** CC to knob, learnt in the editor (right click on a knob), kept in the state. */
    tonwerkui::MidiLearn midiLearn { state };
    tonwerkui::EffectPrograms programs;

    /** For the editor: the step playing (-1 for none), the last note started, the keys held (up to eight). */
    std::atomic<int> playingStep { -1 };
    std::atomic<int> lastNote { -1 };
    std::atomic<int> keyCount { 0 };
    std::array<std::atomic<int>, 8> keys {};

private:
    Engine engine;
    std::vector<KeyEvent> events;
    juce::MidiBuffer out;

    struct StepValues
    {
        std::atomic<float>*on, *note, *velocity, *tie;
    };
    struct Values
    {
        std::atomic<float>*mode, *order, *octaves, *division, *gate, *swing, *length, *latch, *velocitySource,
            *fixedVelocity, *transpose;
        std::array<StepValues, kSteps> steps;
    } values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpProcessor)
};

/** A note's name as Logic shows it: C3 is MIDI note 60; German names, H for B natural. */
juce::String noteName(int midi);
} // namespace tonwerkarp
