#include "AdditiveEditor.h"
#include "AdditiveEngine.h"
#include "AdditivePresets.h"
#include "AdditiveProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkadd;

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

using Knobs = std::initializer_list<std::pair<const char*, float>>;

void set(AdditiveProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/**
 * Plays `notes` on `processor` for `length` seconds. Without a program the path is plain: no hall, no master cut, no
 * stereo spread, the partials starting in phase, full velocity at full level.
 */
Rendered play(AdditiveProcessor& processor, Knobs knobs, std::vector<Note> notes, double length, int program = -1)
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
        set(processor, "phases", 0.0f);
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

Rendered play(Knobs knobs, std::vector<Note> notes, double length, int program = -1)
{
    AdditiveProcessor processor;
    return play(processor, knobs, std::move(notes), length, program);
}

/** One note held for `length` seconds. */
std::vector<float> hold(Knobs knobs, int note, double length)
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


/** The amplitude of the k-th harmonic of `note` in a held tone, from 0.2 s to 0.6 s. */
float harmonic(const std::vector<float>& out, int note, double k)
{
    return amplitudeAt(out, k * noteHz(note), at(0.2), at(0.6));
}

/** As `amplitudeAt` through a Hann window, so a loud neighbour does not leak into a quiet partial. */
float windowedAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    const double length = (double) (to - from);
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < to; ++n)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (double) (n - from) / length);
        re += hann * signal[n] * std::cos(w * (double) n);
        im += hann * signal[n] * std::sin(w * (double) n);
    }
    return (float) (4.0 * std::sqrt(re * re + im * im) / length);
}

constexpr float kSine = 0, kSaw = 1, kSquare = 2, kGlass = 9;
} // namespace

class AdditiveTests : public juce::UnitTest
{
public:
    AdditiveTests() : juce::UnitTest("Tonwerk Additiv", "Additive") {}

    void runTest() override
    {
        beginTest("Every spectrum sounds at the note, across the keyboard");
        {
            for (int spectrum = 0; spectrum < (int) Spectrum::count; ++spectrum)
                for (const int note : { 36, 57, 81 })
                {
                    const auto out = hold({ { "spectrumA", (float) spectrum } }, note, 0.6);
                    const double hz = noteHz(note);
                    const double error = cents(pitchOf(out, at(0.2), 4096, hz / 1.5, hz * 1.5), hz);
                    expectLessThan(std::abs(error), 3.0,
                                   "spectrum " + juce::String(spectrum) + " note " + juce::String(note) + ": "
                                       + juce::String(error, 1) + " ct");
                }
        }

        beginTest("A sawtooth's partials fall as 1/k, a square's even ones are missing");
        {
            const auto saw = hold({}, 45, 0.7);
            for (const int k : { 2, 3, 5, 8 })
                expectWithinAbsoluteError(decibels(harmonic(saw, 45, k) / harmonic(saw, 45, 1)),
                                          decibels(1.0f / (float) k), 0.5f, "partial " + juce::String(k));
            const auto square = hold({ { "spectrumA", kSquare } }, 45, 0.7);
            expectLessThan(decibels(harmonic(square, 45, 2) / harmonic(square, 45, 1)), -60.0f);
            expectWithinAbsoluteError(decibels(harmonic(square, 45, 3) / harmonic(square, 45, 1)),
                                      decibels(1.0f / 3.0f), 0.5f);
        }

        beginTest("Every spectrum is about as loud as the others");
        {
            float softest = 1.0e9f, loudest = 0.0f;
            for (int spectrum = 0; spectrum < (int) Spectrum::count; ++spectrum)
            {
                const auto out = hold({ { "spectrumA", (float) spectrum } }, 48, 0.6);
                const float level = rms(out, at(0.1), at(0.6));
                softest = std::min(softest, level);
                loudest = std::max(loudest, level);
            }
            expectLessThan(decibels(loudest / softest), 0.5f);
        }

        beginTest("Morph blends spectrum A into B");
        {
            const auto half = hold({ { "spectrumA", kSaw }, { "spectrumB", kSquare }, { "morph", 0.5f } }, 45, 0.7);
            // Halfway between the two tables, partial by partial: the second at half the saw's, the first in between.
            const auto& saw = spectra()[(std::size_t) Spectrum::saw];
            const auto& square = spectra()[(std::size_t) Spectrum::square];
            expectWithinAbsoluteError(decibels(harmonic(half, 45, 2) / harmonic(half, 45, 1)),
                                      decibels(0.5f * saw[1] / (0.5f * (saw[0] + square[0]))), 0.5f);
            const auto full = hold({ { "spectrumA", kSaw }, { "spectrumB", kSquare }, { "morph", 1.0f } }, 45, 0.7);
            expectLessThan(decibels(harmonic(full, 45, 2) / harmonic(full, 45, 1)), -60.0f);
        }

        beginTest("The partial count stops the spectrum where it says");
        {
            const auto out = hold({ { "partials", 4 } }, 45, 0.7);
            expectGreaterThan(decibels(harmonic(out, 45, 4) / harmonic(out, 45, 1)), -15.0f);
            expectLessThan(decibels(harmonic(out, 45, 5) / harmonic(out, 45, 1)), -80.0f);
        }

        beginTest("No partial folds back below the sample rate's half");
        {
            // At 2637 Hz the tenth partial would lie at 26.4 kHz and fold down to 21.6 kHz.
            const int note = 100;
            const auto out = hold({}, note, 0.7);
            const double folded = kRate - 10.0 * noteHz(note);
            expectLessThan(decibels(amplitudeAt(out, folded, at(0.2), at(0.6)) / harmonic(out, note, 1)), -80.0f);
            expectGreaterThan(decibels(harmonic(out, note, 7) / harmonic(out, note, 1)), -20.0f);
        }

        beginTest("Tilt adds its dB per octave, odd and even trade places, stretch moves the partials up");
        {
            const auto flat = hold({}, 45, 0.7);
            const auto tilted = hold({ { "tilt", 6 } }, 45, 0.7);
            // Two octaves from the first to the fourth partial: 12 dB more.
            const float gain = decibels(harmonic(tilted, 45, 4) / harmonic(tilted, 45, 1))
                               - decibels(harmonic(flat, 45, 4) / harmonic(flat, 45, 1));
            expectWithinAbsoluteError(gain, 12.0f, 0.5f);

            const auto odd = hold({ { "oddEven", 1.0f } }, 45, 0.7);
            expectLessThan(decibels(harmonic(odd, 45, 2) / harmonic(odd, 45, 1)), -60.0f);
            const auto even = hold({ { "oddEven", -1.0f } }, 45, 0.7);
            expectLessThan(decibels(harmonic(even, 45, 3) / harmonic(even, 45, 1)), -60.0f);
            expectGreaterThan(decibels(harmonic(even, 45, 2) / harmonic(even, 45, 1)), -7.0f);

            const auto stretched = hold({ { "spectrumA", kGlass }, { "stretch", 1.0f } }, 45, 0.7);
            const double hz = noteHz(45);
            const double place = peakBetween(stretched, 18.5 * hz, 21.5 * hz, hz / 50.0, at(0.2), at(0.6)) / hz;
            expectWithinAbsoluteError(place, 16.0 * std::exp2(0.08 * 4.0), 0.05);
        }

        beginTest("The formants stay in place in hertz: an A is strong near 800 Hz, a U near 300 Hz");
        {
            auto ratio = [](float vowel, int note) {
                const auto out = hold({ { "vowel", vowel }, { "formantDepth", 1.0f } }, note, 0.7);
                const double hz = noteHz(note);
                const int k800 = (int) std::lround(800.0 / hz), k300 = (int) std::lround(300.0 / hz);
                return decibels(harmonic(out, note, k800) / harmonic(out, note, k300));
            };
            for (const int note : { 40, 45 })
                expectGreaterThan(ratio(0.0f, note) - ratio(4.0f, note), 15.0f, "note " + juce::String(note));
        }

        beginTest("The comb cuts notches into the spectrum, and its shift moves them");
        {
            const auto plain = hold({ { "combDepth", 1.0f }, { "combSpacing", 4 } }, 45, 0.7);
            const auto flat = hold({}, 45, 0.7);
            // Notches halfway between the peaks: at the 3rd, 7th, ... partial.
            expectLessThan(decibels(harmonic(plain, 45, 3) / harmonic(flat, 45, 3)), -40.0f);
            expectWithinAbsoluteError(decibels(harmonic(plain, 45, 5) / harmonic(flat, 45, 5)), 0.0f, 0.5f);
            const auto shifted = hold({ { "combDepth", 1.0f }, { "combSpacing", 4 }, { "combShift", 0.5f } }, 45, 0.7);
            expectWithinAbsoluteError(decibels(harmonic(shifted, 45, 3) / harmonic(flat, 45, 3)), 0.0f, 0.5f);
            expectLessThan(decibels(harmonic(shifted, 45, 5) / harmonic(flat, 45, 5)), -40.0f);
        }

        beginTest("The spectral filter falls 24 dB per octave above its cutoff, with no ringing of its own");
        {
            const int note = 33;
            const auto flat = hold({}, note, 0.7);
            const auto filtered = hold({ { "filterOn", 1 }, { "cutoff", 440 } }, note, 0.7);
            auto drop = [&](int k) { return decibels(harmonic(filtered, note, k) / harmonic(flat, note, k)); };
            // One octave above the cutoff about 24 dB down, two octaves 48 dB; well below it, untouched.
            expectWithinAbsoluteError(drop(16), -24.0f, 1.0f);
            expectWithinAbsoluteError(drop(32), -48.0f, 1.0f);
            expectWithinAbsoluteError(drop(1), 0.0f, 0.5f);
            const auto high = hold({ { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 880 } }, note, 0.7);
            expectLessThan(decibels(harmonic(high, note, 2) / harmonic(flat, note, 2)), -40.0f);
        }

        beginTest("The partials' own envelope: high partials die sooner and come in later");
        {
            const int note = 45;
            const auto out = hold({ { "spectralOn", 1 }, { "spectralDecay", 1000 }, { "spectralDamping", 1.0f } }, note,
                                  1.2);
            auto level = [&](int k, double centre) {
                return decibels(windowedAt(out, k * noteHz(note), at(centre - 0.05), at(centre + 0.05)));
            };
            // The fundamental falls 60 dB in a second, so 12 dB in 0.2 s; the second partial twice, the fourth four
            // times as fast.
            expectWithinAbsoluteError(level(1, 0.1) - level(1, 0.3), 12.0f, 1.0f);
            expectWithinAbsoluteError(level(2, 0.1) - level(2, 0.3), 24.0f, 1.5f);
            expectWithinAbsoluteError(level(4, 0.1) - level(4, 0.3), 48.0f, 3.0f);

            const auto bloom = hold({ { "spectralOn", 1 }, { "bloom", 1000 }, { "spectralDecay", 20000 },
                                      { "spectralSustain", 1.0f } },
                                    note, 1.2);
            auto bloomLevel = [&](int k, double from) {
                return decibels(amplitudeAt(bloom, k * noteHz(note), at(from), at(from + 0.05)));
            };
            expectWithinAbsoluteError(bloomLevel(1, 0.02), bloomLevel(1, 1.0), 0.5f);
            expectLessThan(bloomLevel(16, 0.02), bloomLevel(16, 1.0) - 6.0f);
        }

        beginTest("An LFO on the pitch makes a vibrato, both ways");
        {
            const auto out = hold({ { "lfo1Rate", 1.0f }, { "lfo1Target", 1 }, { "lfo1Amount", 1.0f / 12.0f } }, 57, 2.0);
            const double hz = noteHz(57);
            const double high = cents(pitchOf(out, at(1.5) - 1024, 2048, hz / 1.3, hz * 1.3), hz);
            const double low = cents(pitchOf(out, at(1.0) - 1024, 2048, hz / 1.3, hz * 1.3), hz);
            expectWithinAbsoluteError(high, 100.0, 15.0);
            expectWithinAbsoluteError(low, -100.0, 15.0);
        }

        beginTest("Width spreads the partials across the sides, the fundamental stays in the middle");
        {
            const auto narrow = play({}, { { 0.0, 1.0, 45 } }, 0.7);
            float difference = 0.0f;
            for (std::size_t i = 0; i < narrow.left.size(); ++i)
                difference = std::max(difference, std::abs(narrow.left[i] - narrow.right[i]));
            expectLessThan(difference, 1.0e-6f);
            const auto wide = play({ { "width", 1.0f } }, { { 0.0, 1.0, 45 } }, 0.7);
            // From the fifth partial on, the even ones all on the right, the odd ones on the left.
            expectGreaterThan(decibels(harmonic(wide.right, 45, 6) / harmonic(wide.left, 45, 6)), 40.0f);
            expectGreaterThan(decibels(harmonic(wide.left, 45, 7) / harmonic(wide.right, 45, 7)), 40.0f);
            expectWithinAbsoluteError(decibels(harmonic(wide.left, 45, 1) / harmonic(wide.right, 45, 1)), 0.0f, 0.1f);
        }

        beginTest("Every spectrum and every route stays finite and in bounds, at the extremes too");
        {
            for (int spectrum = 0; spectrum < (int) Spectrum::count; ++spectrum)
                for (const float extreme : { 0.0f, 1.0f })
                {
                    const float swing = extreme * 2.0f - 1.0f;
                    const auto out = play({ { "spectrumA", (float) spectrum }, { "spectrumB", (float) (11 - spectrum) },
                                            { "morph", extreme }, { "tilt", 12.0f * swing }, { "oddEven", swing },
                                            { "stretch", swing }, { "beating", 50.0f * extreme },
                                            { "combDepth", extreme }, { "formantDepth", extreme },
                                            { "formantShift", 12.0f * swing }, { "filterOn", 1 },
                                            { "filterMode", 3.0f * extreme }, { "resonance", 1.0f },
                                            { "cutoff", 20.0f + 19980.0f * extreme }, { "spectralOn", 1 },
                                            { "bloom", 5000.0f * extreme }, { "spectralDecay", 10.0f + 19990.0f * extreme },
                                            { "lfo1Target", 1.0f + 10.0f * extreme }, { "lfo1Amount", 1.0f },
                                            { "lfo1Rate", 20.0f }, { "lfo2Target", 6 }, { "lfo2Amount", -1.0f },
                                            { "env2Target", 8 }, { "env2Amount", 1.0f }, { "width", 1.0f } },
                                          { { 0.0, 0.6, 12 }, { 0.0, 0.6, 60 }, { 0.1, 0.6, 108 }, { 0.2, 0.6, 127 } },
                                          0.8);
                    const juce::String label = "spectrum " + juce::String(spectrum) + " extreme " + juce::String(extreme);
                    expect(finite(out), label);
                    expectLessThan(peakOf(out), 8.0f, label);
                }
        }

        beginTest("A voice ends once released, and a stolen one fades rather than jumps");
        {
            Engine engine;
            engine.prepare(kRate);
            Settings s;
            std::vector<float> left(kBlock), right(kBlock);
            engine.noteOn(60, 1.0f, s);
            engine.noteOff(60, s);
            for (int i = 0; i < (int) (2.0 * kRate) / kBlock; ++i)
                engine.render(left.data(), right.data(), kBlock, s, {});
            expectEquals(engine.activeVoices(), 0);

            // Twice as many low sines as voices, held: all of them together move at most about 0.15 from one sample
            // to the next, a voice cut off at once would jump by up to 0.35.
            const auto out = play({ { "env1Release", 5000 }, { "spectrumA", kSine } },
                                  [] {
                                      std::vector<Note> notes;
                                      for (int i = 0; i < 2 * Engine::kVoices; ++i)
                                          notes.push_back({ 0.05 * i, 3.0, 24 + i });
                                      return notes;
                                  }(),
                                  2.0)
                                 .left;
            float steepest = 0.0f;
            for (std::size_t i = 1; i < out.size(); ++i)
                steepest = std::max(steepest, std::abs(out[i] - out[i - 1]));
            expectLessThan(steepest, 0.2f);
        }

        beginTest("Every factory sound plays, and names only parameters that exist");
        {
            AdditiveProcessor processor;
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

        beginTest("Sixteen voices of 64 partials each stay well inside real time");
        {
            std::vector<Note> chord;
            for (int i = 0; i < Engine::kVoices; ++i)
                chord.push_back({ 0.0, 4.0, 28 + 2 * i });
            for (const bool shaped : { false, true })
            {
                const auto out = play({ { "beating", 8 }, { "width", 0.8f }, { "fxReverbMix", 0.3f },
                                        { "spectralOn", shaped ? 1.0f : 0.0f }, { "filterOn", shaped ? 1.0f : 0.0f },
                                        { "formantDepth", shaped ? 0.5f : 0.0f }, { "lfo1Target", shaped ? 5.0f : 0.0f },
                                        { "lfo1Amount", 0.2f } },
                                      chord, 4.0);
                const double realtime = 4.0 / out.seconds;
                logMessage(juce::String("16 voices, 64 partials") + (shaped ? ", stretch moving, filter, formants and "
                                                                              "partial envelopes"
                                                                            : "")
                           + ", hall: " + juce::String(realtime, 1) + " times real time");
                expectGreaterThan(realtime, 3.0);
            }
        }

        beginTest("The processor keeps its settings and shows whole numbers with units");
        {
            AdditiveProcessor processor;
            set(processor, "morph", 0.42f);
            set(processor, "lfo2Target", 8.0f);
            set(processor, "spectrumB", kGlass);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            AdditiveProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            const auto s = loaded.readSettings();
            expectWithinAbsoluteError(s.morph, 0.42f, 1.0e-4f);
            expect(s.lfo[1].route.target == Target::cutoff);
            expect(s.spectrumB == Spectrum::glass);

            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("tilt", -3.0f), juce::String("-3 dB/Okt"));
            expectEquals(text("partials", 64.0f), juce::String("64"));
            expectEquals(text("beating", 7.0f), juce::String("7 ct"));
            expectEquals(text("spectralDecay", 3999.6f), juce::String("4000 ms"));
            expectEquals(text("oddEven", -0.5f), juce::String("-50 %"));
            expectEquals(text("vowel", 3.2f), juce::String("O"));
            expectEquals(text("formantShift", 5.0f), juce::String("5 HT"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));
        }

        beginTest("The editor fits its panels");
        {
            AdditiveProcessor processor;
            play(processor, { { "filterOn", 1 }, { "cutoff", 2000 } }, { { 0.0, 2.0, 45 } }, 0.3);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Additiv.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static AdditiveTests additiveTests;
