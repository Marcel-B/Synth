#include "PhysicalEditor.h"
#include "PhysicalEngine.h"
#include "PhysicalPresets.h"
#include "PhysicalProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkphys;

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

void set(PhysicalProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/**
 * Plays `notes` on `processor` for `length` seconds. Without a program the path is plain: no hall, no master cut, no
 * stereo spread, full velocity at full level.
 */
Rendered play(PhysicalProcessor& processor, Knobs knobs, std::vector<Note> notes, double length, int program = -1)
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

Rendered play(Knobs knobs, std::vector<Note> notes, double length, int program = -1)
{
    PhysicalProcessor processor;
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

constexpr float kStrike = 1, kBow = 2, kBlow = 3;
constexpr float kClosed = 1, kOpen = 2, kBar = 3, kBell = 4;
} // namespace

class PhysicalTests : public juce::UnitTest
{
public:
    PhysicalTests() : juce::UnitTest("Tonwerk Physical", "Physical") {}

    void runTest() override
    {
        beginTest("Plucked and struck strings and tubes sound at the note, across the keyboard");
        {
            for (const auto& [resonator, exciter] : { std::pair { 0.0f, 0.0f }, std::pair { 0.0f, kStrike },
                                                      std::pair { kClosed, 0.0f }, std::pair { kOpen, kStrike } })
                for (const int note : { 40, 57, 69, 84 })
                {
                    const auto out = hold({ { "resonator", resonator }, { "exciter", exciter }, { "decay", 8000 },
                                            { "inharmonicity", 0.0f } },
                                          note, 0.6);
                    const double hz = noteHz(note);
                    const double error = cents(pitchOf(out, at(0.2), 4096, hz / 1.5, hz * 1.5), hz);
                    expectLessThan(std::abs(error), 3.0,
                                   "resonator " + juce::String(resonator) + " note " + juce::String(note) + ": "
                                       + juce::String(error, 1) + " ct");
                }
        }

        beginTest("A bowed string and a blown reed play by themselves while held, in tune, and stop when let go");
        {
            for (const auto& [resonator, exciter] : { std::pair { 0.0f, kBow }, std::pair { kClosed, kBlow } })
                for (const int note : { 45, 60, 76 })
                {
                    const auto out = play({ { "resonator", resonator }, { "exciter", exciter }, { "release", 200 } },
                                          { { 0.0, 1.5, note } }, 3.0)
                                         .left;
                    const double hz = noteHz(note);
                    const double error = cents(pitchOf(out, at(1.0), 4096, hz / 1.5, hz * 1.5), hz);
                    const juce::String label = "resonator " + juce::String(resonator) + " note " + juce::String(note);
                    expectLessThan(std::abs(error), 15.0, label + ": " + juce::String(error, 1) + " ct");
                    // Steady while held: the second half as loud as the first within 3 dB.
                    const float early = rms(out, at(0.5), at(1.0)), late = rms(out, at(1.0), at(1.5));
                    expectGreaterThan(decibels(early), -40.0f, label);
                    expectLessThan(std::abs(decibels(late / early)), 3.0f, label);
                    expectLessThan(decibels(rms(out, at(2.5), at(3.0))), -90.0f, label);
                }
        }

        beginTest("A closed tube has only odd overtones, an open one all of them");
        {
            const double hz = noteHz(48);
            for (const float resonator : { kClosed, kOpen })
            {
                const auto out = hold({ { "resonator", resonator }, { "exciter", kStrike }, { "hardness", 0.8f },
                                        { "decay", 3000 }, { "damping", 0.2f } },
                                      48, 1.0);
                const float second = amplitudeAt(out, 2.0 * hz, at(0.1), at(0.6));
                const float third = amplitudeAt(out, 3.0 * hz, at(0.1), at(0.6));
                if (resonator < kOpen)
                    expectLessThan(decibels(second / third), -30.0f);
                else
                    expectGreaterThan(decibels(second / third), -10.0f);
            }
        }

        beginTest("Plucked in the middle a string loses its even overtones");
        {
            const double hz = noteHz(48);
            auto evenOverOdd = [hz](float position) {
                const auto out = hold({ { "hardness", 1.0f }, { "noise", 0.0f }, { "position", position },
                                        { "damping", 0.1f }, { "decay", 4000 } },
                                      48, 1.0);
                return decibels(amplitudeAt(out, 2.0 * hz, at(0.1), at(0.6)) / amplitudeAt(out, hz, at(0.1), at(0.6)));
            };
            expectLessThan(evenOverOdd(0.5f), evenOverOdd(0.2f) - 20.0f);
        }

        beginTest("Decay sets how long a held string rings, and letting go damps it");
        {
            // 60 dB in two seconds, so 30 dB from one second to the next; the fundamental only, whatever the damping.
            const double hz = noteHz(57);
            const auto out = hold({ { "decay", 2000 }, { "damping", 0.0f }, { "hardness", 0.5f } }, 57, 2.0);
            const float drop = decibels(amplitudeAt(out, hz, at(0.4), at(0.6)) / amplitudeAt(out, hz, at(1.4), at(1.6)));
            expectWithinAbsoluteError(drop, 30.0f, 3.0f);

            const auto released = play({ { "decay", 8000 }, { "release", 100 } }, { { 0.0, 0.5, 57 } }, 1.0).left;
            expectLessThan(decibels(rms(released, at(0.9), at(1.0)) / rms(released, at(0.3), at(0.45))), -40.0f);
        }

        beginTest("Inharmonicity stretches a string's overtones upward");
        {
            const double hz = noteHz(45);
            // The k-th overtone, looked for from just below its harmonic place to just above (the next one up lies
            // further than that even on the stiffest string at this note).
            auto overtone = [hz](float stiffness, int k) {
                const auto out = hold({ { "hardness", 1.0f }, { "noise", 0.0f }, { "inharmonicity", stiffness },
                                        { "damping", 0.0f }, { "decay", 10000 }, { "position", 0.3f } },
                                      45, 1.5);
                return peakBetween(out, (k - 0.4) * hz, (k + 0.6) * hz, hz / 50.0, at(0.2), at(1.4)) / hz;
            };
            expectWithinAbsoluteError(overtone(0.0f, 8), 8.0, 0.03);
            expectGreaterThan(overtone(0.6f, 8), 8.1);
            expectGreaterThan(overtone(0.9f, 4), 4.2);
        }

        beginTest("The bar's overtones lie at 4 and 10 times the note, the bell hums an octave below");
        {
            const double hz = noteHz(60);
            const auto barOut = hold({ { "resonator", kBar }, { "exciter", kStrike }, { "hardness", 1.0f },
                                       { "damping", 0.0f }, { "position", 0.13f } },
                                     60, 1.0);
            const float fourth = amplitudeAt(barOut, 3.99 * hz, at(0.05), at(0.5));
            expectGreaterThan(decibels(fourth / amplitudeAt(barOut, 3.0 * hz, at(0.05), at(0.5))), 20.0f);
            expectGreaterThan(decibels(amplitudeAt(barOut, 9.98 * hz, at(0.05), at(0.5))
                                       / amplitudeAt(barOut, 8.0 * hz, at(0.05), at(0.5))),
                              20.0f);
            const auto bellOut = hold({ { "resonator", kBell }, { "exciter", kStrike }, { "hardness", 1.0f } }, 60, 1.0);
            expectGreaterThan(decibels(amplitudeAt(bellOut, 0.5 * hz, at(0.1), at(0.6))
                                       / amplitudeAt(bellOut, 0.75 * hz, at(0.1), at(0.6))),
                              20.0f);
        }

        beginTest("A harder strike is brighter");
        {
            auto brightness = [](float hardness) {
                const auto out = hold({ { "exciter", kStrike }, { "hardness", hardness }, { "damping", 0.3f } }, 48, 0.5);
                const double hz = noteHz(48);
                return decibels(amplitudeAt(out, 12.0 * hz, at(0.02), at(0.3)) / amplitudeAt(out, hz, at(0.02), at(0.3)));
            };
            expectGreaterThan(brightness(0.9f), brightness(0.1f) + 12.0f);
        }

        beginTest("An LFO on the pitch makes a vibrato, both ways");
        {
            // A semitone either way at 1 Hz on a long-ringing string: high at a quarter cycle, low at three quarters.
            const auto out = hold({ { "decay", 20000 }, { "damping", 0.0f }, { "lfo1Rate", 1.0f }, { "lfo1Target", 1 },
                                    { "lfo1Amount", 1.0f / 12.0f } },
                                  57, 2.0);
            const double hz = noteHz(57);
            // The LFO starts at its lowest and is at its highest half a cycle in.
            const double high = cents(pitchOf(out, at(1.5) - 1024, 2048, hz / 1.3, hz * 1.3), hz);
            const double low = cents(pitchOf(out, at(1.0) - 1024, 2048, hz / 1.3, hz * 1.3), hz);
            expectWithinAbsoluteError(high, 100.0, 15.0);
            expectWithinAbsoluteError(low, -100.0, 15.0);
        }

        beginTest("Every exciter on every resonator stays finite and in bounds, at the extremes too");
        {
            for (int resonator = 0; resonator < (int) Resonator::count; ++resonator)
                for (int exciter = 0; exciter < 4; ++exciter)
                    for (const float extreme : { 0.0f, 1.0f })
                    {
                        const auto out = play({ { "resonator", (float) resonator }, { "exciter", (float) exciter },
                                                { "hardness", extreme }, { "pressure", extreme }, { "noise", 1.0f },
                                                { "damping", 1.0f - extreme }, { "inharmonicity", extreme },
                                                { "position", 0.02f + 0.48f * extreme }, { "decay", 20000 },
                                                { "body", 1.0f + 3.0f * extreme }, { "bodyMix", 1.0f },
                                                { "lfo1Target", 4 }, { "lfo1Amount", 1.0f }, { "lfo2Target", 1 },
                                                { "lfo2Amount", 0.5f }, { "lfo2Rate", 7.0f } },
                                              { { 0.0, 0.6, 24 }, { 0.0, 0.6, 60 }, { 0.1, 0.6, 96 }, { 0.2, 0.6, 108 } },
                                              0.8);
                        const juce::String label = "resonator " + juce::String(resonator) + " exciter "
                                                   + juce::String(exciter) + " extreme " + juce::String(extreme);
                        expect(finite(out), label);
                        expectLessThan(peakOf(out), 8.0f, label);
                    }
        }

        beginTest("A voice ends once it has rung out, and a stolen one fades rather than jumps");
        {
            Engine engine;
            engine.prepare(kRate);
            Settings s;
            s.decay = 0.5f;
            std::vector<float> left(kBlock), right(kBlock);
            engine.noteOn(60, 1.0f, s);
            engine.noteOff(60, s);
            for (int i = 0; i < (int) (2.0 * kRate) / kBlock; ++i)
                engine.render(left.data(), right.data(), kBlock, s, {});
            expectEquals(engine.activeVoices(), 0);

            // Twice as many notes as voices on long-ringing strings: no step from one sample to the next stands out.
            const auto out = play({ { "decay", 20000 }, { "release", 20000 } },
                                  [] {
                                      std::vector<Note> notes;
                                      for (int i = 0; i < 2 * Engine::kVoices; ++i)
                                          notes.push_back({ 0.05 * i, 3.0, 40 + i * 2 });
                                      return notes;
                                  }(),
                                  2.0)
                                 .left;
            float steepest = 0.0f;
            for (std::size_t i = 1; i < out.size(); ++i)
                steepest = std::max(steepest, std::abs(out[i] - out[i - 1]));
            expectLessThan(steepest, 0.5f);
        }

        beginTest("The body adds its resonances");
        {
            const double hz = noteHz(43);
            const auto dry = hold({ { "decay", 3000 } }, 43, 1.0);
            const auto guitar = hold({ { "decay", 3000 }, { "body", 1 }, { "bodyMix", 1.0f } }, 43, 1.0);
            // The guitar's air mode near 98 Hz lifts the low G's fundamental.
            expectGreaterThan(decibels(amplitudeAt(guitar, hz, at(0.1), at(0.6)) / amplitudeAt(dry, hz, at(0.1), at(0.6))),
                              3.0f);
        }

        beginTest("Every factory sound plays, and names only parameters that exist");
        {
            PhysicalProcessor processor;
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

        beginTest("Twelve voices of bowed strings or ringing bowls stay well inside real time");
        {
            std::vector<Note> chord;
            for (int i = 0; i < Engine::kVoices; ++i)
                chord.push_back({ 0.0, 4.0, 40 + 3 * i });
            for (const auto& [resonator, exciter] : { std::pair { 0.0f, kBow }, std::pair { 6.0f, kBlow } })
            {
                const auto out = play({ { "resonator", resonator }, { "exciter", exciter }, { "decay", 20000 },
                                        { "filterOn", 1 }, { "body", 1 }, { "fxReverbMix", 0.3f } },
                                      chord, 4.0);
                const double realtime = 4.0 / out.seconds;
                logMessage("12 voices, resonator " + juce::String(resonator) + ", filter, body and hall: "
                           + juce::String(realtime, 1) + " times real time");
                expectGreaterThan(realtime, 3.0);
            }
        }

        beginTest("The processor keeps its settings and shows whole numbers with units");
        {
            PhysicalProcessor processor;
            set(processor, "position", 0.42f);
            set(processor, "lfo2Target", 7.0f);
            set(processor, "resonator", kBell);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            PhysicalProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            const auto s = loaded.readSettings();
            expectWithinAbsoluteError(s.position, 0.42f, 1.0e-4f);
            expect(s.lfo[1].route.target == Target::cutoff);
            expect(s.resonator == Resonator::bell);

            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("decay", 3999.6f), juce::String("4000 ms"));
            expectEquals(text("position", 0.2f), juce::String("20 %"));
            expectEquals(text("release", 300.0f), juce::String("300 ms"));
            expectEquals(text("inharmonicity", 0.05f), juce::String("5 %"));
            expectEquals(text("fine", -7.0f), juce::String("-7 ct"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));
        }

        beginTest("The editor fits its panels");
        {
            PhysicalProcessor processor;
            play(processor, { { "position", 0.3f } }, { { 0.0, 2.0, 57 } }, 0.3);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Physical.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static PhysicalTests physicalTests;
