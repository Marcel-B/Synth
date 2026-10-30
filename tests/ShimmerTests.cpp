#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "ShimmerEditor.h"
#include "ShimmerEngine.h"
#include "ShimmerPresets.h"
#include "ShimmerProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkshimmer;

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
Take play(const Settings& s, const Signal& left, const Signal& right)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { left, right };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        engine.process(take.left.data() + at, take.right.data() + at, count, s);
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

/** Only the tail, nothing filtered, nothing shifted or moved. */
Settings plain()
{
    Settings s;
    s.mix = 1.0f;
    s.predelay = 0.0f;
    s.shimmer = 0.0f;
    s.modulation = 0.0f;
    s.damping = 20000.0f;
    s.lowCut = 20.0f;
    s.outputDb = 0.0f;
    return s;
}

/** The time the tail takes to fall by 60 dB, from the slope of its Schroeder integral between -5 and -25 dB. */
double rt60(const Take& take)
{
    std::vector<double> energy(take.left.size());
    double sum = 0.0;
    for (std::size_t n = take.left.size(); n-- > 0;)
    {
        sum += (double) take.left[n] * take.left[n] + (double) take.right[n] * take.right[n];
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

Take render(ShimmerProcessor& processor, const Signal& source)
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

class ShimmerTests : public juce::UnitTest
{
public:
    ShimmerTests() : juce::UnitTest("Tonwerk Shimmer", "Shimmer") {}

    void runTest() override
    {
        beginTest("The tail falls by 60 dB in the decay time");
        {
            for (const float decay : { 1.0f, 3.0f, 8.0f })
            {
                auto s = plain();
                s.decay = decay;
                const auto in = impulse(decay * 2.0 + 1.0);
                const double measured = rt60(play(s, in, in));
                logMessage("decay " + juce::String(decay, 1) + " s: measured " + juce::String(measured, 2) + " s");
                expectWithinAbsoluteError(measured, (double) decay, 0.1 * decay);
            }
        }

        beginTest("Damping takes the highs out of the tail first");
        {
            auto s = plain();
            s.decay = 4.0f;
            const auto bright = play(s, impulse(3.0), impulse(3.0));
            s.damping = 2000.0f;
            const auto dark = play(s, impulse(3.0), impulse(3.0));
            // The share of 6 kHz against 500 Hz a second into the tail.
            auto tilt = [](const Take& take) {
                return toneLevel(take.left, 6000.0, 1.0, 1.5) / toneLevel(take.left, 500.0, 1.0, 1.5);
            };
            logMessage("6 kHz against 500 Hz at 1 s: " + juce::String(juce::Decibels::gainToDecibels(tilt(bright)), 1)
                       + " dB open, " + juce::String(juce::Decibels::gainToDecibels(tilt(dark)), 1) + " dB damped at 2 kHz");
            expectLessThan(tilt(dark), 0.1f * tilt(bright));
        }

        beginTest("The shimmer lifts the tail by its interval");
        {
            auto s = plain();
            s.decay = 6.0f;
            s.damping = 12000.0f;
            const auto in = sine(440.0, 0.5, 4.0);
            const auto none = play(s, in, in);
            s.shimmer = 0.7f;
            const auto octave = play(s, in, in);
            s.interval = 1;
            const auto fifth = play(s, in, in);
            auto share = [](const Take& take, double hz) {
                return juce::Decibels::gainToDecibels(toneLevel(take.left, hz, 1.5, 3.0) / toneLevel(take.left, 440.0, 1.5, 3.0));
            };
            logMessage("880 Hz against 440 Hz in the tail: " + juce::String(share(none, 880.0), 1) + " dB without, "
                       + juce::String(share(octave, 880.0), 1) + " dB with the octave; 659 Hz with the fifth "
                       + juce::String(share(fifth, 659.26), 1) + " dB, without " + juce::String(share(none, 659.26), 1)
                       + " dB");
            expectGreaterThan(share(octave, 880.0), share(none, 880.0) + 20.0f);
            expectGreaterThan(share(fifth, 659.26), share(none, 659.26) + 20.0f);
            // An octave down lowers it.
            s.interval = 5;
            const auto down = play(s, in, in);
            expectGreaterThan(share(down, 220.0), share(none, 220.0) + 20.0f);
        }

        beginTest("Loud noise into the longest tail with full shimmer stays bounded");
        {
            auto s = plain();
            s.decay = 30.0f;
            s.shimmer = 1.0f;
            s.modulation = 1.0f;
            s.size = 1.0f;
            juce::Random random(5);
            Signal noise((std::size_t) (10.0 * kRate));
            for (auto& v : noise)
                v = 0.9f * (random.nextFloat() * 2.0f - 1.0f);
            const auto take = play(s, noise, noise);
            float peak = 0.0f;
            bool finite = true;
            for (std::size_t n = 0; n < take.left.size(); ++n)
            {
                finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
            }
            logMessage("noise, decay 30 s, full shimmer: peak " + juce::String(peak, 2));
            expect(finite);
            expectLessThan(peak, 4.0f);
        }

        beginTest("Hold keeps the tail and lets nothing new in; the predelay holds the tail back");
        {
            auto s = plain();
            s.decay = 2.0f;
            const auto in = sine(330.0, 0.5, 6.0);
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            engine.process(l.data(), r.data(), 36000, s);
            s.hold = true;
            for (std::size_t at = 36000; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, (int) std::min<std::size_t>(kBlock, l.size() - at), s);
            const float early = rms(l, 1.0, 1.5), late = rms(l, 5.0, 5.5);
            logMessage("held: " + juce::String(juce::Decibels::gainToDecibels(late / early), 1) + " dB over 4 s");
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(late / early), 0.0f, 3.0f);

            auto delayed = plain();
            delayed.predelay = 0.1f;
            const auto click = play(delayed, impulse(0.3), impulse(0.3));
            expectLessThan(rms(click.left, 0.0, 0.099), 1.0e-6f);
            expectGreaterThan(rms(click.left, 0.1, 0.2), 1.0e-3f);
        }

        beginTest("The sides differ, width 0 joins them");
        {
            auto s = plain();
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

        beginTest("It runs far inside real time");
        {
            auto s = plain();
            s.shimmer = 0.6f;
            s.modulation = 0.5f;
            s.damping = 6000.0f;
            s.lowCut = 100.0f;
            s.predelay = 0.05f;
            const auto source = padAndPlucks();
            Signal longer;
            for (int i = 0; i < 3; ++i)
                longer.insert(longer.end(), source.begin(), source.end());
            const auto take = play(s, longer, longer);
            const double factor = (double) longer.size() / kRate / take.seconds;
            logMessage("shimmer: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry source");
        {
            const auto source = padAndPlucks();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_SHIMMER_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                ShimmerProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kShimmerCategories) >= 0,
                       "no group: " + sound);
                expect(! processor.readSettings().hold, sound);
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
            ShimmerProcessor p;
            auto* decay = p.state.getParameter("decay");
            decay->setValueNotifyingHost(decay->convertTo0to1(12.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("decay"), juce::String("12 s"));
            expectEquals(text("predelay"), juce::String("20 ms"));
            expectEquals(text("damping"), juce::String("7000 Hz"));
            expectEquals(text("shimmer"), juce::String("50 %"));
            expectEquals(text("interval"), juce::String("Oktave"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ShimmerProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().decay, 12.0f, 0.01f);
        }

        beginTest("The editor shows the tail and its settings");
        {
            ShimmerProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<ShimmerEditor>(created.get());
            expect(editor != nullptr);
            render(p, padAndPlucks());
            editor->tail.view.refresh();
            expectEquals(editor->tail.view.caption(), juce::String::fromUTF8("DECAY 6.0 S · PREDELAY 20 MS · OKTAVE 50 %"));
            expectGreaterThan(editor->getWidth(), 500);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("shimmer.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static ShimmerTests shimmerTests;
