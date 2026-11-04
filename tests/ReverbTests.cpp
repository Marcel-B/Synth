#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "ReverbEditor.h"
#include "ReverbEngine.h"
#include "ReverbPresets.h"
#include "ReverbProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkreverb;

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
Take play(const Settings& s, const Signal& left, const Signal& right, double bpm = 120.0)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { left, right };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        engine.process(take.left.data() + at, take.right.data() + at, count, s, bpm);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

Signal sine(double hz, double seconds, double length, float level = 0.3f)
{
    Signal out((std::size_t) (length * kRate), 0.0f);
    for (std::size_t n = 0; n < (std::size_t) (seconds * kRate); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal impulse(double length)
{
    Signal out((std::size_t) (length * kRate), 0.0f);
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

/** The level of one frequency between two times (Goertzel), as an amplitude. */
float toneLevel(const Signal& signal, double hz, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    const double w = kTwoPi * hz / kRate, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t n = a; n < b; ++n)
    {
        const double s0 = signal[n] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float) (2.0 * std::sqrt(std::max(0.0, power)) / (double) std::max<std::size_t>(1, b - a));
}

/** Only the tail, nothing filtered, nothing moved. */
Settings plain(int space = 1)
{
    Settings s;
    s.space = space;
    s.mix = 1.0f;
    s.predelay = 0.0f;
    s.early = 0.0f;
    s.modulation = 0.0f;
    s.damping = 20000.0f;
    s.lowCut = 20.0f;
    s.highCut = 20000.0f;
    s.outputDb = 0.0f;
    return s;
}

/** The time a signal takes to fall by 60 dB, from the slope of its Schroeder integral between -5 and -25 dB. */
double rt60(const Signal& left, const Signal& right)
{
    std::vector<double> energy(left.size());
    double sum = 0.0;
    for (std::size_t n = left.size(); n-- > 0;)
    {
        sum += (double) left[n] * left[n] + (double) right[n] * right[n];
        energy[n] = sum;
    }
    auto when = [&](double db) {
        for (std::size_t n = 0; n < energy.size(); ++n)
            if (10.0 * std::log10(energy[n] / energy[0]) <= db)
                return (double) n / kRate;
        return 0.0;
    };
    return 3.0 * (when(-25.0) - when(-5.0));
}
double rt60(const Take& take) { return rt60(take.left, take.right); }

/** A signal through a 4th-order Butterworth-like split (two RBJ biquads), to measure one band's decay. */
Signal band(const Signal& in, double hz, bool low)
{
    Signal out = in;
    for (int pass = 0; pass < 2; ++pass)
    {
        const double w = kTwoPi * hz / kRate, alpha = std::sin(w) / (2.0 * 0.7071), c = std::cos(w);
        const double b0 = low ? (1.0 - c) / 2.0 : (1.0 + c) / 2.0, b1 = low ? 1.0 - c : -(1.0 + c), b2 = b0;
        const double a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        for (auto& v : out)
        {
            const double y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            v = (float) y;
        }
    }
    return out;
}

/**
 * What a reverb is put on: two seconds of a held chord of saws, then plucked notes, peaking at -6 dBFS, then
 * the tail's room.
 */
Signal padAndPlucks()
{
    Signal out((std::size_t) (7.0 * kRate), 0.0f);
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

Take render(ReverbProcessor& processor, const Signal& source)
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

class ReverbTests : public juce::UnitTest
{
public:
    ReverbTests() : juce::UnitTest("Tonwerk Reverb", "Reverb") {}

    void runTest() override
    {
        beginTest("Every space's tail falls by 60 dB in the decay time");
        {
            for (int space = 0; space < (int) kSpaces.size(); ++space)
                for (const float decay : { 0.8f, 2.5f, 8.0f })
                {
                    auto s = plain(space);
                    s.decay = decay;
                    const auto in = impulse(decay * 2.0 + 1.0);
                    const double measured = rt60(play(s, in, in));
                    logMessage(juce::String(kSpaces[(std::size_t) space].name) + " decay " + juce::String(decay, 1)
                               + " s: measured " + juce::String(measured, 2) + " s");
                    expectWithinAbsoluteError(measured, (double) decay, 0.05 * decay + 0.02);
                }
        }

        beginTest("Bass decay stretches the lows' tail and leaves the rest");
        {
            auto s = plain(1);
            s.decay = 2.0f;
            const auto in = impulse(7.0);
            const auto even = play(s, in, in);
            s.bassTime = 2.0f;
            const auto long_ = play(s, in, in);
            const double lowEven = rt60(band(even.left, 120.0, true), band(even.right, 120.0, true));
            const double lowLong = rt60(band(long_.left, 120.0, true), band(long_.right, 120.0, true));
            const double highEven = rt60(band(even.left, 2000.0, false), band(even.right, 2000.0, false));
            const double highLong = rt60(band(long_.left, 2000.0, false), band(long_.right, 2000.0, false));
            logMessage("below 120 Hz: " + juce::String(lowEven, 2) + " s even, " + juce::String(lowLong, 2)
                       + " s at 200 %; above 2 kHz " + juce::String(highEven, 2) + " s and " + juce::String(highLong, 2) + " s");
            expectGreaterThan(lowLong, 1.6 * lowEven);
            expectWithinAbsoluteError(highLong, highEven, 0.2 * highEven);
        }

        beginTest("Damping takes the highs out of the tail first");
        {
            auto s = plain(1);
            s.decay = 4.0f;
            const auto bright = play(s, impulse(3.0), impulse(3.0));
            s.damping = 2000.0f;
            const auto dark = play(s, impulse(3.0), impulse(3.0));
            auto tilt = [](const Take& take) {
                return toneLevel(take.left, 6000.0, 1.0, 1.5) / toneLevel(take.left, 500.0, 1.0, 1.5);
            };
            logMessage("6 kHz against 500 Hz at 1 s: " + juce::String(juce::Decibels::gainToDecibels(tilt(bright)), 1)
                       + " dB open, " + juce::String(juce::Decibels::gainToDecibels(tilt(dark)), 1) + " dB damped at 2 kHz");
            expectLessThan(tilt(dark), 0.1f * tilt(bright));
        }

        beginTest("Early reflections come before the tail, the plate has none");
        {
            for (int space = 0; space < (int) kSpaces.size(); ++space)
            {
                auto s = plain(space);
                s.decay = 1.0f;
                s.diffusion = 0.0f;
                const auto without = play(s, impulse(0.3), impulse(0.3));
                s.early = 1.0f;
                const auto with = play(s, impulse(0.3), impulse(0.3));
                // The first few milliseconds, before the shortest line comes round.
                const double until = 0.009 * kSpaces[(std::size_t) space].scale * sizeScale(s.size);
                const float a = rms(without.left, 0.0, until), b = rms(with.left, 0.0, until);
                logMessage(juce::String(kSpaces[(std::size_t) space].name) + ": first "
                           + juce::String(until * 1000.0, 1) + " ms " + juce::String(juce::Decibels::gainToDecibels(b, -120.0f), 1)
                           + " dB with reflections, " + juce::String(juce::Decibels::gainToDecibels(a, -120.0f), 1) + " dB without");
                if (kSpaces[(std::size_t) space].earlySpan > 0.0)
                    expectGreaterThan(b, 10.0f * a + 1.0e-4f);
                else
                    expectWithinAbsoluteError(b, a, 1.0e-6f);
            }
        }

        beginTest("Diffusion thickens the onset");
        {
            // The echo density of Abel and Huang: the share of samples above the window's standard deviation, about
            // 0.32 for noise, far less where single echoes stand apart, in 20 ms windows over the tail's first 100 ms.
            auto density = [](const Take& take) {
                double sum = 0.0;
                int windows = 0;
                for (double from = 0.03; from < 0.13; from += 0.02, ++windows)
                {
                    const float deviation = rms(take.left, from, from + 0.02);
                    int above = 0;
                    const auto a = (std::size_t) (from * kRate), b = (std::size_t) ((from + 0.02) * kRate);
                    for (std::size_t n = a; n < b; ++n)
                        above += std::abs(take.left[n]) > deviation ? 1 : 0;
                    sum += (double) above / (double) (b - a) / 0.3173;
                }
                return sum / windows;
            };
            auto s = plain(1);
            s.diffusion = 0.0f;
            const auto sparse = play(s, impulse(0.2), impulse(0.2));
            s.diffusion = 1.0f;
            const auto dense = play(s, impulse(0.2), impulse(0.2));
            logMessage("echo density from 30 to 130 ms (1 is noise): " + juce::String(density(sparse), 2) + " sparse, "
                       + juce::String(density(dense), 2) + " diffused");
            expectGreaterThan(density(dense), 1.5 * density(sparse));
        }

        beginTest("Predelay holds the wet back, also on the tempo");
        {
            auto s = plain(1);
            s.early = 1.0f;
            s.predelay = 0.1f;
            const auto click = play(s, impulse(0.3), impulse(0.3));
            expectLessThan(rms(click.left, 0.0, 0.099), 1.0e-6f);
            expectGreaterThan(rms(click.left, 0.1, 0.2), 1.0e-3f);
            s.sync = true;
            s.division = 2; // an eighth: 250 ms at 120 BPM
            expectWithinAbsoluteError(predelaySeconds(s, 120.0), 0.25, 1.0e-9);
            const auto synced = play(s, impulse(0.5), impulse(0.5), 120.0);
            expectLessThan(rms(synced.left, 0.0, 0.249), 1.0e-6f);
            expectGreaterThan(rms(synced.left, 0.25, 0.35), 1.0e-3f);
        }

        beginTest("Freeze keeps the tail and lets nothing new in");
        {
            auto s = plain(1);
            s.decay = 2.0f;
            s.modulation = 0.3f;
            const auto in = sine(330.0, 0.5, 6.0);
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            engine.process(l.data(), r.data(), 36000, s);
            s.freeze = true;
            for (std::size_t at = 36000; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, (int) std::min<std::size_t>(kBlock, l.size() - at), s);
            const float early = rms(l, 1.0, 1.5), late = rms(l, 5.0, 5.5);
            logMessage("frozen: " + juce::String(juce::Decibels::gainToDecibels(late / early), 1) + " dB over 4 s");
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(late / early), 0.0f, 3.0f);
        }

        beginTest("Ducking takes the wet down while the input plays and gives it back");
        {
            auto s = plain(1);
            s.decay = 3.0f;
            s.mix = 1.0f;
            const auto in = sine(220.0, 1.0, 2.0, 0.5f);
            const auto open = play(s, in, in);
            s.duck = 1.0f;
            s.duckRelease = 0.1f;
            const auto ducked = play(s, in, in);
            const float during = rms(ducked.left, 0.5, 0.9) / rms(open.left, 0.5, 0.9);
            const float after = rms(ducked.left, 1.6, 1.9) / rms(open.left, 1.6, 1.9);
            logMessage("ducked: " + juce::String(juce::Decibels::gainToDecibels(during), 1) + " dB while playing, "
                       + juce::String(juce::Decibels::gainToDecibels(after), 1) + " dB after");
            expectLessThan(during, 0.1f);
            expectGreaterThan(after, 0.7f);
        }

        beginTest("The wet's filters cut the lows and the highs");
        {
            auto s = plain(1);
            s.decay = 2.0f;
            const auto open = play(s, impulse(1.5), impulse(1.5));
            s.lowCut = 500.0f;
            s.highCut = 2000.0f;
            const auto cut = play(s, impulse(1.5), impulse(1.5));
            auto change = [&](double hz) {
                return juce::Decibels::gainToDecibels(toneLevel(cut.left, hz, 0.1, 1.0) / toneLevel(open.left, hz, 0.1, 1.0));
            };
            logMessage("filters: 100 Hz " + juce::String(change(100.0), 1) + " dB, 1 kHz " + juce::String(change(1000.0), 1)
                       + " dB, 8 kHz " + juce::String(change(8000.0), 1) + " dB");
            expectLessThan(change(100.0), -20.0f);
            expectLessThan(change(8000.0), -15.0f);
            expectGreaterThan(change(1000.0), -6.0f);
        }

        beginTest("A change of space fades over, without a click");
        {
            auto s = plain(1);
            s.mix = 0.5f;
            const auto in = sine(440.0, 3.0, 3.0, 0.3f);
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            float jump = 0.0f;
            for (std::size_t at = 0; at < l.size(); at += kBlock)
            {
                s.space = at < (std::size_t) kRate * 3 / 2 ? 1 : 2;
                engine.process(l.data() + at, r.data() + at, (int) std::min<std::size_t>(kBlock, l.size() - at), s);
            }
            for (std::size_t n = (std::size_t) (1.4 * kRate); n < (std::size_t) (1.7 * kRate); ++n)
                jump = std::max(jump, std::abs(l[n] - l[n - 1]));
            // A 440 Hz sine of 0.3 moves by about 0.017 per sample; the wet adds to that, a click would add far more.
            logMessage("largest step around the change: " + juce::String(jump, 3));
            expectLessThan(jump, 0.1f);
            expectGreaterThan(rms(l, 2.5, 3.0), 0.1f);
        }

        beginTest("Loud noise into the longest tail stays bounded in every space");
        {
            juce::Random random(5);
            Signal noise((std::size_t) (6.0 * kRate));
            for (auto& v : noise)
                v = 0.9f * (random.nextFloat() * 2.0f - 1.0f);
            for (int space = 0; space < (int) kSpaces.size(); ++space)
            {
                auto s = plain(space);
                s.decay = 20.0f;
                s.bassTime = 2.5f;
                s.modulation = 1.0f;
                s.size = 1.0f;
                s.early = 1.0f;
                const auto take = play(s, noise, noise);
                float peak = 0.0f;
                bool finite = true;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                {
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                logMessage(juce::String(kSpaces[(std::size_t) space].name) + ", noise, decay 20 s, bass 250 %: peak "
                           + juce::String(peak, 2));
                expect(finite);
                expectLessThan(peak, 4.0f);
            }
        }

        beginTest("The sides differ, width 0 joins them");
        {
            auto s = plain(1);
            s.decay = 2.0f;
            const auto take = play(s, impulse(2.0), impulse(2.0));
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (std::size_t n = 2400; n < take.left.size(); ++n)
            {
                lr += (double) take.left[n] * take.right[n];
                ll += (double) take.left[n] * take.left[n];
                rr += (double) take.right[n] * take.right[n];
            }
            const double correlation = lr / std::sqrt(ll * rr);
            logMessage("correlation of the sides: " + juce::String(correlation, 2));
            expectLessThan(std::abs(correlation), 0.3);
            s.width = 0.0f;
            const auto narrow = play(s, impulse(1.0), impulse(1.0));
            for (std::size_t n = 0; n < narrow.left.size(); n += 101)
                expectWithinAbsoluteError(narrow.left[n], narrow.right[n], 1.0e-6f);
        }

        beginTest("Mix 0 passes the dry untouched");
        {
            auto s = plain(1);
            s.mix = 0.0f;
            const auto in = sine(440.0, 0.5, 0.5);
            const auto take = play(s, in, in);
            for (std::size_t n = 0; n < in.size(); n += 37)
                expectEquals(take.left[n], in[n]);
        }

        beginTest("It runs far inside real time");
        {
            auto s = plain(1);
            s.modulation = 0.5f;
            s.damping = 6000.0f;
            s.bassTime = 1.3f;
            s.early = 0.5f;
            s.lowCut = 100.0f;
            s.highCut = 10000.0f;
            s.duck = 0.5f;
            s.predelay = 0.05f;
            const auto source = padAndPlucks();
            Signal longer;
            for (int i = 0; i < 3; ++i)
                longer.insert(longer.end(), source.begin(), source.end());
            const auto take = play(s, longer, longer);
            const double factor = (double) longer.size() / kRate / take.seconds;
            logMessage("reverb: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry source");
        {
            const auto source = padAndPlucks();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_REVERB_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                ReverbProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kReverbCategories) >= 0,
                       "no group: " + sound);
                expect(! processor.readSettings().freeze, sound);
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
            ReverbProcessor p;
            auto* decay = p.state.getParameter("decay");
            decay->setValueNotifyingHost(decay->convertTo0to1(12.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("decay"), juce::String("12 s"));
            expectEquals(text("predelay"), juce::String("20 ms"));
            expectEquals(text("damping"), juce::String("9000 Hz"));
            expectEquals(text("bassTime"), juce::String("100 %"));
            expectEquals(text("space"), juce::String("Saal"));
            expectEquals(text("division"), juce::String("1/16"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ReverbProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().decay, 12.0f, 0.01f);
        }

        beginTest("The editor shows the space and its settings");
        {
            ReverbProcessor p;
            p.setCurrentProgram(5);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<ReverbEditor>(created.get());
            expect(editor != nullptr);
            render(p, padAndPlucks());
            editor->space.view.refresh();
            expectEquals(editor->space.view.caption(), juce::String::fromUTF8("SAAL · DECAY 2.8 S · PREDELAY 25 MS"));
            expectGreaterThan(editor->getWidth(), 500);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("reverb.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static ReverbTests reverbTests;
