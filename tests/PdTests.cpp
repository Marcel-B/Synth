#include "PdEditor.h"
#include "PdEngine.h"
#include "PdPresets.h"
#include "PdProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkpd;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

double noteHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

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

/** The pitch of `signal` from `from` over `length` samples, by de Cheveigné and Kawahara's YIN. */
double pitchOf(const std::vector<float>& signal, std::size_t from, std::size_t length, double lowest, double highest)
{
    const int shortest = (int) (kRate / highest), longest = (int) (kRate / lowest);
    std::vector<double> d((std::size_t) longest + 2, 0.0), normal((std::size_t) longest + 2, 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= longest + 1; ++tau)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < length; ++i)
        {
            const double v = signal[from + i] - signal[from + i + (std::size_t) tau];
            sum += v * v;
        }
        d[(std::size_t) tau] = sum;
        running += sum;
        normal[(std::size_t) tau] = sum * tau / std::max(1.0e-12, running);
    }
    int best = -1;
    for (int tau = std::max(2, shortest); tau <= longest; ++tau)
        if (normal[(std::size_t) tau] < 0.15)
        {
            while (tau + 1 <= longest && normal[(std::size_t) tau + 1] < normal[(std::size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        double lowestValue = 1.0e9;
        for (int tau = std::max(2, shortest); tau <= longest; ++tau)
            if (normal[(std::size_t) tau] < lowestValue)
            {
                lowestValue = normal[(std::size_t) tau];
                best = tau;
            }
    }
    const double a = normal[(std::size_t) best - 1], b = normal[(std::size_t) best], c = normal[(std::size_t) best + 1];
    const double shift = (a - c) / (2.0 * (a - 2.0 * b + c));
    return kRate / (best + (std::isfinite(shift) ? shift : 0.0));
}

double cents(double hz, double reference) { return 1200.0 * std::log2(hz / reference); }

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) (to - from));
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

std::size_t at(double seconds) { return (std::size_t) (seconds * kRate); }

struct Note
{
    double on, off;
    int note;
    int velocity = 127;
};

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

using Knobs = std::vector<std::pair<const char*, float>>;

void set(PdProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/**
 * Plays `notes` on `processor` for `length` seconds. Without a program the path is plain: no hall, no master cut, no
 * stereo spread, full velocity at full level.
 */
Rendered play(PdProcessor& processor, const Knobs& knobs, std::vector<Note> notes, double length, int program = -1)
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    if (program >= 0)
        processor.setCurrentProgram(program);
    else
    {
        set(processor, "fxReverbMix", 0.0f);
        set(processor, "master", 0.0f);
        set(processor, "velocityAmount", 0.0f);
        set(processor, "width", 0.0f);
    }
    for (const auto& [id, value] : knobs)
        set(processor, id, value);
    processor.prepareToPlay(kRate, kBlock);

    const auto total = at(length);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t from = 0; from < total; from += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - from);
        juce::MidiBuffer midi;
        for (const auto& n : notes)
        {
            const auto on = at(n.on);
            const auto off = at(n.off);
            if (on >= from && on < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, (juce::uint8) n.velocity), (int) (on - from));
            if (off >= from && off < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), (int) (off - from));
        }
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + from);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + from);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

Rendered play(const Knobs& knobs, std::vector<Note> notes, double length, int program = -1)
{
    PdProcessor processor;
    return play(processor, knobs, std::move(notes), length, program);
}

/** One note held for `length` seconds. */
std::vector<float> hold(const Knobs& knobs, int note, double length)
{
    return play(knobs, { { 0.0, length + 1.0, note } }, length).left;
}

bool finite(const Rendered& r)
{
    for (std::size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite(r.left[i]) || ! std::isfinite(r.right[i]))
            return false;
    return true;
}

float peakOf(const Rendered& r)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < r.left.size(); ++i)
        peak = std::max({ peak, std::abs(r.left[i]), std::abs(r.right[i]) });
    return peak;
}

/** The amplitudes of the first `count` harmonics of `hz`, from `from` to `to`. */
std::vector<float> harmonics(const std::vector<float>& signal, double hz, int count, std::size_t from, std::size_t to)
{
    std::vector<float> a;
    for (int n = 1; n <= count; ++n)
        a.push_back(amplitudeAt(signal, hz * n, from, to));
    return a;
}

/** How much of the sound lies above the fundamental: harmonics 2 to 16 against the first, in dB. */
float brightness(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const auto a = harmonics(signal, hz, 16, from, to);
    double upper = 0.0;
    for (std::size_t n = 1; n < a.size(); ++n)
        upper += (double) a[n] * a[n];
    return decibels((float) std::sqrt(upper) / std::max(1.0e-9f, a[0]));
}

/** Where the sound's weight lies among the first 32 harmonics, as a harmonic number (1 for a sine). */
float centroid(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const auto a = harmonics(signal, hz, 32, from, to);
    double sum = 0.0, weighted = 0.0;
    for (std::size_t n = 0; n < a.size(); ++n)
    {
        sum += (double) a[n] * a[n];
        weighted += (double) (n + 1) * a[n] * a[n];
    }
    return (float) (weighted / std::max(1.0e-18, sum));
}

/** Line 1 alone with the bend held at its depth, so the sound is the wave at that bend. */
Knobs steady(float wave, float depth)
{
    return { { "line1Wave", wave }, { "line1Depth", depth }, { "line1KeyFollow", 0.0f }, { "line1DcwSustain", 1.0f },
             { "line1AmpSustain", 1.0f }, { "velocityAmount", 0.0f }, { "wheelAmount", 0.0f } };
}

std::vector<float> steadyNote(float wave, float depth, int note, double length = 0.6)
{
    return hold(steady(wave, depth), note, length);
}

constexpr int kWaves = (int) Wave::count;
constexpr float kSquare = 1, kPulse = 2, kDoubleSine = 3, kResSaw = 5;
} // namespace

class PdTests : public juce::UnitTest
{
public:
    PdTests() : juce::UnitTest("Tonwerk PD", "PD") {}

    void runTest() override
    {
        beginTest("Unbent, every wave but the resonances is a plain cosine");
        {
            for (int w = 0; w < kWaves; ++w)
            {
                float worst = 0.0f;
                for (int i = 0; i < 1000; ++i)
                {
                    const double phase = i / 1000.0;
                    const float plain = (float) std::cos(2.0 * juce::MathConstants<double>::pi * phase);
                    worst = std::max(worst, std::abs(pdWave((Wave) w, phase, 0.0f, 0.0) - plain));
                }
                // A resonance unbent is one cycle of the cosine under its window: the window's shape, not the cosine.
                if (! isResonance((Wave) w))
                    expectLessThan(worst, 0.002f, "wave " + juce::String(w));
            }
            const auto out = steadyNote(0.0f, 0.0f, 57);
            expectLessThan(brightness(out, noteHz(57), at(0.2), at(0.5)), -40.0f);
        }

        beginTest("Every wave plays at the note, across the keyboard");
        {
            for (int w = 0; w < kWaves; ++w)
                for (const int note : { 36, 60, 84 })
                {
                    const auto out = steadyNote((float) w, 0.6f, note);
                    const double hz = noteHz(note);
                    const juce::String label = "wave " + juce::String(w) + " note " + juce::String(note);
                    // A resonance at 10 times the note repeats nearly every ninth of a period, which fools YIN: there
                    // the check is that everything lies on the note's harmonics, nothing halfway between.
                    if (! isResonance((Wave) w))
                        expectLessThan(std::abs(cents(pitchOf(out, at(0.2), 2048, hz / 1.5, hz * 1.5), hz)), 5.0, label);
                    float on = 0.0f, between = 0.0f;
                    for (int n = 1; n * hz < 16000.0; ++n)
                    {
                        on = std::max(on, amplitudeAt(out, n * hz, at(0.1), at(0.5)));
                        between = std::max(between, amplitudeAt(out, (n - 0.5) * hz, at(0.1), at(0.5)));
                    }
                    expectLessThan(decibels(between / on), -30.0f, label);
                }
        }

        beginTest("More DCW, brighter");
        {
            for (int w = 0; w < kWaves; ++w)
            {
                float last = 0.0f;
                for (const float depth : { 0.0f, 0.3f, 0.6f, 0.9f })
                {
                    const float c = centroid(steadyNote((float) w, depth, 45), noteHz(45), at(0.2), at(0.5));
                    expectGreaterThan(c, last + 0.02f, "wave " + juce::String(w) + " depth " + juce::String(depth));
                    last = c;
                }
            }
        }

        beginTest("Fully bent, the saw has every overtone, the square only odd ones");
        {
            const double hz = noteHz(45);
            const auto saw = harmonics(steadyNote(0.0f, 1.0f, 45), hz, 6, at(0.2), at(0.5));
            for (std::size_t n = 1; n < saw.size(); ++n)
                // A saw's overtones fall as 1/n: the sixth about 16 dB under the first.
                expectGreaterThan(decibels(saw[n] / saw[0]), -20.0f, "saw harmonic " + juce::String(n + 1));
            const auto square = harmonics(steadyNote(kSquare, 1.0f, 45), hz, 6, at(0.2), at(0.5));
            expectGreaterThan(decibels(square[2] / square[0]), -12.0f);
            for (const std::size_t even : { 1u, 3u, 5u })
                expectLessThan(decibels(square[even] / square[0]), -40.0f, "square harmonic " + juce::String(even + 1));
        }

        beginTest("The resonance's peak climbs with the DCW, sixteen times the note at most");
        {
            const double hz = noteHz(45);
            for (const auto& [depth, expected] : { std::pair { 0.2f, 4 }, std::pair { 0.6f, 10 }, std::pair { 1.0f, 16 } })
            {
                const auto a = harmonics(steadyNote(kResSaw, depth, 45), hz, 20, at(0.2), at(0.5));
                // The strongest overtone above the second, where the window's own low ones fade out.
                std::size_t loudest = 2;
                for (std::size_t n = 2; n < a.size(); ++n)
                    if (a[n] > a[loudest])
                        loudest = n;
                expectLessOrEqual(std::abs((int) loudest + 1 - expected), 1, "depth " + juce::String(depth));
            }
        }

        beginTest("The DCW envelope closes the sound as it falls");
        {
            const auto out = hold({ { "line1Depth", 0.9f }, { "line1DcwDecay", 300 }, { "line1DcwSustain", 0.0f },
                                    { "line1AmpSustain", 1.0f }, { "velocityAmount", 0.0f }, { "wheelAmount", 0.0f } },
                                  45, 1.5);
            const double hz = noteHz(45);
            const float early = brightness(out, hz, at(0.0), at(0.08));
            const float late = brightness(out, hz, at(1.0), at(1.4));
            expectGreaterThan(early - late, 20.0f);
        }

        beginTest("Two waves taking turns add the octave below");
        {
            const double hz = noteHz(57);
            auto knobs = [](float second) {
                return Knobs { { "line1Depth", 0.8f }, { "line1Wave2", second }, { "line1DcwSustain", 1.0f },
                               { "line1AmpSustain", 1.0f }, { "velocityAmount", 0.0f }, { "wheelAmount", 0.0f } };
            };
            const auto single = hold(knobs(0.0f), 57, 0.6);
            const auto pair = hold(knobs(kPulse + 1), 57, 0.6);
            const float below = amplitudeAt(pair, hz / 2, at(0.2), at(0.5));
            expectGreaterThan(decibels(below / amplitudeAt(pair, hz, at(0.2), at(0.5))), -25.0f);
            expectLessThan(decibels(amplitudeAt(single, hz / 2, at(0.2), at(0.5)) / amplitudeAt(single, hz, at(0.2), at(0.5))),
                           -50.0f);
        }

        beginTest("1+1' plays line 1 twice, the copy at line 2's detune");
        {
            const auto out = hold({ { "lines", 2.0f }, { "line1Depth", 0.0f }, { "line1AmpSustain", 1.0f },
                                    { "line2Wave", kResSaw }, { "detuneSemi", 7 }, { "detuneFine", 0 } },
                                  57, 0.6);
            const double hz = noteHz(57);
            const double fifth = noteHz(64);
            // Both sines alone: the note and its fifth, each at its line's level, nothing of line 2's resonance.
            const float note = amplitudeAt(out, hz, at(0.2), at(0.5));
            const float copy = amplitudeAt(out, fifth, at(0.2), at(0.5));
            expectWithinAbsoluteError(decibels(copy / note), 0.0f, 1.5f);
            expectLessThan(decibels(amplitudeAt(out, fifth * 3, at(0.2), at(0.5)) / copy), -40.0f);
        }

        beginTest("Ring modulation leaves the sum and the difference, not the notes");
        {
            auto knobs = [](float modulation) {
                return Knobs { { "lines", 3.0f },        { "modulation", modulation }, { "line1Depth", 0.0f },
                               { "line2Depth", 0.0f },   { "line1AmpSustain", 1.0f },  { "line2AmpSustain", 1.0f },
                               { "detuneSemi", 12 },     { "detuneFine", 0 },          { "line2Octave", 1 },
                               { "velocityAmount", 0.0f } };
            };
            // 220 Hz and 880 Hz: multiplied, 660 and 1100.
            const auto out = hold(knobs(1.0f), 57, 0.6);
            const float sum = amplitudeAt(out, 1100.0, at(0.2), at(0.5));
            const float difference = amplitudeAt(out, 660.0, at(0.2), at(0.5));
            const float upper = amplitudeAt(out, 880.0, at(0.2), at(0.5));
            expectGreaterThan(decibels(sum / std::max(1.0e-9f, upper)), 30.0f);
            expectWithinAbsoluteError(decibels(sum / difference), 0.0f, 1.5f);
            // Line 1 still plays as itself.
            expectGreaterThan(amplitudeAt(out, 220.0, at(0.2), at(0.5)), 0.05f);
        }

        beginTest("Noise blurs line 2 into a band around its pitch");
        {
            auto knobs = [](float modulation) {
                return Knobs { { "lines", 1.0f },          { "modulation", modulation }, { "line2Depth", 0.3f },
                               { "line2AmpSustain", 1.0f }, { "line2DcwSustain", 1.0f },  { "velocityAmount", 0.0f },
                               { "wheelAmount", 0.0f },     { "detuneFine", 0.0f } };
            };
            const double hz = noteHz(57);
            const auto clean = hold(knobs(0.0f), 57, 0.6);
            const auto noisy = hold(knobs(2.0f), 57, 0.6);
            // How far each cycle differs from the one before: nothing for a steady tone.
            auto unsteady = [hz](const std::vector<float>& out) {
                const double period = kRate / hz;
                double difference = 0.0, total = 0.0;
                for (std::size_t n = at(0.1); n < at(0.5); ++n)
                {
                    const double back = (double) n - period;
                    const auto i = (std::size_t) back;
                    const double frac = back - (double) i;
                    const double before = out[i] + (out[i + 1] - out[i]) * frac;
                    difference += (out[n] - before) * (out[n] - before);
                    total += (double) out[n] * out[n];
                }
                return (float) std::sqrt(difference / total);
            };
            expectLessThan(unsteady(clean), 0.05f);
            expectGreaterThan(unsteady(noisy), 0.3f);
            // The note still stands out of the noise: its fundamental carries a good share of the level.
            expectGreaterThan(amplitudeAt(noisy, hz, at(0.1), at(0.5)) / rms(noisy, at(0.1), at(0.5)), 0.5f);
        }

        beginTest("A high note fully bent keeps what folds back well under its overtones");
        {
            for (const float wave : { 0.0f, kSquare, kPulse, kDoubleSine, kResSaw })
            {
                const int note = 96;
                const auto out = steadyNote(wave, 1.0f, note, 0.5);
                const double hz = noteHz(note);
                // Harmonics of C7 are 2093 Hz apart; between them everything is folded back from above Nyquist.
                float harmonic = 0.0f, folded = 0.0f;
                for (int n = 1; n * hz < 20000.0; ++n)
                    harmonic = std::max(harmonic, amplitudeAt(out, n * hz, at(0.1), at(0.4)));
                for (double f = 100.0; f < 20000.0; f += 37.0)
                {
                    const double nearest = std::round(f / hz) * hz;
                    if (std::abs(f - nearest) > 150.0)
                        folded = std::max(folded, amplitudeAt(out, f, at(0.1), at(0.4)));
                }
                const float db = decibels(folded / harmonic);
                logMessage("wave " + juce::String(wave) + " at C7: folded back " + juce::String(db, 1) + " dB");
                expectLessThan(db, -30.0f, "wave " + juce::String(wave));
            }
        }

        beginTest("Every wave, line choice and modulation stays finite and in bounds, at the extremes too");
        {
            for (int w = 0; w < kWaves; ++w)
                for (const float lines : { 0.0f, 1.0f, 2.0f, 3.0f })
                    for (const float modulation : { 0.0f, 1.0f, 2.0f })
                    {
                        const auto out = play({ { "line1Wave", (float) w }, { "line2Wave", (float) ((w + 3) % kWaves) },
                                                { "line1Wave2", (float) ((w + 5) % (kWaves + 1)) }, { "lines", lines },
                                                { "modulation", modulation }, { "line1Depth", 1.0f },
                                                { "line2Depth", 1.0f }, { "line1Octave", 3 }, { "line2Octave", -3 },
                                                { "pitchEnvAmount", 24 }, { "detuneSemi", 24 } },
                                              { { 0.0, 0.3, 24 }, { 0.0, 0.3, 108 }, { 0.1, 0.3, 127 }, { 0.1, 0.3, 0 } },
                                              0.5);
                        const juce::String label = "wave " + juce::String(w) + " lines " + juce::String(lines)
                                                   + " modulation " + juce::String(modulation);
                        expect(finite(out), label);
                        // Four notes, each line at most a quarter of full scale, the offset the walls remove at its start.
                        expectLessThan(peakOf(out), 4.0f, label);
                    }
        }

        beginTest("A voice ends after its release, and a stolen one fades rather than jumps");
        {
            Engine engine;
            engine.prepare(kRate);
            Settings s;
            s.line[0].amp.release = 0.2f;
            std::vector<float> left(kBlock), right(kBlock);
            engine.noteOn(60, 1.0f, s);
            for (int i = 0; i < 20; ++i)
                engine.render(left.data(), right.data(), kBlock, s, {});
            engine.noteOff(60, s);
            for (int i = 0; i < (int) (0.5 * kRate) / kBlock; ++i)
                engine.render(left.data(), right.data(), kBlock, s, {});
            expectEquals(engine.activeVoices(), 0);

            // Twice as many notes as voices on soft sines: no step from one sample to the next stands out.
            const auto out = play({ { "line1Depth", 0.0f }, { "line1AmpSustain", 1.0f } },
                                  [] {
                                      std::vector<Note> notes;
                                      for (int i = 0; i < 2 * Engine::kVoices; ++i)
                                          notes.push_back({ 0.03 * i, 3.0, 40 + i });
                                      return notes;
                                  }(),
                                  1.5)
                                 .left;
            float steepest = 0.0f;
            for (std::size_t i = 1; i < out.size(); ++i)
                steepest = std::max(steepest, std::abs(out[i] - out[i - 1]));
            expectLessThan(steepest, 0.3f);
        }

        beginTest("The pitch envelope starts away from the note and arrives");
        {
            const auto out = hold({ { "line1Depth", 0.3f }, { "pitchEnvAmount", 12 }, { "pitchEnvTime", 200 } }, 57, 0.8);
            const double hz = noteHz(57);
            expectGreaterThan(cents(pitchOf(out, at(0.0), 1024, hz / 1.5, hz * 3.0), hz), 700.0);
            expectLessThan(std::abs(cents(pitchOf(out, at(0.5), 2048, hz / 1.5, hz * 1.5), hz)), 5.0);
        }

        beginTest("The chorus makes one line wide");
        {
            const auto dry = play({ { "line1Depth", 0.5f } }, { { 0.0, 1.0, 57 } }, 0.8);
            const auto wet = play({ { "line1Depth", 0.5f }, { "chorusMix", 1.0f } }, { { 0.0, 1.0, 57 } }, 0.8);
            auto difference = [](const Rendered& r) {
                double sum = 0.0;
                for (std::size_t i = at(0.2); i < at(0.7); ++i)
                    sum += std::abs(r.left[i] - r.right[i]);
                return sum;
            };
            expectLessThan(difference(dry), 1.0e-3);
            expectGreaterThan(difference(wet), 100.0);
        }

        beginTest("Every factory sound plays, and names only parameters that exist");
        {
            PdProcessor processor;
            const auto& presets = factoryPresets();
            expectEquals(processor.getNumPrograms(), (int) presets.size());
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                for (const auto& [id, value] : presets[(std::size_t) i].values)
                    expect(processor.state.getParameter(id) != nullptr, id);
                const auto out = play({}, { { 0.0, 1.0, 48 }, { 0.0, 1.0, 55 } }, 1.5, i);
                expect(finite(out), presets[(std::size_t) i].name);
                expectGreaterThan(rms(out.left, 0, out.left.size()), 1.0e-3f, presets[(std::size_t) i].name);
            }
        }

        beginTest("Sixteen voices of both lines with chorus and hall stay well inside real time");
        {
            std::vector<Note> chord;
            for (int i = 0; i < Engine::kVoices; ++i)
                chord.push_back({ 0.0, 4.0, 40 + 3 * i });
            const auto out = play({ { "lines", 3.0f }, { "modulation", 1.0f }, { "line1Wave", kResSaw },
                                    { "line2Wave", kDoubleSine }, { "line1Wave2", kSquare + 1 },
                                    { "lfo2Target", 2.0f }, { "lfo2Amount", 0.3f }, { "chorusMix", 0.5f },
                                    { "fxReverbMix", 0.3f } },
                                  chord, 4.0);
            const double realtime = 4.0 / out.seconds;
            logMessage("16 voices, both lines, ring, chorus and hall: " + juce::String(realtime, 1) + " times real time");
            expectGreaterThan(realtime, 5.0);
        }

        beginTest("The processor keeps its settings and shows whole numbers with units");
        {
            PdProcessor processor;
            set(processor, "line2Depth", 0.42f);
            set(processor, "lfo2Target", 4.0f);
            set(processor, "line1Wave2", kPulse + 1);
            set(processor, "lines", 3.0f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            PdProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            const auto s = loaded.readSettings();
            expectWithinAbsoluteError(s.line[1].depth, 0.42f, 1.0e-4f);
            expect(s.lfo[1].route.target == Target::dcw2);
            expect(s.line[0].alternate && s.line[0].second == Wave::pulse);
            expect(s.lines == LineSelect::both);

            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("line1DcwDecay", 599.7f), juce::String("600 ms"));
            expectEquals(text("line1Depth", 0.6f), juce::String("60 %"));
            expectEquals(text("detuneFine", -7.0f), juce::String("-7 ct"));
            expectEquals(text("detuneSemi", 7.0f), juce::String("7 HT"));
            expectEquals(text("pitchEnvAmount", 12.0f), juce::String("12 HT"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));
        }

        beginTest("The editor fits its panels");
        {
            PdProcessor processor;
            play(processor, { { "lines", 3.0f }, { "line1Wave2", kSquare + 1 } }, { { 0.0, 2.0, 57 } }, 0.3);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk PD.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static PdTests pdTests;
