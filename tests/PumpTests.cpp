#include "Platform.h"
#include "PumpEditor.h"
#include "PumpEngine.h"
#include "PumpPresets.h"
#include "PumpProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkpump;

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

Signal sine(double hz, double seconds, float level)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

/** A kick: a falling sine with a short click, `level` at its peak. */
void addKick(Signal& out, double at, float level)
{
    double phase = 0.0;
    const auto start = (std::size_t) (at * kRate);
    for (std::size_t n = 0; n < (std::size_t) (0.3 * kRate) && start + n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        phase += kTwoPi * (50.0 + 110.0 * std::exp(-t / 0.03)) / kRate;
        out[start + n] += (float) (level * std::sin(phase) * std::exp(-t / 0.1));
    }
}

/** A kick on every beat at 120 BPM for `seconds`. */
Signal kicks(double seconds, float level = 0.8f)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    for (double t = 0.0; t < seconds; t += 0.5)
        addKick(out, t, level);
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

float db(float gain) { return juce::Decibels::gainToDecibels(gain, -200.0f); }

/** A pad's level change in dB around `t` (5 ms either side) against its level without the pump. */
float changeAt(const Signal& out, const Signal& dry, double t)
{
    return db(rms(out, t - 0.005, t + 0.005) / rms(dry, t - 0.005, t + 0.005));
}

/** What a pump goes on: a bass line and a pad, peaking at -6 dBFS together, as the main signal. */
Signal bassAndPad(double seconds)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    const int bass[] = { 36, 36, 43, 41 };
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const double hz = 440.0 * std::exp2((bass[(int) (t / 2.0) % 4] - 69) / 12.0);
        double saw = 0.0;
        for (int k = 1; k <= 6; ++k)
            saw += std::sin(kTwoPi * k * hz * t) / k;
        double pad = 0.0;
        for (const int note : { 60, 64, 67, 71 })
            pad += std::sin(kTwoPi * 440.0 * std::exp2((note - 69) / 12.0) * t);
        out[n] = (float) (0.35 * saw + 0.1 * pad);
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

/** Through the plugin as Logic would play it, the transport running from bar 1 at 120 BPM, `key` on the side chain. */
Take render(PumpProcessor& processor, const Signal& source, const Signal& key)
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
    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    processor.setBusesLayout(layout);
    processor.setPlayHead(&head);
    processor.setRateAndBufferSizeDetails(kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { source, source };
    juce::AudioBuffer<float> buffer(4, kBlock);
    juce::MidiBuffer midi;
    for (std::size_t at = 0; at < source.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, source.size() - at);
        head.ppq = (double) at / kRate * 2.0;
        buffer.setSize(4, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            buffer.copyFrom(ch, 0, source.data() + at, count);
            buffer.copyFrom(ch + 2, 0, key.data() + at, count);
        }
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

class PumpTests : public juce::UnitTest
{
public:
    PumpTests() : juce::UnitTest("Tonwerk Pump", "Pump") {}

    void runTest() override
    {
        beginTest("Each kick on the side chain plays the curve: down by the depth, back over the release");
        {
            Settings s;
            s.depthDb = 18.0f;
            s.attack = 0.001f;
            s.hold = 0.0f;
            s.release = 0.2f;
            s.shape = 0.0f; // a straight line in dB: half way back at half the release
            const auto pad = sine(440.0, 2.0, 0.25f);
            const auto key = kicks(2.0);
            const auto take = play(s, pad, key);
            const double hit = 1.0;
            logMessage("pad under a kick: " + juce::String(changeAt(take.left, pad, hit + 0.006), 1) + " dB at 6 ms, "
                       + juce::String(changeAt(take.left, pad, hit + 0.101), 1) + " dB at 101 ms, "
                       + juce::String(changeAt(take.left, pad, hit + 0.3), 1) + " dB at 300 ms");
            expectWithinAbsoluteError(changeAt(take.left, pad, hit + 0.006), -17.6f, 1.0f);
            expectWithinAbsoluteError(changeAt(take.left, pad, hit + 0.101), -9.0f, 0.8f);
            expectWithinAbsoluteError(changeAt(take.left, pad, hit + 0.3), 0.0f, 0.05f);
            // A curve bent up holds low longer; one bent down comes back sooner.
            s.shape = 1.0f;
            const auto late = play(s, pad, key);
            s.shape = -1.0f;
            const auto early = play(s, pad, key);
            expectLessThan(changeAt(late.left, pad, hit + 0.101), -14.0f);
            expectGreaterThan(changeAt(early.left, pad, hit + 0.101), -5.0f);
            // Hold keeps it down before it comes back.
            s.shape = 0.0f;
            s.hold = 0.1f;
            const auto held = play(s, pad, key);
            expectWithinAbsoluteError(changeAt(held.left, pad, hit + 0.09), -18.0f, 0.5f);
        }

        beginTest("A kick that rings on triggers once; the next one triggers again");
        {
            Settings s;
            s.thresholdDb = -30.0f;
            Engine engine;
            engine.prepare(kRate);
            const auto key = kicks(2.0);
            Signal l = sine(440.0, 2.0, 0.25f), r = l;
            for (std::size_t at = 0; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, key.data() + at, key.data() + at, kBlock, s, 120.0);
            expectEquals(engine.hitsHeard(), 4);
            // A steady tone over the threshold for two seconds is one hit.
            engine.reset();
            const auto steady = sine(60.0, 2.0, 0.5f);
            for (std::size_t at = 0; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, steady.data() + at, steady.data() + at, kBlock, s, 120.0);
            expectEquals(engine.hitsHeard(), 1);
        }

        beginTest("The side chain's low pass keeps the hats from pumping");
        {
            Settings s;
            s.thresholdDb = -24.0f;
            s.keyLowpass = 150.0f;
            const auto pad = sine(440.0, 1.0, 0.25f);
            Signal hats(pad.size(), 0.0f);
            juce::Random random(3);
            for (double t = 0.25; t < 1.0; t += 0.25)
                for (std::size_t n = 0; n < (std::size_t) (0.03 * kRate); ++n)
                {
                    // Noise through a crude high pass: what is left of a hat after the cymbals' body.
                    hats[(std::size_t) (t * kRate) + n] = 0.5f * (random.nextFloat() * 2.0f - 1.0f) * (float) std::exp(-(double) n / (0.01 * kRate));
                }
            for (std::size_t n = hats.size() - 1; n > 0; --n)
                hats[n] -= hats[n - 1];
            Engine engine;
            engine.prepare(kRate);
            Signal l = pad, r = pad;
            for (std::size_t at = 0; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, hats.data() + at, hats.data() + at, (int) std::min<std::size_t>(kBlock, l.size() - at), s, 120.0);
            logMessage("hats on the key with the low pass at 150 Hz: " + juce::String(engine.hitsHeard()) + " hits");
            expectEquals(engine.hitsHeard(), 0);
            s.keyLowpass = 20000.0f;
            engine.reset();
            l = pad;
            r = pad;
            for (std::size_t at = 0; at < l.size(); at += kBlock)
                engine.process(l.data() + at, r.data() + at, hats.data() + at, hats.data() + at, (int) std::min<std::size_t>(kBlock, l.size() - at), s, 120.0);
            expectGreaterOrEqual(engine.hitsHeard(), 3);
        }

        beginTest("Without a key nothing changes, to the bit");
        {
            Settings s;
            const auto pad = sine(440.0, 1.0, 0.25f);
            const auto take = play(s, pad);
            expect(take.left == pad && take.right == pad);
            s.mode = Mode::compressor;
            const auto compressed = play(s, pad);
            expect(compressed.left == pad);
        }

        beginTest("Dynamics: a soft hit ducks less than a loud one");
        {
            Settings s;
            s.thresholdDb = -30.0f;
            s.dynamics = 1.0f;
            s.release = 0.15f;
            const auto pad = sine(440.0, 1.0, 0.25f);
            Signal key(pad.size(), 0.0f);
            addKick(key, 0.0, 0.8f);  // about 28 dB over the threshold
            addKick(key, 0.5, 0.05f); // about 4 dB over it
            const auto take = play(s, pad, key);
            const float loud = changeAt(take.left, pad, 0.006), soft = changeAt(take.left, pad, 0.506);
            logMessage("dynamics: loud kick " + juce::String(loud, 1) + " dB, soft one " + juce::String(soft, 1) + " dB");
            expectWithinAbsoluteError(loud, -17.6f, 1.0f);
            expectGreaterThan(soft, -10.0f);
            expectLessThan(soft, -2.0f);
        }

        beginTest("Compressor mode follows the key's level by the ratio, at most the depth");
        {
            Settings s;
            s.mode = Mode::compressor;
            s.thresholdDb = -30.0f;
            s.ratio = 4.0f;
            s.depthDb = 40.0f;
            s.attack = 0.001f;
            s.release = 0.1f;
            const auto pad = sine(440.0, 1.0, 0.25f);
            const auto key = sine(1000.0, 1.0, juce::Decibels::decibelsToGain(-10.0f));
            const auto take = play(s, pad, key);
            // 20 dB over the threshold at 4:1: 15 dB down.
            const float change = changeAt(take.left, pad, 0.8);
            logMessage("key 20 dB over the threshold at 4:1: " + juce::String(change, 1) + " dB");
            expectWithinAbsoluteError(change, -15.0f, 0.5f);
            s.depthDb = 6.0f;
            expectWithinAbsoluteError(changeAt(play(s, pad, key).left, pad, 0.8), -6.0f, 0.3f);
            // The release lets go once the key stops.
            Signal shortKey = key;
            std::fill(shortKey.begin() + (std::ptrdiff_t) (0.5 * kRate), shortKey.end(), 0.0f);
            s.depthDb = 40.0f;
            const auto released = play(s, pad, shortKey);
            expectWithinAbsoluteError(changeAt(released.left, pad, 0.5 + 0.1), -15.0f / std::exp(1.0f), 1.5f);
            expectWithinAbsoluteError(changeAt(released.left, pad, 0.95), 0.0f, 0.3f);
        }

        beginTest("Tempo mode ducks on each step of the song and is home before the next");
        {
            Settings s;
            s.mode = Mode::tempo;
            s.division = kQuarter;
            s.depthDb = 12.0f;
            s.shape = 0.0f;
            s.release = 2.0f; // longer than a step: cut to fit
            const auto pad = sine(440.0, 2.0, 0.25f);
            const auto take = play(s, pad, {}, 120.0, 0.0);
            logMessage("tempo: " + juce::String(changeAt(take.left, pad, 1.006), 1) + " dB on the beat, "
                       + juce::String(changeAt(take.left, pad, 1.25), 1) + " dB half way, "
                       + juce::String(changeAt(take.left, pad, 1.493), 1) + " dB before the next");
            expectWithinAbsoluteError(changeAt(take.left, pad, 1.006), -11.7f, 0.8f);
            expectWithinAbsoluteError(changeAt(take.left, pad, 1.25), -6.0f, 0.8f);
            expectGreaterThan(changeAt(take.left, pad, 1.493), -0.5f);
            // From the song position: starting on the off-beat, the next dip comes a quarter later than half a beat.
            s.release = 0.2f;
            const auto late = play(s, pad, {}, 120.0, 0.5);
            expectWithinAbsoluteError(changeAt(late.left, pad, 0.15), 0.0f, 0.05f);
            expectLessThan(changeAt(late.left, pad, 0.256), -10.0f);
        }

        beginTest("Band: only the lows duck, or only the highs, and with no duck the level stays flat");
        {
            Settings s;
            s.band = Band::low;
            s.split = 150.0f;
            s.hold = 0.3f;
            const auto low = sine(50.0, 1.0, 0.25f), high = sine(3000.0, 1.0, 0.25f);
            Signal key(low.size(), 0.0f);
            addKick(key, 0.5, 0.8f);
            const auto lows = play(s, low, key), highs = play(s, high, key);
            logMessage("band Tiefen at 150 Hz: 50 Hz " + juce::String(changeAt(lows.left, low, 0.75), 1) + " dB, 3 kHz "
                       + juce::String(changeAt(highs.left, high, 0.75), 1) + " dB");
            expectLessThan(changeAt(lows.left, low, 0.75), -14.0f);
            expectWithinAbsoluteError(changeAt(highs.left, high, 0.75), 0.0f, 0.3f);
            s.band = Band::high;
            expectWithinAbsoluteError(changeAt(play(s, low, key).left, low, 0.75), 0.0f, 0.3f);
            expectLessThan(changeAt(play(s, high, key).left, high, 0.75), -14.0f);
            // Before the kick the crossover keeps the level of every frequency.
            s.band = Band::low;
            for (const double hz : { 40.0, 150.0, 600.0, 5000.0 })
            {
                const auto tone = sine(hz, 1.0, 0.25f);
                const auto split = play(s, tone, key);
                expectWithinAbsoluteError(db(rms(split.left, 0.2, 0.45) / rms(tone, 0.2, 0.45)), 0.0f, 0.05f);
            }
        }

        beginTest("Mix and listen");
        {
            Settings s;
            s.hold = 0.3f;
            s.mix = 0.5f;
            const auto pad = sine(440.0, 1.0, 0.25f);
            Signal key(pad.size(), 0.0f);
            addKick(key, 0.2, 0.8f);
            const auto half = play(s, pad, key);
            // 18 dB down at full wet: half of it beside the dry is 1/2 + 1/2 * 0.126.
            expectWithinAbsoluteError(changeAt(half.left, pad, 0.4), db(0.5f + 0.5f * 0.1259f), 0.3f);
            s.mix = 1.0f;
            s.listen = true;
            const auto heard = play(s, pad, key);
            expectWithinAbsoluteError(rms(heard.left, 0.2, 0.3), rms(key, 0.2, 0.3), 0.001f);
        }

        beginTest("Through the plugin with the host's side chain bus");
        {
            PumpProcessor processor;
            const auto pad = bassAndPad(2.0);
            const auto key = kicks(2.0);
            const auto take = render(processor, pad, key);
            expect(processor.keyConnected.load());
            expectLessThan(changeAt(take.left, pad, 1.006), -15.0f);
            expectGreaterThan(processor.hits.load(), 2);
        }

        beginTest("It runs far inside real time");
        {
            Settings s;
            s.keyLowpass = 150.0f;
            s.band = Band::low;
            const auto pad = bassAndPad(8.0);
            const auto take = play(s, pad, kicks(8.0));
            const double factor = (double) pad.size() / kRate / take.seconds;
            logMessage("pump: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 50.0);
        }

        beginTest("Factory sounds: groups, names and levels against the dry signal");
        {
            const auto source = bassAndPad(8.0);
            const auto key = kicks(8.0);
            // Selbst-Pumpe listens to its own input: it gets the kick mixed in.
            Signal withKick = source;
            for (std::size_t n = 0; n < withKick.size(); ++n)
                withKick[n] += 0.6f * key[n];
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PUMP_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                PumpProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kPumpCategories) >= 0,
                       "no group: " + sound);
                const bool ownInput = processor.readSettings().source == Source::input;
                const auto& in = ownInput ? withKick : source;
                const double dry = tonwerktest::momentaryMax(in, in);
                const auto take = render(processor, in, key);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 24)
                           + juce::String::fromUTF8(presets[(std::size_t) i].category).paddedRight(' ', 12)
                           + juce::String(change, 1) + " dB against dry, peak " + juce::String(peak, 2));
                if (i > 0)
                    expectWithinAbsoluteError(change, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                // The takes carry the kick as a song would (its own track beside the pumped one), so it is heard.
                auto heard = take;
                if (! ownInput)
                    for (std::size_t n = 0; n < heard.left.size(); ++n)
                    {
                        heard.left[n] += 0.6f * key[n];
                        heard.right[n] += 0.6f * key[n];
                    }
                if (wavDir.isNotEmpty())
                    writeWav(heard, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav({ withKick, withKick }, juce::File(wavDir).getChildFile("00 Quelle trocken.wav"));
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            PumpProcessor p;
            auto* depth = p.state.getParameter("depth");
            depth->setValueNotifyingHost(depth->convertTo0to1(24.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("depth"), juce::String("24 dB"));
            expectEquals(text("ratio"), juce::String("8:1"));
            expectEquals(text("attack"), juce::String("1 ms"));
            expectEquals(text("release"), juce::String("200 ms"));
            expectEquals(text("shape"), juce::String("30 %"));
            expectEquals(text("division"), juce::String("1/4"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            PumpProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().depthDb, 24.0f, 0.01f);
        }

        beginTest("The editor shows the reduction and the settings");
        {
            PumpProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<PumpEditor>(created.get());
            expect(editor != nullptr);
            if (editor == nullptr)
                return;
            const auto pad = bassAndPad(4.0);
            const auto key = kicks(4.0);
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
            p.setBusesLayout(layout);
            p.setRateAndBufferSizeDetails(kRate, 1600);
            p.prepareToPlay(kRate, 1600);
            juce::AudioBuffer<float> buffer(4, 1600);
            juce::MidiBuffer midi;
            for (std::size_t at = 0; at + 1600 <= pad.size(); at += 1600)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    buffer.copyFrom(ch, 0, pad.data() + at, 1600);
                    buffer.copyFrom(ch + 2, 0, key.data() + at, 1600);
                }
                p.processBlock(buffer, midi);
                editor->meter.view.refresh();
            }
            expect(editor->meter.view.caption().contains("SIDECHAIN"));
            expectGreaterThan(editor->getWidth(), 600);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("pump.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static PumpTests pumpTests;
