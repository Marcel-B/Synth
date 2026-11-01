#include "BassProcessor.h"
#include "GrooveProcessor.h"
#include "RigCatalogue.h"
#include "RigEditor.h"
#include "RigProcessor.h"

#include <juce_core/juce_core.h>

#include <set>

using namespace tonwerkrig;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** A host transport, playing from beat 0 at 120 BPM, advanced after each block. */
class Head : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        info.setTimeInSamples((juce::int64) (ppq * kRate / 2.0));
        return info;
    }
    double ppq = 0.0;
};

struct Note
{
    double start, length;
    int key;
};

/** Every output bus of `processor` rendered for `seconds`, the notes played on channel 1 at velocity 100. */
juce::AudioBuffer<float> render(juce::AudioProcessor& processor, const std::vector<Note>& notes, double seconds)
{
    Head head;
    processor.setPlayHead(&head);
    processor.setRateAndBufferSizeDetails(kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    const int total = (int) (seconds * kRate);
    const int channels = std::max(2, processor.getTotalNumOutputChannels());
    juce::AudioBuffer<float> out(channels, total);
    juce::AudioBuffer<float> block(channels, kBlock);
    for (int done = 0; done < total; done += kBlock)
    {
        const int count = std::min(kBlock, total - done);
        juce::MidiBuffer midi;
        for (const auto& note : notes)
        {
            const auto on = (int) (note.start * kRate) - done, off = (int) ((note.start + note.length) * kRate) - done;
            if (on >= 0 && on < count)
                midi.addEvent(juce::MidiMessage::noteOn(1, note.key, (juce::uint8) 100), on);
            if (off >= 0 && off < count)
                midi.addEvent(juce::MidiMessage::noteOff(1, note.key), off);
        }
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), channels, count);
        view.clear();
        processor.processBlock(view, midi);
        for (int c = 0; c < channels; ++c)
            out.copyFrom(c, done, view, c, 0, count);
        head.ppq += count / kRate * 2.0;
    }
    processor.setPlayHead(nullptr);
    return out;
}

float peakOf(const juce::AudioBuffer<float>& buffer, int channel, int from = 0, int to = -1)
{
    if (to < 0)
        to = buffer.getNumSamples();
    return buffer.getMagnitude(channel, from, to - from);
}

float rmsOf(const juce::AudioBuffer<float>& buffer, int channel) { return buffer.getRMSLevel(channel, 0, buffer.getNumSamples()); }

bool finite(const juce::AudioBuffer<float>& buffer)
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite(buffer.getSample(c, i)))
                return false;
    return true;
}

/** The largest difference between two buffers' first two channels. */
float differenceOf(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int shift = 0)
{
    float worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i + shift < a.getNumSamples() && i < b.getNumSamples(); ++i)
            worst = std::max(worst, std::abs(a.getSample(c, i + shift) - b.getSample(c, i)));
    return worst;
}

int programIndex(juce::AudioProcessor& processor, const juce::String& name)
{
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        if (processor.getProgramName(i) == name)
            return i;
    return -1;
}

const std::vector<Note> kPhrase { { 0.0, 0.4, 48 }, { 0.5, 0.4, 55 }, { 1.0, 0.9, 60 }, { 1.0, 0.9, 64 }, { 2.0, 0.3, 36 } };
/** General MIDI's drum notes: bass drum 36, snare 38. */
constexpr int kKick = 36, kSnare = 38;
} // namespace

class RigTests : public juce::UnitTest
{
public:
    RigTests() : juce::UnitTest("Tonwerk Rig", "Rig") {}

    void runTest() override
    {
        beginTest("Every factory rig names plugins and sounds that exist");
        {
            for (const auto& rig : factoryRigs())
            {
                const auto* instrument = find(instruments(), rig.instrument);
                expect(instrument != nullptr, juce::String::fromUTF8(rig.name));
                if (instrument != nullptr && *rig.program != 0)
                {
                    auto made = instrument->make();
                    expect(programIndex(*made, juce::String::fromUTF8(rig.program)) >= 0,
                           juce::String::fromUTF8(rig.name) + ": " + juce::String::fromUTF8(rig.program));
                }
                bool grouped = false;
                for (const auto* group : kRigCategories)
                    grouped = grouped || juce::String::fromUTF8(group) == juce::String::fromUTF8(rig.category);
                expect(grouped, juce::String::fromUTF8(rig.name));
                expect(rig.effects.size() <= (std::size_t) kSlots);
                for (const auto& effect : rig.effects)
                {
                    const auto* kind = find(effects(), effect.kind);
                    expect(kind != nullptr, juce::String::fromUTF8(rig.name) + ": " + effect.kind);
                    expect(effect.source == 0 || juce::String(rig.instrument) == kTracksInstrument, juce::String::fromUTF8(rig.name));
                    if (kind != nullptr && *effect.program != 0)
                    {
                        auto made = kind->make();
                        expect(programIndex(*made, juce::String::fromUTF8(effect.program)) >= 0,
                               juce::String::fromUTF8(rig.name) + ": " + juce::String::fromUTF8(effect.program));
                    }
                }
            }
        }

        beginTest("Every kind's id is unique and every plugin makes its processor");
        {
            std::set<juce::String> ids;
            for (const auto* kinds : { &instruments(), &effects() })
                for (const auto& kind : *kinds)
                {
                    expect(ids.insert(kind.id).second, kind.id);
                    auto made = kind.make();
                    expect(made != nullptr && made->getName() == kind.name, kind.name);
                }
        }

        beginTest("Every factory rig plays, finite, and its effects keep the instrument's level");
        {
            // The instruments' sounds are leveled on their own (PresetTests); a rig's effects may colour them, but
            // neither drown them nor push them more than 3 dB over their own peak.
            RigProcessor rig;
            for (int i = 0; i < rig.getNumPrograms(); ++i)
            {
                rig.setCurrentProgram(i);
                const auto& preset = factoryRigs()[(std::size_t) i];
                auto alone = find(instruments(), preset.instrument)->make();
                for (int p = 0; p < alone->getNumPrograms(); ++p)
                    if (alone->getProgramName(p) == juce::String::fromUTF8(preset.program))
                        alone->setCurrentProgram(p);
                const auto reference = render(*alone, kPhrase, 3.5);
                const auto out = render(rig, kPhrase, 3.5);
                auto peak = [](const juce::AudioBuffer<float>& b) { return std::max(peakOf(b, 0), peakOf(b, 1)); };
                auto level = [](const juce::AudioBuffer<float>& b) {
                    return juce::Decibels::gainToDecibels(std::max(rmsOf(b, 0), rmsOf(b, 1)));
                };
                const float overPeak = juce::Decibels::gainToDecibels(peak(out) / std::max(1.0e-6f, peak(reference)));
                const float change = level(out) - level(reference);
                logMessage(rig.getProgramName(i) + ": peak " + juce::String(peak(out), 2) + " (instrument "
                           + juce::String(peak(reference), 2) + "), RMS " + juce::String(change, 1) + " dB to it");
                expect(finite(out), rig.getProgramName(i));
                expectLessThan(overPeak, 3.0f, rig.getProgramName(i));
                expectGreaterThan(change, -9.0f, rig.getProgramName(i));
            }
        }

        beginTest("Without effects a rig sounds exactly like its instrument");
        {
            RigProcessor rig;
            rig.setCurrentProgram(0);
            rig.setInstrument("bass");
            tonwerkbass::BassProcessor bass;
            const auto alone = render(bass, kPhrase, 2.5);
            const auto inRig = render(rig, kPhrase, 2.5);
            expect(peakOf(alone, 0) > 0.01f);
            expectWithinAbsoluteError(differenceOf(alone, inRig), 0.0f, 1.0e-6f);
            expectEquals(rig.getLatencySamples(), 0);
        }

        beginTest("Groovebox: a track's own output takes it out of the main one");
        {
            tonwerkgroove::GrooveProcessor groove;
            const std::vector<Note> kick { { 0.1, 0.1, kKick } };
            // Program 0 (Init) has a beat, which would play along with the host; the sequencer off, so only the note sounds.
            groove.state.getParameter("seqMode")->setValueNotifyingHost(groove.state.getParameter("seqMode")->convertTo0to1(0.0f));
            const auto together = render(groove, kick, 1.0);
            auto layout = groove.getBusesLayout();
            expectEquals(layout.outputBuses.size(), 1 + tonwerkgroove::kTracks);
            layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
            expect(groove.setBusesLayout(layout));
            // Logic's AU host enables every bus, the stereo variant too, and plays only the first: with the switch off
            // the kick stays in the main output whatever the layout.
            expectWithinAbsoluteError(peakOf(render(groove, kick, 1.0), 0), peakOf(together, 0), 1.0e-4f);
            groove.enableAllBuses();
            expectWithinAbsoluteError(peakOf(render(groove, kick, 1.0), 0), peakOf(together, 0), 1.0e-4f);
            groove.state.getParameter("trackOutputs")->setValueNotifyingHost(1.0f);
            const auto apart = render(groove, kick, 1.0);
            const int first = groove.getChannelIndexInProcessBlockBuffer(false, 1, 0);
            expect(peakOf(together, 0) > 0.1f);
            expectLessThan(peakOf(apart, 0), 0.01f * peakOf(together, 0));
            expectWithinAbsoluteError(peakOf(apart, first), peakOf(together, 0), 1.0e-4f);
        }

        beginTest("An effect on one track changes only that track; latency is made up");
        {
            RigProcessor plain;
            plain.setCurrentProgram(0);
            plain.setInstrument("groovebox");
            auto stopSequencer = [](RigProcessor& rig) {
                auto* groove = dynamic_cast<tonwerkgroove::GrooveProcessor*>(rig.instrument());
                auto* mode = groove->state.getParameter("seqMode");
                mode->setValueNotifyingHost(mode->convertTo0to1(0.0f));
            };
            stopSequencer(plain);
            const std::vector<Note> kickOnly { { 0.1, 0.1, kKick } };
            const auto reference = render(plain, kickOnly, 1.5);

            // Distortion (its oversampling's latency) on the snare: the kick passes, delayed by that latency.
            RigProcessor rig;
            rig.setCurrentProgram(0);
            rig.setInstrument("groovebox");
            stopSequencer(rig);
            rig.setEffect(0, "distortion");
            rig.setSource(0, 2);
            const auto onSnare = render(rig, kickOnly, 1.5);
            const int latency = rig.getLatencySamples();
            expect(latency > 0);
            expectEquals(latency, rig.effect(0)->getLatencySamples());
            expectWithinAbsoluteError(differenceOf(onSnare, reference, latency), 0.0f, 1.0e-5f);

            // On the kick it changes the kick.
            rig.setSource(0, 1);
            const auto onKick = render(rig, kickOnly, 1.5);
            expectGreaterThan(differenceOf(onKick, reference, latency), 0.01f);

        }

        beginTest("Switched off, a slot passes its sound untouched");
        {
            RigProcessor plain;
            plain.setCurrentProgram(0);
            const auto reference = render(plain, kPhrase, 2.0);
            RigProcessor rig;
            rig.setCurrentProgram(0);
            rig.setEffect(2, "distortion");
            rig.setOn(2, false);
            expectEquals(rig.getLatencySamples(), 0);
            expectWithinAbsoluteError(differenceOf(render(rig, kPhrase, 2.0), reference), 0.0f, 1.0e-6f);
        }

        beginTest("The state brings back instrument, effects, sources and their sounds");
        {
            RigProcessor rig;
            rig.setCurrentProgram(4);
            rig.setOn(1, false);
            juce::MemoryBlock saved;
            rig.getStateInformation(saved);
            const auto before = render(rig, kPhrase, 2.0);

            RigProcessor restored;
            restored.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(restored.instrumentKind(), rig.instrumentKind());
            expectEquals(restored.presetName(), rig.presetName());
            for (int i = 0; i < kSlots; ++i)
            {
                expectEquals(restored.effectKind(i), rig.effectKind(i));
                expectEquals(restored.sourceOf(i), rig.sourceOf(i));
                expect(restored.isOn(i) == rig.isOn(i));
            }
            const auto after = render(restored, kPhrase, 2.0);
            expectEquals(restored.getLatencySamples(), rig.getLatencySamples());
            expectWithinAbsoluteError(differenceOf(after, before), 0.0f, 1.0e-4f);
        }

        beginTest("Moving an effect keeps it and its sound");
        {
            RigProcessor rig;
            rig.setCurrentProgram(4);
            const auto first = rig.effectKind(0), second = rig.effectKind(1);
            rig.moveEffect(0, 1);
            expectEquals(rig.effectKind(0), second);
            expectEquals(rig.effectKind(1), first);
        }

        beginTest("The editor shows the chain and the chosen plugin");
        {
            RigProcessor rig;
            rig.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> editor(rig.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // Replacing the instrument while the editor shows it.
            rig.setInstrument("wavetable");
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            rig.setCurrentProgram(1);
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Rig.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static RigTests rigTests;
