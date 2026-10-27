#include "Loudness.h"
#include "PresetCategories.h"
#include "RumbleEditor.h"
#include "RumbleEngine.h"
#include "RumblePresets.h"
#include "RumbleProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkrumble;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kBpm = 120.0;
/** A beat at 120 BPM, in samples. */
constexpr std::size_t kBeat = 24000;

using Signal = std::vector<float>;

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

/**
 * A techno kick on every beat at 120 BPM, peaking at -6 dBFS: a sine falling from 160 to 48 Hz with a short click,
 * 300 ms long. `kicks` of them, then `tail` seconds of silence.
 */
Signal kickLoop(int kicks = 8, double tail = 0.0)
{
    Signal out(kBeat * (std::size_t) kicks + (std::size_t) (tail * kRate), 0.0f);
    juce::Random random(7);
    for (int k = 0; k < kicks; ++k)
    {
        double phase = 0.0;
        for (std::size_t n = 0; n < (std::size_t) (0.3 * kRate); ++n)
        {
            const double t = (double) n / kRate;
            const double hz = 48.0 + 112.0 * std::exp(-t / 0.03);
            phase += kTwoPi * hz / kRate;
            const double body = std::sin(phase) * std::exp(-t / 0.12) * std::min(1.0, t / 0.0005);
            const double click = t < 0.002 ? 0.3 * (random.nextFloat() * 2.0 - 1.0) * (1.0 - t / 0.002) : 0.0;
            out[(std::size_t) k * kBeat + n] += (float) (0.5 * (body + click));
        }
    }
    return out;
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-9f)); }

float rms(const Signal& signal, std::size_t from, std::size_t to)
{
    to = std::min(to, signal.size());
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

float peakOf(const Take& take)
{
    float peak = 0.0f;
    for (std::size_t n = 0; n < take.left.size(); ++n)
        peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
    return peak;
}

/** `input` (the same on both sides unless `right` is given) through the engine alone, the song from beat 0. */
Take play(const Settings& s, const Signal& input, const Signal* right = nullptr)
{
    Engine engine;
    engine.prepare((float) kRate);
    Take take { input, right != nullptr ? *right : input };
    const double perSample = kBpm / 60.0 / kRate;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < input.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, input.size() - at);
        float* channels[] = { take.left.data() + at, take.right.data() + at };
        engine.process(channels, 2, count, s, { (double) at * perSample, perSample, kBpm });
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

/** The rumble alone: the kick switched off. */
Signal rumbleOf(Settings s, const Signal& input)
{
    s.kickDb = kKickOffDb;
    return play(s, input).left;
}

/** The signal through a 12 dB highpass at `hz`, to see what lies above it. */
Signal above(const Signal& signal, float hz)
{
    tonwerkwobble::SvfStage stage;
    const auto g = (float) std::tan(kPi * hz / kRate);
    Signal out(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n)
        out[n] = stage.process(signal[n], g, 1.41421356f).high;
    return out;
}

/** Through the processor with the host playing at 120 BPM from `startBeat`. */
Take render(RumbleProcessor& processor, const Signal& source, double startBeat = 0.0)
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
    head.ppq = startBeat;
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
        head.ppq += count * kBpm / 60.0 / kRate;
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return take;
}

void set(RumbleProcessor& processor, const char* id, float value)
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

/**
 * How far the ducking holds the rumble down: its RMS in the 40 ms after each kick (from the fourth on, once the hall
 * is full) against the last 100 ms before the next, in dB.
 */
float duckingDepth(const Signal& rumble)
{
    double right = 0.0, before = 0.0;
    for (std::size_t k = 4; k < 7; ++k)
    {
        const std::size_t hit = k * kBeat;
        right += rms(rumble, hit + 480, hit + 480 + 1920);
        before += rms(rumble, hit + kBeat - 4800, hit + kBeat);
    }
    return decibels((float) (right / std::max(before, 1.0e-12)));
}
} // namespace

class RumbleTests : public juce::UnitTest
{
public:
    RumbleTests() : juce::UnitTest("Tonwerk Rumble", "Rumble") {}

    void runTest() override
    {
        beginTest("Silence stays silent; the kick passes untouched");
        {
            Settings s;
            const auto quiet = play(s, Signal((std::size_t) kRate, 0.0f));
            expectEquals(peakOf(quiet), 0.0f);
            // With the rumble far down, what comes out is the kick, sample for sample.
            s.rumbleDb = -400.0f;
            const auto input = kickLoop(2);
            const auto take = play(s, input);
            float error = 0.0f;
            for (std::size_t n = 0; n < input.size(); ++n)
                error = std::max({ error, std::abs(take.left[n] - input[n]), std::abs(take.right[n] - input[n]) });
            expectLessThan(error, 1.0e-6f);
        }

        beginTest("The rumble lies in the low end, rings between the kicks and is mono");
        {
            Settings s;
            s.duck = 0.0f;
            const auto input = kickLoop();
            const auto rumble = rumbleOf(s, input);
            const float total = rms(rumble, kBeat, rumble.size());
            const float high = rms(above(rumble, 1000.0f), kBeat, rumble.size());
            logMessage("rumble " + juce::String(decibels(total), 1) + " dBFS RMS, above 1 kHz "
                       + juce::String(decibels(high / total), 1) + " dB");
            expectGreaterThan(total, 0.01f);
            expectLessThan(decibels(high / total), -40.0f);
            // It rings where the kick has died away: the last 100 ms before each kick.
            expectGreaterThan(rms(rumble, 5 * kBeat - 4800, 5 * kBeat), 0.3f * total);

            // A kick hard left gives the same rumble on both sides.
            const Signal silence(input.size(), 0.0f);
            s.kickDb = kKickOffDb;
            const auto take = play(s, input, &silence);
            float difference = 0.0f;
            for (std::size_t n = 0; n < input.size(); ++n)
                difference = std::max(difference, std::abs(take.left[n] - take.right[n]));
            expectEquals(difference, 0.0f);
            expectGreaterThan(rms(take.left, kBeat, take.left.size()), 0.005f);
        }

        beginTest("The kick ducks the rumble and lets it swell back");
        {
            const auto input = kickLoop();
            Settings s;
            s.duck = 0.0f;
            const float none = duckingDepth(rumbleOf(s, input));
            s.duck = 0.9f;
            const float deep = duckingDepth(rumbleOf(s, input));
            s.duck = 1.0f;
            const float full = duckingDepth(rumbleOf(s, input));
            logMessage("after the kick against before the next: duck 0 % " + juce::String(none, 1) + " dB, 90 % "
                       + juce::String(deep, 1) + " dB, 100 % " + juce::String(full, 1) + " dB");
            expectLessThan(deep, none - 12.0f);
            expectLessThan(full, deep);
            // A longer release keeps it down for longer.
            s.duck = 0.9f;
            s.duckReleaseMs = 600.0f;
            const auto slow = rumbleOf(s, input);
            s.duckReleaseMs = 100.0f;
            const auto fast = rumbleOf(s, input);
            const std::size_t mid = 5 * kBeat + 7200;
            expectLessThan(rms(slow, mid, mid + 2400), 0.7f * rms(fast, mid, mid + 2400));
        }

        beginTest("On the grid the rumble ducks on the song's beat, from any start");
        {
            // A sustained source, so only the grid can make the rumble move.
            Signal drone((std::size_t) (6.0 * kRate));
            for (std::size_t n = 0; n < drone.size(); ++n)
                drone[n] = 0.3f * (float) std::sin(kTwoPi * 55.0 * (double) n / kRate);
            for (const double start : { 0.0, 0.37 })
            {
                RumbleProcessor processor;
                set(processor, "duckSync", 1.0f);
                set(processor, "duck", 1.0f);
                set(processor, "duckRelease", 300.0f);
                set(processor, "kick", kKickOffDb);
                const auto take = render(processor, drone, start);
                // Each beat line of the song, in samples of the take.
                for (int beat = 4; beat < 10; ++beat)
                {
                    const auto line = (std::size_t) ((beat - start) * (double) kBeat);
                    const float onLine = rms(take.left, line + 240, line + 960);
                    const float later = rms(take.left, line + 16800, line + 21600);
                    expectLessThan(decibels(onLine / later), -20.0f, "beat " + juce::String(beat) + " from "
                                                                         + juce::String(start));
                }
            }
        }

        beginTest("Decay and the echo shape the tail");
        {
            const auto input = kickLoop(1, 4.0);
            Settings s;
            s.duck = 0.0f;
            s.decaySeconds = 1.0f;
            const auto shortHall = rumbleOf(s, input);
            s.decaySeconds = 6.0f;
            const auto longHall = rumbleOf(s, input);
            const std::size_t from = (std::size_t) (2.5 * kRate);
            expectLessThan(rms(shortHall, from, from + 24000), 0.1f * rms(longHall, from, from + 24000));

            // An echo on quarters (half a second at 120 BPM) brings the kick back after half a second.
            s.space = Space::echo;
            s.echoDivision = tonwerkwobble::kQuarter;
            s.feedback = 0.0f;
            const auto echoed = rumbleOf(s, input);
            const float atRepeat = rms(echoed, kBeat, kBeat + 12000);
            const float between = rms(echoed, 14400, 22800);
            expectGreaterThan(decibels(atRepeat / std::max(between, 1.0e-9f)), 20.0f);
        }

        beginTest("Every curve, the filter's peak and full feedback stay bounded");
        {
            juce::Random random(3);
            Signal noise((std::size_t) (3.0 * kRate));
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            for (int c = 0; c < (int) kCurveNames.size(); ++c)
                for (int space = 0; space < (int) kSpaceNames.size(); ++space)
                {
                    Settings s;
                    s.curve = (Curve) c;
                    s.space = (Space) space;
                    s.driveDb = 36.0f;
                    s.resonance = 1.0f;
                    s.feedback = 0.95f;
                    s.decaySeconds = 10.0f;
                    s.size = 1.0f;
                    s.toneHz = 5000.0f;
                    s.cutoff = 500.0f;
                    s.rumbleDb = 12.0f;
                    const auto take = play(s, noise);
                    bool finite = true;
                    for (const float v : take.left)
                        finite = finite && std::isfinite(v);
                    const juce::String label = juce::String::fromUTF8(kCurveNames[(std::size_t) c]) + " / "
                                              + kSpaceNames[(std::size_t) space];
                    expect(finite, label);
                    expectLessThan(peakOf(take), 16.0f, label);
                }
        }

        beginTest("The drive changes the colour, not the level");
        {
            const auto input = kickLoop();
            for (int c = 0; c < (int) kCurveNames.size(); ++c)
            {
                Settings s;
                s.duck = 0.0f;
                s.curve = (Curve) c;
                s.driveDb = 6.0f;
                const float gentle = rms(rumbleOf(s, input), kBeat, input.size());
                s.driveDb = 36.0f;
                const float hard = rms(rumbleOf(s, input), kBeat, input.size());
                const auto label = juce::String::fromUTF8(kCurveNames[(std::size_t) c]);
                logMessage(label.paddedRight(' ', 8) + "drive 36 dB against 6 dB: " + juce::String(decibels(hard / gentle), 1)
                           + " dB");
                expectWithinAbsoluteError(decibels(hard / gentle), 0.0f, 9.0f, label);
            }
        }

        beginTest("Cheap enough for many tracks");
        {
            const auto input = kickLoop(16);
            for (int space = 0; space < (int) kSpaceNames.size(); ++space)
            {
                Settings s;
                s.space = (Space) space;
                s.curve = Curve::tube;
                const auto take = play(s, input);
                const double factor = (double) input.size() / kRate / take.seconds;
                logMessage(juce::String(kSpaceNames[(std::size_t) space]).paddedRight(' ', 12) + "stereo runs "
                           + juce::String(factor, 0) + " times faster than real time");
                // Generous, for debug builds and busy CI machines; the log shows the real figure.
                expectGreaterThan(factor, 20.0);
            }
        }

        beginTest("Factory sounds: groups, names and levels against the kick alone");
        {
            const auto source = kickLoop(8);
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_RUMBLE_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                RumbleProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kRumbleCategories) >= 0,
                       "no group: " + sound);
                const auto take = render(processor, source);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                const float peak = peakOf(take);
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 18)
                           + juce::String::fromUTF8(presets[(std::size_t) i].category).paddedRight(' ', 8)
                           + juce::String(change, 1) + " dB against the kick, peak " + juce::String(peak, 2));
                // The rumble adds a few dB to the kick, never buries it.
                expectWithinAbsoluteError(change, 3.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav({ source, source }, juce::File(wavDir).getChildFile("00 Kick trocken.wav"));
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            RumbleProcessor p;
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("tone"), juce::String("600 Hz"));
            expectEquals(text("space"), juce::String("Hall"));
            expectEquals(text("decay"), juce::String("2.5 s"));
            expectEquals(text("size"), juce::String("60 %"));
            expectEquals(text("drive"), juce::String("18 dB"));
            expectEquals(text("curve"), juce::String("Weich"));
            expectEquals(text("cutoff"), juce::String("140 Hz"));
            expectEquals(text("echoDivision"), juce::String("1/16"));
            expectEquals(text("duckDivision"), juce::String("1/4"));
            expectEquals(text("duckRelease"), juce::String("200 ms"));
            expectEquals(text("kick"), juce::String("0 dB"));
            set(p, "kick", kKickOffDb);
            expectEquals(text("kick"), juce::String("aus"));
            set(p, "cutoff", 95.0f);
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            RumbleProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().cutoff, 95.0f, 0.5f);
            expectEquals(loaded.readSettings().kickDb, kKickOffDb);
        }

        beginTest("The editor shows kick, rumble and ducking and fits its controls");
        {
            RumbleProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<RumbleEditor*>(created.get());
            expect(editor != nullptr);
            render(p, kickLoop(6));
            editor->view.refresh();
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            // The history saw the kicks, the rumble and the ducking.
            float kick = 0.0f, rumble = 0.0f, duck = 1.0f;
            for (int i = 0; i < RumbleProcessor::kColumns; ++i)
            {
                kick = std::max(kick, p.kickHistory[(std::size_t) i].load());
                rumble = std::max(rumble, p.rumbleHistory[(std::size_t) i].load());
                duck = std::min(duck, p.duckHistory[(std::size_t) i].load());
            }
            expectGreaterThan(kick, 0.4f);
            expectGreaterThan(rumble, 0.01f);
            expectLessThan(duck, 0.3f);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("rumble.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static RumbleTests rumbleTests;
