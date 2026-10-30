#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "WobbleEditor.h"
#include "WobbleEngine.h"
#include "WobblePresets.h"
#include "WobbleProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkwobble;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 2.0 * kPi;
/** Dubstep's tempo; the presets are made for it. */
constexpr double kBpm = 140.0;
/** Every source peaks here, about where a recorded track sits: -6 dBFS. */
constexpr float kSourcePeak = 0.5f;

using Signal = std::vector<float>;

double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

Signal sine(double frequency, double seconds, float amplitude = 0.5f)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(kTwoPi * frequency * (double) n / kRate);
    return out;
}

void normalise(Signal& signal)
{
    float highest = 0.0f;
    for (const float sample : signal)
        highest = std::max(highest, std::abs(sample));
    if (highest > 0.0f)
        for (auto& sample : signal)
            sample *= kSourcePeak / highest;
}

/** Sawtooths at `notes`, two per note seven cents apart, a bar each; with `sub`, a sine an octave down. */
Signal saws(std::initializer_list<std::vector<int>> bars, double seconds, bool sub, double brightness)
{
    Signal out((std::size_t) (seconds * kRate));
    const double barSeconds = 4.0 * 60.0 / kBpm;
    std::vector<double> phases(16, 0.0);
    double low = 0.0;
    const std::vector<std::vector<int>> chords(bars);
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const auto& chord = chords[(std::size_t) (t / barSeconds) % chords.size()];
        double sum = 0.0;
        std::size_t p = 0;
        for (const int note : chord)
            for (const double cents : { -7.0, 7.0 })
            {
                auto& phase = phases[p++];
                phase = std::fmod(phase + hz(note) * std::pow(2.0, cents / 1200.0) / kRate, 1.0);
                sum += 2.0 * phase - 1.0;
            }
        if (sub)
        {
            auto& phase = phases[p];
            phase = std::fmod(phase + hz(chord.front() - 12) / kRate, 1.0);
            sum += 1.5 * std::sin(kTwoPi * phase);
        }
        low += brightness * (sum - low);
        out[n] = (float) low;
    }
    normalise(out);
    return out;
}

/** A held dubstep bass, F1 then G#1, D#1 and C#1, a bar each: what the wobbles are made for. */
const Signal& bass()
{
    static const Signal take = saws({ { 29 }, { 32 }, { 27 }, { 25 } }, 4 * 4 * 60.0 / kBpm, true, 0.6);
    return take;
}

/** A pad of four notes, a little dark, for the gates, the pumping and the slow moves. */
const Signal& pad()
{
    static const Signal take =
        saws({ { 53, 56, 60, 63 }, { 51, 55, 58, 63 } }, 4 * 4 * 60.0 / kBpm, false, 0.25);
    return take;
}

struct Take
{
    Signal left, right;
};

/** Runs a mono source (on both sides) through the processor as the host plays it from `startPpq` at `bpm`. */
Take render(WobbleProcessor& processor, const Signal& source, double bpm = kBpm, double startPpq = 0.0,
            bool playing = true)
{
    struct PlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0, tempo = 120.0;
        bool running = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(running);
            info.setBpm(tempo);
            info.setPpqPosition(ppq);
            return info;
        }
    } head;
    head.ppq = startPpq;
    head.tempo = bpm;
    head.running = playing;
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
        head.ppq += count * bpm / 60.0 / kRate;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return take;
}

void set(WobbleProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/** Root mean square of `signal` from `from` seconds for `seconds`. */
double rms(const Signal& signal, double from, double seconds)
{
    const auto start = (std::size_t) (from * kRate);
    const auto end = std::min(signal.size(), (std::size_t) ((from + seconds) * kRate));
    double sum = 0.0;
    for (auto n = start; n < end; ++n)
        sum += (double) signal[n] * signal[n];
    return end > start ? std::sqrt(sum / (double) (end - start)) : 0.0;
}

double decibels(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-9)); }

float peakOf(const Take& take)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < take.left.size(); ++i)
        peak = std::max({ peak, std::abs(take.left[i]), std::abs(take.right[i]) });
    return peak;
}

/** The amplitude of `frequency` in `signal` from `from` seconds on (Goertzel). */
double amplitudeAt(const Signal& signal, double frequency, double from)
{
    const double w = kTwoPi * frequency / kRate;
    double re = 0.0, im = 0.0;
    std::size_t count = 0;
    for (auto n = (std::size_t) (from * kRate); n < signal.size(); ++n, ++count)
    {
        re += signal[n] * std::cos(w * (double) n);
        im -= signal[n] * std::sin(w * (double) n);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double) std::max<std::size_t>(1, count);
}

void writeWav(const Signal& left, const Signal& right, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    const auto writer = juce::WavAudioFormat().createWriterFor(
        stream, juce::AudioFormatWriterOptions().withSampleRate(kRate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr)
        return;
    const float* channels[] = { left.data(), right.data() };
    writer->writeFromFloatArrays(channels, 2, (int) left.size());
}

/** The source a sound is made for: the bass for wobbles and vowels, the pad for the rest. */
const Signal& sourceFor(const char* category)
{
    const juce::String group = juce::String::fromUTF8(category);
    return group == "Wobble" || group == "Yoi & Vokal" || group.isEmpty() ? bass() : pad();
}
} // namespace

/**
 * Tonwerk Wobble: the curves draw and read back what was drawn, the LFOs sit on the host's beat, the filter and the
 * volume do what their curves say, and the processor keeps the curves in a project.
 */
class WobbleTests : public juce::UnitTest
{
public:
    WobbleTests() : juce::UnitTest("Tonwerk Wobble", "Wobble") {}

    void runTest() override
    {
        beginTest("Curves: corners, steps, bends and text");
        {
            const Curve line({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            expectWithinAbsoluteError(line.valueAt(0.25f), 0.25f, 1.0e-6f);
            const auto square = shapeCurve(Shape::square);
            expectEquals(square.valueAt(0.49f), 1.0f);
            expectEquals(square.valueAt(0.5f), 0.0f);
            expectEquals(square.valueAt(0.99f), 0.0f);

            // A bend found for a point halfway is the bend that goes through it, rising or falling.
            for (const auto& [y0, y1, through] : { std::tuple { 0.0f, 1.0f, 0.8f }, std::tuple { 1.0f, 0.2f, 0.9f },
                                                   std::tuple { 0.3f, 0.6f, 0.35f } })
            {
                Curve bent({ { 0.0f, y0, Curve::bendThrough(y0, y1, through) }, { 1.0f, y1, 0.0f } });
                expectWithinAbsoluteError(bent.valueAt(0.5f), through, 1.0e-3f);
            }
            // Bending up lifts the middle whichever way the segment goes.
            expectGreaterThan(Curve({ { 0.0f, 0.0f, 0.5f }, { 1.0f, 1.0f, 0.0f } }).valueAt(0.5f), 0.5f);
            expectGreaterThan(Curve({ { 0.0f, 1.0f, 0.5f }, { 1.0f, 0.0f, 0.0f } }).valueAt(0.5f), 0.5f);

            const auto sine = shapeCurve(Shape::sine);
            for (int i = 0; i <= 20; ++i)
            {
                const float x = (float) i / 20.0f;
                expectWithinAbsoluteError(sine.valueAt(x), (float) (0.5 - 0.5 * std::cos(kTwoPi * x)), 0.03f);
            }

            const auto read = Curve::fromString(sine.toString());
            expectEquals(juce::String(read.toString()), juce::String(sine.toString()));
            expect(Curve::fromString("Unsinn").points.empty());
            expect(Curve::fromString("0.5 0.5 0").points.empty());
            // Out of order and out of range: sorted, clamped, the ends pinned to 0 and 1.
            const auto tidied = Curve::fromString("0.7 2 0;0.2 -1 0;0.4 0.5 0");
            expectEquals((int) tidied.points.size(), 3);
            expectEquals(tidied.points.front().x, 0.0f);
            expectEquals(tidied.points.front().y, 0.0f);
            expectEquals(tidied.points.back().x, 1.0f);
            expectEquals(tidied.points.back().y, 1.0f);

            // Reversed twice is the curve again; inverted is upside down.
            const auto saw = shapeCurve(Shape::sawDown);
            expectWithinAbsoluteError(saw.reversed().reversed().valueAt(0.3f), saw.valueAt(0.3f), 1.0e-5f);
            // Backwards in time a power curve is only nearly the same: close enough to see as the mirror image.
            expectWithinAbsoluteError(saw.reversed().valueAt(0.3f), saw.valueAt(0.7f), 0.06f);
            expectWithinAbsoluteError(saw.inverted().valueAt(0.3f), 1.0f - saw.valueAt(0.3f), 1.0e-5f);
        }

        beginTest("Gates open for their steps' share");
        {
            const auto curve = gate("x.x.", 0.5f);
            double sum = 0.0;
            for (int i = 0; i < 1000; ++i)
                sum += curve.valueAt(((float) i + 0.5f) / 1000.0f);
            expectWithinAbsoluteError(sum / 1000.0, 0.25, 0.01);
            expectEquals(curve.valueAt(0.1f), 1.0f);
            expectEquals(curve.valueAt(0.2f), 0.0f);
            expectEquals(curve.valueAt(0.6f), 1.0f);
        }

        beginTest("The volume LFO sits on the host's beat");
        {
            // A square on the quarters: open for the first eighth of each beat, shut for the second.
            WobbleProcessor processor;
            set(processor, "filterOn", 0);
            set(processor, "volumeOn", 1);
            set(processor, "smooth", 0);
            processor.setCurve(Lane::volume, shapeCurve(Shape::square));
            const auto take = render(processor, sine(1000.0, 2.0), 120.0);
            for (int beat = 0; beat < 3; ++beat)
            {
                const double at = beat * 0.5;
                expectGreaterThan(rms(take.left, at + 0.02, 0.2), 0.3, "open in beat " + juce::String(beat));
                expectLessThan(rms(take.left, at + 0.27, 0.2), 0.001, "shut in beat " + juce::String(beat));
            }
            // Started half a beat into the song: the curve is where the song is, so it starts shut.
            const auto late = render(processor, sine(1000.0, 1.0), 120.0, 0.5);
            expectLessThan(rms(late.left, 0.02, 0.2), 0.001);
            expectGreaterThan(rms(late.left, 0.27, 0.2), 0.3);

            // Stopped, it runs on at the tempo: 1/8 at 140 BPM is 4.67 cycles a second.
            set(processor, "volumeDivision", kEighth);
            const auto stopped = render(processor, sine(1000.0, 3.0), 140.0, 0.0, false);
            expectWithinAbsoluteError(cyclesPerSecond(stopped.left), 140.0 / 60.0 * 2.0, 0.1);

            // Free: the rate in Hz, whatever the tempo.
            set(processor, "volumeSync", 0);
            set(processor, "volumeRate", 3.0f);
            const auto free = render(processor, sine(1000.0, 3.0), 90.0);
            expectWithinAbsoluteError(cyclesPerSecond(free.left), 3.0, 0.1);

            // Spread 180: the right side is shut while the left is open.
            set(processor, "volumeSync", 1);
            set(processor, "volumeDivision", kQuarter);
            set(processor, "spread", 180);
            const auto wide = render(processor, sine(1000.0, 1.0), 120.0);
            expectGreaterThan(rms(wide.left, 0.02, 0.2), 0.3);
            expectLessThan(rms(wide.right, 0.02, 0.2), 0.001);
            expectGreaterThan(rms(wide.right, 0.27, 0.2), 0.3);
        }

        beginTest("Depth and smoothing");
        {
            WobbleProcessor processor;
            set(processor, "filterOn", 0);
            set(processor, "volumeOn", 1);
            set(processor, "volumeDepth", 0.5f);
            set(processor, "smooth", 0);
            processor.setCurve(Lane::volume, shapeCurve(Shape::square));
            const auto take = render(processor, sine(1000.0, 1.0), 120.0);
            // Half the depth: the shut half is 6 dB down, not silent.
            expectWithinAbsoluteError(decibels(rms(take.left, 0.27, 0.2) / rms(take.left, 0.02, 0.2)), -6.02, 0.2);

            // Smoothing takes the step's click away: no jump between two samples bigger than the sine's own.
            set(processor, "volumeDepth", 1.0f);
            set(processor, "smooth", 5);
            const auto smooth = render(processor, sine(100.0, 1.0), 120.0);
            float jump = 0.0f;
            for (std::size_t n = 1; n < smooth.left.size(); ++n)
                jump = std::max(jump, std::abs(smooth.left[n] - smooth.left[n - 1]));
            expectLessThan(jump, (float) (0.5 * kTwoPi * 100.0 / kRate) * 1.05f);
        }

        beginTest("The filter goes where its curve says");
        {
            WobbleProcessor processor;
            set(processor, "resonance", 0);
            set(processor, "filterLow", 200);
            set(processor, "filterHigh", 8000);
            auto level = [&](float curveValue, float frequency) {
                processor.setCurve(Lane::filter, Curve({ { 0.0f, curveValue, 0.0f }, { 1.0f, curveValue, 0.0f } }));
                const auto take = render(processor, sine(frequency, 0.5), 120.0);
                return decibels(rms(take.left, 0.2, 0.3) / (0.5 / std::sqrt(2.0)));
            };
            // At the bottom 200 Hz: 2 kHz is more than 60 dB down with 24 dB a octave over three octaves and more.
            expectLessThan(level(0.0f, 2000.0f), -60.0);
            // At the top 8 kHz: 1 kHz passes.
            expectWithinAbsoluteError(level(1.0f, 1000.0f), 0.0, 0.5);
            // Halfway up in octaves, 1265 Hz: the cutoff itself, where a resonance-free 24 dB filter is 6 dB down.
            expectWithinAbsoluteError(level(0.5f, 1265.0f), -6.0, 1.0);

            set(processor, "filterSlope", 0);
            expectWithinAbsoluteError(level(0.0f, 2000.0f), -40.0, 2.0);

            // High pass: the other way round.
            set(processor, "filterType", 2);
            set(processor, "filterSlope", 1);
            expectLessThan(level(1.0f, 1000.0f), -30.0);
            expectWithinAbsoluteError(level(0.0f, 2000.0f), 0.0, 0.5);

            // The resonance lifts the cutoff.
            set(processor, "filterType", 0);
            set(processor, "resonance", 0.9f);
            expectGreaterThan(level(0.5f, 1265.0f), 6.0);
        }

        beginTest("The vowel filter goes from u to i");
        {
            WobbleProcessor processor;
            set(processor, "filterType", 3);
            // A plain sawtooth on A1: every harmonic there for the formants to pick.
            const auto saw = saws({ { 45 } }, 2.0, false, 1.0);
            auto take = [&](float curveValue) {
                processor.setCurve(Lane::filter, Curve({ { 0.0f, curveValue, 0.0f }, { 1.0f, curveValue, 0.0f } }));
                return render(processor, saw, kBpm);
            };
            const auto u = take(0.0f), i = take(1.0f);
            // The 8th harmonic (880 Hz) is in u's second formant and between i's first two; the 28th (3080 Hz) in
            // i's third formant and far above all of u's.
            const auto at = [](const Take& t, double frequency) { return amplitudeAt(t.left, frequency, 0.5); };
            const double uOver = decibels(at(u, 880.0) / at(i, 880.0)), iOver = decibels(at(i, 3080.0) / at(u, 3080.0));
            logMessage("880 Hz: u " + juce::String(uOver, 1) + " dB over i; 3080 Hz: i " + juce::String(iOver, 1)
                       + " dB over u");
            expectGreaterThan(uOver, 6.0);
            expectGreaterThan(iOver, 6.0);
            // Narrow bands, but brought back to about a bass's loudness, the dark vowel and the bright one alike.
            for (const float curveValue : { 0.0f, 1.0f })
            {
                processor.setCurve(Lane::filter, Curve({ { 0.0f, curveValue, 0.0f }, { 1.0f, curveValue, 0.0f } }));
                const auto vowel = render(processor, bass(), kBpm);
                const double change = tonwerktest::momentaryMax(vowel.left, vowel.right)
                                      - tonwerktest::momentaryMax(bass(), bass());
                expectWithinAbsoluteError(change, 0.0, 4.0);
            }
        }

        beginTest("Mix 0 leaves the input alone");
        {
            WobbleProcessor processor;
            set(processor, "mix", 0);
            set(processor, "volumeOn", 1);
            set(processor, "drive", 12);
            const auto source = sine(300.0, 0.5);
            const auto take = render(processor, source);
            float difference = 0.0f;
            for (std::size_t n = 0; n < source.size(); ++n)
                difference = std::max(difference, std::abs(take.left[n] - source[n]));
            expectLessThan(difference, 1.0e-6f);
        }

        beginTest("Curves are kept in the state and come with the presets");
        {
            WobbleProcessor processor;
            const auto drawn = gate("x..x..x.", 0.6f);
            processor.setCurve(Lane::volume, drawn);
            set(processor, "filterLow", 333);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);

            WobbleProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(juce::String(loaded.curve(Lane::volume).toString()), juce::String(drawn.toString()));
            expectEquals(juce::String(loaded.curve(Lane::filter).toString()),
                         juce::String(defaultCurve(Lane::filter).toString()));
            expectWithinAbsoluteError(loaded.state.getRawParameterValue("filterLow")->load(), 333.0f, 0.5f);

            // A preset brings its curves, and Init the defaults back.
            const auto& presets = factoryPresets();
            int trance = -1;
            for (int p = 0; p < (int) presets.size(); ++p)
                if (juce::String::fromUTF8(presets[(std::size_t) p].name) == "Trance-Gate")
                    trance = p;
            expectGreaterThan(trance, 0);
            loaded.setCurrentProgram(trance);
            expectEquals(juce::String(loaded.curve(Lane::volume).toString()),
                         juce::String(factoryCurves()[(std::size_t) trance].volume.toString()));
            loaded.setCurrentProgram(0);
            expectEquals(juce::String(loaded.curve(Lane::volume).toString()),
                         juce::String(defaultCurve(Lane::volume).toString()));
            expectEquals(factoryCurves().size(), presets.size());
        }

        beginTest("Cheap enough for many tracks");
        {
            WobbleProcessor processor;
            set(processor, "volumeOn", 1);
            set(processor, "drive", 6);
            set(processor, "spread", 90);
            const auto source = sine(110.0, 10.0);
            const auto start = std::chrono::steady_clock::now();
            render(processor, source);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double realtime = 10.0 / seconds;
            logMessage("Stereo with drive, filter and volume runs " + juce::String(realtime, 0)
                       + " times faster than real time");
            expectGreaterThan(realtime, 20.0);

            set(processor, "filterType", 3);
            const auto vowelStart = std::chrono::steady_clock::now();
            render(processor, source);
            const double vowel = 10.0 / std::chrono::duration<double>(std::chrono::steady_clock::now() - vowelStart).count();
            logMessage("The vowel filter runs " + juce::String(vowel, 0) + " times faster than real time");
            expectGreaterThan(vowel, 20.0);
        }

        beginTest("Editor");
        {
            WobbleProcessor processor;
            processor.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()), child->getName());
            if (auto* wobble = tonwerkui::innerEditor<WobbleEditor>(editor.get()))
            {
                // The menu's shapes draw into the processor.
                wobble->volume.view.applyMenu((int) Shape::square + 1);
                expectEquals(juce::String(processor.curve(Lane::volume).toString()),
                             juce::String(shapeCurve(Shape::square).toString()));
                wobble->volume.view.applyMenu((int) Shape::pump + 1);
            }
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                // Some sound first, so the playheads are somewhere.
                set(processor, "volumeOn", 1);
                render(processor, sine(110.0, 0.3));
                if (auto* wobble = tonwerkui::innerEditor<WobbleEditor>(editor.get()))
                {
                    wobble->filter.view.refresh();
                    wobble->volume.view.refresh();
                }
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Wobble.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }

private:
    /** How often a gated sine opens per second: rising edges of its level. */
    static double cyclesPerSecond(const Signal& signal)
    {
        const int window = 96;
        int openings = 0;
        bool open = false;
        std::size_t first = 0, last = 0;
        for (std::size_t at = 0; at + (std::size_t) window < signal.size(); at += (std::size_t) window)
        {
            double sum = 0.0;
            for (int n = 0; n < window; ++n)
                sum += std::abs(signal[at + (std::size_t) n]);
            const bool now = sum / window > 0.1;
            if (now && ! open)
            {
                if (openings == 0)
                    first = at;
                last = at;
                ++openings;
            }
            open = now;
        }
        return openings > 1 ? (openings - 1) / ((double) (last - first) / kRate) : 0.0;
    }
};

/**
 * Tonwerk Wobble's factory sounds: each in a group of its menu, every name once, and each about as loud as its dry
 * source (a held bass for wobbles and vowels, a pad for the rest, both at -6 dBFS, the host at 140 BPM).
 */
class WobblePresetTests : public juce::UnitTest
{
public:
    WobblePresetTests() : juce::UnitTest("Tonwerk Wobble factory sounds", "Presets") {}

    static constexpr double kTolerance = 2.0;

    void runTest() override
    {
        const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});
        beginTest("Tonwerk Wobble: groups, names and levels");
        const auto& presets = factoryPresets();
        std::set<juce::String> names;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            const auto& preset = presets[(std::size_t) i];
            WobbleProcessor processor;
            processor.setCurrentProgram(i);
            const auto sound = processor.getProgramName(i);
            expect(names.insert(sound).second, "twice: " + sound);
            expect(i == 0 || tonwerkui::categoryIndex(preset.category, kWobbleCategories) >= 0, "no group: " + sound);

            const auto& source = sourceFor(preset.category);
            const auto take = render(processor, source);
            const double change = tonwerktest::momentaryMax(take.left, take.right)
                                  - tonwerktest::momentaryMax(source, source);
            const float peak = peakOf(take);
            logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 20)
                       + juce::String::fromUTF8(preset.category).paddedRight(' ', 12) + juce::String(change, 1)
                       + " dB against dry, peak " + juce::String(peak, 2));
            if (i > 0)
            {
                expectWithinAbsoluteError(change, 0.0, kTolerance, sound);
                expectLessThan(peak, 1.0f, sound);
            }
            if (wavDir.isNotEmpty())
                writeWav(take.left, take.right,
                         juce::File(wavDir).getChildFile("Tonwerk Wobble").getChildFile(
                             juce::String(i + 1).paddedLeft('0', 2) + " " + sound.replaceCharacters("/&", "-+") + ".wav"));
        }
        if (wavDir.isNotEmpty())
        {
            writeWav(bass(), bass(), juce::File(wavDir).getChildFile("Tonwerk Wobble").getChildFile("00 Quelle Bass.wav"));
            writeWav(pad(), pad(), juce::File(wavDir).getChildFile("Tonwerk Wobble").getChildFile(juce::String::fromUTF8("00 Quelle Fläche.wav")));
        }
    }
};

static WobbleTests wobbleTests;
static WobblePresetTests wobblePresetTests;
