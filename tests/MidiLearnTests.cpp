#include "BassProcessor.h"
#include "LfoProcessor.h"
#include "MidiLearn.h"
#include "NightCity.h"

#include <juce_core/juce_core.h>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

float cutoffOf(tonwerkbass::BassProcessor& bass) { return bass.state.getRawParameterValue("cutoff")->load(); }

/** One block of `midi` through the instrument. */
void play(juce::AudioProcessor& p, juce::MidiBuffer midi)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    p.processBlock(buffer, midi);
}

juce::MidiBuffer controller(int number, int value)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, number, value), 0);
    return midi;
}
} // namespace

class MidiLearnTests : public juce::UnitTest
{
public:
    MidiLearnTests() : juce::UnitTest("MIDI learn", "LFO") {}

    void runTest() override
    {
        using tonwerkbass::BassProcessor;
        const auto& range = [] {
            BassProcessor probe;
            return probe.state.getParameter("cutoff")->getNormalisableRange();
        }();

        beginTest("A learnt controller moves its knob; the next one learnt replaces it");
        {
            BassProcessor bass;
            bass.setPlayConfigDetails(0, 2, kRate, kBlock);
            bass.prepareToPlay(kRate, kBlock);
            auto& learn = bass.midiLearn;
            expect(tonwerkui::MidiLearn::of(bass.state) == &learn);
            learn.learn("cutoff");
            expectEquals(learn.learning(), juce::String("cutoff"));
            // A fine half (32 to 63) and a channel mode message are not learnt; the first plain controller is.
            play(bass, controller(52, 10));
            play(bass, controller(123, 0));
            expectEquals(learn.controllerFor("cutoff"), -1);
            play(bass, controller(21, 127));
            learn.commitLearnt();
            expectEquals(learn.controllerFor("cutoff"), 21);
            expect(learn.learning().isEmpty());
            expectWithinAbsoluteError(cutoffOf(bass), range.end, 1.0f);
            play(bass, controller(21, 0));
            expectWithinAbsoluteError(cutoffOf(bass), range.start, 1.0f);
            // Unmapped controllers change nothing.
            play(bass, controller(22, 64));
            expectWithinAbsoluteError(cutoffOf(bass), range.start, 1.0f);

            learn.learn("cutoff");
            play(bass, controller(1, 64));
            learn.commitLearnt();
            expectEquals(learn.controllerFor("cutoff"), 1);
            play(bass, controller(21, 127));
            expectWithinAbsoluteError(cutoffOf(bass), range.convertFrom0to1(64.0f / 127.0f), 1.0f);

            learn.forget("cutoff");
            expectEquals(learn.controllerFor("cutoff"), -1);
        }

        beginTest("14 bits: the fine half on the controller + 32 refines the coarse one");
        {
            BassProcessor bass;
            bass.setPlayConfigDetails(0, 2, kRate, kBlock);
            bass.prepareToPlay(kRate, kBlock);
            bass.midiLearn.assign(20, "cutoff");
            // 7 bits alone span the whole knob.
            play(bass, controller(20, 127));
            expectWithinAbsoluteError(cutoffOf(bass), range.end, 1.0f);
            // A pair: 64 and 37 is 8229 of 16383.
            juce::MidiBuffer pair;
            pair.addEvent(juce::MidiMessage::controllerEvent(1, 20, 64), 0);
            pair.addEvent(juce::MidiMessage::controllerEvent(1, 52, 37), 0);
            play(bass, pair);
            expectWithinAbsoluteError(range.convertTo0to1(cutoffOf(bass)), 8229.0f / 16383.0f, 1.0e-4f);
        }

        beginTest("The mapping is saved with the project and comes back with it");
        {
            BassProcessor bass;
            bass.midiLearn.assign(20, "cutoff");
            bass.midiLearn.assign(21, "resonance");
            juce::MemoryBlock saved;
            bass.getStateInformation(saved);
            BassProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.midiLearn.controllerFor("cutoff"), 20);
            expectEquals(loaded.midiLearn.controllerFor("resonance"), 21);
            // A factory sound keeps it.
            loaded.setCurrentProgram(3);
            expectEquals(loaded.midiLearn.controllerFor("cutoff"), 20);
        }

        beginTest("The knob shows its controller");
        {
            BassProcessor bass;
            bass.midiLearn.assign(20, "cutoff");
            tonwerkui::Control knob(bass.state, "cutoff", "Cutoff");
            expectEquals(knob.shownName(), juce::String::fromUTF8("Cutoff · CC20"));
            tonwerkui::Control other(bass.state, "resonance", "Resonanz");
            expectEquals(other.shownName(), juce::String("Resonanz"));
        }

        beginTest("Tonwerk LFO into an instrument: the knob follows the LFO");
        {
            tonwerklfo::LfoProcessor lfo;
            BassProcessor bass;
            lfo.prepareToPlay(kRate, kBlock);
            bass.setPlayConfigDetails(0, 2, kRate, kBlock);
            bass.prepareToPlay(kRate, kBlock);
            bass.midiLearn.assign(20, "cutoff");
            auto* sync = lfo.state.getParameter("sync");
            sync->setValueNotifyingHost(0.0f);
            auto* rate = lfo.state.getParameter("rate");
            rate->setValueNotifyingHost(rate->convertTo0to1(2.0f));
            juce::AudioBuffer<float> none(0, kBlock), audio(2, kBlock);
            float lowest = 1.0f, highest = 0.0f;
            for (int block = 0; block < (int) (kRate / kBlock); ++block)
            {
                juce::MidiBuffer midi;
                lfo.processBlock(none, midi);
                bass.processBlock(audio, midi);
                const float at = range.convertTo0to1(cutoffOf(bass));
                lowest = std::min(lowest, at);
                highest = std::max(highest, at);
            }
            expectLessThan(lowest, 0.01f);
            expectGreaterThan(highest, 0.99f);
        }
    }
};

static MidiLearnTests midiLearnTests;
