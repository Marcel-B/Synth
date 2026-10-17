#include "Loudness.h"
#include "PresetCategories.h"
#include "ZoneEditor.h"
#include "ZoneEngine.h"
#include "ZonePresets.h"
#include "ZoneProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkzone;

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

/**
 * A stereo signal through the engine alone, in blocks as a host would send them; the key may be empty. The LFO runs
 * free at `s.lfoRate`, from phase 0.
 */
Take play(const Settings& s, const Signal& in, const Signal& key = {})
{
    Engine engine;
    engine.prepare(kRate);
    Take take { in, in };
    const auto started = std::chrono::steady_clock::now();
    const double increment = s.lfoRate / kRate;
    for (std::size_t at = 0; at < in.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, in.size() - at);
        const float* k = key.empty() ? nullptr : key.data() + at;
        engine.process(take.left.data() + at, take.right.data() + at, k, k, count, s,
                       Clock { (double) at * increment, increment });
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

Signal sine(double hz, double seconds, float level)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal add(Signal a, const Signal& b)
{
    for (std::size_t n = 0; n < std::min(a.size(), b.size()); ++n)
        a[n] += b[n];
    return a;
}

float db(float gain) { return juce::Decibels::gainToDecibels(gain, -200.0f); }

/** The level of one frequency in a signal (from `from` seconds on), by correlating with a sine and a cosine. */
float amplitudeAt(const Signal& signal, double hz, double from)
{
    double c = 0.0, s = 0.0;
    const auto start = (std::size_t) (from * kRate);
    // A whole number of cycles, so neighbours cancel.
    const auto cycles = (std::size_t) std::floor((double) (signal.size() - start) / kRate * hz);
    const auto end = start + (std::size_t) ((double) cycles / hz * kRate);
    for (std::size_t n = start; n < end; ++n)
    {
        const double phase = kTwoPi * hz * (double) n / kRate;
        c += signal[n] * std::cos(phase);
        s += signal[n] * std::sin(phase);
    }
    return (float) (2.0 * std::sqrt(c * c + s * s) / (double) (end - start));
}

/**
 * What a dynamic EQ is put on: four bars at 120 BPM of a kick on every beat, a snare on two and four, a
 * bass line, a pad and hats, peaking at -6 dBFS.
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
        // Off-beat hats: noise through a crude high pass, short.
        float last = 0.0f;
        const auto hat = start + (std::size_t) (0.25 * kRate);
        for (std::size_t n = 0; n < (std::size_t) (0.04 * kRate) && hat + n < out.size(); ++n)
        {
            const float noise = random.nextFloat() * 2.0f - 1.0f;
            out[hat + n] += 0.25f * (noise - last) * (float) std::exp(-(double) n / kRate / 0.01);
            last = noise;
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

/** Through the plugin as a host would play it. */
Take render(ZoneProcessor& processor, const Signal& source)
{
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
    return take;
}

/** The RMS level in dB of `seconds` around `centre`. */
float levelAround(const Signal& signal, double centre, double seconds)
{
    const auto from = (std::size_t) ((centre - 0.5 * seconds) * kRate);
    const auto to = std::min(signal.size(), (std::size_t) ((centre + 0.5 * seconds) * kRate));
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return db((float) std::sqrt(2.0 * sum / (double) (to - from)));
}

/** A host that plays at 120 BPM, standing at `ppq`. */
class StandingHead : public juce::AudioPlayHead
{
public:
    explicit StandingHead(double at) : ppq(at) {}
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        info.setIsPlaying(true);
        return info;
    }

private:
    double ppq;
};

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

class ZoneTests : public juce::UnitTest
{
public:
    ZoneTests() : juce::UnitTest("Tonwerk Zone", "Zone") {}

    void runTest() override
    {
        beginTest("Under the threshold the signal passes to the bit, whatever the shape");
        {
            const auto source = mixLoop();
            for (const auto shape : { Shape::bell, Shape::low, Shape::high })
            {
                Settings s;
                s.shape = shape;
                s.thresholdDb = 0.0f;
                const auto take = play(s, source);
                float worst = 0.0f;
                for (std::size_t n = 0; n < source.size(); ++n)
                    worst = std::max(worst, std::abs(take.left[n] - source[n]));
                expectLessThan(worst, 1.0e-6f);
            }
        }

        beginTest("Band turns down only the range, Breitband everything");
        {
            Settings s;
            s.frequency = 3000.0f;
            s.widthSemitones = 12.0f;
            s.thresholdDb = -30.0f;
            s.ratio = 20.0f;
            s.attackMs = 1.0f;
            const auto in = add(sine(3000.0, 1.0, 0.25f), sine(200.0, 1.0, 0.25f));
            const auto take = play(s, in);
            const float range = db(amplitudeAt(take.left, 3000.0, 0.5) / 0.25f);
            const float rest = db(amplitudeAt(take.left, 200.0, 0.5) / 0.25f);
            logMessage("bell at 3 kHz, 20:1 from -30 dB: 3 kHz " + juce::String(range, 2) + " dB, 200 Hz "
                       + juce::String(rest, 2) + " dB");
            // -12 dBFS against -30: 18 dB over, at 20:1 down by 17.1 dB.
            expectWithinAbsoluteError(range, -17.1f, 0.6f);
            expectWithinAbsoluteError(rest, 0.0f, 0.3f);
            s.mode = Mode::wide;
            const auto wide = play(s, in);
            expectWithinAbsoluteError(db(amplitudeAt(wide.left, 200.0, 0.5) / 0.25f),
                                      db(amplitudeAt(wide.left, 3000.0, 0.5) / 0.25f), 0.3f);
            expectLessThan(db(amplitudeAt(wide.left, 200.0, 0.5) / 0.25f), -15.0f);
        }

        beginTest("The detector hears only the range");
        {
            Settings s;
            s.frequency = 5000.0f;
            s.thresholdDb = -30.0f;
            s.ratio = 10.0f;
            s.mode = Mode::wide;
            const auto take = play(s, sine(100.0, 1.0, 0.5f));
            logMessage("100 Hz at -6 dBFS, range at 5 kHz: " + juce::String(db(amplitudeAt(take.left, 100.0, 0.5) / 0.5f), 2)
                       + " dB");
            expectWithinAbsoluteError(db(amplitudeAt(take.left, 100.0, 0.5) / 0.5f), 0.0f, 0.2f);
        }

        beginTest("The range limits how far it goes; the shelves take everything below or above");
        {
            Settings s;
            s.frequency = 1000.0f;
            s.thresholdDb = -40.0f;
            s.ratio = 20.0f;
            s.rangeDb = 6.0f;
            const auto limited = play(s, sine(1000.0, 1.0, 0.5f));
            expectWithinAbsoluteError(db(amplitudeAt(limited.left, 1000.0, 0.5) / 0.5f), -6.0f, 0.3f);

            s.rangeDb = 24.0f;
            s.thresholdDb = -30.0f;
            s.shape = Shape::low;
            s.frequency = 200.0f;
            const auto lows = play(s, add(sine(50.0, 1.0, 0.25f), sine(4000.0, 1.0, 0.25f)));
            const float low = db(amplitudeAt(lows.left, 50.0, 0.5) / 0.25f);
            const float high = db(amplitudeAt(lows.left, 4000.0, 0.5) / 0.25f);
            logMessage("low shelf at 200 Hz: 50 Hz " + juce::String(low, 2) + " dB, 4 kHz " + juce::String(high, 2) + " dB");
            expectLessThan(low, -10.0f);
            expectWithinAbsoluteError(high, 0.0f, 0.2f);

            s.shape = Shape::high;
            s.frequency = 2000.0f;
            const auto highs = play(s, add(sine(100.0, 1.0, 0.25f), sine(8000.0, 1.0, 0.25f)));
            expectWithinAbsoluteError(db(amplitudeAt(highs.left, 100.0, 0.5) / 0.25f), 0.0f, 0.2f);
            expectLessThan(db(amplitudeAt(highs.left, 8000.0, 0.5) / 0.25f), -10.0f);
            // The editor's curve says the same: the shelf down by the gain far from its corner, nothing far on the other side.
            expectWithinAbsoluteError(responseDb(20000.0, s, 0.25f), db(0.25f), 0.1f);
            expectWithinAbsoluteError(responseDb(20.0, s, 0.25f), 0.0f, 0.1f);
        }

        beginTest("Attack and release take their time");
        {
            Settings s;
            s.thresholdDb = -30.0f;
            s.ratio = 20.0f;
            s.attackMs = 10.0f;
            s.releaseMs = 200.0f;
            Signal in = sine(1000.0, 3.0, 0.01f);
            for (std::size_t n = (std::size_t) kRate; n < (std::size_t) (2.0 * kRate); ++n)
                in[n] *= 50.0f;
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            std::vector<float> curve;
            for (std::size_t at = 0; at + 48 <= in.size(); at += 48)
            {
                engine.process(l.data() + at, r.data() + at, nullptr, nullptr, 48, s);
                curve.push_back(engine.reductionNow());
            }
            const float full = curve[1990];
            auto crossing = [&curve](std::size_t from, float level, bool rising) {
                for (std::size_t i = from; i < curve.size(); ++i)
                    if (rising ? curve[i] >= level : curve[i] <= level)
                        return (int) (i - from);
                return -1;
            };
            const int attackMs = crossing(1000, (1.0f - std::exp(-1.0f)) * full, true);
            const int releaseMs = crossing(2000, std::exp(-1.0f) * full, false);
            logMessage("reduction " + juce::String(full, 1) + " dB, 63 % after " + juce::String(attackMs)
                       + " ms, back to 37 % after " + juce::String(releaseMs) + " ms");
            expectWithinAbsoluteError(attackMs, 10, 3);
            expectWithinAbsoluteError(releaseMs, 200, 30);
        }

        beginTest("A key on the side chain ducks the range; without one, nothing happens");
        {
            Settings s;
            s.source = Source::sidechain;
            s.shape = Shape::low;
            s.frequency = 150.0f;
            s.thresholdDb = -30.0f;
            s.ratio = 10.0f;
            s.attackMs = 1.0f;
            s.releaseMs = 50.0f;
            const auto bass = add(sine(60.0, 2.0, 0.25f), sine(2000.0, 2.0, 0.1f));
            Signal key(bass.size(), 0.0f);
            for (std::size_t n = (std::size_t) kRate; n < (std::size_t) (1.2 * kRate); ++n)
                key[n] = 0.8f * (float) std::sin(kTwoPi * 55.0 * (double) n / kRate);
            const auto ducked = play(s, bass, key);
            auto levelAt = [&ducked](double hz, double from, double to) {
                Signal part(ducked.left.begin() + (long) (from * kRate), ducked.left.begin() + (long) (to * kRate));
                return amplitudeAt(part, hz, 0.0);
            };
            const float during = db(levelAt(60.0, 1.05, 1.2) / 0.25f), after = db(levelAt(60.0, 1.6, 2.0) / 0.25f);
            const float growl = db(levelAt(2000.0, 1.05, 1.2) / 0.1f);
            logMessage("bass under the key: 60 Hz " + juce::String(during, 1) + " dB, 2 kHz " + juce::String(growl, 1)
                       + " dB; afterwards " + juce::String(after, 1) + " dB");
            expectLessThan(during, -12.0f);
            expectWithinAbsoluteError(growl, 0.0f, 0.5f);
            expectWithinAbsoluteError(after, 0.0f, 0.3f);
            const auto alone = play(s, bass);
            float worst = 0.0f;
            for (std::size_t n = 0; n < bass.size(); ++n)
                worst = std::max(worst, std::abs(alone.left[n] - bass[n]));
            expectLessThan(worst, 1.0e-6f);

            // Through the plugin with the host's side chain bus.
            ZoneProcessor processor;
            auto layout = processor.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
            expect(processor.setBusesLayout(layout));
            processor.setCurrentProgram(10);
            expectEquals(processor.getProgramName(10), juce::String("Kick-Platz im Bass"));
            processor.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(4, kBlock);
            juce::MidiBuffer midi;
            Signal out(bass.size());
            for (std::size_t at = 0; at + kBlock <= bass.size(); at += kBlock)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    buffer.copyFrom(ch, 0, bass.data() + at, kBlock);
                    buffer.copyFrom(ch + 2, 0, key.data() + at, kBlock);
                }
                processor.processBlock(buffer, midi);
                std::copy_n(buffer.getReadPointer(0), kBlock, out.data() + at);
            }
            expect(processor.keyConnected.load());
            Signal part(out.begin() + (long) (1.05 * kRate), out.begin() + (long) (1.2 * kRate));
            expectLessThan(db(amplitudeAt(part, 60.0, 0.0) / 0.25f), -10.0f);
        }

        beginTest("Hören plays what the detector hears");
        {
            Settings s;
            s.frequency = 3000.0f;
            s.listen = true;
            const auto take = play(s, add(sine(3000.0, 1.0, 0.25f), sine(100.0, 1.0, 0.25f)));
            expectWithinAbsoluteError(db(amplitudeAt(take.left, 3000.0, 0.5) / 0.25f), 0.0f, 0.3f);
            expectLessThan(db(amplitudeAt(take.left, 100.0, 0.5) / 0.25f), -25.0f);
        }

        beginTest("The LFO walks the range across the spectrum");
        {
            // A loud 1 kHz tone, a narrow bell on it moved two octaves either way at 0.5 Hz: the tone is cut while the
            // bell passes it (every second) and left alone while the bell is two octaves away (half a second later).
            Settings s;
            s.frequency = 1000.0f;
            s.widthSemitones = 4.0f;
            s.thresholdDb = -40.0f;
            s.ratio = 20.0f;
            s.attackMs = 0.5f;
            s.releaseMs = 10.0f;
            s.lfoRate = 0.5f;
            s.lfoSemitones = 24.0f;
            const auto take = play(s, sine(1000.0, 4.0, 0.5f));
            for (const double at : { 1.0, 2.0, 3.0 })
            {
                expectLessThan(levelAround(take.left, at, 0.005) - db(0.5f), -12.0f);
                expectWithinAbsoluteError(levelAround(take.left, at + 0.5, 0.005) - db(0.5f), 0.0f, 1.0f);
            }
            // At depth 0 the rate and shape change nothing: the same as the plugin before the LFO, to the bit.
            Settings still = s, plain = s;
            still.lfoSemitones = 0.0f;
            still.lfoShape = LfoShape::random;
            still.lfoRate = 13.0f;
            plain.lfoSemitones = 0.0f;
            const auto source = mixLoop();
            const auto a = play(still, source), b = play(plain, source);
            expect(a.left == b.left && a.right == b.right);
        }

        beginTest("A synced LFO sits where the song is");
        {
            // A rising saw over a whole bar, an octave either way: half a bar in it is at the knob's frequency,
            // three quarters in half an octave up.
            for (const auto& [ppq, expected] : { std::pair { 2.0, 1000.0f }, { 3.0, 1414.2f }, { 6.0, 1000.0f } })
            {
                ZoneProcessor p;
                auto set = [&p](const char* id, float value) {
                    auto* parameter = p.state.getParameter(id);
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
                };
                set("lfoSync", 1.0f);
                set("lfoShape", (float) LfoShape::sawUp);
                set("lfoDivision", (float) tonwerkwobble::kWhole);
                set("lfoDepth", 12.0f);
                StandingHead head(ppq);
                p.setPlayHead(&head);
                p.setPlayConfigDetails(2, 2, kRate, 64);
                p.prepareToPlay(kRate, 64);
                juce::AudioBuffer<float> buffer(2, 64);
                buffer.clear();
                juce::MidiBuffer midi;
                p.processBlock(buffer, midi);
                expectWithinAbsoluteError(p.frequency.load(), expected, expected * 0.01f);
                p.setPlayHead(nullptr);
            }
        }

        beginTest("It runs far inside real time");
        {
            Settings s;
            s.thresholdDb = -40.0f;
            const auto source = mixLoop();
            const auto take = play(s, source);
            const double factor = (double) source.size() / kRate / take.seconds;
            logMessage("zone: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
            s.lfoSemitones = 24.0f;
            const auto moving = play(s, source);
            const double movingFactor = (double) source.size() / kRate / moving.seconds;
            logMessage("zone with the LFO: " + juce::String(movingFactor, 0) + " times faster than real time");
            expectGreaterThan(movingFactor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry mix");
        {
            const auto source = mixLoop();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_ZONE_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                ZoneProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kZoneCategories) >= 0,
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
            ZoneProcessor p;
            auto* frequency = p.state.getParameter("frequency");
            frequency->setValueNotifyingHost(frequency->convertTo0to1(6500.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("frequency"), juce::String("6500 Hz"));
            expectEquals(text("width"), juce::String("12 HT"));
            expectEquals(text("ratio"), juce::String("4:1"));
            expectEquals(text("shape"), juce::String("Glocke"));
            expectEquals(text("range"), juce::String("24 dB"));
            expectEquals(text("lfoDepth"), juce::String("0 HT"));
            expectEquals(text("lfoShape"), juce::String("Sinus"));
            expectEquals(text("lfoDivision"), juce::String("1/4"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ZoneProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().frequency, 6500.0f, 1.0f);
        }

        beginTest("The editor shows the range at work");
        {
            ZoneProcessor p;
            p.setCurrentProgram(4);
            expectEquals(p.getProgramName(4), juce::String::fromUTF8("Härte zähmen"));
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<ZoneEditor*>(created.get());
            expect(editor != nullptr);
            const auto source = mixLoop();
            p.setPlayConfigDetails(2, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(2, 1600);
            juce::MidiBuffer midi;
            for (std::size_t at = 0; at + 1600 <= (std::size_t) (1.55 * kRate); at += 1600)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, source.data() + at, 1600);
                p.processBlock(buffer, midi);
                editor->zone.view.refresh();
            }
            expect(editor->zone.view.caption().startsWith("GLOCKE 3000 HZ"));
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("zone.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static ZoneTests zoneTests;
