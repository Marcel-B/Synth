#include "FilterEditor.h"
#include "FilterEngine.h"
#include "FilterPresets.h"
#include "FilterProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkfilter;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kBpm = 120.0;

using Signal = std::vector<float>;

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

Signal sine(double hz, double seconds, float level = 0.3f)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

/** The amplitude of `hz` in the second half of `signal` (Goertzel). */
float amplitudeAt(const Signal& signal, double hz)
{
    const std::size_t from = signal.size() / 2;
    const double w = kTwoPi * hz / kRate, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t n = from; n < signal.size(); ++n)
    {
        const double s0 = signal[n] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float) (2.0 * std::sqrt(std::max(0.0, power)) / (double) (signal.size() - from));
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

/** Both channels of `input` through the engine alone, in host-sized blocks, the LFO free from phase 0. */
Take play(const Settings& s, const Signal& input, Engine* engineOut = nullptr)
{
    Engine local;
    Engine& engine = engineOut != nullptr ? *engineOut : local;
    engine.prepare((float) kRate);
    Take take { input, input };
    const double increment = s.rate / kRate;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < input.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, input.size() - at);
        float* channels[] = { take.left.data() + at, take.right.data() + at };
        engine.process(channels, 2, count, s, { (double) at * increment, increment });
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

/** The gain of `type` at `hz` with the cutoff at 1 kHz, in dB, from a sine through the engine. */
float gainAt(Type type, float hz, float resonance = 0.0f)
{
    Settings s;
    s.type = type;
    s.cutoff = 1000.0f;
    s.resonance = resonance;
    const auto take = play(s, sine(hz, 0.5, 0.05f));
    return decibels(amplitudeAt(take.left, hz) / 0.05f);
}

/**
 * What a filter is put on: a bass line of saws in eighths at 120 BPM under a held chord, peaking at -6 dBFS, four
 * seconds.
 */
Signal bassAndChord()
{
    Signal out((std::size_t) (4.0 * kRate), 0.0f);
    auto saw = [](double hz, double t) {
        double v = 0.0;
        for (int k = 1; k * hz < 10000.0; ++k)
            v += std::sin(kTwoPi * k * hz * t) / k;
        return v;
    };
    for (const int note : { 57, 60, 64 })
    {
        const double hz = 440.0 * std::exp2((note - 69) / 12.0);
        for (std::size_t n = 0; n < out.size(); ++n)
        {
            const double t = (double) n / kRate;
            out[n] += (float) (0.12 * saw(hz, t) * std::min(1.0, t / 0.05));
        }
    }
    const int line[] = { 33, 33, 45, 33, 36, 33, 43, 31 };
    for (int i = 0; i < 32; ++i)
    {
        const double hz = 440.0 * std::exp2((line[i % 8] - 69) / 12.0);
        const auto start = (std::size_t) (0.25 * i * kRate);
        for (std::size_t n = 0; n < (std::size_t) (0.22 * kRate) && start + n < out.size(); ++n)
        {
            const double t = (double) n / kRate;
            out[start + n] += (float) (0.5 * saw(hz, t) * std::min(1.0, t / 0.003) * std::exp(-t / 0.15));
        }
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

/** Through the processor with the host playing at 120 BPM from bar 1; `cutoffs` collects the left cutoff per block. */
Take render(FilterProcessor& processor, const Signal& source, std::vector<float>* cutoffs = nullptr)
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
        head.ppq += count * kBpm / 60.0 / kRate;
        if (cutoffs != nullptr)
            cutoffs->push_back(processor.cutoffNow.load());
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return take;
}

void set(FilterProcessor& processor, const char* id, float value)
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

juce::String nameOf(Type type) { return juce::String::fromUTF8(kTypeNames[(std::size_t) type]); }
} // namespace

class FilterTests : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest("Tonwerk Filter", "Filter") {}

    void runTest() override
    {
        beginTest("Each type passes and stops what it should, two octaves from the cutoff");
        {
            struct Expected
            {
                Type type;
                float below, at, above; // dB at 250 Hz, 1 kHz, 4 kHz; the tolerance is 3 dB
            };
            const Expected expected[] = {
                { Type::lowpass12, 0.0f, -3.0f, -24.0f }, { Type::lowpass24, 0.0f, -6.0f, -48.0f },
                { Type::highpass12, -24.0f, -3.0f, 0.0f }, { Type::highpass24, -48.0f, -6.0f, 0.0f },
                { Type::bandpass, -9.0f, 0.0f, -9.0f },
            };
            for (const auto& e : expected)
            {
                const float below = gainAt(e.type, 250.0f), at = gainAt(e.type, 1000.0f), above = gainAt(e.type, 4000.0f);
                logMessage(nameOf(e.type).paddedRight(' ', 12) + juce::String(below, 1) + " / " + juce::String(at, 1)
                           + " / " + juce::String(above, 1) + " dB at 250 Hz / 1 kHz / 4 kHz");
                expectWithinAbsoluteError(below, e.below, 3.0f, nameOf(e.type));
                expectWithinAbsoluteError(at, e.at, 3.0f, nameOf(e.type));
                expectWithinAbsoluteError(above, e.above, 3.0f, nameOf(e.type));
            }
            // The notch cuts its frequency deep and leaves the rest.
            expectLessThan(gainAt(Type::notch, 1000.0f), -30.0f);
            expectWithinAbsoluteError(gainAt(Type::notch, 250.0f), 0.0f, 1.0f);
            expectWithinAbsoluteError(gainAt(Type::notch, 4000.0f), 0.0f, 1.0f);
            // The ladder: flat below, 24 dB an octave above.
            const float ladderBelow = gainAt(Type::ladder, 250.0f), ladderAbove = gainAt(Type::ladder, 4000.0f);
            logMessage("Ladder 24   " + juce::String(ladderBelow, 1) + " dB at 250 Hz, " + juce::String(ladderAbove, 1)
                       + " dB at 4 kHz");
            expectWithinAbsoluteError(ladderBelow, 0.0f, 1.5f);
            expectLessThan(ladderAbove, -40.0f);
        }

        beginTest("Resonance makes a peak at the cutoff; the ladder sings on at the top");
        {
            for (const auto type : { Type::lowpass12, Type::lowpass24, Type::ladder })
            {
                const float flat = gainAt(type, 1000.0f, 0.0f), peaked = gainAt(type, 1000.0f, 0.9f);
                logMessage(nameOf(type).paddedRight(' ', 12) + "at the cutoff: " + juce::String(flat, 1) + " dB, at 90 % "
                           + juce::String(peaked, 1) + " dB");
                expectGreaterThan(peaked - flat, 12.0f, nameOf(type));
            }
            // A click into the ladder at full resonance keeps ringing near the cutoff.
            Settings s;
            s.type = Type::ladder;
            s.cutoff = 500.0f;
            s.resonance = 1.0f;
            Signal click((std::size_t) kRate, 0.0f);
            click[10] = 0.5f;
            const auto take = play(s, click);
            const float late = rms(take.left, (std::size_t) (0.8 * kRate), (std::size_t) kRate);
            logMessage("Ladder at 100 %: " + juce::String(decibels(late), 1) + " dBFS RMS 0.8 s after a click");
            expectGreaterThan(decibels(late), -40.0f);
            expect(std::isfinite(late) && late < 1.5f);
        }

        beginTest("The comb peaks on the cutoff's harmonics and dips between them");
        {
            Settings s;
            s.type = Type::comb;
            s.cutoff = 200.0f;
            s.resonance = 0.8f;
            auto gain = [&](double hz) { return decibels(amplitudeAt(play(s, sine(hz, 0.5, 0.05f)).left, hz) / 0.05f); };
            const float onTooth = gain(600.0), between = gain(700.0);
            logMessage("Comb at 200 Hz: " + juce::String(onTooth, 1) + " dB at 600 Hz, " + juce::String(between, 1)
                       + " dB at 700 Hz");
            expectGreaterThan(onTooth - between, 15.0f);
        }

        beginTest("The envelope opens the filter as the note hits and lets it close");
        {
            Settings s;
            s.cutoff = 200.0f;
            s.envelopeAmount = 1.0f;
            s.attackMs = 5.0f;
            s.releaseMs = 100.0f;
            Signal input = sine(110.0, 0.5, 0.5f);
            input.resize((std::size_t) kRate, 0.0f);
            Engine engine;
            engine.prepare((float) kRate);
            std::vector<float> cutoffs;
            Take take { input, input };
            for (std::size_t at = 0; at + kBlock <= input.size(); at += kBlock)
            {
                float* channels[] = { take.left.data() + at, take.right.data() + at };
                engine.process(channels, 2, kBlock, s, {});
                cutoffs.push_back(engine.channel(0).cutoffNow());
            }
            const float during = cutoffs[(std::size_t) (0.4 * kRate / kBlock)];
            const float after = cutoffs.back();
            logMessage("Cutoff at 200 Hz, envelope 100 %: " + juce::String(during, 0) + " Hz during a note at -6 dBFS, "
                       + juce::String(after, 0) + " Hz half a second after it");
            // Half of full scale is half the envelope's 6 octaves: 3 octaves up, 1600 Hz, less the follower's sag
            // between the sine's peaks.
            expect(during > 1200.0f && during < 1700.0f);
            expectWithinAbsoluteError(after, 200.0f, 10.0f);

            // Negative closes, and the sensitivity reaches the top with a quiet track.
            s.envelopeAmount = -0.5f;
            s.cutoff = 4000.0f;
            s.sensitivityDb = 6.0f;
            engine.prepare((float) kRate);
            Take again { input, input };
            for (std::size_t at = 0; at < (std::size_t) (0.4 * kRate); at += kBlock)
            {
                float* channels[] = { again.left.data() + at, again.right.data() + at };
                engine.process(channels, 2, kBlock, s, {});
            }
            // Half of -6 octaves at full level, again less the sag: down by about two and a half.
            expect(engine.channel(0).cutoffNow() > 500.0f && engine.channel(0).cutoffNow() < 750.0f);
        }

        beginTest("A synced LFO sits on the song's beat; the spread moves the right side apart");
        {
            FilterProcessor processor;
            set(processor, "cutoff", 1000.0f);
            set(processor, "lfoShape", (float) Shape::square);
            set(processor, "lfoSync", 1.0f);
            set(processor, "lfoDivision", (float) tonwerkwobble::kQuarter);
            set(processor, "lfoAmount", 0.5f);
            std::vector<float> cutoffs;
            render(processor, sine(220.0, 2.0), &cutoffs);
            // A quarter at 120 BPM is half a second: the first half of each beat up two octaves, the second down two.
            auto at = [&](double seconds) { return cutoffs[(std::size_t) (seconds * kRate / kBlock)]; };
            logMessage("Square on quarters: " + juce::String(at(0.6), 0) + " Hz early in a beat, " + juce::String(at(0.85), 0)
                       + " Hz late");
            expectWithinAbsoluteError(at(0.6), 4000.0f, 100.0f);
            expectWithinAbsoluteError(at(0.85), 250.0f, 10.0f);

            Settings s;
            s.lfoAmount = 0.5f;
            s.rate = 1.0f;
            s.spreadDegrees = 180.0f;
            Engine engine;
            play(s, sine(220.0, 0.25), &engine);
            const float left = engine.channel(0).cutoffNow(), right = engine.channel(1).cutoffNow();
            // Opposite phases of a sine: the two cutoffs lie as far above 1 kHz as below it, in octaves.
            expectWithinAbsoluteError(std::log2(left / 1000.0f), -std::log2(right / 1000.0f), 0.05f);
            expectGreaterThan(std::abs(std::log2(left / right)), 1.0f);
        }

        beginTest("A square or random LFO steps without clicking");
        {
            for (const auto shape : { Shape::square, Shape::random })
            {
                Settings s;
                s.type = Type::lowpass12;
                s.shape = shape;
                s.rate = 8.0f;
                s.lfoAmount = 1.0f;
                const auto input = sine(220.0, 1.0, 0.5f);
                const auto take = play(s, input);
                float worst = 0.0f;
                for (std::size_t n = 1; n < take.left.size(); ++n)
                    worst = std::max(worst, std::abs(take.left[n] - take.left[n - 1]));
                // A 220 Hz sine at 0.5 moves at most 0.0144 a sample; a click would be many times that.
                expectLessThan(worst, 0.03f, juce::String((int) shape));
            }
        }

        beginTest("Mix 0 leaves the input alone; the output level scales it");
        {
            Settings s;
            s.type = Type::comb;
            s.resonance = 0.9f;
            s.driveDb = 18.0f;
            s.mix = 0.0f;
            s.outputDb = -6.0f;
            const auto input = sine(330.0, 0.2);
            const auto take = play(s, input);
            float worst = 0.0f;
            for (std::size_t n = 0; n < input.size(); ++n)
                worst = std::max(worst, std::abs(take.left[n] - 0.5011872f * input[n]));
            expectLessThan(worst, 1.0e-5f);
        }

        beginTest("Loud noise at full resonance and drive stays bounded, silence stays silence");
        {
            juce::Random random(7);
            Signal noise((std::size_t) kRate);
            for (auto& v : noise)
                v = random.nextFloat() * 2.0f - 1.0f;
            noise.resize((std::size_t) (3.0 * kRate), 0.0f);
            for (int t = 0; t < (int) kTypeNames.size(); ++t)
            {
                Settings s;
                s.type = (Type) t;
                s.resonance = 1.0f;
                s.driveDb = 24.0f;
                s.lfoAmount = 1.0f;
                s.rate = 5.0f;
                s.envelopeAmount = 1.0f;
                const auto take = play(s, noise);
                float peak = 0.0f;
                bool finite = true;
                for (const float v : take.left)
                {
                    finite = finite && std::isfinite(v);
                    peak = std::max(peak, std::abs(v));
                }
                expect(finite, nameOf(s.type));
                logMessage(nameOf(s.type).paddedRight(' ', 12) + "peak " + juce::String(peak, 2));
                expectLessThan(peak, 8.0f, nameOf(s.type));
                // The self-oscillating ladder may ring; everything else dies out within two seconds.
                if (s.type != Type::ladder)
                    expectLessThan(rms(take.left, (std::size_t) (2.5 * kRate), take.left.size()), 1.0e-4f, nameOf(s.type));
            }
        }

        beginTest("Cheap enough for many tracks");
        {
            const auto input = bassAndChord();
            for (int t = 0; t < (int) kTypeNames.size(); ++t)
            {
                Settings s;
                s.type = (Type) t;
                s.resonance = 0.6f;
                s.driveDb = 6.0f;
                s.envelopeAmount = 0.5f;
                s.lfoAmount = 0.5f;
                const auto take = play(s, input);
                const double factor = (double) input.size() / kRate / take.seconds;
                logMessage(nameOf(s.type).paddedRight(' ', 12) + "stereo runs " + juce::String(factor, 0)
                           + " times faster than real time");
                // Generous, for debug builds and busy CI machines; the log shows the real figure.
                expectGreaterThan(factor, 20.0, nameOf(s.type));
            }
        }

        beginTest("Factory sounds: groups, names and levels against the dry source");
        {
            const auto source = bassAndChord();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_FILTER_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                FilterProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kFilterCategories) >= 0,
                       "no group: " + sound);
                const auto take = render(processor, source);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 20)
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
            FilterProcessor p;
            set(p, "cutoff", 2500.0f);
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("cutoff"), juce::String("2500 Hz"));
            expectEquals(text("type"), juce::String("Tiefpass 24"));
            expectEquals(text("resonance"), juce::String("30 %"));
            expectEquals(text("attack"), juce::String("10 ms"));
            expectEquals(text("lfoRate"), juce::String("1.0 Hz"));
            expectEquals(text("lfoDivision"), juce::String("1/4"));
            expectEquals(text("spread"), juce::String::fromUTF8("0 °"));
            set(p, "envAmount", -0.4f);
            expectEquals(text("envAmount"), juce::String("-40 %"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            FilterProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().cutoff, 2500.0f, 1.0f);
            expectWithinAbsoluteError(loaded.readSettings().envelopeAmount, -0.4f, 0.001f);
        }

        beginTest("The editor shows the response and fits its controls");
        {
            FilterProcessor p;
            p.setCurrentProgram(3);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<FilterEditor*>(created.get());
            expect(editor != nullptr);
            render(p, bassAndChord());
            editor->filter.view.refresh();
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            // The drawn response follows the engine's at the cutoff and an octave either side, within a few dB.
            Settings s;
            s.type = Type::lowpass24;
            expectWithinAbsoluteError(ResponseView::responseDb(s, 1000.0f, 4000.0f), gainAt(Type::lowpass24, 4000.0f, 0.3f),
                                      3.0f);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("filter.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static FilterTests filterTests;
