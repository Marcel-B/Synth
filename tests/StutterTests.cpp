#include "PresetCategories.h"
#include "StutterEditor.h"
#include "StutterEngine.h"
#include "StutterPresets.h"
#include "StutterProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkstutter;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kBpm = 120.0;
constexpr double kBeatsPerSample = kBpm / 60.0 / kRate;
/** A beat at 120 BPM, in samples. */
constexpr std::size_t kBeat = 24000;

using Signal = std::vector<float>;

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

/** White noise at about -10 dBFS: every sample different, so where a repeat reads from shows. */
Signal noise(double seconds)
{
    Signal out((std::size_t) (seconds * kRate));
    std::uint32_t state = 12345u;
    for (auto& v : out)
    {
        state = state * 1664525u + 1013904223u;
        v = 0.55f * ((float) (state >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

Signal sine(double hz, double seconds, float level = 0.5f)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

float rms(const Signal& signal, std::size_t from, std::size_t to)
{
    to = std::min(to, signal.size());
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

/** How bright a stretch is: the RMS of its slope against its own RMS. */
float brightness(const Signal& signal, std::size_t from, std::size_t to)
{
    double slope = 0.0, level = 0.0;
    for (std::size_t n = from + 1; n < to; ++n)
    {
        const double d = signal[n] - signal[n - 1];
        slope += d * d;
        level += (double) signal[n] * signal[n];
    }
    return (float) std::sqrt(slope / std::max(1.0e-12, level));
}

/** The pitch from rising zero crossings, in Hz. */
double frequencyOf(const Signal& signal, std::size_t from, std::size_t to)
{
    std::vector<double> crossings;
    for (auto n = from + 1; n < to; ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
            crossings.push_back((double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]));
    if (crossings.size() < 2)
        return 0.0;
    return (double) (crossings.size() - 1) * kRate / (crossings.back() - crossings.front());
}

/** The largest difference between `a` and `b` over [from, to). */
float largestDifference(const Signal& a, const Signal& b, std::size_t from, std::size_t to)
{
    float worst = 0.0f;
    for (std::size_t n = from; n < std::min({ to, a.size(), b.size() }); ++n)
        worst = std::max(worst, std::abs(a[n] - b[n]));
    return worst;
}

/** `input` delayed by `lag` samples: what a repeat of a slice `lag` long plays. */
Signal delayed(const Signal& input, std::size_t lag)
{
    Signal out(input.size(), 0.0f);
    for (std::size_t n = lag; n < input.size(); ++n)
        out[n] = input[n - lag];
    return out;
}

/**
 * Both channels of `input` through the engine alone at 120 BPM from `startBeat`; `change` may alter the settings
 * before each block (given the block's first beat).
 */
Take play(Settings s, const Signal& input, double startBeat = 0.0,
          const std::function<void(Settings&, double)>& change = {})
{
    Engine engine;
    engine.prepare(kRate);
    Take take { input, input };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < input.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, input.size() - at);
        const double beat = startBeat + (double) at * kBeatsPerSample;
        if (change)
            change(s, beat);
        float* channels[] = { take.left.data() + at, take.right.data() + at };
        engine.process(channels, 2, count, s, { beat, kBeatsPerSample });
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

/** A bar's repeat on beat three: a sixteenth (6000 samples) live, then three repeats of it. */
Settings onThree()
{
    Settings s;
    s.chance = 1.0f;
    return s;
}

/** A drum loop at 120 BPM peaking near -6 dBFS: kicks on the beats, a noise snare on two and four, hats in eighths. */
Signal drums(double seconds)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    std::uint32_t state = 99u;
    auto random = [&state] {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) / 8388608.0f - 1.0f;
    };
    for (std::size_t eighth = 0; eighth * kBeat / 2 < out.size(); ++eighth)
    {
        const std::size_t start = eighth * kBeat / 2;
        for (std::size_t n = 0; n < kBeat / 2 && start + n < out.size(); ++n)
        {
            const double t = (double) n / kRate;
            float v = 0.15f * random() * (float) std::exp(-t / 0.02);
            if (eighth % 2 == 0)
                v += 0.45f * (float) (std::sin(kTwoPi * (50.0 * t + 60.0 * (1.0 - std::exp(-t / 0.03)) * 0.03))
                                      * std::exp(-t / 0.15));
            if (eighth % 4 == 2)
                v += 0.3f * random() * (float) std::exp(-t / 0.08);
            out[start + n] += v;
        }
    }
    return out;
}

/** Through the processor with the host playing at 120 BPM from bar 1. */
Take render(StutterProcessor& processor, const Signal& source)
{
    struct PlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(true);
            info.setBpm(kBpm);
            info.setPpqPosition(ppq);
            return info;
        }
    } head;
    processor.setPlayHead(&head);
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { source, source };
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < source.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, source.size() - at);
        buffer.setSize(2, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, source.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, take.right.data() + at);
        head.ppq += count * kBeatsPerSample;
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return take;
}

void set(StutterProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void writeWav(const Take& take, const juce::File& file)
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

class StutterTests : public juce::UnitTest
{
public:
    StutterTests() : juce::UnitTest("Tonwerk Stutter", "Stutter") {}

    void runTest() override
    {
        // Beat three of bar one at 120 BPM, and a sixteenth's slice there.
        constexpr std::size_t start = 2 * kBeat, slice = kBeat / 4, fade = 72;

        beginTest("At no chance the signal passes untouched");
        {
            Settings s;
            s.chance = 0.0f;
            const auto input = noise(4.0);
            const auto take = play(s, input);
            expectEquals(largestDifference(take.left, input, 0, input.size()), 0.0f);
        }

        beginTest("On beat three a sixteenth plays live, then repeats until the gate's four sixteenths are over");
        {
            const auto input = noise(2.5);
            const auto take = play(onThree(), input);
            // Untouched before, through the live slice, and after the gate.
            expectEquals(largestDifference(take.left, input, 0, start + slice - fade), 0.0f);
            expectLessThan(largestDifference(take.left, input, start + 4 * slice + fade, 4 * kBeat), 1.0e-6f);
            // Each repeat plays the slice again: a sixteenth, two and three back.
            for (std::size_t k = 1; k < 4; ++k)
                expectLessThan(largestDifference(take.left, delayed(input, k * slice), start + k * slice + fade,
                                                 start + (k + 1) * slice - fade),
                               1.0e-6f, "repeat " + juce::String((int) k));
            expectEquals(largestDifference(take.left, take.right, 0, input.size()), 0.0f);
        }

        beginTest("Mischen lays the repeats over the signal; Gate lets only them through");
        {
            const auto input = noise(2.5);
            auto s = onThree();
            s.mode = Mode::mix;
            const auto mixed = play(s, input);
            Signal sum(input.size());
            const auto back = delayed(input, slice);
            for (std::size_t n = 0; n < sum.size(); ++n)
                sum[n] = input[n] + back[n];
            expectLessThan(largestDifference(mixed.left, sum, start + slice + fade, start + 2 * slice - fade), 1.0e-6f);
            expectEquals(largestDifference(mixed.left, input, 0, start + slice), 0.0f);

            s.mode = Mode::gate;
            const auto gated = play(s, input);
            expectEquals(rms(gated.left, 0, start - fade), 0.0f);
            expectLessThan(largestDifference(gated.left, input, start + fade, start + slice - fade), 1.0e-6f);
            expectEquals(rms(gated.left, start + 4 * slice + fade, 4 * kBeat), 0.0f);
        }

        beginTest("Rückwärts plays the slice backwards");
        {
            const auto input = noise(2.5);
            auto s = onThree();
            s.reverse = 1.0f;
            const auto take = play(s, input);
            float worst = 0.0f;
            for (std::size_t p = fade; p < slice - fade; ++p)
                worst = std::max(worst, std::abs(take.left[start + slice + p] - input[start + slice - 1 - p]));
            expectLessThan(worst, 1.0e-6f);
        }

        beginTest("Tonhöhe moves the repeats step by step; Ausklang fades them; the filter closes over the gate");
        {
            // An eighth's slice over a bar's gate: seven repeats, the last 7/8 of the way.
            auto s = onThree();
            s.grid = 1;
            s.gate = 16;
            s.pitch = -12.0f;
            constexpr std::size_t eighth = kBeat / 2;
            const auto lowered = play(s, sine(200.0, 4.0), 0.0);
            const double last = frequencyOf(lowered.left, start + 7 * eighth + 500, start + 8 * eighth - 500);
            logMessage("pitch -12: last repeat " + juce::String(last, 1) + " Hz");
            expectWithinAbsoluteError(last, 200.0 * std::exp2(-7.0 / 8.0), 1.5);
            expectWithinAbsoluteError(frequencyOf(lowered.left, start + 500, start + eighth - 500), 200.0, 0.5);

            s.pitch = 0.0f;
            s.decay = 1.0f;
            const auto faded = play(s, noise(4.0));
            const float first = rms(faded.left, start + eighth + fade, start + 2 * eighth - fade);
            const float end = rms(faded.left, start + 7 * eighth + fade, start + 8 * eighth - fade);
            logMessage("decay 100 %: last repeat " + juce::String(juce::Decibels::gainToDecibels(end / first), 1)
                       + " dB under the first");
            expectWithinAbsoluteError(end / first, (1.0f / 8.0f) / (7.0f / 8.0f), 0.03f);

            s.decay = 0.0f;
            s.filter = -1.0f;
            const auto closed = play(s, noise(4.0));
            const float bright = brightness(closed.left, start + eighth + fade, start + 2 * eighth - fade);
            const float dark = brightness(closed.left, start + 7 * eighth + fade, start + 8 * eighth - fade);
            logMessage("filter -100 %: brightness " + juce::String(bright, 3) + " -> " + juce::String(dark, 3));
            expectLessThan(dark, bright * 0.2f);
        }

        beginTest("The dice: about the chance, the same each run, another pattern with another Variante");
        {
            auto s = onThree();
            s.interval = 0; // every beat
            s.offset = 0;
            s.gate = 2;
            s.chance = 0.5f;
            s.mode = Mode::gate;
            const Signal ones((std::size_t) (64.5 * kBeat), 0.5f);
            auto pattern = [&ones](const Settings& settings) {
                const auto take = play(settings, ones);
                std::vector<bool> fired;
                for (std::size_t beat = 0; beat < 64; ++beat)
                    fired.push_back(std::abs(take.left[beat * kBeat + 1000]) > 0.1f);
                return fired;
            };
            const auto first = pattern(s), again = pattern(s);
            const auto count = std::count(first.begin(), first.end(), true);
            logMessage("chance 50 %: " + juce::String((int) count) + " of 64 beats");
            expect(count >= 20 && count <= 44);
            expect(first == again);
            s.seed = 2;
            const auto other = pattern(s);
            int differ = 0;
            for (std::size_t i = 0; i < first.size(); ++i)
                differ += first[i] != other[i] ? 1 : 0;
            expectGreaterThan(differ, 10);
        }

        beginTest("Variation changes the slice now and then");
        {
            auto s = onThree();
            s.variation = 1.0f;
            const auto input = noise(16 * 2.0 + 0.5);
            const auto take = play(s, input);
            std::set<std::size_t> lags;
            for (std::size_t bar = 0; bar < 16; ++bar)
            {
                const auto at = bar * 4 * kBeat + start;
                // The shortest lag that fits is the slice: a 1/32's second repeat fits a 1/16's lag too.
                for (const std::size_t lag : { kBeat / 8, kBeat / 6, kBeat / 4, kBeat / 3 })
                {
                    // The first repeat, a little in.
                    float worst = 0.0f;
                    for (std::size_t n = at + lag + fade; n < at + lag + 200; ++n)
                        worst = std::max(worst, std::abs(take.left[n] - input[n - lag]));
                    if (worst < 1.0e-6f)
                    {
                        lags.insert(lag);
                        break;
                    }
                }
            }
            logMessage("slice lengths heard: " + juce::String((int) lags.size()));
            expectGreaterThan((int) lags.size(), 1);
            expect(lags.count(kBeat / 4) == 0);
        }

        beginTest("Starting mid-bar waits for the next bar; a jump in the song cancels a repeat");
        {
            const auto input = noise(5.0);
            // From beat 3.0: bar one's start (beat 2) is past, bar two's (beat 6) comes.
            const auto take = play(onThree(), input, 3.0);
            expectEquals(largestDifference(take.left, input, 0, 3 * kBeat + slice - fade), 0.0f);
            expectLessThan(largestDifference(take.left, delayed(input, slice), 3 * kBeat + slice + fade,
                                             3 * kBeat + 2 * slice - fade),
                           1.0e-6f);
        }

        beginTest("Halten repeats from the next sixteenth for as long as it is on");
        {
            auto s = onThree();
            s.chance = 0.0f;
            const auto input = noise(3.0);
            // On from beat 1.1 (the block after), off from beat 2.6.
            const auto take = play(s, input, 0.0, [](Settings& settings, double beat) {
                settings.hold = beat >= 1.1 && beat < 2.6;
            });
            const std::size_t from = kBeat + kBeat / 4; // the sixteenth line at beat 1.25
            expectEquals(largestDifference(take.left, input, 0, from + slice - fade), 0.0f);
            for (std::size_t k = 1; k < 5; ++k)
                expectLessThan(largestDifference(take.left, delayed(input, k * slice), from + k * slice + fade,
                                                 from + (k + 1) * slice - fade),
                               1.0e-6f, "repeat " + juce::String((int) k));
            const auto off = (std::size_t) (2.6 * kBeat) + kBlock + fade;
            expectLessThan(largestDifference(take.left, input, off, input.size()), 1.0e-6f);
        }

        beginTest("No clicks: every edge fades");
        {
            auto s = onThree();
            s.interval = 1;
            s.offset = 3;
            s.gate = 6;
            s.grid = 4;
            s.variation = 1.0f;
            s.reverse = 0.5f;
            s.pitch = 7.0f;
            const auto input = sine(220.0, 16.0, 0.5f);
            for (const auto mode : { Mode::insert, Mode::mix, Mode::gate })
            {
                s.mode = mode;
                const auto take = play(s, input);
                float jump = 0.0f;
                for (std::size_t n = 1; n < take.left.size(); ++n)
                    jump = std::max(jump, std::abs(take.left[n] - take.left[n - 1]));
                logMessage("mode " + juce::String((int) mode) + ": largest step " + juce::String(jump, 4));
                // The sine itself steps up to 0.0144; a hard cut would step up to 1.
                expectLessThan(jump, 0.05f);
            }
        }

        beginTest("Every preset on a drum loop: finite, below full scale, about as loud as the loop");
        {
            const auto loop = drums(16.0);
            const float dry = rms(loop, 0, loop.size());
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_STUTTER_WAV_DIR", {});
            const auto& presets = factoryPresets();
            std::set<juce::String> names;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                StutterProcessor p;
                p.setCurrentProgram(i);
                const auto sound = p.getProgramName(i);
                expect(names.insert(sound).second, sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kStutterCategories) >= 0,
                       sound);
                const auto take = render(p, loop);
                float peak = 0.0f;
                bool finite = true;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                {
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                const float level = juce::Decibels::gainToDecibels(rms(take.left, 0, take.left.size()) / dry);
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 24) + juce::String(level, 1)
                           + " dB, peak " + juce::String(peak, 2));
                expect(finite, sound);
                expectLessThan(peak, 1.0f, sound);
                expectGreaterThan(level, -6.0f, sound);
                expectLessThan(level, 2.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i).paddedLeft('0', 2) + " " + sound + ".wav"));
            }
        }

        beginTest("Runs far inside real time");
        {
            StutterProcessor p;
            set(p, "interval", 0);
            set(p, "chance", 1.0f);
            set(p, "offset", 1);
            set(p, "gate", 3);
            set(p, "grid", 5);
            set(p, "pitch", 5);
            set(p, "filter", -0.5f);
            const auto take = render(p, drums(20.0));
            const double factor = 20.0 / take.seconds;
            logMessage("a repeat every beat with pitch and filter: " + juce::String(factor, 0)
                       + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            StutterProcessor p;
            set(p, "gate", 12);
            set(p, "pitch", -5);
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("offset"), juce::String("8/16"));
            expectEquals(text("gate"), juce::String("12/16"));
            expectEquals(text("chance"), juce::String("50 %"));
            expectEquals(text("interval"), juce::String("1 Takt"));
            expectEquals(text("grid"), juce::String("1/16"));
            expectEquals(text("pitch"), juce::String("-5 HT"));
            expectEquals(text("filter"), juce::String("0 %"));
            expectEquals(text("output"), juce::String("0 dB"));
            expectEquals(text("mode"), juce::String("Ersetzen"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            StutterProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.readSettings().gate, 12);
            expectEquals((int) loaded.readSettings().pitch, -5);
        }

        beginTest("The editor shows the pattern");
        {
            StutterProcessor p;
            p.setCurrentProgram(11);
            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            expect(dynamic_cast<StutterEditor*>(editor.get()) != nullptr);
            render(p, drums(3.6));
            if (auto* stutter = dynamic_cast<StutterEditor*>(editor.get()))
                stutter->pattern.view.refresh();
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("stutter.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static StutterTests stutterTests;
