#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "TapeEditor.h"
#include "TapePresets.h"
#include "TapeProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerktape;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 2.0 * kPi;

using Signal = std::vector<float>;
using Knobs = std::vector<std::pair<const char*, float>>;

Signal sine(double hz, float amplitude, double seconds, double rate = kRate)
{
    Signal out((std::size_t) (seconds * rate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(kTwoPi * hz * (double) n / rate);
    return out;
}

/** The amplitude of `hz` in `signal` from `from` on (Goertzel). */
float amplitudeAt(const Signal& signal, double hz, std::size_t from, double rate = kRate)
{
    const double w = kTwoPi * hz / rate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < signal.size(); ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (signal.size() - from));
}

double decibels(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

double rms(const Signal& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

/** A play head that runs from bar one at 90 BPM, so the processor sees a transport start. */
struct PlayHead : juce::AudioPlayHead
{
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setBpm(90.0);
        return info;
    }
};

/** Runs `input` (on both sides, or one) through `processor` in host-sized blocks; the latency is taken off. */
Take run(TapeProcessor& processor, const Signal& input, int channels = 2, double rate = kRate, int block = kBlock)
{
    PlayHead head;
    processor.setPlayHead(&head);
    processor.setPlayConfigDetails(channels, channels, rate, block);
    processor.prepareToPlay(rate, block);
    Take take { input, input };
    juce::AudioBuffer<float> buffer(channels, block);
    juce::MidiBuffer midi;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < input.size(); at += (std::size_t) block)
    {
        const int count = (int) std::min<std::size_t>((std::size_t) block, input.size() - at);
        buffer.setSize(channels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, input.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(channels - 1), count, take.right.data() + at);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    const auto latency = (long) processor.getLatencySamples();
    take.left.erase(take.left.begin(), take.left.begin() + latency);
    take.right.erase(take.right.begin(), take.right.begin() + latency);
    return take;
}

void set(TapeProcessor& processor, const Knobs& knobs)
{
    for (const auto& [id, value] : knobs)
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

Take process(const Signal& input, const Knobs& knobs, int channels = 2, double rate = kRate)
{
    TapeProcessor processor;
    set(processor, knobs);
    return run(processor, input, channels, rate);
}

/** Everything off but what a test turns on: the reel, no drive, no wobble, no hiss, the tone wide open. */
Knobs clean(Knobs extra = {})
{
    Knobs knobs { { "type", 0 }, { "drive", 0.0f }, { "wow", 0.0f }, { "flutter", 0.0f }, { "noise", kNoiseOff },
                  { "tone", 20000 } };
    knobs.insert(knobs.end(), extra.begin(), extra.end());
    return knobs;
}

/** The frequency of a sine in windows of `window` seconds, from its rising zero crossings, as fractions off `hz`. */
std::vector<double> speedDeviations(const Signal& signal, double hz, std::size_t from, double window = 0.01)
{
    std::vector<double> crossings;
    for (std::size_t n = from + 1; n < signal.size(); ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
            crossings.push_back((double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]));
    std::vector<double> out;
    const auto periods = (std::size_t) std::max(1.0, window * hz);
    for (std::size_t i = 0; i + periods < crossings.size(); i += periods)
    {
        const double measured = (double) periods * kRate / (crossings[i + periods] - crossings[i]);
        out.push_back(measured / hz - 1.0);
    }
    return out;
}

double largest(const std::vector<double>& values)
{
    double out = 0.0;
    for (const double v : values)
        out = std::max(out, std::abs(v));
    return out;
}

// ---- A lo-fi beat at 90 BPM for the factory sounds: electric piano chords, a bass, kick, snare and swung hats. ----

constexpr double kBeat = 60.0 / 90.0;

double midiHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

Signal lofiBeat(int bars)
{
    Signal out((std::size_t) (bars * 4 * kBeat * kRate + 0.5 * kRate), 0.0f);
    auto add = [&out](double at, double seconds, const std::function<double(double)>& voice) {
        const auto from = (std::size_t) (at * kRate);
        for (std::size_t n = 0; n < (std::size_t) (seconds * kRate) && from + n < out.size(); ++n)
            out[from + n] += (float) voice((double) n / kRate);
    };
    // A tine piano: two-operator FM whose index falls, so the strike is bright and the tail round.
    auto piano = [](int note, double velocity) {
        const double f = midiHz(note);
        return [f, velocity](double t) {
            const double index = 1.8 * std::exp(-t / 0.25);
            const double env = std::exp(-t / 1.4) * std::min(1.0, t / 0.002);
            return velocity * env * std::sin(kTwoPi * f * t + index * std::sin(kTwoPi * f * t));
        };
    };
    const int chords[4][4] = { { 53, 57, 60, 64 }, { 52, 55, 59, 62 }, { 50, 53, 57, 60 }, { 48, 52, 55, 59 } };
    const int roots[4] = { 41, 40, 38, 36 };
    juce::Random random(17);
    for (int bar = 0; bar < bars; ++bar)
    {
        const double start = bar * 4 * kBeat;
        const auto& chord = chords[bar % 4];
        for (const double at : { 0.0, 1.5 * kBeat, 2.75 * kBeat })
            for (int i = 0; i < 4; ++i)
                add(start + at + 0.008 * i, 1.6, piano(chord[i], 0.12));
        // The bass on the one and the three, sine with a little second harmonic.
        for (const double at : { 0.0, 2.5 * kBeat })
        {
            const double f = midiHz(roots[bar % 4]);
            add(start + at, 0.9, [f](double t) {
                const double env = std::exp(-t / 0.5) * std::min(1.0, t / 0.005);
                return 0.35 * env * (std::sin(kTwoPi * f * t) + 0.2 * std::sin(2.0 * kTwoPi * f * t));
            });
        }
        for (const double at : { 0.0, 2.5 * kBeat })
            add(start + at, 0.4, [](double t) {
                const double phase = kTwoPi * (48.0 * t + 90.0 * 0.03 * (1.0 - std::exp(-t / 0.03)));
                return 0.6 * std::sin(phase) * std::exp(-t / 0.15);
            });
        for (const double at : { kBeat, 3.0 * kBeat })
            add(start + at, 0.3, [&random](double t) {
                const double body = std::sin(kTwoPi * 185.0 * t) * std::exp(-t / 0.05);
                const double rattle = (random.nextFloat() * 2.0 - 1.0) * std::exp(-t / 0.08);
                return 0.25 * body + 0.3 * rattle;
            });
        for (int eighth = 0; eighth < 8; ++eighth)
        {
            // Swung: every second eighth late.
            const double at = (eighth / 2) * kBeat + (eighth % 2 == 1 ? 0.62 * kBeat : 0.0);
            double previous = 0.0;
            add(start + at, 0.05, [&random, &previous](double t) {
                const double noise = random.nextFloat() * 2.0 - 1.0;
                const double high = noise - previous;
                previous = noise;
                return 0.08 * high * std::exp(-t / 0.012);
            });
        }
    }
    float peak = 0.0f;
    for (const float s : out)
        peak = std::max(peak, std::abs(s));
    // Peaks at -6 dBFS, about where a recorded track sits.
    for (auto& s : out)
        s *= 0.5f / peak;
    return out;
}

void writeWav(const Signal& left, const Signal& right, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    const auto writer = juce::WavAudioFormat().createWriterFor(
        stream, juce::AudioFormatWriterOptions().withSampleRate(kRate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr)
        return;
    const float* channels[] = { left.data(), right.data() };
    writer->writeFromFloatArrays(channels, 2, (int) left.size());
}
} // namespace

class TapeTests : public juce::UnitTest
{
public:
    TapeTests() : juce::UnitTest("Tonwerk Tape", "Tape") {}

    void runTest() override
    {
        beginTest("With everything off, a tone passes unchanged and as late as reported");
        {
            const auto take = process(sine(1000.0, 0.25f, 1.0), clean());
            const auto from = (std::size_t) (0.2 * kRate);
            expectWithinAbsoluteError(decibels(amplitudeAt(take.left, 1000.0, from) / 0.25), 0.0, 0.3);
            expectLessThan(decibels(amplitudeAt(take.left, 3000.0, from) / 0.25), -50.0);

            Signal click(4800, 0.0f);
            click[1000] = 0.1f;
            const auto impulse = process(click, clean());
            std::size_t at = 0;
            for (std::size_t n = 0; n < impulse.left.size(); ++n)
                if (std::abs(impulse.left[n]) > std::abs(impulse.left[at]))
                    at = n;
            expect(at >= 1000 && at <= 1001, "the click comes at " + juce::String((int) at));
            TapeProcessor processor;
            processor.prepareToPlay(kRate, kBlock);
            expectEquals(processor.getLatencySamples(), 384);
        }

        beginTest("Saturation: harmonics with drive, even ones from the cassette's bias, the level kept");
        {
            const auto input = sine(200.0, 0.5f, 1.0);
            const auto from = (std::size_t) (0.3 * kRate);
            auto harmonics = [&](int type, float drive) {
                const auto take = process(input, clean({ { "type", (float) type }, { "drive", drive } }));
                return std::array<double, 3> { decibels(amplitudeAt(take.left, 200.0, from) / 0.5),
                                                decibels(amplitudeAt(take.left, 400.0, from) / 0.5),
                                                decibels(amplitudeAt(take.left, 600.0, from) / 0.5) };
            };
            const auto none = harmonics(0, 0.0f), reel = harmonics(0, 1.0f), ferric = harmonics(2, 1.0f);
            logMessage("drive 0: H3 " + juce::String(none[2], 1) + " dB; reel at 100 %: H1 " + juce::String(reel[0], 1)
                       + ", H2 " + juce::String(reel[1], 1) + ", H3 " + juce::String(reel[2], 1)
                       + " dB; ferric: H2 " + juce::String(ferric[1], 1) + ", H3 " + juce::String(ferric[2], 1));
            expectLessThan(none[2], -45.0);
            expectGreaterThan(reel[2], -25.0);
            expectGreaterThan(ferric[1], -35.0);
            expectGreaterThan(ferric[1], reel[1] + 6.0);
            // A tone at -12 dBFS keeps its peak however hard the tape is driven (the fundamental grows a little as the
            // wave squares up).
            const auto quiet = sine(200.0, 0.25f, 1.0);
            const auto driven = process(quiet, clean({ { "drive", 1.0f } }));
            expectWithinAbsoluteError(decibels(amplitudeAt(driven.left, 200.0, from) / 0.25), 0.0, 1.5);
        }

        beginTest("The highs saturate first, as the record EQ lifts them");
        {
            auto squash = [](double hz) {
                const auto input = sine(hz, 0.5f, 0.6);
                const auto from = (std::size_t) (0.3 * kRate);
                const auto soft = process(input, clean({ { "drive", 0.0f } }));
                const auto hard = process(input, clean({ { "drive", 0.8f } }));
                return decibels(amplitudeAt(hard.left, hz, from) / amplitudeAt(soft.left, hz, from));
            };
            const double low = squash(200.0), high = squash(8000.0);
            logMessage("drive 80 % against 0 %: 200 Hz " + juce::String(low, 1) + " dB, 8 kHz " + juce::String(high, 1)
                       + " dB");
            expectLessThan(high, low - 1.0);
        }

        beginTest("Wow and flutter: the speed swings by what the knobs say, and holds still without them");
        {
            const auto input = sine(1000.0, 0.25f, 8.0);
            const auto from = (std::size_t) (0.5 * kRate);
            const auto still = largest(speedDeviations(process(input, clean()).left, 1000.0, from));
            const auto wow = largest(speedDeviations(process(input, clean({ { "wow", 1.0f } })).left, 1000.0, from));
            const auto flutter =
                largest(speedDeviations(process(input, clean({ { "flutter", 1.0f } })).left, 1000.0, from));
            const auto little = largest(speedDeviations(process(input, clean({ { "wow", 0.2f } })).left, 1000.0, from));
            logMessage("largest speed change: still " + juce::String(100.0 * still, 3) + " %, wow 100 % "
                       + juce::String(100.0 * wow, 2) + " %, wow 20 % " + juce::String(100.0 * little, 2)
                       + " %, flutter 100 % " + juce::String(100.0 * flutter, 2) + " %");
            expectLessThan(still, 0.0002);
            expectGreaterThan(wow, 0.008);
            expectLessThan(wow, 0.026);
            expectWithinAbsoluteError(little / wow, 0.2, 0.08);
            expectGreaterThan(flutter, 0.002);
            expectLessThan(flutter, 0.007);
        }

        beginTest("Tone: the playback head's band ends where the knob says");
        {
            const auto input = sine(8000.0, 0.1f, 0.5);
            const auto from = (std::size_t) (0.2 * kRate);
            const auto open = process(input, clean());
            const auto dark = process(input, clean({ { "tone", 8000 } }));
            const auto cut = decibels(amplitudeAt(dark.left, 8000.0, from) / amplitudeAt(open.left, 8000.0, from));
            expectWithinAbsoluteError(cut, -3.0, 1.0);
            // The dictaphone's band ends early whatever the knob.
            const auto dictaphone = process(input, clean({ { "type", 3 } }));
            expectLessThan(decibels(amplitudeAt(dictaphone.left, 8000.0, from) / 0.1), -6.0);
        }

        beginTest("Hiss at its level, and nothing at all when it is off");
        {
            Signal silence((std::size_t) (2.0 * kRate), 0.0f);
            const auto hiss = process(silence, clean({ { "noise", -40 } }));
            const auto level = decibels(rms(hiss.left, (std::size_t) (0.5 * kRate), hiss.left.size()));
            logMessage("hiss at -40 dB measures " + juce::String(level, 1) + " dB RMS");
            expectWithinAbsoluteError(level, -40.0, 1.5);
            // The two tracks hiss apart.
            double same = 0.0;
            for (std::size_t n = 0; n < hiss.left.size(); ++n)
                same += (double) hiss.left[n] * hiss.right[n];
            expectLessThan(std::abs(same) / (double) hiss.left.size(), 0.1 * std::pow(10.0, -40.0 / 10.0));

            const auto off = process(silence, { { "noise", kNoiseOff }, { "wow", 1.0f }, { "drive", 1.0f } });
            expectEquals(rms(off.left, 0, off.left.size()), 0.0);
        }

        beginTest("Dropouts: the level sinks now and then with the knob up, never without");
        {
            const auto input = sine(1000.0, 0.25f, 8.0);
            auto dips = [&](float amount) {
                const auto take = process(input, clean({ { "dropouts", amount } }));
                const auto window = (std::size_t) (0.01 * kRate);
                double deepest = 0.0;
                int count = 0;
                bool inDip = false;
                for (std::size_t at = (std::size_t) (0.2 * kRate); at + window < take.left.size(); at += window)
                {
                    const double level = decibels(rms(take.left, at, at + window) / (0.25 / std::sqrt(2.0)));
                    deepest = std::min(deepest, level);
                    if (level < -3.0 && ! inDip)
                        ++count;
                    inDip = level < -3.0;
                }
                return std::make_pair(deepest, count);
            };
            const auto none = dips(0.0f), many = dips(1.0f);
            logMessage("dropouts 100 %: " + juce::String(many.second) + " in 8 s, deepest " + juce::String(many.first, 1)
                       + " dB");
            expectGreaterThan(none.first, -0.5);
            expectGreaterThan(many.second, 4);
            expectLessThan(many.first, -6.0);
        }

        beginTest("Mix at 0 is the input, delayed as the tape is");
        {
            const auto input = sine(440.0, 0.4f, 1.0);
            const auto take = process(input, { { "mix", 0.0f }, { "wow", 1.0f }, { "noise", -30 }, { "drive", 1.0f } });
            double worst = 0.0;
            for (std::size_t n = 1000; n < take.left.size(); ++n)
                worst = std::max(worst, (double) std::abs(take.left[n] - input[n]));
            expectLessThan(worst, 1.0e-6);
        }

        beginTest("The same tape every time: a bounce repeats");
        {
            const auto input = sine(300.0, 0.3f, 3.0);
            const Knobs worn { { "wow", 0.8f }, { "flutter", 0.6f }, { "noise", -50 }, { "dropouts", 0.8f } };
            const auto a = process(input, worn), b = process(input, worn);
            expect(a.left == b.left && a.right == b.right);
        }

        beginTest("A transport start mid-sound glides to the new tape instead of jumping");
        {
            TapeProcessor processor;
            set(processor, clean({ { "wow", 1.0f }, { "flutter", 1.0f } }));
            PlayHead head;
            processor.setPlayHead(&head);
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            const auto input = sine(1000.0, 0.25f, 4.0);
            Signal out(input.size());
            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            for (std::size_t at = 0; at + kBlock <= input.size(); at += kBlock)
            {
                // Stopped for one block after 1.7 s, then playing again: the dice start over.
                head.playing = at < (std::size_t) (1.7 * kRate) || at >= (std::size_t) (1.7 * kRate) + kBlock;
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, input.data() + at, kBlock);
                processor.processBlock(buffer, midi);
                std::copy_n(buffer.getReadPointer(0), kBlock, out.data() + at);
            }
            processor.setPlayHead(nullptr);
            const auto restart = largest(speedDeviations(out, 1000.0, (std::size_t) (0.5 * kRate)));
            logMessage("largest speed change around a restart: " + juce::String(100.0 * restart, 2) + " %");
            expectLessThan(restart, 0.025);
        }

        beginTest("Mono, and other sample rates");
        {
            const auto input = sine(500.0, 0.3f, 1.0);
            const Knobs some { { "wow", 0.4f }, { "noise", kNoiseOff } };
            const auto stereo = process(input, some), mono = process(input, some, 1);
            double worst = 0.0;
            for (std::size_t n = 0; n < mono.left.size(); ++n)
                worst = std::max(worst, (double) std::abs(mono.left[n] - stereo.left[n]));
            expectLessThan(worst, 1.0e-6);

            for (const double rate : { 44100.0, 96000.0 })
            {
                TapeProcessor processor;
                set(processor, clean());
                const auto take = run(processor, sine(1000.0, 0.25f, 1.0, rate), 2, rate);
                expectEquals(processor.getLatencySamples(), (int) std::lround(kCentreSeconds * rate));
                expectWithinAbsoluteError(decibels(amplitudeAt(take.left, 1000.0, (std::size_t) (0.2 * rate), rate) / 0.25),
                                          0.0, 0.3);
            }
        }

        beginTest("Values in whole numbers with units, and the state keeps them");
        {
            TapeProcessor processor;
            auto text = [&processor](const char* id) {
                auto* p = processor.state.getParameter(id);
                return p->getText(p->getValue(), 32);
            };
            expectEquals(text("drive"), juce::String("40 %"));
            expectEquals(text("tone"), juce::String("12000 Hz"));
            expectEquals(text("noise"), juce::String("-66 dB"));
            expectEquals(text("type"), juce::String("Ferro"));
            set(processor, { { "noise", kNoiseOff } });
            expectEquals(text("noise"), juce::String("Aus"));

            set(processor, { { "wow", 0.7f }, { "type", 3 } });
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            TapeProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().wow, 0.7f, 1.0e-4f);
            expect(loaded.readSettings().type == TapeType::dictaphone);
        }

        beginTest("CPU");
        {
            const auto input = sine(220.0, 0.4f, 20.0);
            const auto take = process(input, { { "wow", 1.0f }, { "flutter", 1.0f }, { "noise", -40 }, { "dropouts", 1.0f },
                                               { "drive", 1.0f }, { "type", 3 } });
            const double factor = 20.0 / take.seconds;
            logMessage("stereo, everything on: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 20.0);
        }

        beginTest("The editor: the reels turn while audio runs");
        {
            TapeProcessor processor;
            set(processor, { { "wow", 1.0f } });
            std::unique_ptr<juce::AudioProcessorEditor> created(processor.createEditor());
            auto* editor = tonwerkui::innerEditor<TapeEditor>(created.get());
            expect(editor != nullptr);
            editor->transport.cassette.refresh();
            expect(! editor->transport.cassette.running());
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            const auto beat = lofiBeat(1);
            for (int i = 0; i < 300; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, beat.data() + (std::size_t) (i * kBlock) % (beat.size() - kBlock), kBlock);
                processor.processBlock(buffer, midi);
            }
            editor->transport.cassette.refresh();
            expect(editor->transport.cassette.running());
            expect(editor->getWidth() >= TapeEditor::kWidth);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("tape.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }

        beginTest("Factory sounds: groups, names and levels");
        {
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});
            const auto source = lofiBeat(wavDir.isNotEmpty() ? 4 : 2);
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto& presets = factoryPresets();
            std::set<juce::String> names;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                const auto& preset = presets[(std::size_t) i];
                TapeProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(preset.category, tonwerkui::kTapeCategories) >= 0,
                       "no group: " + sound);
                const auto take = run(processor, source);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 18)
                           + juce::String::fromUTF8(preset.category).paddedRight(' ', 12) + juce::String(change, 1)
                           + " dB against dry, peak " + juce::String(peak, 2));
                expectWithinAbsoluteError(change, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take.left, take.right,
                             juce::File(wavDir).getChildFile("Tonwerk Tape").getChildFile(
                                 juce::String(i + 1).paddedLeft('0', 2) + " " + sound.replaceCharacters("/", "-")
                                 + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav(source, source, juce::File(wavDir).getChildFile("Tonwerk Tape").getChildFile("00 Trocken.wav"));
        }
    }
};

static TapeTests tapeTests;
