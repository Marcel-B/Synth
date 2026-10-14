#include "ChorusEditor.h"
#include "ChorusEngine.h"
#include "ChorusPresets.h"
#include "ChorusProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkchorus;

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

/** A stereo signal through the engine alone, in blocks as a host would send them. */
Take play(const Settings& s, const Signal& left, const Signal& right, double bpm = 120.0, double startBeat = -1.0)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { left, right };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        const double beat = startBeat < 0.0 ? -1.0 : startBeat + (double) at / kRate * bpm / 60.0;
        engine.process(take.left.data() + at, take.right.data() + at, count, s, bpm, beat);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

Signal sine(double hz, double seconds, float level = 0.3f)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal impulse(double seconds)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    out[0] = 0.5f;
    return out;
}

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

/** Only the effect: everything wet, nothing filtered. */
Settings wetOnly(Mode mode)
{
    Settings s;
    s.mode = mode;
    s.mix = 1.0f;
    s.lowCut = 20.0f;
    s.highCut = 20000.0f;
    s.outputDb = 0.0f;
    return s;
}

/**
 * What a chorus is mostly put on: two seconds of a held chord of saws, then plucked notes, peaking at -6 dBFS.
 */
Signal padAndPlucks()
{
    Signal out((std::size_t) (4.5 * kRate), 0.0f);
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

/** The frequency between rising zero crossings, cycle by cycle, from `from` seconds on. */
std::vector<double> cycleFrequencies(const Signal& signal, double from)
{
    std::vector<double> out;
    double last = -1.0;
    for (auto n = (std::size_t) (from * kRate) + 1; n < signal.size(); ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
        {
            const double at = (double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]);
            if (last >= 0.0)
                out.push_back(kRate / (at - last));
            last = at;
        }
    return out;
}

Take render(ChorusProcessor& processor, const Signal& source)
{
    struct PlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(true);
            info.setBpm(120.0);
            return info;
        }
    } head;
    processor.setPlayHead(&head);
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { source, source };
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (std::size_t at = 0; at < source.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, source.size() - at);
        buffer.setSize(2, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, source.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, take.right.data() + at);
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
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

class ChorusTests : public juce::UnitTest
{
public:
    ChorusTests() : juce::UnitTest("Tonwerk Chorus", "Chorus") {}

    void runTest() override
    {
        beginTest("Without depth a chorus line is the delay itself");
        {
            auto s = wetOnly(Mode::chorus);
            s.voices = 1;
            s.depth = 0.0f;
            s.delay = 0.01f;
            const auto in = impulse(0.1);
            const auto take = play(s, in, in);
            std::size_t best = 0;
            for (std::size_t n = 0; n < take.left.size(); ++n)
                if (std::abs(take.left[n]) > std::abs(take.left[best]))
                    best = n;
            expectEquals((int) best, 480);
            // The high cut at 20 kHz spreads the click over a few samples; together they are the click.
            float sum = 0.0f;
            for (std::size_t n = 470; n < 500; ++n)
                sum += take.left[n];
            expectWithinAbsoluteError(sum, 0.5f, 0.03f);
        }

        beginTest("The sweep bends the pitch as far as the depth and rate say");
        {
            // A delay swept by A seconds at f Hz moves a tone f0 by 2 pi f A f0 at most.
            auto s = wetOnly(Mode::chorus);
            s.voices = 1;
            s.rate = 1.0f;
            s.depth = 0.5f;
            s.delay = 0.012f;
            const auto in = sine(1000.0, 3.0);
            const auto take = play(s, in, in);
            const auto hz = cycleFrequencies(take.left, 0.5);
            const auto [low, high] = std::minmax_element(hz.begin(), hz.end());
            const double expected = kTwoPi * 1.0 * 0.0025 * 1000.0;
            logMessage("1 kHz under a 1 Hz sweep of 2.5 ms: " + juce::String(*low, 1) + " to " + juce::String(*high, 1)
                       + " Hz, expected +-" + juce::String(expected, 1));
            expectWithinAbsoluteError(*high - 1000.0, expected, 0.15 * expected);
            expectWithinAbsoluteError(1000.0 - *low, expected, 0.15 * expected);
            // More voices: each sweeps alike, together they keep the level.
            s.voices = 4;
            const auto four = play(s, in, in);
            const float ratio = rms(four.left, 0.5, 3.0) / rms(take.left, 0.5, 3.0);
            logMessage("4 voices against 1: " + juce::String(juce::Decibels::gainToDecibels(ratio), 1) + " dB");
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(ratio), 0.0f, 3.0f);
        }

        beginTest("The ensemble keeps a held chord's level steady");
        {
            // Mixed half and half, as it is played: alone, three moving lines swirl by a few dB, as a string machine's do.
            auto s = wetOnly(Mode::ensemble);
            s.mix = 0.5f;
            s.depth = 0.8f;
            s.delay = 0.008f;
            const auto source = padAndPlucks();
            const auto take = play(s, source, source);
            // The chord beats by itself; the ensemble may add a few dB of swirl to that, no deep holes.
            auto variation = [](const Signal& signal) {
                float low = 1.0e9f, high = 0.0f;
                for (double t = 0.3; t < 1.8; t += 0.05)
                {
                    const float level = rms(signal, t, t + 0.05);
                    low = std::min(low, level);
                    high = std::max(high, level);
                }
                return juce::Decibels::gainToDecibels(high / low);
            };
            logMessage("held chord: level varies " + juce::String(variation(source), 1) + " dB dry, "
                       + juce::String(variation(take.left), 1) + " dB through the ensemble, in 50 ms windows");
            expectLessThan(variation(take.left), variation(source) + 3.0f);
            // And it is something else than the dry chord: it moves.
            expectGreaterThan(rms(take.left, 0.3, 1.8), 0.3f * rms(source, 0.3, 1.8));
        }

        beginTest("Flanger: the comb's first notch sits where the delay puts it, feedback deepens the peaks");
        {
            // Mixed half and half with 1 ms: 500 Hz cancels, 1 kHz doubles.
            auto s = wetOnly(Mode::flanger);
            s.depth = 0.0f;
            s.delay = 0.001f;
            s.mix = 0.5f;
            const auto notch = play(s, sine(500.0, 1.0), sine(500.0, 1.0));
            const auto peak = play(s, sine(1000.0, 1.0), sine(1000.0, 1.0));
            const float dry = rms(sine(500.0, 1.0), 0.5, 1.0);
            const float notchDb = juce::Decibels::gainToDecibels(rms(notch.left, 0.5, 1.0) / dry);
            const float peakDb = juce::Decibels::gainToDecibels(rms(peak.left, 0.5, 1.0) / dry);
            logMessage("1 ms flanger: 500 Hz " + juce::String(notchDb, 1) + " dB, 1 kHz " + juce::String(peakDb, 1) + " dB");
            expectLessThan(notchDb, -25.0f);
            expectWithinAbsoluteError(peakDb, 6.0f, 0.3f);
            // Feedback 50 % (small tone, the saturation linear): the peak rises to 1 + 1/(1 - 0.5) = 3, about 9.5 dB.
            s.feedback = 0.5f;
            const auto fed = play(s, sine(1000.0, 1.0, 0.05f), sine(1000.0, 1.0, 0.05f));
            const float fedDb = juce::Decibels::gainToDecibels(rms(fed.left, 0.5, 1.0) / (0.05f / 0.3f * dry));
            logMessage("with 50 % feedback: 1 kHz " + juce::String(fedDb, 1) + " dB");
            expectWithinAbsoluteError(fedDb, 9.5f, 0.5f);
        }

        beginTest("Flanger and phaser at full feedback stay bounded");
        {
            juce::Random random(3);
            Signal noise((std::size_t) (3.0 * kRate));
            for (auto& v : noise)
                v = 0.9f * (random.nextFloat() * 2.0f - 1.0f);
            for (const auto mode : { Mode::flanger, Mode::phaser })
                for (const float feedback : { 0.95f, -0.95f })
                {
                    auto s = wetOnly(mode);
                    s.feedback = feedback;
                    s.depth = 1.0f;
                    s.rate = 3.0f;
                    s.stages = 3;
                    s.delay = 0.002f;
                    const auto take = play(s, noise, noise);
                    float peak = 0.0f;
                    bool finite = true;
                    for (std::size_t n = 0; n < take.left.size(); ++n)
                    {
                        finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                        peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                    }
                    expect(finite);
                    expectLessThan(peak, 8.0f);
                }
        }

        beginTest("Phaser: four stages at 1 kHz cancel at 414 Hz and pass 1 kHz doubled");
        {
            // Each first-order allpass turns its own frequency by 90 degrees; four turn about 0.414 times it by 180 in
            // all (tan 22.5 degrees, through the bilinear transform's warping).
            auto s = wetOnly(Mode::phaser);
            s.depth = 0.0f;
            s.frequency = 1000.0f;
            s.stages = 0;
            s.mix = 0.5f;
            const float dry = rms(sine(1000.0, 1.0), 0.5, 1.0);
            const double notchHz = kRate / juce::MathConstants<double>::pi
                                   * std::atan(std::tan(22.5 / 180.0 * juce::MathConstants<double>::pi)
                                               * std::tan(juce::MathConstants<double>::pi * 1000.0 / kRate));
            const auto notch = play(s, sine(notchHz, 1.0), sine(notchHz, 1.0));
            const auto centre = play(s, sine(1000.0, 1.0), sine(1000.0, 1.0));
            const float notchDb = juce::Decibels::gainToDecibels(rms(notch.left, 0.5, 1.0) / dry);
            const float centreDb = juce::Decibels::gainToDecibels(rms(centre.left, 0.5, 1.0) / dry);
            logMessage("phaser 4 stages at 1 kHz: " + juce::String(notchHz, 1) + " Hz " + juce::String(notchDb, 1) + " dB, 1 kHz "
                       + juce::String(centreDb, 1) + " dB");
            // Not deeper: the wet's low cut, even at 20 Hz, turns 414 Hz by about 3 degrees against the dry.
            expectLessThan(notchDb, -25.0f);
            expectWithinAbsoluteError(centreDb, 6.0f, 0.3f);
        }

        beginTest("Stereo: the sides move apart by the spread, width 0 joins them");
        {
            auto s = wetOnly(Mode::chorus);
            s.voices = 1;
            s.spread = 180.0f;
            const auto in = sine(700.0, 2.0);
            const auto apart = play(s, in, in);
            double difference = 0.0;
            for (std::size_t n = (std::size_t) kRate / 2; n < apart.left.size(); ++n)
                difference += std::abs(apart.left[n] - apart.right[n]);
            expectGreaterThan(difference / (1.5 * kRate), 0.05);
            s.spread = 0.0f;
            const auto together = play(s, in, in);
            s.spread = 180.0f;
            s.width = 0.0f;
            const auto narrow = play(s, in, in);
            for (std::size_t n = 0; n < together.left.size(); n += 97)
            {
                expectWithinAbsoluteError(together.left[n], together.right[n], 1.0e-6f);
                expectWithinAbsoluteError(narrow.left[n], narrow.right[n], 1.0e-6f);
            }
        }

        beginTest("In sync the LFO sits on the song's bars");
        {
            auto s = wetOnly(Mode::flanger);
            s.sync = true;
            s.division = kWhole;
            Engine engine;
            engine.prepare(kRate);
            Signal l(kBlock, 0.0f), r(kBlock, 0.0f);
            // Beat 6 of a bar-long cycle is half way; the block moves it on by 256 samples at 120 BPM.
            engine.process(l.data(), r.data(), kBlock, s, 120.0, 6.0);
            expectWithinAbsoluteError(engine.phaseNow(), 0.5 + kBlock / kRate / 2.0, 1.0e-9);
            expectWithinAbsoluteError(rateOf(s, 120.0), 0.5, 1.0e-9);
            s.division = kTwoBars;
            expectWithinAbsoluteError(rateOf(s, 120.0), 0.25, 1.0e-9);
        }

        beginTest("Changing the mode fades rather than clicks");
        {
            const auto in = sine(110.0, 1.0, 0.5f);
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            auto s = wetOnly(Mode::chorus);
            s.mix = 0.5f;
            float jump = 0.0f, normal = 0.0f;
            for (std::size_t at = 0; at < l.size(); at += kBlock)
            {
                if (at >= l.size() / 2)
                    s.mode = Mode::flanger;
                const int count = (int) std::min<std::size_t>(kBlock, l.size() - at);
                engine.process(l.data() + at, r.data() + at, count, s, 120.0);
            }
            const std::size_t switchAt = l.size() / 2;
            for (std::size_t n = 1; n < l.size(); ++n)
            {
                const bool near = n > switchAt && n < switchAt + 2400;
                auto& largest = near ? jump : normal;
                largest = std::max(largest, std::abs(l[n] - l[n - 1]));
            }
            logMessage("largest step around the switch " + juce::String(jump, 4) + ", elsewhere " + juce::String(normal, 4));
            expectLessThan(jump, 2.0f * normal);
        }

        beginTest("Every mode runs far inside real time");
        {
            const auto source = padAndPlucks();
            Signal longer;
            for (int i = 0; i < 4; ++i)
                longer.insert(longer.end(), source.begin(), source.end());
            for (const auto mode : { Mode::chorus, Mode::ensemble, Mode::flanger, Mode::phaser })
            {
                auto s = wetOnly(mode);
                s.voices = 4;
                s.stages = 3;
                s.feedback = 0.5f;
                s.lowCut = 150.0f;
                s.highCut = 8000.0f;
                const auto take = play(s, longer, longer);
                const double factor = (double) longer.size() / kRate / take.seconds;
                logMessage(juce::String(kChorusCategories[(std::size_t) mode]) + ": " + juce::String(factor, 0)
                           + " times faster than real time");
                expectGreaterThan(factor, 50.0);
            }
        }

        beginTest("Factory sounds: groups, names and levels against the dry source");
        {
            const auto source = padAndPlucks();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_CHORUS_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                ChorusProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kChorusCategories) >= 0,
                       "no group: " + sound);
                const auto take = render(processor, source);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 24)
                           + juce::String::fromUTF8(presets[(std::size_t) i].category).paddedRight(' ', 12)
                           + juce::String(change, 1) + " dB against dry, peak " + juce::String(peak, 2));
                expectWithinAbsoluteError(change, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav({ source, source }, juce::File(wavDir).getChildFile("00 Quelle trocken.wav"));
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            ChorusProcessor p;
            auto* delay = p.state.getParameter("delay");
            delay->setValueNotifyingHost(delay->convertTo0to1(2.5f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("delay"), juce::String("2.5 ms"));
            expectEquals(text("rate"), juce::String("0.8 Hz"));
            expectEquals(text("depth"), juce::String("50 %"));
            expectEquals(text("spread"), juce::String::fromUTF8("90 °"));
            expectEquals(text("frequency"), juce::String("800 Hz"));
            expectEquals(text("stages"), juce::String("4"));
            expectEquals(text("output"), juce::String("-3 dB"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ChorusProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().delay, 0.0025f, 0.0001f);
        }

        beginTest("The editor shows the sweep and swaps the line's knobs for the phaser's");
        {
            ChorusProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<ChorusEditor*>(created.get());
            expect(editor != nullptr);
            render(p, padAndPlucks());
            editor->sweep.view.refresh();
            expectEquals(editor->sweep.view.caption(), juce::String::fromUTF8("CHORUS · 3 STIMMEN · 11.3-16.8 MS · 0.6 HZ"));
            expect(editor->line.isVisible() && ! editor->phaser.isVisible());
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("chorus.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
            p.setCurrentProgram(13);
            expect(editor->phaser.isVisible() && ! editor->line.isVisible());
        }
    }
};

static ChorusTests chorusTests;
