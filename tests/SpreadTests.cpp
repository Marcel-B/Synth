#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "SpreadEditor.h"
#include "SpreadEngine.h"
#include "SpreadPresets.h"
#include "SpreadProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <random>
#include <set>

using namespace tonwerkspread;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;

using Signal = std::vector<float>;

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

/** A stereo signal through the engine alone, in blocks as a host would send them; `change` may alter the settings. */
Take play(Settings s, const Signal& left, const Signal& right,
          const std::function<void(std::size_t, Settings&)>& change = {})
{
    Engine engine;
    engine.prepare(kRate);
    Take take { left, right };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        if (change)
            change(at, s);
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        engine.process(take.left.data() + at, take.right.data() + at, count, s);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

Signal sine(double hz, double length, float level = 0.3f)
{
    Signal out((std::size_t) (length * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal noise(double length, unsigned seed, float level = 0.3f)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> uniform(-level, level);
    Signal out((std::size_t) (length * kRate));
    for (auto& v : out)
        v = uniform(random);
    return out;
}

Signal silence(double length) { return Signal((std::size_t) (length * kRate), 0.0f); }

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

float correlation(const Take& take, double from)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t n = (std::size_t) (from * kRate); n < take.left.size(); ++n)
    {
        lr += (double) take.left[n] * take.right[n];
        ll += (double) take.left[n] * take.left[n];
        rr += (double) take.right[n] * take.right[n];
    }
    return (float) (lr / std::sqrt(std::max(1.0e-30, ll * rr)));
}

/** Left over right in dB, after the settling. */
float lean(const Take& take) { return juce::Decibels::gainToDecibels(rms(take.left, 0.5, 2.0) / rms(take.right, 0.5, 2.0)); }

/** Everything that widens at its most, the wander on. */
Settings widest(int mode)
{
    Settings s;
    s.mode = mode;
    s.width = 2.0f;
    s.amount = 1.0f;
    s.motion = 1.0f;
    s.bassMono = 150.0f;
    s.highCut = 8000.0f;
    return s;
}

/** Nothing made, nothing filtered, nothing moved. */
Settings neutral()
{
    Settings s;
    s.amount = 0.0f;
    s.motion = 0.0f;
    s.bassMono = 20.0f;
    return s;
}

/**
 * What a widener is put on: a mono synth part peaking at -6 dBFS: two seconds of a held chord of saws, then plucked
 * notes, then their room.
 */
Signal padAndPlucks()
{
    Signal out((std::size_t) (5.0 * kRate), 0.0f);
    auto saw = [](double hz, double t) {
        double v = 0.0;
        for (int k = 1; k * hz < 8000.0; ++k)
            v += std::sin(kTwoPi * k * hz * t) / k;
        return v;
    };
    for (const int note : { 48, 55, 60, 64 })
    {
        const double hz = 440.0 * std::exp2((note - 69) / 12.0);
        for (std::size_t n = 0; n < (std::size_t) (2.0 * kRate); ++n)
        {
            const double t = (double) n / kRate;
            const double envelope = std::min(1.0, t / 0.05) * std::min(1.0, (2.0 - t) / 0.1);
            out[n] += (float) (0.25 * saw(hz, t) * envelope);
        }
    }
    const int line[] = { 64, 67, 72, 71, 67, 64, 62, 60 };
    for (int i = 0; i < 8; ++i)
    {
        const double hz = 440.0 * std::exp2((line[i] - 69) / 12.0);
        const auto start = (std::size_t) ((2.1 + 0.25 * i) * kRate);
        for (std::size_t n = 0; n < (std::size_t) (0.4 * kRate) && start + n < out.size(); ++n)
        {
            const double t = (double) n / kRate;
            out[start + n] += (float) (saw(hz, t) * std::min(1.0, t / 0.002) * std::exp(-t / 0.12));
        }
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

Take render(SpreadProcessor& processor, const Signal& left, const Signal& right)
{
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { left, right };
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        buffer.setSize(2, count, false, false, true);
        buffer.copyFrom(0, 0, left.data() + at, count);
        buffer.copyFrom(1, 0, right.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, take.right.data() + at);
    }
    processor.releaseResources();
    return take;
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

class SpreadTests : public juce::UnitTest
{
public:
    SpreadTests() : juce::UnitTest("Tonwerk Spread", "Spread") {}

    void runTest() override
    {
        beginTest("The mono sum stays the input's in every mode, however wide");
        {
            const auto left = noise(1.0, 1), right = noise(1.0, 2);
            for (int mode = 0; mode < (int) kModeNames.size(); ++mode)
            {
                Settings s = widest(mode);
                s.bassMono = 20.0f;
                const auto take = play(s, left, right);
                float worst = 0.0f;
                for (std::size_t n = 0; n < left.size(); ++n)
                    worst = std::max(worst, std::abs((take.left[n] + take.right[n]) - (left[n] + right[n])));
                expectLessThan(worst, 1.0e-4f, kModeNames[(std::size_t) mode]);

                // Bass mono works on the side alone, so the sum stays exact with it too.
                const auto bassMono = play(widest(mode), left, right);
                worst = 0.0f;
                for (std::size_t n = 0; n < left.size(); ++n)
                    worst = std::max(worst, std::abs((bassMono.left[n] + bassMono.right[n]) - (left[n] + right[n])));
                expectLessThan(worst, 1.0e-4f, "bass mono");
            }
        }

        beginTest("At width 100 % with nothing made, the input passes");
        {
            const auto left = noise(0.5, 3), right = noise(0.5, 4);
            const auto take = play(neutral(), left, right);
            float worst = 0.0f;
            for (std::size_t n = 0; n < left.size(); ++n)
                worst = std::max({ worst, std::abs(take.left[n] - left[n]), std::abs(take.right[n] - right[n]) });
            expectLessThan(worst, 1.0e-6f);
        }

        beginTest("A mono sound is spread in every mode, more with more amount");
        {
            const auto mono = noise(2.0, 5);
            for (int mode = 0; mode < (int) kModeNames.size(); ++mode)
            {
                Settings s = neutral();
                s.mode = mode;
                expectWithinAbsoluteError(correlation(play(s, mono, mono), 0.2), 1.0f, 1.0e-4f);
                s.amount = 0.5f;
                const float half = correlation(play(s, mono, mono), 0.2);
                s.amount = 1.0f;
                const float full = correlation(play(s, mono, mono), 0.2);
                logMessage(juce::String(kModeNames[(std::size_t) mode]) + ": correlation " + juce::String(half, 2)
                           + " at 50 %, " + juce::String(full, 2) + " at 100 %");
                expectLessThan(half, 0.9f);
                expectLessThan(full, half);
                expectLessThan(full, 0.2f);
                expectGreaterThan(full, -0.2f, "never against each other on the whole");
            }
        }

        beginTest("Width scales the input's side: 0 is mono, 200 % twice as much");
        {
            const auto left = noise(1.0, 6), right = noise(1.0, 7);
            Settings s = neutral();
            s.width = 0.0f;
            auto take = play(s, left, right);
            float worst = 0.0f;
            for (std::size_t n = 2400; n < left.size(); ++n)
                worst = std::max(worst, std::abs(take.left[n] - take.right[n]));
            expectLessThan(worst, 1.0e-5f);
            s.width = 2.0f;
            take = play(s, left, right);
            Signal side(left.size()), sideIn(left.size());
            for (std::size_t n = 0; n < left.size(); ++n)
            {
                side[n] = take.left[n] - take.right[n];
                sideIn[n] = left[n] - right[n];
            }
            expectWithinAbsoluteError(rms(side, 0.2, 1.0) / rms(sideIn, 0.2, 1.0), 2.0f, 0.02f);
        }

        beginTest("Bass mono takes the side out of the lows and leaves the highs");
        {
            Settings s = neutral();
            s.bassMono = 200.0f;
            const auto low = sine(50.0, 2.0), high = sine(2000.0, 2.0), quiet = silence(2.0);
            const auto lowTake = play(s, low, quiet);
            expectWithinAbsoluteError(lean(lowTake), 0.0f, 0.5f, "a hard-left bass comes out in the middle");
            // The high pass turns the side's phase a little above the crossover, so a trace leaks to the right.
            expectGreaterThan(lean(play(s, high, quiet)), 15.0f, "a hard-left treble stays left");
            expectGreaterThan(lean(play(s, sine(8000.0, 2.0), quiet)), 25.0f, "and more so higher up");
            s.amount = 1.0f;
            s.mode = comb;
            const auto made = play(s, sine(40.0, 2.0), sine(40.0, 2.0));
            expectWithinAbsoluteError(lean(made), 0.0f, 0.3f, "nothing is made of a centred bass either");
        }

        beginTest("Comb and bands send their own frequencies to their side, as the editor draws it");
        {
            Settings s = neutral();
            s.amount = 0.8f;
            s.highCut = 20000.0f;
            s.mode = comb;
            s.time = 0.010f;
            const double combs[] = { 100.0, 150.0, 1000.0, 1050.0, 333.0 };
            // Deep leans are steep, so the tolerance grows with them.
            auto agrees = [this, &s](double hz, const juce::String& what) {
                const float measured = lean(play(s, sine(hz, 2.0), sine(hz, 2.0)));
                const float drawn = Engine::sideGainAt(s, hz);
                expectWithinAbsoluteError(measured, drawn, std::max(0.6f, 0.1f * std::abs(drawn)),
                                          what + " at " + juce::String(hz) + " Hz");
                return measured;
            };
            for (const double hz : combs)
                agrees(hz, "comb");
            expectGreaterThan(Engine::sideGainAt(s, 100.0), 10.0f, "a whole period of delay: left");
            expectLessThan(Engine::sideGainAt(s, 150.0), -10.0f, "half a period: right");

            // The outermost bands lack a neighbour on one side and lean less; the ones between are what is heard.
            s.mode = bands;
            for (int k = 1; k + 1 < kBands; ++k)
            {
                const double centre = kFirstBand * std::exp2(k), between = centre * std::sqrt(2.0);
                expectGreaterThan(agrees(centre, "band centre"), 3.0f, "band centre " + juce::String(centre) + " Hz");
                if (k + 2 < kBands)
                    expectLessThan(agrees(between, "between bands"), -3.0f, "gap " + juce::String(between) + " Hz");
            }

            s.mode = diffuse;
            for (const double hz : { 220.0, 1234.0, 5000.0 })
                agrees(hz, "diffuse");
        }

        beginTest("Every mode keeps its level flat: what one side gains the other loses, no band drowns");
        {
            // The made side has the middle's level at every frequency, so a mono noise comes out as loud on each side.
            const auto mono = noise(2.0, 8);
            for (int mode = 0; mode < (int) kModeNames.size(); ++mode)
            {
                Settings s = neutral();
                s.mode = mode;
                s.amount = 1.0f;
                const auto take = play(s, mono, mono);
                const float gainDb = juce::Decibels::gainToDecibels(
                    std::sqrt(0.5f * (rms(take.left, 0.3, 2.0) * rms(take.left, 0.3, 2.0)
                                      + rms(take.right, 0.3, 2.0) * rms(take.right, 0.3, 2.0)))
                    / rms(mono, 0.3, 2.0));
                logMessage(juce::String(kModeNames[(std::size_t) mode]) + ": " + juce::String(gainDb, 2)
                           + " dB per side at 100 %");
                expectWithinAbsoluteError(gainDb, 3.0f, 0.5f, "uncorrelated: the power doubles");
                // Bänder sends what lies above its last band to the right, and white noise has most of its power there.
                if (mode != bands)
                    expectWithinAbsoluteError(lean(take), 0.0f, 0.5f, "noise stays centred");
            }
        }

        beginTest("Mono-check plays the sum on both sides");
        {
            Settings s = widest(diffuse);
            s.monoCheck = true;
            const auto left = noise(1.0, 9), right = noise(1.0, 10);
            const auto take = play(s, left, right);
            float worst = 0.0f, apart = 0.0f;
            for (std::size_t n = 4800; n < left.size(); ++n)
            {
                worst = std::max(worst, std::abs(take.left[n] - take.right[n]));
                apart = std::max(apart, std::abs(take.left[n] - 0.5f * (left[n] + right[n])));
            }
            expectLessThan(worst, 1.0e-5f);
            expectLessThan(apart, 1.0e-4f);
        }

        beginTest("Changing mode, time or width glides without a click");
        {
            const auto tone = sine(330.0, 3.0);
            const auto take = play(widest(diffuse), tone, tone, [](std::size_t at, Settings& s) {
                if (at >= (std::size_t) (1.0 * kRate))
                    s.mode = comb;
                if (at >= (std::size_t) (1.5 * kRate))
                    s.mode = bands;
                if (at >= (std::size_t) (2.0 * kRate))
                {
                    s.time = 0.035f;
                    s.width = 0.0f;
                    s.mode = diffuse;
                }
            });
            // A 330 Hz sine at 0.3 moves at most 0.013 a sample; anything well above is a click.
            float step = 0.0f;
            for (std::size_t n = 1; n < take.left.size(); ++n)
                step = std::max({ step, std::abs(take.left[n] - take.left[n - 1]),
                                  std::abs(take.right[n] - take.right[n - 1]) });
            expectLessThan(step, 0.06f);
        }

        beginTest("Loud input stays bounded and finite at every extreme");
        {
            const auto left = noise(1.0, 11, 1.0f), right = noise(1.0, 12, 1.0f);
            for (int mode = 0; mode < (int) kModeNames.size(); ++mode)
            {
                Settings s = widest(mode);
                s.time = kMaxTime;
                s.highCut = 20000.0f;
                s.outputDb = 0.0f;
                const auto take = play(s, left, right);
                float peak = 0.0f;
                for (std::size_t n = 0; n < left.size(); ++n)
                {
                    expect(std::isfinite(take.left[n]) && std::isfinite(take.right[n]));
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                expectLessThan(peak, 8.0f);
            }
        }

        beginTest("Mix 0 passes the dry");
        {
            Settings s = widest(comb);
            s.mix = 0.0f;
            const auto left = noise(0.5, 13), right = noise(0.5, 14);
            const auto take = play(s, left, right);
            float worst = 0.0f;
            for (std::size_t n = 0; n < left.size(); ++n)
                worst = std::max({ worst, std::abs(take.left[n] - left[n]), std::abs(take.right[n] - right[n]) });
            expectLessThan(worst, 1.0e-6f);
        }

        beginTest("It runs far inside real time");
        {
            const auto left = noise(10.0, 15), right = noise(10.0, 16);
            double seconds = 0.0;
            for (int mode = 0; mode < (int) kModeNames.size(); ++mode)
                seconds += play(widest(mode), left, right).seconds;
            const double factor = 30.0 / std::max(1.0e-6, seconds);
            logMessage("spread: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry source");
        {
            const auto source = padAndPlucks();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_SPREAD_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                SpreadProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kSpreadCategories) >= 0,
                       "no group: " + sound);
                expect(! processor.readSettings().monoCheck, sound);
                const auto take = render(processor, source, source);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 24)
                           + juce::String::fromUTF8(presets[(std::size_t) i].category).paddedRight(' ', 12)
                           + juce::String(change, 1) + " dB against dry, correlation "
                           + juce::String(correlation(take, 0.1), 2) + ", peak " + juce::String(peak, 2));
                expectWithinAbsoluteError(change, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav({ source, source }, juce::File(wavDir).getChildFile("00 Quelle trocken (mono).wav"));
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            SpreadProcessor p;
            auto* time = p.state.getParameter("time");
            time->setValueNotifyingHost(time->convertTo0to1(25.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("time"), juce::String("25 ms"));
            expectEquals(text("width"), juce::String("100 %"));
            expectEquals(text("amount"), juce::String("50 %"));
            expectEquals(text("bassMono"), juce::String("120 Hz"));
            expectEquals(text("mode"), juce::String("Diffus"));
            auto* bass = p.state.getParameter("bassMono");
            bass->setValueNotifyingHost(0.0f);
            expectEquals(text("bassMono"), tonwerkui::tr("Aus"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            SpreadProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().time, 0.025f, 0.0001f);
            expect(loaded.readSettings().bassMono <= kBassOff);
        }

        beginTest("A mono input comes out stereo; a mono output is the input");
        {
            SpreadProcessor p;
            p.setPlayConfigDetails(1, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            const auto source = noise(0.5, 17);
            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            Take take { source, source };
            for (std::size_t at = 0; at + kBlock <= source.size(); at += kBlock)
            {
                buffer.clear();
                buffer.copyFrom(0, 0, source.data() + at, kBlock);
                p.processBlock(buffer, midi);
                std::copy_n(buffer.getReadPointer(0), kBlock, take.left.data() + at);
                std::copy_n(buffer.getReadPointer(1), kBlock, take.right.data() + at);
            }
            expectLessThan(correlation(take, 0.1), 0.95f);
            expectLessThan(p.correlationOut.load(), 0.95f);
            expectGreaterThan(p.correlationIn.load(), 0.99f);
            expectGreaterThan(p.sideShare.load(), 0.1f);
        }

        beginTest("The editor shows the stereo image and its settings");
        {
            SpreadProcessor p;
            p.setCurrentProgram(4);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<SpreadEditor>(created.get());
            expect(editor != nullptr);
            // The held chord's end, so the goniometer has something to show.
            auto source = padAndPlucks();
            source.resize((std::size_t) (1.8 * kRate));
            render(p, source, source);
            editor->stereo.view.refresh();
            expectEquals(editor->stereo.view.caption(), juce::String::fromUTF8("DIFFUS · 20 MS · BASS MONO 120 HZ"));
            expectGreaterThan(editor->getWidth(), 500);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("spread.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static SpreadTests spreadTests;
