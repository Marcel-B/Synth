#include "Loudness.h"
#include "MultibandEditor.h"
#include "MultibandEngine.h"
#include "MultibandPresets.h"
#include "MultibandProcessor.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <set>

using namespace tonwerkmultiband;

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
Take play(const Settings& s, const Signal& in)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { in, in };
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < in.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, in.size() - at);
        engine.process(take.left.data() + at, take.right.data() + at, count, s);
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

/** Settings that leave every band alone: the crossovers and allpasses still run. */
Settings neutral()
{
    Settings s;
    for (auto& band : s.bands)
        band.on = false;
    return s;
}

/**
 * What a multiband compressor is put on: four bars at 120 BPM of a kick on every beat, a snare on two and four, a
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
Take render(MultibandProcessor& processor, const Signal& source)
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

class MultibandTests : public juce::UnitTest
{
public:
    MultibandTests() : juce::UnitTest("Tonwerk Multiband", "Multiband") {}

    void runTest() override
    {
        beginTest("Left alone, the three bands add up to a flat response, and dry and wet do not comb");
        {
            // An impulse's spectrum after the crossovers: flat within a tenth of a dB from 20 Hz to 20 kHz.
            constexpr int order = 15, size = 1 << order;
            Signal impulse(size, 0.0f);
            impulse[0] = 1.0f;
            auto spectrumOf = [&](const Settings& s) {
                const auto take = play(s, impulse);
                std::vector<float> fft(2 * size, 0.0f);
                std::copy(take.left.begin(), take.left.end(), fft.begin());
                juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(fft.data());
                return fft;
            };
            auto s = neutral();
            float lowest = 0.0f, highest = 0.0f;
            const auto flat = spectrumOf(s);
            for (int k = (int) (20.0 * size / kRate); k < (int) (20000.0 * size / kRate); ++k)
            {
                lowest = std::min(lowest, db(flat[(std::size_t) k]));
                highest = std::max(highest, db(flat[(std::size_t) k]));
            }
            logMessage("bands summed: " + juce::String(lowest, 3) + " to " + juce::String(highest, 3) + " dB");
            expectWithinAbsoluteError(lowest, 0.0f, 0.1f);
            expectWithinAbsoluteError(highest, 0.0f, 0.1f);
            // Half dry, half wet, with a band turned down: no notch at the crossovers, only the band's own dip.
            s.mix = 0.5f;
            s.bands[1].on = true;
            s.bands[1].thresholdDb = 0.0f;
            s.bands[1].gainDb = -6.0f;
            const auto mixed = spectrumOf(s);
            for (const float hz : { s.lowSplit, s.highSplit })
            {
                const float level = db(mixed[(std::size_t) (hz * size / kRate)]);
                logMessage("mix 50 %, mid -6 dB: " + juce::String(level, 2) + " dB at " + juce::String(hz) + " Hz");
                expectGreaterThan(level, -3.0f);
            }
        }

        beginTest("Each band's gain moves only its own range");
        {
            const auto in = add(add(sine(60.0, 1.0, 0.1f), sine(700.0, 1.0, 0.1f)), sine(8000.0, 1.0, 0.1f));
            for (std::size_t b = 0; b < kBands; ++b)
            {
                auto s = neutral();
                s.bands[b].on = true;
                s.bands[b].thresholdDb = 0.0f;
                s.bands[b].gainDb = -12.0f;
                const auto take = play(s, in);
                const float levels[] = { db(amplitudeAt(take.left, 60.0, 0.5) / 0.1f),
                                         db(amplitudeAt(take.left, 700.0, 0.5) / 0.1f),
                                         db(amplitudeAt(take.left, 8000.0, 0.5) / 0.1f) };
                logMessage(juce::String(kBandNames[b]) + " -12 dB: 60 Hz " + juce::String(levels[0], 2) + ", 700 Hz "
                           + juce::String(levels[1], 2) + ", 8 kHz " + juce::String(levels[2], 2) + " dB");
                for (std::size_t k = 0; k < kBands; ++k)
                    expectWithinAbsoluteError(levels[k], k == b ? -12.0f : 0.0f, 0.5f);
            }
        }

        beginTest("Above the threshold a band falls by its ratio; below, upward lifts it");
        {
            auto s = neutral();
            auto& mid = s.bands[1];
            mid.on = true;
            mid.thresholdDb = -30.0f;
            mid.attackMs = 1.0f;
            // Alone, so the neighbours' share of the tone (their crossovers fall 24 dB an octave) does not count.
            mid.solo = true;
            for (const float ratio : { 2.0f, 4.0f, 10.0f })
            {
                mid.ratio = ratio;
                const auto take = play(s, sine(700.0, 1.0, 0.5f));
                const float out = db(amplitudeAt(take.left, 700.0, 0.6));
                const float expected = -30.0f + (db(0.5f) + 30.0f) / ratio;
                logMessage(juce::String(ratio, 0) + ":1, -6 dBFS in: " + juce::String(out, 2) + " dBFS out, expected "
                           + juce::String(expected, 2));
                expectWithinAbsoluteError(out, expected, 0.6f);
            }
            // Below it, nothing changes without upward; with 50 % a sine 20 dB under comes up by 10.
            mid.ratio = 4.0f;
            mid.thresholdDb = -20.0f;
            const auto quiet = play(s, sine(700.0, 1.5, 0.01f));
            expectWithinAbsoluteError(db(amplitudeAt(quiet.left, 700.0, 1.0)), -40.0f, 0.3f);
            mid.upward = 0.5f;
            const auto lifted = play(s, sine(700.0, 1.5, 0.01f));
            logMessage("-40 dBFS with 50 % upward under -20: " + juce::String(db(amplitudeAt(lifted.left, 700.0, 1.0)), 2)
                       + " dBFS");
            expectWithinAbsoluteError(db(amplitudeAt(lifted.left, 700.0, 1.0)), -30.0f, 0.6f);
            // Never more than 18 dB, and nothing under the floor: hiss stays hiss.
            mid.upward = 1.0f;
            mid.thresholdDb = 0.0f;
            const auto most = play(s, sine(700.0, 1.5, 0.01f));
            expectWithinAbsoluteError(db(amplitudeAt(most.left, 700.0, 1.0)), -40.0f + kMaxBoostDb, 0.6f);
            const auto hiss = play(s, sine(700.0, 1.5, 0.0001f));
            expectWithinAbsoluteError(db(amplitudeAt(hiss.left, 700.0, 1.0)), -80.0f, 0.5f);
            // Depth scales it all: half the depth, half the dB.
            s.depth = 0.5f;
            const auto half = play(s, sine(700.0, 1.5, 0.01f));
            expectWithinAbsoluteError(db(amplitudeAt(half.left, 700.0, 1.0)), -40.0f + 0.5f * kMaxBoostDb, 0.6f);
        }

        beginTest("Attack and release take their time");
        {
            auto s = neutral();
            auto& mid = s.bands[1];
            mid.on = true;
            mid.thresholdDb = -30.0f;
            mid.ratio = 20.0f;
            mid.attackMs = 10.0f;
            mid.releaseMs = 200.0f;
            Signal in = sine(1000.0, 3.0, 0.01f);
            for (std::size_t n = (std::size_t) kRate; n < (std::size_t) (2.0 * kRate); ++n)
                in[n] *= 50.0f;
            Engine engine;
            engine.prepare(kRate);
            Signal l = in, r = in;
            std::vector<float> curve;
            for (std::size_t at = 0; at + 48 <= in.size(); at += 48)
            {
                engine.process(l.data() + at, r.data() + at, 48, s);
                curve.push_back(engine.changeNow(1));
            }
            const float full = curve[1990];
            auto crossing = [&curve](std::size_t from, float level, bool falling) {
                for (std::size_t i = from; i < curve.size(); ++i)
                    if (falling ? curve[i] <= level : curve[i] >= level)
                        return (int) (i - from);
                return -1;
            };
            const int attackMs = crossing(1000, (1.0f - std::exp(-1.0f)) * full, true);
            const int releaseMs = crossing(2000, std::exp(-1.0f) * full, false);
            logMessage("change " + juce::String(full, 1) + " dB, 63 % after " + juce::String(attackMs)
                       + " ms, back to 37 % after " + juce::String(releaseMs) + " ms");
            expectWithinAbsoluteError(attackMs, 10, 3);
            expectWithinAbsoluteError(releaseMs, 200, 30);
        }

        beginTest("Solo plays only the chosen bands");
        {
            const auto in = add(sine(60.0, 1.0, 0.2f), sine(6000.0, 1.0, 0.2f));
            auto s = neutral();
            s.bands[2].solo = true;
            const auto take = play(s, in);
            expectLessThan(db(amplitudeAt(take.left, 60.0, 0.5) / 0.2f), -40.0f);
            expectWithinAbsoluteError(db(amplitudeAt(take.left, 6000.0, 0.5) / 0.2f), 0.0f, 0.3f);
        }

        beginTest("It runs far inside real time");
        {
            Settings s;
            for (auto& band : s.bands)
                band.upward = 0.5f;
            const auto source = mixLoop();
            const auto take = play(s, source);
            const double factor = (double) source.size() / kRate / take.seconds;
            logMessage("multiband: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 30.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry mix");
        {
            const auto source = mixLoop();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_MULTIBAND_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                MultibandProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kMultibandCategories) >= 0,
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
            MultibandProcessor p;
            auto* threshold = p.state.getParameter("midThreshold");
            threshold->setValueNotifyingHost(threshold->convertTo0to1(-18.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("midThreshold"), juce::String("-18 dB"));
            expectEquals(text("lowRatio"), juce::String("3:1"));
            expectEquals(text("lowSplit"), juce::String("200 Hz"));
            expectEquals(text("highAttack"), juce::String("5 ms"));
            expectEquals(text("depth"), juce::String("100 %"));
            expectEquals(ratioText(1.5f), juce::String("1.5:1"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            MultibandProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().bands[1].thresholdDb, -18.0f, 0.01f);
        }

        beginTest("The editor shows the bands at work");
        {
            MultibandProcessor p;
            p.setCurrentProgram(5);
            expectEquals(p.getProgramName(5), juce::String("OTT"));
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<MultibandEditor*>(created.get());
            expect(editor != nullptr);
            const auto source = mixLoop();
            p.setPlayConfigDetails(2, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(2, 1600);
            juce::MidiBuffer midi;
            for (std::size_t at = 0; at + 1600 <= (std::size_t) (2.1 * kRate); at += 1600)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, source.data() + at, 1600);
                p.processBlock(buffer, midi);
                editor->bands.view.refresh();
            }
            expect(editor->bands.view.caption().startsWith("TIEF "));
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("multiband.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static MultibandTests multibandTests;
