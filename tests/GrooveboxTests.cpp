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

/** Like `hit`, with the track set up by `setup`. */
template <typename Setup>
std::vector<float> shaped(Instrument instrument, Setup&& setup, double length, float velocity = 1.0f)
{
    Engine engine;
    engine.prepare(kRate);
    Settings s;
    auto& t = s.track[(std::size_t) instrument];
    t.pan = 0.0f;
    setup(t);
    std::vector<float> left(at(length)), right(left.size()), delay(left.size()), reverb(left.size());
    engine.trigger((int) instrument, velocity, s);
    engine.render(left.data(), right.data(), delay.data(), reverb.data(), (int) left.size(), s);
    return left;
}

/** Peak over RMS, in dB. */
float crest(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    return decibels(peak(signal, from, to) / rms(signal, from, to));
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

        beginTest("Attack fades the hit in, Halten keeps it up before it falls");
        {
            const auto struck = hit(Instrument::snare, {}, 0.1);
            const auto soft = shaped(Instrument::snare, [](TrackSettings& s) { s.attack = 0.05f; }, 0.1);
            // The first 3 ms are far quieter; once the attack is over the hit is as loud as without it.
            expectLessThan(decibels(rms(soft, 0, at(0.003)) / rms(struck, 0, at(0.003))), -18.0f);
            expectWithinAbsoluteError(decibels(rms(soft, at(0.06), at(0.1)) / rms(struck, at(0.06), at(0.1))), 0.0f, 0.5f);

            const auto gated = shaped(Instrument::openHat, [](TrackSettings& s) {
                s.decay = 0.1f;
                s.hold = 0.3f;
            }, 0.6);
            const auto open = hit(Instrument::openHat, { { &TrackSettings::decay, 0.1f } }, 0.6);
            // Still at its strike's level after a quarter second, then gone 100 ms after the hold.
            expectGreaterThan(decibels(rms(gated, at(0.25), at(0.29)) / rms(gated, at(0.01), at(0.05))), -3.0f);
            expectLessThan(decibels(rms(open, at(0.25), at(0.29)) / rms(open, at(0.01), at(0.05))), -60.0f);
            expectLessThan(decibels(rms(gated, at(0.45), at(0.5)) / rms(gated, at(0.01), at(0.05))), -50.0f);

            // The clap's tail holds after its hands.
            const auto clap = shaped(Instrument::clap, [](TrackSettings& s) {
                s.decay = 0.1f;
                s.hold = 0.3f;
            }, 0.5);
            expectGreaterThan(rms(clap, at(0.25), at(0.3)), 0.3f * rms(clap, at(0.06), at(0.1)));
        }

        beginTest("Pitch-Mod bends the whole instrument and falls back in Mod-Zeit");
        {
            // A tom with hardly any sweep of its own, bent an octave up by a slow mod envelope: twice its note early on.
            const auto bent = shaped(Instrument::highTom, [](TrackSettings& s) {
                s.tone = 0.0f;
                s.decay = 2.0f;
                s.pitchMod = 12.0f;
                s.modDecay = 2.0f;
            }, 0.2);
            // 40 ms in, a mod envelope of 2 s to -60 dB is still at 87 %: 10.5 semitones above 150 Hz.
            expectWithinAbsoluteError(peakBetween(bent, 200.0, 400.0, 1.0, at(0.02), at(0.06)), 280.0, 20.0);
            // A fast one is back on the note.
            const auto quick = shaped(Instrument::highTom, [](TrackSettings& s) {
                s.tone = 0.0f;
                s.decay = 2.0f;
                s.pitchMod = 12.0f;
                s.modDecay = 0.02f;
            }, 0.4);
            expectWithinAbsoluteError(peakBetween(quick, 100.0, 400.0, 1.0, at(0.2), at(0.4)), 150.0, 5.0);
            // Down works too, and moves the metal of a hat.
            const auto hat = hit(Instrument::closedHat,
                                 { { &TrackSettings::decay, 0.3f }, { &TrackSettings::character, 0.0f } }, 0.1);
            const auto lower = shaped(Instrument::closedHat, [](TrackSettings& s) {
                s.decay = 0.3f;
                s.pitchMod = -24.0f;
                s.modDecay = 2.0f;
                s.character = 0.0f;
            }, 0.1);
            expectLessThan(shareAbove(lower, 6000.0), shareAbove(hat, 6000.0));
        }

        beginTest("The track filter: lowpass darkens, highpass thins, Filter-Mod sweeps");
        {
            const auto cymbal = hit(Instrument::cymbal, {}, 0.3);
            const auto dark = shaped(Instrument::cymbal, [](TrackSettings& s) {
                s.filter = TrackFilter::lowpass;
                s.cutoff = 1000.0f;
            }, 0.3);
            // The cymbal's metal sits above a few kilohertz, so little is left under a lowpass at 1 kHz.
            expectLessThan(decibels(rms(dark, 0, dark.size()) / rms(cymbal, 0, cymbal.size())), -20.0f);

            const auto kick = hit(Instrument::kick, { { &TrackSettings::decay, 1.0f } }, 0.5);
            const auto thin = shaped(Instrument::kick, [](TrackSettings& s) {
                s.decay = 1.0f;
                s.filter = TrackFilter::highpass;
                s.cutoff = 500.0f;
            }, 0.5);
            expectLessThan(decibels(amplitudeAt(thin, 50.0, at(0.2), at(0.4)) / amplitudeAt(kick, 50.0, at(0.2), at(0.4))),
                           -30.0f);

            // A lowpass opened by the mod envelope: far brighter than the same lowpass standing still at the strike, the
            // same once the envelope has fallen.
            auto hatThrough = [](float filterMod) {
                return shaped(Instrument::openHat, [filterMod](TrackSettings& s) {
                    s.decay = 1.0f;
                    s.filter = TrackFilter::lowpass;
                    s.cutoff = 300.0f;
                    s.filterMod = filterMod;
                    s.modDecay = 0.1f;
                }, 0.4);
            };
            const auto swept = hatThrough(1.0f), still = hatThrough(0.0f);
            expectGreaterThan(decibels(rms(swept, 0, at(0.01)) / rms(still, 0, at(0.01))), 20.0f);
            expectWithinAbsoluteError(decibels(rms(swept, at(0.3), at(0.4)) / rms(still, at(0.3), at(0.4))), 0.0f, 1.0f);

            // Resonance rings at the cutoff.
            auto atCutoff = [](float resonance) {
                const auto out = shaped(Instrument::snare, [resonance](TrackSettings& s) {
                    s.filter = TrackFilter::bandpass;
                    s.cutoff = 1500.0f;
                    s.resonance = resonance;
                    s.character = 1.0f;
                }, 0.2);
                return amplitudeAt(out, 1500.0, 0, out.size()) / rms(out, 0, out.size());
            };
            expectGreaterThan(atCutoff(1.0f), 2.0f * atCutoff(0.0f));
        }

        beginTest("Zerrung squeezes the hit, Bitcrush and Downsample coarsen it");
        {
            const auto tom = hit(Instrument::lowTom, {}, 0.4);
            const auto driven = shaped(Instrument::lowTom, [](TrackSettings& s) { s.drive = 1.0f; }, 0.4);
            expectLessThan(crest(driven, 0, driven.size()), crest(tom, 0, tom.size()) - 3.0f);
            // No louder at its peak than the plain hit.
            expectLessThan(peak(driven, 0, driven.size()), peak(tom, 0, tom.size()) * 1.05f);

            // Two bits: the hit takes a handful of values (steps of half full scale), however the track's level scales them.
            const auto crushed = shaped(Instrument::snare, [](TrackSettings& s) { s.bits = 2.0f; }, 0.2);
            std::set<float> values(crushed.begin(), crushed.end());
            expectLessOrEqual((int) values.size(), 7);

            // Held at 1000 Hz: the signal changes at most a thousand times a second.
            const auto held = shaped(Instrument::closedHat, [](TrackSettings& s) {
                s.crush = 1000.0f;
                s.decay = 0.5f;
            }, 0.5);
            int changes = 0;
            for (std::size_t i = 1; i < held.size(); ++i)
                changes += std::abs(held[i] - held[i - 1]) > 0.0f ? 1 : 0;
            expectLessOrEqual(changes, 501);
            expectGreaterThan(changes, 400);
        }

        beginTest("Dynamik sets how much the velocity counts, Zufall varies each hit");
        {
            auto level = [](float velocity, float dynamics) {
                const auto out = shaped(Instrument::closedHat, [dynamics](TrackSettings& s) { s.velocity = dynamics; },
                                        0.05, velocity);
                return rms(out, 0, out.size());
            };
            expectWithinAbsoluteError(decibels(level(0.5f, 0.0f) / level(1.0f, 0.0f)), 0.0f, 0.1f);
            expectWithinAbsoluteError(decibels(level(0.5f, 0.5f) / level(1.0f, 0.5f)), decibels(0.75f), 0.3f);

            auto hits = [](float random) {
                Engine engine;
                engine.prepare(kRate);
                Settings s;
                auto& t = s.track[(std::size_t) Instrument::closedHat];
                t.random = random;
                t.pan = 0.0f;
                // All metal, no noise, so nothing but the dice can tell one hit from the next.
                t.character = 0.0f;
                std::vector<float> levels;
                std::vector<float> l(at(0.1)), r(l.size()), d(l.size()), v(l.size());
                for (int i = 0; i < 16; ++i)
                {
                    std::fill(l.begin(), l.end(), 0.0f);
                    engine.trigger((int) Instrument::closedHat, 1.0f, s);
                    engine.render(l.data(), r.data(), d.data(), v.data(), (int) l.size(), s);
                    levels.push_back(decibels(rms(l, 0, l.size())));
                }
                const auto [low, high] = std::minmax_element(levels.begin(), levels.end());
                return *high - *low;
            };
            expectLessThan(hits(0.0f), 0.01f);
            expectGreaterThan(hits(1.0f), 1.5f);
            // 3 dB either way, and a little more from the detuning.
            expectLessThan(hits(1.0f), 8.0f);
        }

        beginTest("Tonwerk is the machine every track starts on, with the Groovebox's own instruments");
        {
            const Settings d;
            for (const auto& track : d.track)
                expect(track.machine == Machine::tonwerk);
            for (int t = 0; t < kTracks; ++t)
            {
                const auto a = voicingFor(Machine::tonwerk, (Instrument) t), b = tonwerkVoicing((Instrument) t);
                expectEquals(a.hz, b.hz);
                expectEquals(a.gain, b.gain);
                expectEquals(a.bits, 16.0f);
                expectEquals(a.rate, 48000.0f);
            }
            GrooveProcessor processor;
            expectEquals(processor.state.getParameter("kickMachine")->getCurrentValueAsText(), juce::String("Tonwerk"));
        }

        beginTest("Every machine plays every instrument about as loud as Tonwerk's");
        {
            auto loudness = [](Machine machine, Instrument instrument) {
                const auto out = shaped(instrument, [machine](TrackSettings& t) { t.machine = machine; }, 1.0);
                return tonwerktest::momentaryMax(out, out);
            };
            for (int t = 0; t < kTracks; ++t)
            {
                const double reference = loudness(Machine::tonwerk, (Instrument) t);
                juce::String line = juce::String(kTrackIds[(std::size_t) t]).paddedRight(' ', 10);
                for (int m = 1; m < kMachines; ++m)
                {
                    const auto out = shaped((Instrument) t, [m](TrackSettings& ts) { ts.machine = (Machine) m; }, 1.0);
                    bool clean = true;
                    for (const float x : out)
                        clean = clean && std::isfinite(x) && std::abs(x) < 1.5f;
                    expect(clean, kMachineNames[(std::size_t) m]);
                    const double off = loudness((Machine) m, (Instrument) t) - reference;
                    line << juce::String(off, 1).paddedLeft(' ', 7);
                    expectWithinAbsoluteError(off, 0.0, 1.0,
                                              juce::String(kMachineNames[(std::size_t) m]) + " " + kTrackIds[(std::size_t) t]);
                }
                logMessage(line);
            }
        }

        beginTest("The machines sound like their classics");
        {
            auto kick = [](Machine machine, float tone = 0.5f) {
                return shaped(Instrument::kick, [=](TrackSettings& t) {
                    t.machine = machine;
                    t.tone = tone;
                    t.decay = 1.5f;
                }, 0.6);
            };
            // Boom 88's kick is deep and barely bends; Punch 99's starts far above its note.
            const auto boom = kick(Machine::boom88), punch = kick(Machine::punch99);
            const double boomNote = peakBetween(boom, 35.0, 80.0, 0.5, at(0.3), at(0.55));
            expectWithinAbsoluteError(boomNote, 47.0, 2.0);
            expectLessThan(boomNote, peakBetween(punch, 35.0, 80.0, 0.5, at(0.3), at(0.55)));
            auto start = [](const std::vector<float>& out) {
                return amplitudeAt(out, 140.0, 0, at(0.012)) / peak(out, 0, out.size());
            };
            expectGreaterThan(start(punch), 2.0f * start(boom));
            // Kompakt 78's thump has no beater, Studio 80's a big one.
            auto beater = [&](Machine machine) {
                const auto out = shaped(Instrument::kick, [=](TrackSettings& t) {
                    t.machine = machine;
                    t.character = 1.0f;
                }, 0.01);
                return shareAbove(out, 2000.0);
            };
            expectGreaterThan(beater(Machine::studio80), 4.0f * beater(Machine::kompakt78));

            // Sechseck's toms fall an octave and more; Boom 88's hold their pitch.
            auto fall = [](Machine machine) {
                const auto out = shaped(Instrument::lowTom, [=](TrackSettings& t) {
                    t.machine = machine;
                    t.decay = 2.0f;
                }, 0.5);
                const double early = peakBetween(out, 60.0, 400.0, 1.0, 0, at(0.02));
                const double late = peakBetween(out, 60.0, 400.0, 1.0, at(0.35), at(0.5));
                return early / late;
            };
            expectGreaterThan(fall(Machine::sechseck), 1.8);
            expectLessThan(fall(Machine::boom88), 1.3);

            // Kompakt 78's hats are hiss, Boom 88's ring of squares: the squares give a few strong lines.
            auto peakiness = [](Machine machine) {
                const auto out = shaped(Instrument::openHat, [=](TrackSettings& t) {
                    t.machine = machine;
                    t.character = 0.0f;
                }, 0.3);
                double strongest = 0.0, sum = 0.0;
                int count = 0;
                for (double hz = 4000.0; hz < 16000.0; hz += 25.0, ++count)
                {
                    const double a = amplitudeAt(out, hz, 0, at(0.2));
                    strongest = std::max(strongest, a);
                    sum += a;
                }
                return strongest / (sum / count);
            };
            expectGreaterThan(peakiness(Machine::boom88), 1.5 * peakiness(Machine::kompakt78));

            // The sampling machines play from few bits: a hit takes only a few hundred values.
            auto values = [](Machine machine) {
                const auto out = shaped(Instrument::snare, [=](TrackSettings& t) { t.machine = machine; }, 0.3);
                return std::set<float>(out.begin(), out.end()).size();
            };
            for (const auto machine : { Machine::neon77, Machine::studio80, Machine::street81 })
                expectLessThan((int) values(machine), 300, kMachineNames[(std::size_t) machine]);
            expectGreaterThan((int) values(Machine::boom88), 3000);
        }

        // For a listen: TONWERK_PRESET_WAV_DIR=/path writes two bars of one groove on every machine.
        if (const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {}); wavDir.isNotEmpty())
        {
            const Pattern groove = only({ { Instrument::kick, "x... ..x. x... .... x... ..x. x... ...." },
                                          { Instrument::snare, ".... x... .... x... .... x... .... x.xx" },
                                          { Instrument::clap, ".... x... .... x... .... x... .... ...." },
                                          { Instrument::lowTom, ".... .... .... .... .... .... ..x. ...." },
                                          { Instrument::highTom, ".... .... .... .... .... .... x... ...." },
                                          { Instrument::closedHat, "x.x. x.x. x.x. x.x. x.x. x.x. .... ...." },
                                          { Instrument::openHat, ".... .... .... ..x. .... .... .... ...." },
                                          { Instrument::cymbal, "x... .... .... .... .... .... .... ...." } });
            for (int m = 0; m < kMachines; ++m)
            {
                GrooveProcessor processor;
                processor.setPattern(0, groove);
                set(processor, "length", 32.0f);
                // Kick, snare, clap and cymbal on one step: room under full scale.
                set(processor, "master", -3.0f);
                for (const auto* track : kTrackIds)
                    set(processor, juce::String(track) + "Machine", (float) m);
                Head head;
                const auto take = run(processor, 4.5, &head, {}, true);
                writeWav(take, juce::File(wavDir).getChildFile("Tonwerk Groovebox Maschinen").getChildFile(
                                   juce::String(m + 1).paddedLeft('0', 2) + " " + kMachineNames[(std::size_t) m] + ".wav"));
            }
        }

        beginTest("A kit keeps the machines, and a project from before them loads on Tonwerk");
        {
            GrooveProcessor processor;
            set(processor, "snareMachine", (float) Machine::punch99);
            processor.setCurrentProgram(3);
            expect(processor.readSettings().track[1].machine == Machine::punch99);
            expect(processor.readSettings().track[0].machine == Machine::tonwerk);

            juce::MemoryBlock saved;
            GrooveProcessor().getStateInformation(saved);
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            for (auto* param = xml->getFirstChildElement(); param != nullptr;)
            {
                auto* next = param->getNextElement();
                if (param->getStringAttribute("id").endsWith("Machine"))
                    xml->removeChildElement(param, true);
                param = next;
            }
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary(*xml, old);
            GrooveProcessor loaded;
            set(loaded, "kickMachine", (float) Machine::sechseck);
            loaded.setStateInformation(old.getData(), (int) old.getSize());
            for (const auto& track : loaded.readSettings().track)
                expect(track.machine == Machine::tonwerk);
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

        beginTest("The effects switch: off fades delay and hall out in 20 ms and leaves the tracks dry");
        {
            // A snare into delay and hall; the effects go off at 0.3 s, back on at 0.6 s, without a new hit.
            // 0: effects on throughout, 1: switched, 2: off throughout.
            auto take = [](int mode) {
                const bool switching = mode == 1;
                GrooveProcessor processor;
                plain(processor);
                set(processor, "seqMode", 0.0f);
                set(processor, "fxReverbMix", 0.5f);
                set(processor, "fxDelayMix", 0.5f);
                set(processor, "fxDelaySync", 0.0f);
                set(processor, "fxDelayTime", 120.0f);
                set(processor, "fxDelayFeedback", 0.6f);
                set(processor, "snareReverb", 0.7f);
                set(processor, "snareDelay", 0.7f);
                set(processor, "snareDecay", 800.0f);
                processor.setPlayConfigDetails(0, 2, kRate, kBlock);
                if (mode == 2)
                    set(processor, "fxOn", 0.0f);
                processor.prepareToPlay(kRate, kBlock);
                juce::AudioBuffer<float> buffer(2, kBlock);
                juce::MidiBuffer none;
                for (int waited = 0; ! processor.roomPlaying() && waited < 5000; ++waited)
                {
                    processor.processBlock(buffer, none);
                    juce::Thread::sleep(1);
                }
                std::vector<float> out(at(1.0));
                for (std::size_t from = 0; from < out.size(); from += kBlock)
                {
                    if (switching && from == at(0.3) / kBlock * kBlock)
                        set(processor, "fxOn", 0.0f);
                    if (switching && from == at(0.6) / kBlock * kBlock)
                        set(processor, "fxOn", 1.0f);
                    juce::MidiBuffer midi;
                    if (from == 0)
                        midi.addEvent(juce::MidiMessage::noteOn(10, 38, (juce::uint8) 127), 0);
                    processor.processBlock(buffer, midi);
                    std::copy_n(buffer.getReadPointer(0), std::min<std::size_t>(kBlock, out.size() - from),
                                out.data() + from);
                }
                return out;
            };
            const auto on = take(0);
            const auto switched = take(1);
            const auto dry = take(2);
            const auto off = at(0.3) / kBlock * kBlock;
            // Until the switch both are the same; while it fades the effects are still there, after it nothing.
            expectWithinAbsoluteError(peak(switched, 0, off), peak(on, 0, off), 0.01f * peak(on, 0, off));
            expectGreaterThan(rms(on, off + at(0.025), at(0.6)), 1.0e-3f);
            expectGreaterThan(rms(switched, off, off + at(0.005)), 0.3f * rms(on, off, off + at(0.005)));
            expectGreaterThan(rms(dry, off, at(0.6)), 1.0e-5f); // the snare's own tail, which stays
            std::vector<float> difference(switched.size());
            for (std::size_t i = 0; i < difference.size(); ++i)
                difference[i] = switched[i] - dry[i];
            expectLessThan(rms(difference, off + at(0.025), at(0.6)), 1.0e-6f);
            // The fade is a ramp: no sample jumps further than the take with the effects on does.
            float jump = 0.0f, onJump = 0.0f;
            for (std::size_t i = off; i < off + at(0.03); ++i)
            {
                jump = std::max(jump, std::abs(switched[i + 1] - switched[i]));
                onJump = std::max(onJump, std::abs(on[i + 1] - on[i]));
            }
            expectLessThan(jump, onJump + 1.0e-3f);
            // Back on, the closed sends left nothing in the lines: the old repeats and hall do not return.
            expectLessThan(rms(difference, at(0.6), at(1.0)), 0.05f * rms(on, at(0.6), at(1.0)));

            // A kit leaves the switch as it is; a new instance starts with the effects on.
            GrooveProcessor processor;
            expectEquals(processor.state.getRawParameterValue("fxOn")->load(), 1.0f);
            set(processor, "fxOn", 0.0f);
            processor.setCurrentProgram(3);
            expectEquals(processor.state.getRawParameterValue("fxOn")->load(), 0.0f);
            auto* parameter = processor.state.getParameter("fxOn");
            expectEquals(parameter->getText(1.0f, 32), juce::String("An"));
            expectEquals(parameter->getText(0.0f, 32), juce::String("Aus"));
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

        beginTest("A project from before the shaping loads with the shaping at its defaults");
        {
            GrooveProcessor before;
            set(before, "kickTune", -5.0f);
            juce::MemoryBlock saved;
            before.getStateInformation(saved);
            // What the first version saved: the same state without the parameters added since.
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            for (auto* param = xml->getFirstChildElement(); param != nullptr;)
            {
                auto* next = param->getNextElement();
                const auto paramId = param->getStringAttribute("id");
                for (const auto* suffix : { "Attack", "Hold", "PitchMod", "ModDecay", "Filter", "Cutoff", "Resonance",
                                            "FilterMod", "Drive", "Bits", "Crush", "Velocity", "Random" })
                    if (paramId.endsWith(suffix) && ! paramId.startsWith("fx"))
                    {
                        xml->removeChildElement(param, true);
                        break;
                    }
                param = next;
            }
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary(*xml, old);
            GrooveProcessor loaded;
            set(loaded, "kickHold", 400.0f);
            set(loaded, "kickFilter", 1.0f);
            loaded.setStateInformation(old.getData(), (int) old.getSize());
            const auto kick = loaded.readSettings().track[0];
            expectWithinAbsoluteError(kick.tune, -5.0f, 1.0e-3f);
            expectEquals(kick.hold, 0.0f);
            expect(kick.filter == TrackFilter::off);
            expectEquals(kick.velocity, 1.0f);
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
            expectEquals(text("snareHold", 249.7f), juce::String("250 ms"));
            expectEquals(text("kickPitchMod", 12.2f), juce::String("12 HT"));
            expectEquals(text("hatOpenCutoff", 1000.3f), juce::String("1000 Hz"));
            expectEquals(text("hatOpenFilterMod", -0.5f), juce::String("-50 %"));
            expectEquals(text("clapBits", 8.0f), juce::String("8 Bit"));
            expectEquals(text("clapBits", 16.0f), juce::String("Aus"));
            expectEquals(text("clapCrush", 48000.0f), juce::String("Aus"));
            expectEquals(text("clapCrush", 4000.0f), juce::String("4000 Hz"));
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

            // The same with every track's shaping working its hardest: the mod envelope moving pitch and filter.
            for (const auto* track : kTrackIds)
            {
                const juce::String t = track;
                set(processor, t + "PitchMod", 12.0f);
                set(processor, t + "ModDecay", 2000.0f);
                set(processor, t + "Filter", 1.0f);
                set(processor, t + "Resonance", 0.5f);
                set(processor, t + "FilterMod", 0.5f);
                set(processor, t + "Drive", 0.5f);
                set(processor, t + "Bits", 8.0f);
                set(processor, t + "Crush", 8000.0f);
                set(processor, t + "Random", 0.5f);
            }
            const auto shapedOut = run(processor, 4.0, nullptr, {}, true);
            const double shapedRealtime = 4.0 / shapedOut.seconds;
            logMessage("The same with every track shaped: " + juce::String(shapedRealtime, 1) + " times real time");
            expect(finite(shapedOut));
            expectGreaterThan(shapedRealtime, 5.0);

            // And every track on a sampling machine, whose bits and rate come on top of the shaping.
            for (const auto* track : kTrackIds)
                set(processor, juce::String(track) + "Machine", (float) Machine::studio80);
            const auto machineOut = run(processor, 4.0, nullptr, {}, true);
            const double machineRealtime = 4.0 / machineOut.seconds;
            logMessage("The same on Studio 80: " + juce::String(machineRealtime, 1) + " times real time");
            expect(finite(machineOut));
            expectGreaterThan(machineRealtime, 5.0);
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
