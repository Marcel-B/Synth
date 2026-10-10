#include "WaveEngine.h"
#include "WavetableEditor.h"
#include "WavetablePresets.h"
#include "WavetableProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstring>
#include <numeric>

using namespace tonwerkwave;

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

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) (to - from));
}

float peak(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    float worst = 0.0f;
    for (std::size_t n = from; n < to; ++n)
        worst = std::max(worst, std::abs(signal[n]));
    return worst;
}

/** Frequency from the rising zero crossings between two times, for plain waves. */
double frequency(const std::vector<float>& signal, double fromSeconds, double toSeconds)
{
    const auto from = (std::size_t) (fromSeconds * kRate);
    const auto to = (std::size_t) (toSeconds * kRate);
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t n = from + 1; n < to; ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
        {
            const double at = (double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]);
            if (first < 0.0)
                first = at;
            else
                ++crossings;
            last = at;
        }
    return crossings > 0 ? crossings * kRate / (last - first) : 0.0;
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

struct Note
{
    double on, off;
    int note;
};

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

using Knobs = std::initializer_list<std::pair<const char*, float>>;

void set(WavetableProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/** Plays `notes` on a fresh processor (after `program`, when given, then `knobs`) for `length` seconds. */
Rendered play(Knobs knobs, std::vector<Note> notes, double length, int program = -1)
{
    WavetableProcessor processor;
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    if (program >= 0)
        processor.setCurrentProgram(program);
    // A plain path unless a test asks for more: no filter, no master cut.
    if (program < 0)
    {
        set(processor, "filterOn", 0.0f);
        set(processor, "master", 0.0f);
    }
    for (const auto& [id, value] : knobs)
        set(processor, id, value);
    processor.prepareToPlay(kRate, kBlock);

    const auto total = (std::size_t) (length * kRate);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < total; at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - at);
        juce::MidiBuffer midi;
        for (const auto& n : notes)
        {
            const auto on = (std::size_t) (n.on * kRate);
            const auto off = (std::size_t) (n.off * kRate);
            if (on >= at && on < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, (juce::uint8) 100), (int) (on - at));
            if (off >= at && off < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), (int) (off - at));
        }
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + at);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

Rendered hold(Knobs knobs, int note, double length, int program = -1)
{
    return play(knobs, { { 0.0, length + 1.0, note } }, length, program);
}

constexpr float kSaw = 2.0f / 3.0f;

/** One cycle of `table`'s `frame` at its richest level, measured for harmonic `n`. */
float harmonic(const Wavetable& table, int frame, int n)
{
    const int size = Wavetable::size(0);
    double re = 0.0, im = 0.0;
    for (int i = 0; i < size; ++i)
    {
        const double v = table.read(0, (float) frame, (double) i / size);
        re += v * std::cos(2.0 * juce::MathConstants<double>::pi * n * i / size);
        im += v * std::sin(2.0 * juce::MathConstants<double>::pi * n * i / size);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / size);
}
} // namespace

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest("Tonwerk Wavetable", "Wavetable") {}

    void runTest() override
    {
        beginTest("The basic table runs from a pure sine to a square");
        {
            const auto& basic = WavetableBank::instance().table(0);
            expectWithinAbsoluteError(harmonic(basic, 0, 1), 1.0f, 0.01f);
            expectLessThan(harmonic(basic, 0, 3), 0.01f);
            const float h1 = harmonic(basic, kFrames - 1, 1);
            expectWithinAbsoluteError(harmonic(basic, kFrames - 1, 3) / h1, 1.0f / 3.0f, 0.02f);
            expectLessThan(harmonic(basic, kFrames - 1, 2) / h1, 0.01f);
        }

        beginTest("Every frame peaks near 1 and each level keeps below Nyquist");
        {
            const auto& bank = WavetableBank::instance();
            expectEquals(bank.count(), (int) WavetableBank::names().size());
            for (int t = 0; t < bank.count(); ++t)
            {
                const auto& table = bank.table(t);
                // Each frame is scaled to peak at 1 in its richest form; poorer levels ring a little above.
                float loudest = 0.0f, quietest = 10.0f;
                bool finite = true;
                for (int f = 0; f < kFrames; ++f)
                    for (int level = 0; level < kLevels; ++level)
                    {
                        const float* cycle = table.cycle(level, f);
                        float levelPeak = 0.0f;
                        for (int i = 0; i <= Wavetable::size(level); ++i)
                        {
                            finite = finite && std::isfinite(cycle[i]);
                            levelPeak = std::max(levelPeak, std::abs(cycle[i]));
                        }
                        loudest = std::max(loudest, levelPeak);
                        if (level == 0)
                            quietest = std::min(quietest, levelPeak);
                    }
                expect(finite);
                expectLessThan(loudest, 1.5f, juce::String(table.name));
                expectGreaterThan(quietest, 0.9f, juce::String(table.name));
            }
            for (double cps = 0.0001; cps < 0.5; cps *= 1.3)
            {
                const int level = Wavetable::levelFor(cps);
                if (level < kLevels - 1)
                    expectLessOrEqual(Wavetable::harmonics(level) * cps, 0.5);
            }
        }

        beginTest("A high saw has no aliasing");
        {
            const int note = 96;
            const auto out = hold({ { "aPosition", kSaw } }, note, 1.0);
            const auto from = (std::size_t) (0.1 * kRate), to = out.left.size();
            const double f = noteHz(note);
            double harmonicPower = 0.0;
            for (int k = 1; k * f < kRate / 2.0; ++k)
                harmonicPower += 0.5 * std::pow(amplitudeAt(out.left, k * f, from, to), 2.0);
            const double total = std::pow(rms(out.left, from, to), 2.0);
            const float other = decibels((float) std::sqrt(std::max(1.0e-12, total - harmonicPower) / total));
            logMessage("Not on a harmonic: " + juce::String(other, 1) + " dB");
            expectLessThan(other, -40.0f);
        }

        beginTest("Unison spreads the voices across the stereo field at the level of one");
        {
            const auto one = hold({ { "aPosition", kSaw } }, 45, 1.0);
            const auto seven = hold({ { "aPosition", kSaw }, { "aUnison", 7 }, { "aDetune", 30 }, { "aWidth", 1 } }, 45,
                                    1.0);
            const auto from = (std::size_t) (0.2 * kRate), to = one.left.size();
            expect(one.left == one.right);
            double same = 0.0, l2 = 0.0, r2 = 0.0;
            for (std::size_t n = from; n < to; ++n)
            {
                same += seven.left[n] * seven.right[n];
                l2 += seven.left[n] * seven.left[n];
                r2 += seven.right[n] * seven.right[n];
            }
            const double correlation = same / std::sqrt(l2 * r2);
            logMessage("Correlation of left and right with 7 voices: " + juce::String(correlation, 2));
            expectLessThan(correlation, 0.9);
            const float level = decibels(0.5f * (rms(seven.left, from, to) + rms(seven.right, from, to))
                                         / rms(one.left, from, to));
            logMessage("7 voices against 1: " + juce::String(level, 1) + " dB");
            expectLessThan(std::abs(level), 3.0f);
        }

        beginTest("The lowpass takes the top off a saw");
        {
            const auto open = hold({ { "aPosition", kSaw } }, 45, 1.0);
            const auto closed = hold({ { "aPosition", kSaw }, { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 500 },
                                       { "resonance", 0 } },
                                     45, 1.0);
            const auto from = (std::size_t) (0.2 * kRate), to = open.left.size();
            const double f = noteHz(45);
            const float fundamental = decibels(amplitudeAt(closed.left, f, from, to) / amplitudeAt(open.left, f, from, to));
            const float twentieth = decibels(amplitudeAt(closed.left, 20 * f, from, to)
                                             / amplitudeAt(open.left, 20 * f, from, to));
            logMessage("24 dB lowpass at 500 Hz: 110 Hz " + juce::String(fundamental, 1) + " dB, 2.2 kHz "
                       + juce::String(twentieth, 1) + " dB");
            expectGreaterThan(fundamental, -3.0f);
            expectLessThan(twentieth, -30.0f);
        }

        beginTest("Attack and release take their time");
        {
            const auto out = play({ { "aPosition", 0 }, { "env1Attack", 100 }, { "env1Release", 200 } },
                                  { { 0.0, 0.5, 57 } }, 1.0);
            auto around = [&out](double seconds) {
                return peak(out.left, (std::size_t) ((seconds - 0.005) * kRate), (std::size_t) ((seconds + 0.005) * kRate));
            };
            const float full = around(0.4);
            expectWithinAbsoluteError(around(0.05) / full, 0.5f, 0.1f);
            expectLessThan(around(0.5 + 0.3) / full, 0.01f);
        }

        beginTest("Legato glides from one note to the next");
        {
            const auto out = play({ { "aPosition", 0 }, { "voiceMode", 2 }, { "glide", 200 } },
                                  { { 0.0, 0.6, 48 }, { 0.5, 1.5, 60 } }, 1.5);
            const double before = frequency(out.left, 0.3, 0.5);
            const double during = frequency(out.left, 0.52, 0.56);
            const double after = frequency(out.left, 1.2, 1.45);
            logMessage("Glide: " + juce::String(before, 1) + " Hz, " + juce::String(during, 1) + " Hz, "
                       + juce::String(after, 1) + " Hz");
            expectWithinAbsoluteError(before, noteHz(48), 1.0);
            expect(during > noteHz(48) + 5.0 && during < noteHz(60) - 5.0);
            expectWithinAbsoluteError(after, noteHz(60), 1.0);
        }

        beginTest("The matrix moves the pitch, both ways when bipolar");
        {
            const auto up = hold({ { "aPosition", 0 }, { "macro1", 1 }, { "mod1Source", 10 }, { "mod1Target", 3 },
                                   { "mod1Amount", 0.5f } },
                                 57, 0.5);
            expectWithinAbsoluteError(frequency(up.left, 0.1, 0.5), 440.0, 1.0);
            const auto down = hold({ { "aPosition", 0 }, { "mod1Source", 10 }, { "mod1Target", 3 }, { "mod1Amount", 0.5f },
                                     { "mod1Bipolar", 1 } },
                                   57, 0.5);
            expectWithinAbsoluteError(frequency(down.left, 0.1, 0.5), 110.0, 1.0);
        }

        beginTest("An LFO on the eighths wobbles the filter four times a second at 120 BPM");
        {
            // Noise through the filter: its loudness follows the cutoff and nothing else.
            const auto out = hold({ { "aOn", 0 }, { "noise", 1 }, { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 100 },
                                    { "resonance", 0.3f }, { "lfo1Division", 9 }, { "mod1Source", 1 },
                                    { "mod1Target", 13 }, { "mod1Amount", 0.6f } },
                                  36, 2.0);
            // Loudness in 10 ms steps: one cycle of the wobble is 25 steps.
            std::vector<float> loudness;
            for (std::size_t n = (std::size_t) (0.25 * kRate); n + 480 <= out.left.size(); n += 480)
                loudness.push_back(rms(out.left, n, n + 480));
            const double mean = std::accumulate(loudness.begin(), loudness.end(), 0.0) / (double) loudness.size();
            auto similarity = [&loudness, mean](std::size_t lag) {
                double a = 0.0, b = 0.0, c = 0.0;
                for (std::size_t i = 0; i + lag < loudness.size(); ++i)
                {
                    a += (loudness[i] - mean) * (loudness[i + lag] - mean);
                    b += (loudness[i] - mean) * (loudness[i] - mean);
                    c += (loudness[i + lag] - mean) * (loudness[i + lag] - mean);
                }
                return a / std::sqrt(b * c);
            };
            const auto [quietest, loudest] = std::minmax_element(loudness.begin(), loudness.end());
            logMessage("Wobble: " + juce::String(decibels(*loudest / *quietest), 1) + " dB, similarity after a cycle "
                       + juce::String(similarity(25), 3) + ", after half " + juce::String(similarity(12), 3));
            expectGreaterThan(decibels(*loudest / *quietest), 10.0f);
            expectGreaterThan(similarity(25), 0.95);
            expectLessThan(similarity(12), 0.0);
        }

        beginTest("Without retrigger a synced LFO follows the song position");
        {
            Settings s;
            s.a.position = kSaw;
            s.filter.cutoff = 100.0f;
            s.lfo[0].retrigger = false;
            s.lfo[0].division = 9;
            s.mods[0] = { ModSource::lfo1, ModTarget::cutoff, 0.6f, false };
            auto render = [&s](double ppq) {
                Engine engine;
                engine.prepare(kRate);
                engine.noteOn(36, 1.0f, s);
                Transport transport;
                transport.playing = true;
                transport.hasPosition = true;
                std::vector<float> left(24000), right(24000);
                for (std::size_t at = 0; at < left.size(); at += kBlock)
                {
                    transport.ppq = ppq + (double) at / kRate * 2.0;
                    const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
                    engine.render(left.data() + at, right.data() + at, count, s, transport);
                }
                return left;
            };
            const auto onBeat = render(0.0);
            const auto cycleLater = render(0.5);
            const auto halfLater = render(0.25);
            float sameDifference = 0.0f, halfDifference = 0.0f;
            for (std::size_t n = 0; n < onBeat.size(); ++n)
            {
                sameDifference = std::max(sameDifference, std::abs(onBeat[n] - cycleLater[n]));
                halfDifference = std::max(halfDifference, std::abs(onBeat[n] - halfLater[n]));
            }
            expectLessThan(sameDifference, 1.0e-4f);
            expectGreaterThan(halfDifference, 0.05f);
        }

        beginTest("Every warp stays finite and in bounds, high and low");
        {
            for (int warp = 0; warp <= 4; ++warp)
                for (int note : { 24, 96 })
                {
                    const auto out = hold({ { "aTable", 4 }, { "aPosition", 0.5f }, { "aWarpMode", (float) warp },
                                            { "aWarp", 1 }, { "aUnison", 3 } },
                                          note, 0.3);
                    bool finite = true;
                    for (float v : out.left)
                        finite = finite && std::isfinite(v);
                    expect(finite);
                    expectLessThan(peak(out.left, 0, out.left.size()), 3.0f);
                    expectGreaterThan(rms(out.left, 4800, out.left.size()), 0.05f);
                }
        }

        beginTest("Every filter type, with resonance and drive, stays finite");
        {
            for (int mode = 0; mode <= 5; ++mode)
            {
                const auto out = hold({ { "aPosition", kSaw }, { "filterOn", 1 }, { "filterMode", (float) mode },
                                        { "cutoff", 800 }, { "resonance", 1 }, { "filterDrive", 1 } },
                                      40, 0.5);
                bool finite = true;
                for (float v : out.left)
                    finite = finite && std::isfinite(v);
                expect(finite);
                expectLessThan(peak(out.left, 0, out.left.size()), 8.0f);
            }
        }

        beginTest("Every factory sound plays, and names only parameters that exist");
        {
            WavetableProcessor processor;
            const auto& presets = factoryPresets();
            expectEquals(processor.getNumPrograms(), (int) presets.size());
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                for (const auto& [id, value] : presets[(std::size_t) i].values)
                    expect(processor.state.getParameter(id) != nullptr, id);
                const auto out = play({}, { { 0.0, 1.0, 36 }, { 0.0, 1.0, 43 } }, 1.5, i);
                const float loudest = peak(out.left, 0, out.left.size());
                bool finite = true;
                for (float v : out.left)
                    finite = finite && std::isfinite(v);
                expect(finite);
                logMessage(juce::String::fromUTF8(presets[(std::size_t) i].name) + ": peak "
                           + juce::String(decibels(loudest), 1) + " dB");
                expectGreaterThan(loudest, 0.03f, presets[(std::size_t) i].name);
                expectLessThan(loudest, 2.0f, presets[(std::size_t) i].name);
            }
        }

        beginTest("Eight chords of supersaws cost little CPU");
        {
            int supersaw = 0;
            while (std::strcmp(factoryPresets()[(std::size_t) supersaw].name, "Supersaw") != 0)
                ++supersaw;
            std::vector<Note> chord;
            for (int note : { 48, 52, 55, 59, 60, 64, 67, 71 })
                chord.push_back({ 0.0, 4.0, note });
            const auto out = play({ { "distOn", 1 } }, chord, 4.0, supersaw);
            const double realtime = 4.0 / out.seconds;
            logMessage("8 voices with 7 + 5 unison and distortion: " + juce::String(realtime, 1)
                       + " times real time");
            expectGreaterThan(realtime, 3.0);
        }

        beginTest("The processor keeps its settings and shows whole numbers with units");
        {
            WavetableProcessor processor;
            set(processor, "aPosition", 0.42f);
            set(processor, "mod3Target", 13.0f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            WavetableProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            const auto s = loaded.readSettings();
            expectWithinAbsoluteError(s.a.position, 0.42f, 1.0e-4f);
            expect(s.mods[2].target == ModTarget::cutoff);

            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("cutoff", 1000.4f), juce::String("1000 Hz"));
            expectEquals(text("aPosition", 0.4999999f), juce::String("50 %"));
            expectEquals(text("mod1Amount", -0.5f), juce::String("-50 %"));
            expectEquals(text("aDetune", 20.0f), juce::String("20 ct"));
            expectEquals(text("env1Attack", 2.0f), juce::String("2 ms"));
            expectEquals(text("aOctave", -1.0f), juce::String("-1 Okt"));
            expectEquals(text("lfo1Rate", 2.5f), juce::String("2.5 Hz"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
        }
    }
};

static WavetableTests wavetableTests;
