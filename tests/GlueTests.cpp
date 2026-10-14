#include "GlueEditor.h"
#include "GlueEngine.h"
#include "GluePresets.h"
#include "GlueProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkglue;

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

/** A stereo signal through the engine alone, in blocks as a host would send them; the key may be empty. */
Take play(const Settings& s, const Signal& in, const Signal& key = {}, double bpm = 120.0, double startBeat = -1.0)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { in, in };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < in.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, in.size() - at);
        const double beat = startBeat < 0.0 ? -1.0 : startBeat + (double) at / kRate * bpm / 60.0;
        const float* k = key.empty() ? nullptr : key.data() + at;
        engine.process(take.left.data() + at, take.right.data() + at, k, k, count, s, bpm, beat);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

/** The reduction over time, read every millisecond, for a signal through the engine. */
std::vector<float> reductionCurve(const Settings& s, const Signal& in)
{
    Engine engine;
    engine.prepare(kRate);
    Signal l = in, r = in;
    std::vector<float> out;
    for (std::size_t at = 0; at + 48 <= in.size(); at += 48)
    {
        engine.process(l.data() + at, r.data() + at, nullptr, nullptr, 48, s, 120.0);
        out.push_back(engine.reductionNow());
    }
    return out;
}

Signal sine(double hz, double seconds, float level)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

/** A 1 kHz square: the same level every sample, so the detector's timing shows plainly. */
Signal square(double seconds, float level, std::size_t from = 0)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    for (std::size_t n = from; n < out.size(); ++n)
        out[n] = (n / 24) % 2 == 0 ? level : -level;
    return out;
}

float peakBetween(const Signal& signal, double from, double to)
{
    float peak = 0.0f;
    for (auto n = (std::size_t) (from * kRate); n < std::min(signal.size(), (std::size_t) (to * kRate)); ++n)
        peak = std::max(peak, std::abs(signal[n]));
    return peak;
}

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

float db(float gain) { return juce::Decibels::gainToDecibels(gain, -200.0f); }

/** How long, in ms, until a curve read every ms first crosses `level` (rising or falling), from index `from`. */
int crossing(const std::vector<float>& curve, std::size_t from, float level, bool rising)
{
    for (std::size_t i = from; i < curve.size(); ++i)
        if (rising ? curve[i] >= level : curve[i] <= level)
            return (int) (i - from);
    return -1;
}

/**
 * What a bus compressor is put on: four bars at 120 BPM of a kick on every beat, a snare on two and four, a bass line
 * and a pad, peaking at -6 dBFS.
 */
Signal mixLoop()
{
    Signal out((std::size_t) (8.0 * kRate), 0.0f);
    juce::Random random(11);
    for (int beat = 0; beat < 16; ++beat)
    {
        const auto start = (std::size_t) (beat * 0.5 * kRate);
        double phase = 0.0;
        for (std::size_t n = 0; n < (std::size_t) (0.35 * kRate); ++n)
        {
            const double t = (double) n / kRate;
            phase += kTwoPi * (50.0 + 100.0 * std::exp(-t / 0.03)) / kRate;
            out[start + n] += (float) (1.0 * std::sin(phase) * std::exp(-t / 0.12));
        }
        if (beat % 2 == 1)
            for (std::size_t n = 0; n < (std::size_t) (0.2 * kRate); ++n)
            {
                const double t = (double) n / kRate;
                out[start + n] += (float) ((0.5 * (random.nextFloat() * 2.0f - 1.0f) + 0.3 * std::sin(kTwoPi * 190.0 * t))
                                           * std::exp(-t / 0.05));
            }
    }
    const int bass[] = { 36, 36, 43, 41 };
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const double hz = 440.0 * std::exp2((bass[(int) (t / 2.0) % 4] - 69) / 12.0);
        double saw = 0.0;
        for (int k = 1; k <= 6; ++k)
            saw += std::sin(kTwoPi * k * hz * t) / k;
        double pad = 0.0;
        for (const int note : { 60, 64, 67 })
            pad += std::sin(kTwoPi * 440.0 * std::exp2((note - 69) / 12.0) * t);
        out[n] += (float) (0.35 * saw + 0.08 * pad);
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

/** Through the plugin as Logic would play it, the transport running from bar 1 at 120 BPM. */
Take render(GlueProcessor& processor, const Signal& source)
{
    struct PlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(true);
            info.setBpm(120.0);
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
    for (std::size_t at = 0; at < source.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, source.size() - at);
        head.ppq = (double) at / kRate * 2.0;
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

class GlueTests : public juce::UnitTest
{
public:
    GlueTests() : juce::UnitTest("Tonwerk Glue", "Glue") {}

    void runTest() override
    {
        beginTest("Above the threshold the level rises by one over the ratio");
        {
            Settings s;
            s.thresholdDb = -20.0f;
            s.kneeDb = 0.0f;
            s.attack = 0;
            s.release = 3;
            const auto in = sine(1000.0, 1.0, 0.5f);
            for (int r = 0; r < (int) kRatios.size(); ++r)
            {
                s.ratio = r;
                const auto take = play(s, in);
                const float out = db(peakBetween(take.left, 0.5, 1.0));
                const float expected = -20.0f + (db(0.5f) + 20.0f) / kRatios[(std::size_t) r];
                logMessage(juce::String(kRatios[(std::size_t) r], 1) + ":1, -6 dBFS in: " + juce::String(out, 2)
                           + " dBFS out, expected " + juce::String(expected, 2));
                expectWithinAbsoluteError(out, expected, 0.3f);
            }
            // Below it, nothing changes; makeup and output add on top.
            s.ratio = 2;
            const auto quiet = play(s, sine(1000.0, 1.0, 0.03f));
            expectWithinAbsoluteError(db(peakBetween(quiet.left, 0.5, 1.0)), db(0.03f), 0.05f);
            s.makeupDb = 4.0f;
            s.outputDb = -1.0f;
            const auto lifted = play(s, sine(1000.0, 1.0, 0.03f));
            expectWithinAbsoluteError(db(peakBetween(lifted.left, 0.5, 1.0)), db(0.03f) + 3.0f, 0.05f);
            // The knee: at the threshold itself, an eighth of its width times the slope.
            expectWithinAbsoluteError(reductionFor(-20.0f, -20.0f, 4.0f, 6.0f), 0.75f * 6.0f / 8.0f, 1.0e-5f);
            expectWithinAbsoluteError(reductionFor(-10.0f, -20.0f, 4.0f, 6.0f), 7.5f, 1.0e-5f);
            expectEquals(reductionFor(-30.0f, -20.0f, 4.0f, 6.0f), 0.0f);
        }

        beginTest("Attack and release take their time");
        {
            Settings s;
            s.thresholdDb = -30.0f;
            s.ratio = 4;
            s.kneeDb = 0.0f;
            s.attack = 4;  // 10 ms
            s.release = 1; // 0.3 s
            // Quiet, then loud for a second, then quiet again.
            Signal in = square(3.0, 0.01f);
            for (std::size_t n = (std::size_t) kRate; n < (std::size_t) (2.0 * kRate); ++n)
                in[n] *= 50.0f;
            const auto curve = reductionCurve(s, in);
            const float full = curve[1990];
            const int attackMs = crossing(curve, 1000, (1.0f - std::exp(-1.0f)) * full, true);
            const int releaseMs = crossing(curve, 2000, std::exp(-1.0f) * full, false);
            logMessage("reduction " + juce::String(full, 1) + " dB, 63 % after " + juce::String(attackMs)
                       + " ms, back to 37 % after " + juce::String(releaseMs) + " ms");
            expectWithinAbsoluteError(attackMs, 10, 2);
            expectWithinAbsoluteError(releaseMs, 300, 30);
        }

        beginTest("Auto release lets a short hit go at once and a long passage slowly");
        {
            Settings s;
            s.thresholdDb = -30.0f;
            s.ratio = 4;
            s.attack = 0;
            s.release = kAutoRelease;
            auto release = [&s](double loudSeconds) {
                Signal in = square(1.0 + loudSeconds + 4.0, 0.01f);
                for (std::size_t n = (std::size_t) kRate; n < (std::size_t) ((1.0 + loudSeconds) * kRate); ++n)
                    in[n] *= 50.0f;
                const auto curve = reductionCurve(s, in);
                const auto end = (std::size_t) ((1.0 + loudSeconds) * 1000.0);
                return crossing(curve, end, std::exp(-1.0f) * curve[end - 5], false);
            };
            const int shortHit = release(0.05), longPassage = release(3.0);
            logMessage("auto release: back to 37 % after " + juce::String(shortHit) + " ms (50 ms hit), "
                       + juce::String(longPassage) + " ms (3 s passage)");
            expectLessThan(shortHit, 200);
            expectGreaterThan(longPassage, 3 * shortHit);
        }

        beginTest("The side chain's high pass keeps the bass from driving the compressor");
        {
            Settings s;
            s.thresholdDb = -20.0f;
            s.ratio = 3;
            const auto bass = sine(50.0, 1.0, 0.5f);
            const auto open = reductionCurve(s, bass);
            s.sidechainHighpass = 300.0f;
            const auto filtered = reductionCurve(s, bass);
            logMessage("50 Hz at -6 dBFS: " + juce::String(open[900], 1) + " dB reduction, with the high pass at 300 Hz "
                       + juce::String(filtered[900], 1) + " dB");
            expectGreaterThan(open[900], 8.0f);
            expectLessThan(filtered[900], 0.25f * open[900]);
        }

        beginTest("A key on the side chain ducks the signal; without one, nothing happens");
        {
            Settings s;
            s.source = Source::sidechain;
            s.thresholdDb = -30.0f;
            s.ratio = 3;
            s.attack = 0;
            s.release = 0;
            const auto pad = sine(440.0, 2.0, 0.25f);
            // A kick-like burst every half second on the key.
            Signal key(pad.size(), 0.0f);
            for (int beat = 0; beat < 4; ++beat)
                for (std::size_t n = 0; n < (std::size_t) (0.05 * kRate); ++n)
                    key[(std::size_t) (beat * 0.5 * kRate) + n] = 0.8f * (float) std::sin(kTwoPi * 60.0 * (double) n / kRate);
            const auto ducked = play(s, pad, key);
            const float during = rms(ducked.left, 1.01, 1.04), between = rms(ducked.left, 1.44, 1.49);
            logMessage("pad under the key: " + juce::String(db(during / between), 1) + " dB on the hit");
            expectLessThan(db(during / between), -10.0f);
            // Just before the next hit, 400 ms after this one, the 100 ms release has let go but for a dB.
            expectWithinAbsoluteError(db(between / 0.25f * std::sqrt(2.0f)), 0.0f, 1.0f);
            const auto alone = play(s, pad);
            expectWithinAbsoluteError(db(rms(alone.left, 0.5, 2.0) / rms(pad, 0.5, 2.0)), 0.0f, 0.01f);

            // Through the plugin with the host's side chain bus.
            GlueProcessor processor;
            auto layout = processor.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
            expect(processor.setBusesLayout(layout));
            processor.setCurrentProgram(13);
            expectEquals(processor.getProgramName(13), juce::String("Kick-Ducking"));
            processor.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(4, kBlock);
            juce::MidiBuffer midi;
            Signal out(pad.size());
            for (std::size_t at = 0; at + kBlock <= pad.size(); at += kBlock)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    buffer.copyFrom(ch, 0, pad.data() + at, kBlock);
                    buffer.copyFrom(ch + 2, 0, key.data() + at, kBlock);
                }
                processor.processBlock(buffer, midi);
                std::copy_n(buffer.getReadPointer(0), kBlock, out.data() + at);
            }
            expect(processor.keyConnected.load());
            expectLessThan(db(rms(out, 1.01, 1.04) / rms(out, 1.44, 1.49)), -10.0f);
        }

        beginTest("The pump ducks on each step of the tempo and comes back along its curve");
        {
            Settings s;
            s.thresholdDb = 0.0f;
            s.pump = true;
            s.pumpDivision = kQuarter;
            s.pumpDepthDb = 12.0f;
            s.pumpLength = 0.5f;
            s.pumpCurve = 0.0f;
            const auto in = sine(200.0, 2.0, 0.25f);
            // At 120 BPM a quarter is 500 ms: down by 12 dB on the beat, half way back at 125 ms, home at 250 ms.
            const auto take = play(s, in, {}, 120.0, 0.0);
            auto levelAt = [&take](double t) { return db(rms(take.left, t - 0.005, t + 0.005) / (0.25f / std::sqrt(2.0f))); };
            logMessage("pump: " + juce::String(levelAt(1.007), 1) + " dB on the beat, " + juce::String(levelAt(1.125), 1)
                       + " dB at 125 ms, " + juce::String(levelAt(1.4), 1) + " dB at 400 ms");
            expectWithinAbsoluteError(levelAt(1.007), -12.0f, 1.0f);
            expectWithinAbsoluteError(levelAt(1.125), -6.0f, 0.7f);
            expectWithinAbsoluteError(levelAt(1.4), 0.0f, 0.1f);
            // From the song position: starting on the off-beat, the first dip comes half a quarter later.
            const auto late = play(s, in, {}, 120.0, 0.5);
            expectWithinAbsoluteError(db(rms(late.left, 0.1, 0.2) / (0.25f / std::sqrt(2.0f))), 0.0f, 0.1f);
            expectLessThan(db(rms(late.left, 0.252, 0.262) / (0.25f / std::sqrt(2.0f))), -9.0f);
        }

        beginTest("Mix puts the compressed beside the dry");
        {
            Settings s;
            s.thresholdDb = -40.0f;
            s.ratio = 4;
            s.mix = 0.5f;
            const auto in = sine(1000.0, 1.0, 0.5f);
            const auto take = play(s, in);
            s.mix = 1.0f;
            const auto wet = play(s, in);
            expectWithinAbsoluteError(peakBetween(take.left, 0.5, 1.0), 0.5f * (0.5f + peakBetween(wet.left, 0.5, 1.0)),
                                      0.005f);
        }

        beginTest("It runs far inside real time");
        {
            Settings s;
            s.thresholdDb = -20.0f;
            s.release = kAutoRelease;
            s.sidechainHighpass = 100.0f;
            s.pump = true;
            const auto source = mixLoop();
            const auto take = play(s, source);
            const double factor = (double) source.size() / kRate / take.seconds;
            logMessage("glue: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry mix");
        {
            const auto source = mixLoop();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_GLUE_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                GlueProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kGlueCategories) >= 0,
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
            GlueProcessor p;
            auto* threshold = p.state.getParameter("threshold");
            threshold->setValueNotifyingHost(threshold->convertTo0to1(-18.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("threshold"), juce::String("-18 dB"));
            expectEquals(text("ratio"), juce::String("4:1"));
            expectEquals(text("attack"), juce::String("10 ms"));
            expectEquals(text("release"), juce::String("Auto"));
            expectEquals(text("pumpLength"), juce::String("60 %"));
            expectEquals(text("scHighpass"), juce::String("20 Hz"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            GlueProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().thresholdDb, -18.0f, 0.01f);
        }

        beginTest("The editor shows the reduction and the settings");
        {
            GlueProcessor p;
            p.setCurrentProgram(3);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<GlueEditor*>(created.get());
            expect(editor != nullptr);
            const auto source = mixLoop();
            // Feed the meter as the timer would while a song plays.
            GlueProcessor& processor = p;
            {
                processor.setPlayConfigDetails(2, 2, kRate, kBlock);
                processor.prepareToPlay(kRate, kBlock);
                juce::AudioBuffer<float> buffer(2, 1600);
                juce::MidiBuffer midi;
                for (std::size_t at = 0; at + 1600 <= source.size(); at += 1600)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.copyFrom(ch, 0, source.data() + at, 1600);
                    processor.processBlock(buffer, midi);
                    editor->meter.view.refresh();
                }
            }
            expect(editor->meter.view.caption().contains(juce::String::fromUTF8("4:1 · 10 MS · 0.3 S")));
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("glue.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static GlueTests glueTests;
