#include "GrooveEditor.h"
#include "GrooveEngine.h"
#include "GroovePresets.h"
#include "GrooveProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkgroove;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

std::size_t at(double seconds) { return (std::size_t) (seconds * kRate); }

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

float peak(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t n = from; n < std::min(to, signal.size()); ++n)
        p = std::max(p, std::abs(signal[n]));
    return p;
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

/** The amplitude of `hz` in `signal` between `from` and `to` (Goertzel). */
float amplitudeAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < to; ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (to - from));
}

/** The frequency with the most energy between `low` and `high` hertz, in steps of `step`. */
double peakBetween(const std::vector<float>& signal, double low, double high, double step, std::size_t from,
                   std::size_t to)
{
    double best = low, strongest = 0.0;
    for (double hz = low; hz <= high; hz += step)
        if (const double a = amplitudeAt(signal, hz, from, to); a > strongest)
        {
            strongest = a;
            best = hz;
        }
    return best;
}

/** Share of the energy above `hz`, by a second-order highpass. */
float shareAbove(const std::vector<float>& signal, double hz)
{
    tonwerk::Biquad highpass;
    highpass.set(tonwerk::FilterType::highpass, hz, 0.0, kRate);
    double total = 0.0, high = 0.0;
    for (const float x : signal)
    {
        const double y = highpass.process(x);
        total += (double) x * x;
        high += y * y;
    }
    return (float) (high / std::max(1.0e-12, total));
}

/** A host transport for the processor: playing from `ppq` at `bpm`, advanced by the test after each block. */
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

    bool playing = true;
    double bpm = 120.0;
    double ppq = 0.0;
};

/** One engine track hit once, rendered alone and dry. */
std::vector<float> hit(Instrument instrument, std::initializer_list<std::pair<float TrackSettings::*, float>> knobs,
                       double length, float velocity = 1.0f)
{
    Engine engine;
    engine.prepare(kRate);
    Settings s;
    auto& t = s.track[(std::size_t) instrument];
    t.pan = 0.0f;
    for (const auto& [member, value] : knobs)
        t.*member = value;
    std::vector<float> left(at(length)), right(left.size()), delay(left.size()), reverb(left.size());
    engine.trigger((int) instrument, velocity, s);
    engine.render(left.data(), right.data(), delay.data(), reverb.data(), (int) left.size(), s);
    return left;
}

/** The sample at which the signal first reaches `threshold` after `from`. */
std::size_t onset(const std::vector<float>& signal, std::size_t from, float threshold = 0.02f)
{
    for (std::size_t n = from; n < signal.size(); ++n)
        if (std::abs(signal[n]) >= threshold)
            return n;
    return signal.size();
}

void set(GrooveProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/** A pattern with just these tracks. */
Pattern only(std::initializer_list<std::pair<Instrument, const char*>> tracks)
{
    Pattern p {};
    for (const auto& [instrument, text] : tracks)
        p[(std::size_t) instrument] = parseTrack(text);
    return p;
}

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

/**
 * Runs the processor for `length` seconds with `head` as the host (null: none). `step` advances the head; MIDI notes
 * come as (seconds, note, velocity).
 */
Rendered run(GrooveProcessor& processor, double length, Head* head = nullptr,
             std::vector<std::tuple<double, int, int>> notes = {}, bool waitForRoom = false)
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    processor.setPlayHead(head);
    processor.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(2, kBlock);
    if (waitForRoom)
    {
        // The hall builds its room on a background thread; a kit measures the same in every run once it plays.
        juce::MidiBuffer none;
        const bool wasPlaying = head != nullptr && head->playing;
        if (head != nullptr)
            head->playing = false;
        const auto mode = processor.state.getRawParameterValue("seqMode")->load();
        set(processor, "seqMode", 0.0f);
        for (int waited = 0; ! processor.roomPlaying() && waited < 5000; ++waited)
        {
            processor.processBlock(buffer, none);
            juce::Thread::sleep(1);
        }
        for (int block = 0; block < (int) (0.1 * kRate) / kBlock; ++block)
            processor.processBlock(buffer, none);
        set(processor, "seqMode", mode);
        if (head != nullptr)
            head->playing = wasPlaying;
    }
    const auto total = at(length);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t from = 0; from < total; from += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - from);
        juce::MidiBuffer midi;
        for (const auto& [time, note, velocity] : notes)
            if (const auto on = at(time); on >= from && on < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8) velocity), (int) (on - from));
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + from);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + from);
        if (head != nullptr && head->playing)
            head->ppq += count / kRate * head->bpm / 60.0;
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    processor.setPlayHead(nullptr);
    return out;
}

/** A plain processor: no effects, no swing, tracks in the middle, Init's pattern cleared. */
void plain(GrooveProcessor& processor)
{
    for (int p = 0; p < kPatterns; ++p)
        processor.setPattern(p, {});
    set(processor, "fxReverbMix", 0.0f);
    set(processor, "fxDelayMix", 0.0f);
    for (const auto* track : kTrackIds)
        set(processor, juce::String(track) + "Pan", 0.0f);
}

bool finite(const Rendered& r)
{
    for (std::size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite(r.left[i]) || ! std::isfinite(r.right[i]))
            return false;
    return true;
}

/** How many steps of a kit's pattern text there are. */
int stepsIn(const char* text)
{
    int n = 0;
    for (const char* c = text; *c != 0; ++c)
        n += (*c == '.' || *c == 'x' || *c == 'X') ? 1 : 0;
    return n;
}

void writeWav(const Rendered& take, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    const auto writer = juce::WavAudioFormat().createWriterFor(
        stream, juce::AudioFormatWriterOptions().withSampleRate(kRate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr)
        return;
    const float* channels[] = { take.left.data(), take.right.data() };
    writer->writeFromFloatArrays(channels, 2, (int) take.left.size());
}
} // namespace

class GrooveboxTests : public juce::UnitTest
{
public:
    GrooveboxTests() : juce::UnitTest("Tonwerk Groovebox", "Groovebox") {}

    /**
     * The level the kits are matched to, in LUFS (momentary, at its loudest), and how far one may be off. 4 dB under
     * the synths: drums peak 15 dB above their loudness, and a beat has to stay under full scale.
     */
    static constexpr double kTarget = -16.0;
    static constexpr double kTolerance = 2.0;

    void runTest() override
    {
        beginTest("The kick sounds at its note after the sweep, and its pitch follows Stimmung");
        {
            for (const float tune : { -12.0f, 0.0f, 7.0f })
            {
                const auto out = hit(Instrument::kick, { { &TrackSettings::tune, tune }, { &TrackSettings::decay, 2.0f } },
                                     0.6);
                const double expected = 50.0 * std::exp2(tune / 12.0);
                const double found = peakBetween(out, expected * 0.7, expected * 1.4, 0.5, at(0.25), at(0.55));
                expectWithinAbsoluteError(found, expected, expected * 0.03, "tune " + juce::String(tune));
            }
            // Punch: the start is pitched well above the note.
            const auto punchy = hit(Instrument::kick, { { &TrackSettings::tone, 1.0f } }, 0.1);
            const auto soft = hit(Instrument::kick, { { &TrackSettings::tone, 0.0f } }, 0.1);
            expectGreaterThan(amplitudeAt(punchy, 150.0, 0, at(0.02)), 2.0f * amplitudeAt(soft, 150.0, 0, at(0.02)));
        }

        beginTest("Abklingen is the time to -60 dB");
        {
            for (const auto instrument : { Instrument::kick, Instrument::lowTom, Instrument::openHat, Instrument::cymbal })
            {
                const auto out = hit(instrument, { { &TrackSettings::decay, 1.0f } }, 1.5);
                // From 0.2 s to 0.7 s the level falls 30 dB at 60 dB per second.
                const float drop = decibels(rms(out, at(0.2), at(0.25)) / rms(out, at(0.7), at(0.75)));
                expectWithinAbsoluteError(drop, 30.0f, 4.0f, "instrument " + juce::String((int) instrument));
            }
        }

        beginTest("Snare: Snappy turns the snares up against the drum");
        {
            auto bright = [](float snappy) {
                return shareAbove(hit(Instrument::snare, { { &TrackSettings::character, snappy } }, 0.3), 2000.0);
            };
            expectGreaterThan(bright(1.0f), bright(0.0f) + 0.3f);
        }

        beginTest("Hats and cymbal are bright, the closed hat short, the open one long");
        {
            const auto closed = hit(Instrument::closedHat, {}, 0.6);
            const auto open = hit(Instrument::openHat, {}, 0.6);
            const auto cymbal = hit(Instrument::cymbal, {}, 0.6);
            for (const auto* out : { &closed, &open, &cymbal })
            {
                expectGreaterThan(shareAbove(*out, 3000.0), 0.85f);
            }
            expectLessThan(decibels(rms(closed, at(0.15), at(0.2)) / rms(closed, 0, at(0.02))), -40.0f);
            expectGreaterThan(decibels(rms(open, at(0.15), at(0.2)) / rms(open, 0, at(0.02))), -30.0f);
            // Brighter with Ton.
            auto above = [](float tone) {
                return shareAbove(hit(Instrument::closedHat, { { &TrackSettings::tone, tone } }, 0.2), 9000.0);
            };
            expectGreaterThan(above(1.0f), above(0.0f));
        }

        beginTest("The clap is a few quick hands, then its tail");
        {
            const auto out = hit(Instrument::clap, { { &TrackSettings::character, 1.0f } }, 0.4);
            // At full spread the hands come 18 ms apart; between them the level dips.
            const auto gap = at(0.018);
            const float hand = peak(out, 0, at(0.002)), between = rms(out, gap - at(0.003), gap - at(0.001));
            expectGreaterThan(decibels(hand / between), 12.0f);
            expectGreaterThan(rms(out, 3 * gap + at(0.01), 3 * gap + at(0.03)), 0.0f);
        }

        beginTest("Velocity sets the level; every voice stops once it has rung out");
        {
            const auto loud = hit(Instrument::snare, {}, 0.2, 1.0f);
            const auto quiet = hit(Instrument::snare, {}, 0.2, 0.5f);
            expectWithinAbsoluteError(decibels(rms(quiet, 0, quiet.size()) / rms(loud, 0, loud.size())), -6.0f, 0.5f);

            Engine engine;
            engine.prepare(kRate);
            Settings s;
            for (int t = 0; t < kTracks; ++t)
                engine.trigger(t, 1.0f, s);
            std::vector<float> l(kBlock), r(kBlock), d(kBlock), v(kBlock);
            for (int i = 0; i < (int) (8.0 * kRate) / kBlock; ++i)
                engine.render(l.data(), r.data(), d.data(), v.data(), kBlock, s);
            expectEquals(engine.activeVoices(), 0);
        }

        beginTest("The closed hat chokes the open hat");
        {
            Engine engine;
            engine.prepare(kRate);
            Settings s;
            s.track[(std::size_t) Instrument::closedHat].levelDb = -100.0f;
            std::vector<float> l(at(0.3)), r(l.size()), d(l.size()), v(l.size());
            engine.trigger((int) Instrument::openHat, 1.0f, s);
            engine.render(l.data(), r.data(), d.data(), v.data(), (int) at(0.05), s);
            engine.trigger((int) Instrument::closedHat, 1.0f, s);
            engine.render(l.data() + at(0.05), r.data() + at(0.05), d.data() + at(0.05), v.data() + at(0.05),
                          (int) (l.size() - at(0.05)), s);
            expectLessThan(decibels(rms(l, at(0.07), at(0.1)) / rms(l, at(0.03), at(0.05))), -60.0f);
        }

        beginTest("A hit on a sounding track does not click");
        {
            // A long kick struck again mid-swing, every 30th of a second: no step between samples stands out.
            Engine engine;
            engine.prepare(kRate);
            Settings s;
            s.track[0].decay = 4.0f;
            s.track[0].character = 0.0f;
            std::vector<float> l(at(1.0)), r(l.size()), d(l.size()), v(l.size());
            const int every = (int) (kRate / 30.0) + 17;
            for (int from = 0; from < (int) l.size(); from += every)
            {
                engine.trigger(0, 1.0f, s);
                engine.render(l.data() + from, r.data() + from, d.data() + from, v.data() + from,
                              std::min(every, (int) l.size() - from), s);
            }
            float steepest = 0.0f;
            for (std::size_t i = 1; i < l.size(); ++i)
                steepest = std::max(steepest, std::abs(l[i] - l[i - 1]));
            // A 50 Hz sine at its peak level moves at most 0.0065 a sample; the sweep's start is steeper, a jump of
            // the full level would be 1.
            expectLessThan(steepest, 0.25f);
        }

        beginTest("The clock: steps on the grid, swing on every second one, counted from the song's start");
        {
            std::vector<std::pair<int, int>> fired;
            auto collect = [&fired](int offset, int step) { fired.push_back({ offset, step }); };
            // 120 BPM: 24000 samples a beat; sixteenths 6000 apart.
            Clock::steps(0.0, 1.0, 24000.0, 24000, 16, StepRate::sixteenth, 0.5f, collect);
            expect(fired == std::vector<std::pair<int, int>> { { 0, 0 }, { 6000, 1 }, { 12000, 2 }, { 18000, 3 } });
            fired.clear();
            // Swing 75 %: the second sixteenth of each pair comes on the dotted eighth's place.
            Clock::steps(0.0, 1.0, 24000.0, 24000, 16, StepRate::sixteenth, 0.75f, collect);
            expect(fired == std::vector<std::pair<int, int>> { { 0, 0 }, { 9000, 1 }, { 12000, 2 }, { 21000, 3 } });
            fired.clear();
            // Starting in bar 3 at beat 1.5 of the bar: step 6 of a 16-step pattern, next comes 7.
            Clock::steps(9.5, 10.0, 24000.0, 12000, 16, StepRate::sixteenth, 0.5f, collect);
            expect(fired == std::vector<std::pair<int, int>> { { 0, 6 }, { 6000, 7 } });
            fired.clear();
            // Twelve steps of triplets; a stretch ending on a step leaves it to the next.
            Clock::steps(0.0, 2.0, 24000.0, 48000, 12, StepRate::sixteenthTriplet, 0.5f, collect);
            expectEquals((int) fired.size(), 12);
            expectEquals(fired.back().first, 44000);
            Clock::steps(2.0, 2.5, 24000.0, 12000, 12, StepRate::sixteenthTriplet, 0.5f, collect);
            expectEquals(fired[12].second, 0);
            expectEquals(fired[12].first, 0);
        }

        beginTest("With the host the sequencer plays on its beats, from wherever it starts, and stops with it");
        {
            GrooveProcessor processor;
            plain(processor);
            processor.setPattern(0, only({ { Instrument::kick, "x... x... x... x..." } }));
            set(processor, "kickDecay", 100.0f);
            Head head;
            head.ppq = 4.0;
            const auto out = run(processor, 2.0, &head).left;
            // A kick on every beat: 0, 0.5, 1.0, 1.5 s.
            for (const double beat : { 0.0, 0.5, 1.0, 1.5 })
            {
                const auto found = onset(out, at(beat) > 100 ? at(beat) - 100 : 0);
                expectLessThan(std::abs((double) found - (double) at(beat)), 64.0, juce::String(beat));
            }
            expectEquals(processor.hitCount(0), 4);

            // Started half a beat in: the first kick comes with the next beat, not at once.
            GrooveProcessor late;
            plain(late);
            late.setPattern(0, only({ { Instrument::kick, "x... x... x... x..." } }));
            Head middle;
            middle.ppq = 0.5;
            const auto lateOut = run(late, 0.5, &middle).left;
            expectLessThan(peak(lateOut, 0, at(0.24)), 1.0e-4f);
            expectGreaterThan(peak(lateOut, at(0.25), at(0.3)), 0.05f);

            // Stopped: nothing.
            GrooveProcessor stopped;
            plain(stopped);
            stopped.setPattern(0, only({ { Instrument::kick, "xxxx xxxx xxxx xxxx" } }));
            Head halted;
            halted.playing = false;
            run(stopped, 1.0, &halted);
            expectEquals(stopped.hitCount(0), 0);
        }

        beginTest("Free, length, rate, pattern and mute");
        {
            auto hits = [](std::initializer_list<std::pair<const char*, float>> knobs, const Pattern& p, int index = 0) {
                GrooveProcessor processor;
                plain(processor);
                processor.setPattern(index, p);
                set(processor, "seqMode", 2.0f);
                for (const auto& [id, value] : knobs)
                    set(processor, id, value);
                run(processor, 2.0 - 1.0e-3);
                std::array<int, kTracks> counts {};
                for (int t = 0; t < kTracks; ++t)
                    counts[(std::size_t) t] = processor.hitCount(t);
                return counts;
            };
            const auto every = only({ { Instrument::snare, "xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx" },
                                      { Instrument::kick, "x" } });
            // Two seconds at 120 BPM: 16 sixteenths, 8 eighths, 32 thirty-seconds, 24 triplets.
            expectEquals(hits({}, every)[1], 16);
            expectEquals(hits({ { "rate", 1.0f } }, every)[1], 8);
            expectEquals(hits({ { "rate", 3.0f } }, every)[1], 32);
            expectEquals(hits({ { "rate", 2.0f } }, every)[1], 24);
            // A pattern of four steps with one set plays it every four.
            expectEquals(hits({ { "length", 4.0f } }, every)[0], 4);
            expectEquals(hits({}, every)[0], 1);
            // Another pattern plays only when chosen.
            expectEquals(hits({}, every, 2)[1], 0);
            expectEquals(hits({ { "pattern", 2.0f } }, every, 2)[1], 16);
            expectEquals(hits({ { "snareMute", 1.0f } }, every)[1], 0);
            expectEquals(hits({ { "seqMode", 0.0f } }, every)[1], 0);
        }

        beginTest("Accents are louder than plain steps by Akzent");
        {
            auto level = [](const char* steps, float accent) {
                GrooveProcessor processor;
                plain(processor);
                processor.setPattern(0, only({ { Instrument::lowTom, steps } }));
                set(processor, "seqMode", 2.0f);
                set(processor, "accent", accent);
                return peak(run(processor, 0.4).left, 0, at(0.4));
            };
            expectWithinAbsoluteError(decibels(level("x", 1.0f) / level("X", 1.0f)), -6.0f, 0.5f);
            expectWithinAbsoluteError(decibels(level("x", 0.0f) / level("X", 1.0f)), 0.0f, 0.2f);
        }

        beginTest("MIDI notes play the tracks on General MIDI's drum notes");
        {
            GrooveProcessor processor;
            plain(processor);
            set(processor, "seqMode", 0.0f);
            run(processor, 1.0, nullptr,
                { { 0.0, 36, 100 }, { 0.1, 38, 100 }, { 0.2, 39, 100 }, { 0.3, 45, 100 }, { 0.4, 48, 100 },
                  { 0.5, 42, 100 }, { 0.6, 46, 100 }, { 0.7, 49, 100 }, { 0.8, 60, 100 } });
            for (int t = 0; t < kTracks; ++t)
                expectEquals(processor.hitCount(t), 1, kTrackNames[(std::size_t) t]);
            for (int t = 0; t < kTracks; ++t)
                expectEquals(trackForNote(noteForTrack(t)), t);
        }

        beginTest("Sends feed the hall, pan places the track");
        {
            auto tail = [](float send) {
                GrooveProcessor processor;
                plain(processor);
                set(processor, "fxReverbMix", 1.0f);
                set(processor, "snareReverb", send);
                set(processor, "snareDecay", 100.0f);
                set(processor, "seqMode", 0.0f);
                const auto out = run(processor, 1.0, nullptr, { { 0.0, 38, 127 } }, true).left;
                return rms(out, at(0.5), at(0.9));
            };
            expectLessThan(tail(0.0f), 1.0e-5f);
            expectGreaterThan(tail(1.0f), 1.0e-3f);

            GrooveProcessor processor;
            plain(processor);
            set(processor, "seqMode", 0.0f);
            set(processor, "snarePan", -1.0f);
            const auto out = run(processor, 0.3, nullptr, { { 0.0, 38, 127 } });
            expectLessThan(rms(out.right, 0, out.right.size()), 1.0e-6f);
            expectGreaterThan(rms(out.left, 0, out.left.size()), 0.01f);
        }

        beginTest("The state keeps the knobs and every pattern");
        {
            GrooveProcessor processor;
            processor.setCurrentProgram(5);
            set(processor, "kickTune", -7.0f);
            processor.setStep(6, 3, 31, 2);
            processor.setStep(6, 2, 0, 1);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            GrooveProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().track[0].tune, -7.0f, 1.0e-3f);
            for (int p = 0; p < kPatterns; ++p)
                expect(loaded.pattern(p) == processor.pattern(p), "pattern " + juce::String(p));
            expectEquals(loaded.step(6, 3, 31), 2);
            expectEquals(loaded.step(6, 2, 0), 1);
            expectEquals(loaded.presetName(), processor.presetName());
        }

        beginTest("Values are whole numbers with units");
        {
            GrooveProcessor processor;
            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("kickDecay", 499.6f), juce::String("500 ms"));
            expectEquals(text("kickTune", -3.0f), juce::String("-3 HT"));
            expectEquals(text("snareTone", 0.42f), juce::String("42 %"));
            expectEquals(text("swing", 0.58f), juce::String("58 %"));
            expectEquals(text("length", 12.0f), juce::String("12 Schritte"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));
        }

        beginTest("Every kit: in a group, named once, its patterns as long as it plays them, and leveled");
        {
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});
            const auto& kits = factoryKits();
            std::set<juce::String> names;
            for (int i = 0; i < (int) kits.size(); ++i)
            {
                const auto& kit = kits[(std::size_t) i];
                const auto kitName = juce::String::fromUTF8(kit.name);
                expect(names.insert(kitName).second, "twice: " + kitName);
                expect(i == 0 || tonwerkui::categoryIndex(kit.category, tonwerkui::kGrooveCategories) >= 0, kitName);
                GrooveProcessor processor;
                processor.setCurrentProgram(i);
                for (const auto& [id, value] : kit.values)
                    expect(processor.state.getParameter(id) != nullptr, kitName + ": " + id);
                const int length = processor.readSettings().length;
                for (const auto& pattern : kit.patterns)
                    for (const auto* track : pattern)
                        expect(stepsIn(track) == 0 || stepsIn(track) == length,
                               kitName + ": " + juce::String(stepsIn(track)) + " steps, not " + juce::String(length));

                // Two bars of pattern A at 120 BPM, as the host plays it.
                Head head;
                const auto take = run(processor, 4.0, &head, {}, true);
                expect(finite(take), kitName);
                const double level = tonwerktest::momentaryMax(take.left, take.right);
                const float loudest = std::max(peak(take.left, 0, take.left.size()), peak(take.right, 0, take.right.size()));
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + kitName.paddedRight(' ', 22)
                           + juce::String::fromUTF8(kit.category).paddedRight(' ', 15) + juce::String(level, 1)
                           + " LUFS, peak " + juce::String(loudest, 2));
                expectGreaterThan(level, kTarget - kTolerance, kitName);
                expectLessThan(level, kTarget + kTolerance, kitName);
                expectLessThan(loudest, 1.0f, kitName);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile("Tonwerk Groovebox").getChildFile(
                                       juce::String(i + 1).paddedLeft('0', 2) + " " + kitName + ".wav"));
            }
        }

        beginTest("Eight tracks on every thirty-second step with the hall stay well inside real time");
        {
            GrooveProcessor processor;
            plain(processor);
            Pattern all {};
            for (auto& t : all)
                t = parseTrack("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
            processor.setPattern(0, all);
            set(processor, "rate", 3.0f);
            set(processor, "length", 32.0f);
            set(processor, "seqMode", 2.0f);
            set(processor, "fxReverbMix", 0.5f);
            set(processor, "fxDelayMix", 0.5f);
            set(processor, "drive", 0.5f);
            set(processor, "cymbalDecay", 8000.0f);
            const auto out = run(processor, 4.0, nullptr, {}, true);
            const double realtime = 4.0 / out.seconds;
            logMessage("All tracks on 1/32 with delay, hall and drive: " + juce::String(realtime, 1) + " times real time");
            expect(finite(out));
            expectGreaterThan(realtime, 5.0);
        }

        beginTest("The editor fits its panels");
        {
            GrooveProcessor processor;
            Head head;
            run(processor, 0.3, &head);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Groovebox.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static GrooveboxTests grooveboxTests;
