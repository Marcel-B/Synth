#include "ArpEditor.h"
#include "ArpEngine.h"
#include "ArpPresets.h"
#include "ArpProcessor.h"

#include <juce_core/juce_core.h>

#include <map>

using namespace tonwerkarp;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
/** A sixteenth at 120 BPM. */
constexpr long long kSixteenth = 6000;

/** A note starting (velocity > 0) or ending, in samples from the start. */
struct Out
{
    long long at;
    int note;
    int velocity;
};

struct Key
{
    long long at;
    int note;
    int velocity = 100;
};

struct Transport
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
    /** Stops playing at this sample (when playing). */
    long long stopAt = -1;
};

/** Runs the engine for `samples` in blocks of `block`, the keys going in at their samples. */
std::vector<Out> play(const Settings& s, long long samples, std::vector<Key> keys, Transport transport = {},
                      int block = kBlock)
{
    Engine engine;
    engine.prepare(kRate);
    engine.update(s);
    std::vector<Out> out;
    std::vector<KeyEvent> events;
    const double samplesPerBeat = kRate * 60.0 / transport.bpm;
    for (long long at = 0; at < samples; at += block)
    {
        const int count = (int) std::min<long long>(block, samples - at);
        events.clear();
        for (const auto& key : keys)
            if (key.at >= at && key.at < at + count)
                events.push_back({ (int) (key.at - at), key.note, key.velocity, 1 });
        Position position;
        position.bpm = transport.bpm;
        position.playing = transport.playing && (transport.stopAt < 0 || at < transport.stopAt);
        // As a host gives it: the block's start, rounded to what a double holds.
        position.ppq = transport.ppq + (double) at / samplesPerBeat;
        engine.process(count, position, events.data(), (int) events.size(),
                       [&](int offset, int note, int velocity, int) { out.push_back({ at + offset, note, velocity }); });
    }
    return out;
}

std::vector<Out> starts(const std::vector<Out>& events)
{
    std::vector<Out> on;
    for (const auto& e : events)
        if (e.velocity > 0)
            on.push_back(e);
    return on;
}

std::vector<int> pitches(const std::vector<Out>& events, std::size_t count = 1000)
{
    std::vector<int> notes;
    for (const auto& e : starts(events))
        if (notes.size() < count)
            notes.push_back(e.note);
    return notes;
}

std::vector<Key> chord(std::initializer_list<int> notes, long long at = 0)
{
    std::vector<Key> keys;
    for (const int note : notes)
        keys.push_back({ at, note });
    return keys;
}

/** Every note ends, and no note starts again while it still sounds. */
bool balanced(const std::vector<Out>& events)
{
    std::map<int, int> sounding;
    for (const auto& e : events)
    {
        if (e.velocity > 0)
        {
            if (sounding[e.note] != 0)
                return false;
            sounding[e.note] = 1;
        }
        else
            sounding[e.note] = 0;
    }
    for (const auto& [note, on] : sounding)
        if (on != 0)
            return false;
    return true;
}

class Head : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        return info;
    }

    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;
};

void set(ArpProcessor& processor, const juce::String& id, float value)
{
    auto* p = processor.state.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
} // namespace

class ArpTests : public juce::UnitTest
{
public:
    ArpTests() : juce::UnitTest("Tonwerk Arp", "Arp") {}

    void runTest() override
    {
        beginTest("The arpeggio starts on the first key and walks up the chord on sixteenths");
        {
            const auto out = play({}, 24000, chord({ 64, 60, 67 }, 1000));
            const auto on = starts(out);
            expectEquals((int) on.size(), 4);
            const std::array<int, 4> notes { 60, 64, 67, 60 };
            for (std::size_t i = 0; i < 4; ++i)
            {
                expectEquals(on[i].note, notes[i]);
                expectEquals(on[i].at, 1000 + (long long) i * kSixteenth);
            }
            // Gate 50 %: each note ends half a step after it starts.
            for (const auto& e : out)
                if (e.velocity == 0 && e.note == 64)
                    expectEquals(e.at, 1000 + kSixteenth + kSixteenth / 2);
            expect(balanced(out));
        }

        beginTest("Orders");
        {
            auto order = [](Order o, int octaves, std::vector<Key> keys, std::size_t count) {
                Settings s;
                s.order = o;
                s.octaves = octaves;
                return pitches(play(s, (long long) count * kSixteenth, std::move(keys)), count);
            };
            expect(order(Order::up, 2, chord({ 60, 64, 67 }), 7) == std::vector<int> { 60, 64, 67, 72, 76, 79, 60 });
            expect(order(Order::down, 2, chord({ 60, 64, 67 }), 7) == std::vector<int> { 79, 76, 72, 67, 64, 60, 79 });
            expect(order(Order::upDown, 1, chord({ 60, 64, 67 }), 7) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67 });
            expect(order(Order::downUp, 1, chord({ 60, 64, 67 }), 7) == std::vector<int> { 67, 64, 60, 64, 67, 64, 60 });
            // As played: the key pressed first leads.
            expect(order(Order::played, 1, { { 0, 67 }, { 10, 60 }, { 20, 64 } }, 4) == std::vector<int> { 67, 60, 64, 67 });

            const auto random = order(Order::random, 1, chord({ 60, 64, 67 }), 40);
            std::set<int> seen(random.begin(), random.end());
            expect(seen == std::set<int> { 60, 64, 67 });
            for (std::size_t i = 1; i < random.size(); ++i)
                expect(random[i] != random[i - 1], "the same note twice in a row");
        }

        beginTest("While the host plays, the steps sit on its grid, also across odd blocks");
        {
            Transport transport;
            transport.playing = true;
            transport.ppq = 0.1;
            for (const int block : { 256, 333, 1 << 12 })
            {
                // Two bars from beat 0.1: steps on every sixteenth from 0.25 to 7.75 after the start.
                const auto on = starts(play({}, 8 * 24000, chord({ 60 }), transport, block));
                expectEquals((int) on.size(), 32, "block " + juce::String(block));
                for (std::size_t i = 0; i < on.size(); ++i)
                    expectEquals(on[i].at, 3600 + (long long) i * kSixteenth);
            }

            // A four-step pattern with only its first step on plays on the beats.
            Settings s;
            s.length = 4;
            for (int i = 1; i < 4; ++i)
                s.steps[(std::size_t) i].on = false;
            for (const auto& e : starts(play(s, 4 * 24000, chord({ 60 }), transport)))
            {
                const double beat = transport.ppq + (double) e.at / 24000.0;
                expectWithinAbsoluteError(beat, std::round(beat), 1.0e-4);
            }
        }

        beginTest("Swing delays every second step");
        {
            Settings s;
            s.swing = 0.75f;
            const auto on = starts(play(s, 4 * kSixteenth, chord({ 60 })));
            expectEquals((int) on.size(), 4);
            const std::array<long long, 4> at { 0, 9000, 12000, 21000 };
            for (std::size_t i = 0; i < 4; ++i)
                expectEquals(on[i].at, at[i]);
        }

        beginTest("Rests skip a step, a tie holds the note before through its step");
        {
            Settings s;
            s.length = 4;
            s.steps[1].on = false;
            s.steps[3].tie = true;
            const auto out = play(s, 6 * kSixteenth, chord({ 60, 64 }));
            const auto on = starts(out);
            expectEquals((int) on.size(), 3);
            // The rest leaves the order where it was: C, (rest), E, (tie), C.
            expect(pitches(out) == std::vector<int> { 60, 64, 60 });
            expectEquals(on[1].at, 2 * kSixteenth);
            expectEquals(on[2].at, 4 * kSixteenth);
            // E sounds through the tied step and ends at its gate.
            for (const auto& e : out)
                if (e.velocity == 0 && e.note == 64)
                    expectEquals(e.at, 3 * kSixteenth + kSixteenth / 2);
            expect(balanced(out));
        }

        beginTest("The sequencer plays the steps' notes on the held key, a chord as one");
        {
            Settings s;
            s.mode = Mode::sequence;
            s.length = 4;
            const std::array<int, 4> offsets { 0, 7, 12, 3 };
            for (std::size_t i = 0; i < 4; ++i)
                s.steps[i].note = offsets[i];
            expect(pitches(play(s, 4 * kSixteenth, chord({ 48 }))) == std::vector<int> { 48, 55, 60, 51 });

            const auto two = starts(play(s, 2 * kSixteenth, chord({ 48, 52 })));
            expectEquals((int) two.size(), 4);
            expectEquals(two[0].at, two[1].at);

            // Two octaves: the second time round an octave up, then back.
            s.octaves = 2;
            expect(pitches(play(s, 12 * kSixteenth, chord({ 48 })))
                   == std::vector<int> { 48, 55, 60, 51, 60, 67, 72, 63, 48, 55, 60, 51 });
        }

        beginTest("Latch keeps the chord after the keys are let go, a new chord replaces it");
        {
            std::vector<Key> keys = chord({ 60, 64 });
            keys.push_back({ 20000, 60, 0 });
            keys.push_back({ 20000, 64, 0 });
            keys.push_back({ 50000, 67 });
            keys.push_back({ 60000, 67, 0 });
            Settings s;
            const auto plain = starts(play(s, 100000, keys));
            for (const auto& e : plain)
                expect(e.at < 20000 || (e.at >= 50000 && e.at < 60000));

            s.latch = true;
            const auto latched = play(s, 100000, keys);
            std::set<int> after;
            for (const auto& e : starts(latched))
                if (e.at > 20000 && e.at < 50000)
                    after.insert(e.note);
                else if (e.at >= 50000)
                    expectEquals(e.note, 67);
            expect(after == std::set<int> { 60, 64 });
            expect(starts(latched).back().at > 90000);
        }

        beginTest("Velocity: as played or fixed, scaled by the step");
        {
            Settings s;
            s.steps[0].velocity = 0.5f;
            const auto played = starts(play(s, 2 * kSixteenth, { { 0, 60, 90 } }));
            expectEquals(played[0].velocity, 45);
            expectEquals(played[1].velocity, 90);
            s.velocitySource = VelocitySource::fixed;
            s.fixedVelocity = 120;
            const auto fixed = starts(play(s, 2 * kSixteenth, { { 0, 60, 90 } }));
            expectEquals(fixed[0].velocity, 60);
            expectEquals(fixed[1].velocity, 120);
        }

        beginTest("Transpose moves every note");
        {
            Settings s;
            s.transpose = -12;
            expect(pitches(play(s, 2 * kSixteenth, chord({ 60, 64 }))) == std::vector<int> { 48, 52 });
        }

        beginTest("A long gate overlaps the next note; the same note ends before it starts again");
        {
            Settings s;
            s.gate = 1.5f;
            const auto out = play(s, 3 * kSixteenth, chord({ 60, 64 }));
            // E starts while C still sounds: legato on a mono synth.
            long long cEnds = -1;
            for (const auto& e : out)
                if (e.note == 60 && e.velocity == 0 && cEnds < 0)
                    cEnds = e.at;
            expectEquals(cEnds, kSixteenth + kSixteenth / 2);
            std::vector<Key> held = chord({ 60 });
            held.push_back({ 18 * kSixteenth, 60, 0 });
            expect(balanced(play(s, 24 * kSixteenth, held)));
        }

        beginTest("The transport stopping ends the notes");
        {
            Settings s;
            s.gate = 2.0f;
            Transport transport;
            transport.playing = true;
            transport.stopAt = 10 * kBlock;
            std::vector<Key> keys = chord({ 60 });
            keys.push_back({ 9 * kBlock, 60, 0 });
            const auto out = play(s, 20 * kBlock, keys, transport);
            expect(balanced(out));
            expectEquals(out.back().velocity, 0);
            expectEquals(out.back().at, 10LL * kBlock);
        }

        beginTest("No stuck notes, whatever is played");
        {
            juce::Random random(11);
            for (int round = 0; round < 40; ++round)
            {
                Settings s;
                s.mode = (Mode) random.nextInt(2);
                s.order = (Order) random.nextInt(6);
                s.octaves = 1 + random.nextInt(4);
                s.division = (Division) random.nextInt(12);
                s.gate = 0.05f + random.nextFloat() * 1.95f;
                s.swing = 0.5f + random.nextFloat() * 0.25f;
                s.length = 1 + random.nextInt(kSteps);
                for (auto& step : s.steps)
                    step = { random.nextFloat() < 0.8f, random.nextInt(49) - 24, random.nextFloat(), random.nextFloat() < 0.3f };
                std::vector<Key> keys;
                std::set<int> down;
                for (long long at = 0; at < 4 * 48000; at += 500 + random.nextInt(8000))
                {
                    const int note = 36 + random.nextInt(48);
                    keys.push_back({ at, note, down.count(note) != 0 ? 0 : 1 + random.nextInt(127) });
                    if (down.count(note) != 0)
                        down.erase(note);
                    else
                        down.insert(note);
                }
                for (const int note : down)
                    keys.push_back({ 4 * 48000, note, 0 });
                std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.at < b.at; });
                Transport transport;
                transport.playing = random.nextBool();
                transport.ppq = random.nextDouble() * 8.0;
                transport.bpm = 60.0 + random.nextDouble() * 120.0;
                const auto out = play(s, 18 * 48000, keys, transport, 64 + random.nextInt(1000));
                expect(balanced(out), "round " + juce::String(round));
                for (const auto& e : out)
                    expect(e.note >= 0 && e.note <= 127 && e.velocity >= 0 && e.velocity <= 127);
            }
        }

        beginTest("The plugin: keys in, notes out, everything else through");
        {
            ArpProcessor processor;
            expect(processor.isMidiEffect());
            expectEquals(processor.getTotalNumInputChannels(), 0);
            expectEquals(processor.getTotalNumOutputChannels(), 0);
            expectEquals((int) processor.getParameters().size(), 11 + 4 * kSteps);
            Head head;
            processor.setPlayHead(&head);
            processor.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(0, kBlock);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 10);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 20);
            processor.processBlock(buffer, midi);
            int notes = 0, controllers = 0;
            for (const auto event : midi)
            {
                const auto m = event.getMessage();
                if (m.isNoteOn())
                {
                    ++notes;
                    expectEquals(event.samplePosition, 20);
                }
                if (m.isController())
                {
                    ++controllers;
                    expectEquals(event.samplePosition, 10);
                }
            }
            expectEquals(notes, 1);
            expectEquals(controllers, 1);

            // All notes off stops the arpeggio, latched or not.
            set(processor, "latch", 1.0f);
            midi.clear();
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            processor.processBlock(buffer, midi);
            bool ended = false;
            for (const auto event : midi)
                ended = ended || event.getMessage().isNoteOff();
            expect(ended);
            for (int b = 0; b < 100; ++b)
            {
                midi.clear();
                processor.processBlock(buffer, midi);
                for (const auto event : midi)
                    expect(! event.getMessage().isNoteOn());
            }
            processor.setPlayHead(nullptr);
        }

        beginTest("Every preset plays a held chord");
        {
            ArpProcessor processor;
            Head head;
            processor.setPlayHead(&head);
            processor.prepareToPlay(kRate, kBlock);
            for (const auto& preset : factoryPresets())
                for (const auto& [id, value] : preset.values)
                    expect(processor.state.getParameter(id) != nullptr, juce::String(preset.name) + ": " + id);
            for (int p = 0; p < processor.getNumPrograms(); ++p)
            {
                processor.setCurrentProgram(p);
                expectEquals(processor.getProgramName(p), juce::String::fromUTF8(factoryPresets()[(std::size_t) p].name));
                processor.prepareToPlay(kRate, kBlock);
                juce::AudioBuffer<float> buffer(0, kBlock);
                int notes = 0;
                std::map<int, int> sounding;
                bool overlap = false;
                for (int b = 0; b < (int) (4.0 * kRate) / kBlock; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b == 0)
                        for (const int note : { 48, 55, 60 })
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
                    if (b == (int) (3.0 * kRate) / kBlock)
                        for (const int note : { 48, 55, 60 })
                            midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                    processor.processBlock(buffer, midi);
                    for (const auto event : midi)
                    {
                        const auto m = event.getMessage();
                        if (m.isNoteOn())
                        {
                            ++notes;
                            overlap = overlap || sounding[m.getNoteNumber()] != 0;
                            sounding[m.getNoteNumber()] = 1;
                        }
                        else if (m.isNoteOff())
                            sounding[m.getNoteNumber()] = 0;
                    }
                }
                const auto program = processor.getProgramName(p);
                expectGreaterThan(notes, 4, program);
                expect(! overlap, program);
                for (const auto& [note, on] : sounding)
                    expectEquals(on, 0, program + ": a note still sounds");
            }
            processor.setPlayHead(nullptr);
        }

        beginTest("The editor fits its panels");
        {
            ArpProcessor processor;
            processor.setCurrentProgram(11);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Arp.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static ArpTests arpTests;
