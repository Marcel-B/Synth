#include "DelayEditor.h"
#include "DelayEngine.h"
#include "DelayPresets.h"
#include "DelayProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkdelay;

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

/** A click of `size`; small enough by default that the feedback's saturation leaves it alone. */
Signal impulse(double seconds, float size = 0.2f)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    out[0] = size;
    return out;
}

/** 30 ms of 500 Hz at 0.2, from the start, then silence. */
Signal burst(double seconds)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    for (std::size_t n = 0; n < (std::size_t) (0.03 * kRate); ++n)
        out[n] = 0.2f * (float) std::sin(kTwoPi * 500.0 * (double) n / kRate);
    return out;
}

/** Where the biggest value lies between `from` and `to` seconds, and how big it is. */
std::pair<double, float> peakBetween(const Signal& signal, double from, double to)
{
    std::size_t best = (std::size_t) (from * kRate);
    for (auto n = best; n < std::min(signal.size(), (std::size_t) (to * kRate)); ++n)
        if (std::abs(signal[n]) > std::abs(signal[best]))
            best = n;
    return { (double) best / kRate, std::abs(signal[best]) };
}

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

/** Settings for measuring the line itself: everything wet, no filtering, no saturation to speak of. */
Settings clean()
{
    Settings s;
    s.mix = 1.0f;
    s.lowCut = 20.0f;
    s.highCut = 20000.0f;
    s.width = 1.0f;
    return s;
}

/**
 * A synth line of short stabs at 120 BPM with rests between, peaking at -6 dBFS, then a second of silence: what a
 * delay is mostly put on, with gaps for the repeats.
 */
Signal stabs()
{
    Signal out((std::size_t) (5.0 * kRate), 0.0f);
    const int notes[] = { 60, 63, 67, 70, 72, 70, 67, 63 };
    for (int i = 0; i < 8; ++i)
    {
        const double start = i * 0.5;
        const double hz = 440.0 * std::exp2((notes[i] - 69) / 12.0);
        for (std::size_t n = 0; n < (std::size_t) (0.18 * kRate); ++n)
        {
            const double t = (double) n / kRate;
            double saw = 0.0;
            for (int k = 1; k * hz < 8000.0; ++k)
                saw += std::sin(kTwoPi * k * hz * t) / k;
            const double envelope = std::min(1.0, t / 0.003) * std::exp(-t / 0.08);
            out[(std::size_t) (start * kRate) + n] += (float) (saw * envelope);
        }
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

Take render(DelayProcessor& processor, const Signal& source)
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

class DelayTests : public juce::UnitTest
{
public:
    DelayTests() : juce::UnitTest("Tonwerk Delay", "Delay") {}

    void runTest() override
    {
        beginTest("The echo comes at its time, free or from the tempo");
        {
            auto s = clean();
            s.mode = Mode::stereo;
            s.sync = false;
            s.time = 0.3f;
            s.feedback = 0.0f;
            const auto in = impulse(1.0);
            auto take = play(s, in, in);
            const auto [at, size] = peakBetween(take.left, 0.01, 1.0);
            logMessage("300 ms free: echo at " + juce::String(at * 1000.0, 2) + " ms, " + juce::String(size, 3));
            expectWithinAbsoluteError(at, 0.3, 0.0001);
            expectWithinAbsoluteError(size, 0.2f, 0.004f);

            // A dotted eighth at 120 BPM is 375 ms, at 90 BPM 500 ms.
            s.sync = true;
            s.division = kDottedEighth;
            expectWithinAbsoluteError(peakBetween(play(s, in, in, 120.0).left, 0.01, 1.0).first, 0.375, 0.0001);
            expectWithinAbsoluteError(peakBetween(play(s, in, in, 90.0).left, 0.01, 1.0).first, 0.5, 0.0001);

            // Stereo: the right side 25 % longer.
            s.offset = 0.25f;
            const auto offset = play(s, in, in, 120.0);
            expectWithinAbsoluteError(peakBetween(offset.right, 0.01, 1.0).first, 0.375 * 1.25, 0.0001);
        }

        beginTest("Feedback: each repeat the feedback's share of the last");
        {
            auto s = clean();
            s.mode = Mode::mono;
            s.sync = false;
            s.time = 0.1f;
            s.feedback = 0.5f;
            const auto in = burst(1.0);
            const auto take = play(s, in, in);
            const float first = rms(take.left, 0.1, 0.13);
            const float second = rms(take.left, 0.2, 0.23);
            const float third = rms(take.left, 0.3, 0.33);
            logMessage("repeats " + juce::String(first, 3) + ", " + juce::String(second, 3) + ", "
                       + juce::String(third, 3));
            expectWithinAbsoluteError(second / first, 0.5f, 0.03f);
            expectWithinAbsoluteError(third / second, 0.5f, 0.03f);
        }

        beginTest("Ping-pong: the repeats alternate sides");
        {
            auto s = clean();
            s.mode = Mode::pingPong;
            s.sync = false;
            s.time = 0.2f;
            s.feedback = 0.6f;
            const auto in = burst(1.0);
            const auto take = play(s, in, in);
            const float firstLeft = rms(take.left, 0.2, 0.23), firstRight = rms(take.right, 0.2, 0.23);
            const float secondLeft = rms(take.left, 0.4, 0.43), secondRight = rms(take.right, 0.4, 0.43);
            expectGreaterThan(firstLeft, 100.0f * firstRight);
            expectGreaterThan(secondRight, 100.0f * secondLeft);
            expectWithinAbsoluteError(secondRight / firstLeft, 0.6f, 0.03f);
            // Width 0 puts them in the middle.
            s.width = 0.0f;
            const auto narrow = play(s, in, in);
            expectWithinAbsoluteError(rms(narrow.left, 0.2, 0.23), rms(narrow.right, 0.2, 0.23), 0.0001f);
        }

        beginTest("The filters darken and thin each repeat further");
        {
            auto s = clean();
            s.mode = Mode::mono;
            s.sync = false;
            s.time = 0.25f;
            s.feedback = 0.9f;
            s.lowCut = 400.0f;
            s.highCut = 2000.0f;
            juce::Random random(4);
            Signal burst((std::size_t) (1.5 * kRate), 0.0f);
            for (std::size_t n = 0; n < (std::size_t) (0.05 * kRate); ++n)
                burst[n] = 0.5f * (random.nextFloat() * 2.0f - 1.0f);
            const auto take = play(s, burst, burst);
            // The slope's share of the level falls with each repeat: fewer highs.
            auto brightness = [&take](double from) {
                double slope = 0.0, level = 0.0;
                for (auto n = (std::size_t) (from * kRate) + 1; n < (std::size_t) ((from + 0.06) * kRate); ++n)
                {
                    const double d = take.left[n] - take.left[n - 1];
                    slope += d * d;
                    level += (double) take.left[n] * take.left[n];
                }
                return std::sqrt(slope / std::max(1.0e-12, level));
            };
            logMessage("brightness of repeats 1 and 4: " + juce::String(brightness(0.25), 3) + ", "
                       + juce::String(brightness(1.0), 3));
            expectLessThan(brightness(1.0), brightness(0.25) * 0.8);
        }

        beginTest("Ducking holds the repeats down while the input plays and lets them back after");
        {
            auto s = clean();
            s.mode = Mode::mono;
            s.sync = false;
            s.time = 0.05f;
            s.feedback = 0.9f;
            s.duck = 1.0f;
            s.duckRelease = 0.1f;
            Signal tone((std::size_t) (2.0 * kRate), 0.0f);
            for (std::size_t n = 0; n < (std::size_t) kRate; ++n)
                tone[n] = 0.4f * (float) std::sin(kTwoPi * 300.0 * (double) n / kRate);
            const auto ducked = play(s, tone, tone);
            s.duck = 0.0f;
            const auto open = play(s, tone, tone);
            // Wet only (mix 1): while the tone plays, the repeats are pulled far down; after it, they ring.
            const float during = rms(ducked.left, 0.5, 0.9) / rms(open.left, 0.5, 0.9);
            const float after = rms(ducked.left, 1.4, 1.6) / rms(open.left, 1.4, 1.6);
            logMessage("ducked against open: " + juce::String(juce::Decibels::gainToDecibels(during), 0)
                       + " dB while playing, " + juce::String(juce::Decibels::gainToDecibels(after), 1) + " dB after");
            expectLessThan(during, 0.1f);
            expectGreaterThan(after, 0.7f);
        }

        beginTest("At full feedback the saturation keeps it bounded; hold keeps it going without the input");
        {
            auto s = clean();
            s.sync = false;
            s.time = 0.1f;
            s.feedback = 1.0f;
            s.drive = 1.0f;
            juce::Random random(2);
            Signal noise((std::size_t) (6.0 * kRate));
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
            logMessage("noise at full feedback and drive: peak " + juce::String(peak, 2));
            expect(finite);
            expectLessThan(peak, 4.0f);

            // Held after a stab: no new input goes in, what is there keeps circulating.
            auto held = clean();
            held.mode = Mode::mono;
            held.sync = false;
            held.time = 0.2f;
            held.feedback = 0.3f;
            const auto source = stabs();
            Engine engine;
            engine.prepare(kRate);
            Signal l = source, r = source;
            engine.process(l.data(), r.data(), 24000, held, 120.0);
            held.hold = true;
            engine.process(l.data() + 24000, r.data() + 24000, (int) l.size() - 24000, held, 120.0);
            expectGreaterThan(rms(l, 4.0, 4.5), 0.3f * rms(l, 0.6, 1.1));
        }

        beginTest("Two stereo lines with filters, drive, wow and ducking run far inside real time");
        {
            auto s = clean();
            s.drive = 0.5f;
            s.wow = 0.5f;
            s.duck = 0.5f;
            s.lowCut = 200.0f;
            s.highCut = 4000.0f;
            const auto source = stabs();
            Signal longer;
            for (int i = 0; i < 4; ++i)
                longer.insert(longer.end(), source.begin(), source.end());
            const auto take = play(s, longer, longer);
            const double factor = (double) longer.size() / kRate / take.seconds;
            logMessage("delay: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry stabs");
        {
            const auto source = stabs();
            const double dry = tonwerktest::momentaryMax(source, source);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_DELAY_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                DelayProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kDelayCategories) >= 0,
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
            DelayProcessor p;
            auto* time = p.state.getParameter("time");
            time->setValueNotifyingHost(time->convertTo0to1(250.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("time"), juce::String("250 ms"));
            expectEquals(text("division"), juce::String("1/8."));
            expectEquals(text("feedback"), juce::String("45 %"));
            expectEquals(text("highCut"), juce::String("6000 Hz"));
            expectEquals(text("output"), juce::String("0 dB"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            DelayProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().time, 0.25f, 0.001f);
        }

        beginTest("The editor shows the repeats and the time");
        {
            DelayProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<DelayEditor*>(created.get());
            expect(editor != nullptr);
            render(p, stabs());
            editor->echo.view.refresh();
            expectEquals(editor->echo.view.timeText(), juce::String::fromUTF8("1/8. · 375 MS"));
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("delay.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static DelayTests delayTests;
