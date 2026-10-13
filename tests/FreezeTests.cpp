#include "FreezeEditor.h"
#include "FreezeEngine.h"
#include "FreezePresets.h"
#include "FreezeProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkfreeze;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kBpm = 120.0;

using Signal = std::vector<float>;

Signal sine(double hz, double seconds, float amplitude)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal operator+(Signal a, const Signal& b)
{
    a.resize(std::max(a.size(), b.size()), 0.0f);
    for (std::size_t n = 0; n < b.size(); ++n)
        a[n] += b[n];
    return a;
}

/** A sawtooth with PolyBLEP edges, as a synth plays it. */
void addSaw(Signal& out, double from, double to, double hz, float amplitude)
{
    const double dt = hz / kRate;
    double phase = 0.0;
    for (auto n = (std::size_t) (from * kRate); n < std::min(out.size(), (std::size_t) (to * kRate)); ++n)
    {
        double v = 2.0 * phase - 1.0;
        if (phase < dt)
        {
            const double t = phase / dt;
            v -= t + t - t * t - 1.0;
        }
        else if (phase > 1.0 - dt)
        {
            const double t = (phase - 1.0) / dt;
            v -= t * t + t + t + 1.0;
        }
        out[n] += amplitude * (float) v;
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}

double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

/** A pad: a chord of detuned sawtooths, softened by a one-pole lowpass, from `from` to `to` seconds. */
void addChord(Signal& out, double from, double to, std::initializer_list<int> notes, float amplitude = 0.08f)
{
    Signal chord(out.size(), 0.0f);
    for (const int note : notes)
    {
        addSaw(chord, from, to, hz(note) * 1.003, amplitude);
        addSaw(chord, from, to, hz(note) / 1.003, amplitude);
    }
    float state = 0.0f;
    const float a = 1.0f - std::exp(-2.0f * (float) kPi * 2500.0f / (float) kRate);
    const auto start = (std::size_t) (from * kRate), end = std::min(out.size(), (std::size_t) (to * kRate));
    for (std::size_t n = start; n < end; ++n)
    {
        state += a * (chord[n] - state);
        // 20 ms in and out, so the chord's edges do not click.
        const double edge = std::min({ 1.0, (double) (n - start) / (0.02 * kRate), (double) (end - n) / (0.02 * kRate) });
        out[n] += (float) edge * state;
    }
}

/** A plucked note (Karplus-Strong), for a melody over the frozen pad. */
void addPluck(Signal& out, double at, double seconds, int midi, float velocity, juce::Random& random)
{
    const auto period = std::max(2, (int) std::round(kRate / hz(midi)));
    std::vector<float> line((std::size_t) period);
    for (auto& s : line)
        s = velocity * (random.nextFloat() * 2.0f - 1.0f);
    const auto from = (std::size_t) (at * kRate);
    std::size_t i = 0;
    for (std::size_t n = 0; n < (std::size_t) (seconds * kRate) && from + n < out.size(); ++n)
    {
        const auto next = (i + 1) % line.size();
        const float s = line[i];
        line[i] = 0.498f * (s + line[next]);
        out[from + n] += s;
        i = next;
    }
}

/** A sung "a": a sawtooth through three formants, at `note` from `from` to `to`, with a little vibrato. */
void addVowel(Signal& out, double from, double to, int note, float amplitude)
{
    struct Resonator
    {
        double b0, a1, a2, y1 = 0, y2 = 0, x2 = 0, x1 = 0;
        Resonator(double f, double bw)
        {
            const double r = std::exp(-kPi * bw / kRate);
            a1 = -2.0 * r * std::cos(kTwoPi * f / kRate);
            a2 = r * r;
            b0 = 1.0 - r;
        }
        double process(double x)
        {
            const double y = b0 * (x - x2) - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Resonator f1(730, 90), f2(1090, 110), f3(2440, 160);
    double phase = 0.0;
    const auto start = (std::size_t) (from * kRate), end = std::min(out.size(), (std::size_t) (to * kRate));
    for (std::size_t n = start; n < end; ++n)
    {
        const double t = (double) (n - start) / kRate;
        const double f = hz(note) * (1.0 + 0.004 * std::sin(kTwoPi * 5.5 * t) * std::min(1.0, t));
        phase += f / kRate;
        phase -= std::floor(phase);
        const double source = 2.0 * phase - 1.0;
        const double edge = std::min({ 1.0, t / 0.05, (double) (end - n) / (0.05 * kRate) });
        out[n] += (float) (edge * amplitude * (f1.process(source) + 0.6 * f2.process(source) + 0.3 * f3.process(source)));
    }
}

struct Take
{
    Signal left, right;
};

Take runEngine(Engine& engine, const Signal& left, const Signal& right)
{
    Take take { Signal(left.size()), Signal(left.size()) };
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        engine.process(left.data() + at, right.data() + at, take.left.data() + at, take.right.data() + at, count);
    }
    return take;
}

/** The amplitude of `f` in `signal[from, to)` (Goertzel over a Hann window). */
double amplitudeAt(const Signal& signal, double f, double from, double to)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const auto a = (std::size_t) (from * kRate), b = (std::size_t) (to * kRate);
    const double w = kTwoPi * f / kRate;
    for (std::size_t n = a; n < b; ++n)
    {
        const double h = 0.5 - 0.5 * std::cos(kTwoPi * (double) (n - a) / (double) (b - a));
        re += h * signal[n] * std::cos(w * (double) n);
        im += h * signal[n] * std::sin(w * (double) n);
        sum += h;
    }
    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

double rms(const Signal& signal, double from, double to)
{
    double sum = 0.0;
    const auto a = (std::size_t) (from * kRate), b = (std::size_t) (to * kRate);
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return std::sqrt(sum / (double) (b - a));
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

Signal slice(const Signal& signal, double from, double to)
{
    return Signal(signal.begin() + (long) (from * kRate), signal.begin() + (long) std::min((double) signal.size(), to * kRate));
}

/** A change to a parameter at a time, as Logic's automation would make it. */
struct Change
{
    double seconds;
    const char* id;
    float value;
};

/** Runs a stereo source through the processor, the host playing at 120 BPM from beat 0 (or stopped). */
Take render(FreezeProcessor& processor, const Signal& left, const Signal& right, std::vector<Change> changes,
            bool playing = true)
{
    struct PlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(playing);
            info.setBpm(kBpm);
            info.setPpqPosition(ppq);
            return info;
        }
    } head;
    head.playing = playing;
    processor.setPlayHead(&head);
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { left, right };
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    std::size_t next = 0;
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        while (next < changes.size() && changes[next].seconds * kRate <= (double) at)
        {
            auto* parameter = processor.state.getParameter(changes[next].id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(changes[next].value));
            ++next;
        }
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        buffer.setSize(2, count, false, false, true);
        buffer.copyFrom(0, 0, left.data() + at, count);
        buffer.copyFrom(1, 0, right.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, take.right.data() + at);
        if (playing)
            head.ppq += count * kBpm / 60.0 / kRate;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return take;
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

Settings still()
{
    Settings s;
    s.motion = 0.0f;
    s.blur = 0.0f;
    s.width = 0.0f;
    s.mix = 1.0f;
    s.fadeMs = 200.0f;
    return s;
}
} // namespace

class FreezeTests : public juce::UnitTest
{
public:
    FreezeTests() : juce::UnitTest("Tonwerk Freeze", "Freeze") {}

    void runTest() override
    {
        beginTest("A frozen sine keeps its pitch and level after the input stops, and stays pure");
        {
            Engine engine;
            engine.update(still());
            engine.prepare(kRate);
            auto input = sine(440.0, 1.0, 0.5f);
            input.resize((std::size_t) (5.0 * kRate), 0.0f);
            Take take;
            {
                const auto first = slice(input, 0.0, 0.9), rest = slice(input, 0.9, 5.0);
                const auto a = runEngine(engine, first, first);
                engine.setFrozen(true);
                const auto b = runEngine(engine, rest, rest);
                take.left = a.left;
                take.left.insert(take.left.end(), b.left.begin(), b.left.end());
                take.right = a.right;
                take.right.insert(take.right.end(), b.right.begin(), b.right.end());
            }
            for (const double at : { 1.5, 3.0, 4.5 })
            {
                expectWithinAbsoluteError(db(amplitudeAt(take.left, 440.0, at, at + 0.4) / 0.5), 0.0, 1.0,
                                          "level at " + juce::String(at) + " s");
                // Nothing but the sine: its amplitude carries all of the signal's power.
                const double total = rms(take.left, at, at + 0.4) * std::sqrt(2.0);
                expectWithinAbsoluteError(db(amplitudeAt(take.left, 440.0, at, at + 0.4) / total), 0.0, 0.5, "purity");
            }
            float apart = 0.0f;
            for (std::size_t n = 0; n < take.left.size(); ++n)
                apart = std::max(apart, std::abs(take.left[n] - take.right[n]));
            expectLessThan(apart, 1.0e-3f, "a mono input at width 0 stays mono");
            for (const double off : { 430.0, 450.0 })
                expectLessThan(amplitudeAt(take.left, off, 2.0, 4.0), 0.5 * 0.03, "beside the sine");
        }

        beginTest("A frozen chord holds every note at its level");
        {
            Engine engine;
            engine.update(still());
            engine.prepare(kRate);
            const auto chord = sine(hz(57), 3.0, 0.2f) + sine(hz(60), 3.0, 0.2f) + sine(hz(64), 3.0, 0.2f);
            const auto first = slice(chord, 0.0, 1.0);
            runEngine(engine, first, first);
            engine.setFrozen(true);
            const Signal silence((std::size_t) (3.0 * kRate), 0.0f);
            const auto take = runEngine(engine, silence, silence);
            for (const int note : { 57, 60, 64 })
                expectWithinAbsoluteError(db(amplitudeAt(take.left, hz(note), 1.0, 2.5) / 0.2), 0.0, 1.5,
                                          "note " + juce::String(note));
        }

        beginTest("The freeze fades in and out over its time, and the dry passes untouched while not frozen");
        {
            Engine engine;
            auto s = still();
            s.fadeMs = 1000.0f;
            s.mix = 0.5f;
            engine.update(s);
            engine.prepare(kRate);
            const auto tone = sine(440.0, 1.0, 0.5f);
            const auto dry = runEngine(engine, tone, tone);
            expect(std::equal(dry.left.begin(), dry.left.end(), tone.begin()), "not frozen: the input as it was");
            engine.setFrozen(true);
            const Signal silence((std::size_t) (2.0 * kRate), 0.0f);
            const auto in = runEngine(engine, silence, silence);
            const double full = amplitudeAt(in.left, 440.0, 1.4, 1.9);
            expectWithinAbsoluteError(db(full / 0.5), 0.0, 1.0, "faded in");
            expectWithinAbsoluteError(amplitudeAt(in.left, 440.0, 0.4, 0.6) / full, 0.5, 0.1, "halfway at half the time");
            engine.setFrozen(false);
            const auto out = runEngine(engine, silence, silence);
            expectWithinAbsoluteError(amplitudeAt(out.left, 440.0, 0.4, 0.6) / full, 0.5, 0.1, "halfway out");
            expectLessThan(rms(out.left, 1.2, 2.0), 1.0e-6, "gone after the fade");
        }

        beginTest("Mix: at 50 % the dry stays at full level over the pad, at 100 % only the pad is heard");
        {
            for (const float mix : { 0.5f, 1.0f })
            {
                Engine engine;
                auto s = still();
                s.mix = mix;
                engine.update(s);
                engine.prepare(kRate);
                const auto a = sine(440.0, 1.0, 0.5f);
                runEngine(engine, a, a);
                engine.setFrozen(true);
                const auto b = sine(660.0, 2.0, 0.5f);
                const auto take = runEngine(engine, b, b);
                const double newTone = amplitudeAt(take.left, 660.0, 1.0, 1.8);
                const double frozen = amplitudeAt(take.left, 440.0, 1.0, 1.8);
                expectWithinAbsoluteError(db(frozen / 0.5), 0.0, 1.0, "the pad at mix " + juce::String(mix));
                if (mix < 0.75f)
                    expectWithinAbsoluteError(db(newTone / 0.5), 0.0, 0.2, "the dry at 50 %");
                else
                    expectLessThan(newTone, 0.5 * 0.01, "no dry at 100 %");
            }
        }

        beginTest("Pitch and shimmer move the frozen partials; the cuts take the bottom and the top away");
        {
            auto frozenTake = [](Settings s, double f) {
                Engine engine;
                engine.update(s);
                engine.prepare(kRate);
                const auto tone = sine(f, 1.0, 0.5f);
                runEngine(engine, tone, tone);
                engine.setFrozen(true);
                const Signal silence((std::size_t) (2.0 * kRate), 0.0f);
                return runEngine(engine, silence, silence);
            };
            auto s = still();
            s.pitch = 12.0f;
            auto up = frozenTake(s, 440.0);
            expectWithinAbsoluteError(db(amplitudeAt(up.left, 880.0, 1.0, 1.8) / 0.5), 0.0, 1.5, "an octave up");
            expectLessThan(amplitudeAt(up.left, 440.0, 1.0, 1.8), 0.5 * 0.03, "nothing left at the old pitch");
            s.pitch = -7.0f;
            auto down = frozenTake(s, 660.0);
            expectWithinAbsoluteError(db(amplitudeAt(down.left, 660.0 * std::pow(2.0, -7.0 / 12.0), 1.0, 1.8) / 0.5), 0.0,
                                      1.5, "a fifth down");
            s.pitch = 0.0f;
            s.shimmer = 0.5f;
            auto shimmer = frozenTake(s, 440.0);
            expectWithinAbsoluteError(db(amplitudeAt(shimmer.left, 440.0, 1.0, 1.8) / 0.5), 0.0, 1.0, "the tone");
            expectWithinAbsoluteError(db(amplitudeAt(shimmer.left, 880.0, 1.0, 1.8) / 0.25), 0.0, 1.5, "its octave at half");
            s.shimmer = 0.0f;
            s.lowCutHz = 1000.0f;
            expectLessThan(db(amplitudeAt(frozenTake(s, 200.0).left, 200.0, 1.0, 1.8) / 0.5), -40.0, "low cut");
            s.lowCutHz = 20.0f;
            s.highCutHz = 1000.0f;
            expectLessThan(db(amplitudeAt(frozenTake(s, 5000.0).left, 5000.0, 1.0, 1.8) / 0.5), -40.0, "high cut");
        }

        beginTest("Width parts a mono input; motion moves the pad's level, blur keeps it");
        {
            auto pad = [](Settings s) {
                Engine engine;
                engine.update(s);
                engine.prepare(kRate);
                Signal chord((std::size_t) (1.0 * kRate), 0.0f);
                addChord(chord, 0.0, 1.0, { 57, 60, 64 });
                runEngine(engine, chord, chord);
                engine.setFrozen(true);
                const Signal silence((std::size_t) (12.0 * kRate), 0.0f);
                return runEngine(engine, silence, silence);
            };
            auto correlation = [](const Take& t) {
                double lr = 0.0, ll = 0.0, rr = 0.0;
                for (std::size_t n = (std::size_t) kRate; n < t.left.size(); ++n)
                {
                    lr += (double) t.left[n] * t.right[n];
                    ll += (double) t.left[n] * t.left[n];
                    rr += (double) t.right[n] * t.right[n];
                }
                return lr / std::sqrt(ll * rr);
            };
            auto s = still();
            const auto narrow = pad(s);
            s.width = 1.0f;
            const auto wide = pad(s);
            expectGreaterThan(correlation(narrow), 0.999, "width 0");
            logMessage("correlation at width 100 %: " + juce::String(correlation(wide), 2));
            expectLessThan(std::abs(correlation(wide)), 0.2, "width 100 %");
            expectWithinAbsoluteError(db(rms(wide.left, 2.0, 12.0) / rms(narrow.left, 2.0, 12.0)), 0.0, 1.0, "same level");

            // The level of each half second: still without motion, moving with it.
            auto spread = [](const Take& t) {
                double lowest = 1.0e9, highest = 0.0;
                for (double at = 1.0; at + 0.5 <= 12.0; at += 0.5)
                {
                    const double level = rms(t.left, at, at + 0.5);
                    lowest = std::min(lowest, level);
                    highest = std::max(highest, level);
                }
                return db(highest / lowest);
            };
            s.width = 0.0f;
            s.motion = 1.0f;
            s.driftSeconds = 2.0f;
            const auto moving = pad(s);
            logMessage("level spread over 11 s: still " + juce::String(spread(narrow), 2) + " dB, moving "
                       + juce::String(spread(moving), 2) + " dB");
            expectLessThan(spread(narrow), 0.5, "no motion");
            expectGreaterThan(spread(moving), 1.5, "motion");
            s.motion = 0.0f;
            for (const float blur : { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f })
            {
                s.blur = blur;
                const auto washed = pad(s);
                const double change = db(rms(washed.left, 2.0, 12.0) / rms(narrow.left, 2.0, 12.0));
                logMessage("blur " + juce::String(blur, 1) + ": " + juce::String(change, 2) + " dB");
                expectWithinAbsoluteError(change, 0.0, 1.0, "blur");
            }
        }

        beginTest("A new freeze morphs to the new sound, or with stack keeps both");
        {
            for (const float stack : { 0.0f, 1.0f })
            {
                Engine engine;
                auto s = still();
                s.stack = stack;
                s.fadeMs = 300.0f;
                engine.update(s);
                engine.prepare(kRate);
                const auto a = sine(440.0, 1.0, 0.5f);
                runEngine(engine, a, a);
                engine.setFrozen(true);
                const auto b = sine(660.0, 1.0, 0.5f);
                runEngine(engine, b, b);
                engine.capture();
                const Signal silence((std::size_t) (2.0 * kRate), 0.0f);
                const auto take = runEngine(engine, silence, silence);
                const double oldTone = db(amplitudeAt(take.left, 440.0, 1.0, 1.8) / 0.5);
                const double newTone = db(amplitudeAt(take.left, 660.0, 1.0, 1.8) / 0.5);
                if (stack < 0.5f)
                {
                    expectLessThan(oldTone, -30.0, "replaced");
                    expectWithinAbsoluteError(newTone, 0.0, 1.0, "the new one");
                }
                else
                {
                    // Both, the pair as loud as the louder one: 3 dB under each alone.
                    expectWithinAbsoluteError(oldTone, -3.0, 1.0, "stacked: the old one");
                    expectWithinAbsoluteError(newTone, -3.0, 1.0, "stacked: the new one");
                }
                expectEquals(engine.freezesTaken(), 2);
            }
        }

        beginTest("On tempo it freezes anew on every grid line while the song plays");
        {
            FreezeProcessor processor;
            processor.state.getParameter("trigger")->setValueNotifyingHost(1.0f);
            processor.state.getParameter("interval")->setValueNotifyingHost(
                processor.state.getParameter("interval")->convertTo0to1(0.0f));
            // Quarter notes at 120 BPM over 2 s: the lines at 0, 0.5, 1.0 and 1.5 s.
            Signal tone = sine(440.0, 2.0, 0.3f);
            render(processor, tone, tone, { { 0.0, "freeze", 1.0f } });
            expectEquals(processor.freezes.load(), 4);
            render(processor, tone, tone, { { 0.0, "freeze", 1.0f } }, false);
            expectEquals(processor.freezes.load(), 1, "stopped: only the freeze the switch takes");
            processor.state.getParameter("trigger")->setValueNotifyingHost(0.0f);
            render(processor, tone, tone, { { 0.0, "freeze", 1.0f } });
            expectEquals(processor.freezes.load(), 1, "manual");
        }

        beginTest("The processor: a mono track, its state, its texts and its editor");
        {
            FreezeProcessor processor;
            juce::AudioProcessor::BusesLayout mono;
            mono.inputBuses.add(juce::AudioChannelSet::mono());
            mono.outputBuses.add(juce::AudioChannelSet::mono());
            expect(processor.setBusesLayout(mono));
            processor.prepareToPlay(kRate, kBlock);
            juce::AudioBuffer<float> buffer(1, kBlock);
            juce::MidiBuffer midi;
            processor.state.getParameter("mix")->setValueNotifyingHost(1.0f);
            processor.state.getParameter("motion")->setValueNotifyingHost(0.0f);
            const auto tone = sine(440.0, 3.0, 0.5f);
            Signal out(tone.size());
            for (std::size_t at = 0; at + kBlock <= tone.size(); at += kBlock)
            {
                if (at == 48000 / kBlock * kBlock)
                    processor.state.getParameter("freeze")->setValueNotifyingHost(1.0f);
                buffer.copyFrom(0, 0, tone.data() + at, kBlock);
                processor.processBlock(buffer, midi);
                std::copy_n(buffer.getReadPointer(0), kBlock, out.data() + at);
            }
            expectWithinAbsoluteError(db(amplitudeAt(out, 440.0, 2.2, 2.9) / 0.5), 0.0, 1.0, "mono");
            expectLessThan(amplitudeAt(out, 440.0, 0.2, 0.9) - 0.5, 1.0e-3, "dry until frozen");
            expect(processor.liveBands[40].load() > -100.0f && processor.frozenLevel.load() > 0.99f);

            processor.state.getParameter("pitch")->setValueNotifyingHost(processor.state.getParameter("pitch")->convertTo0to1(7.0f));
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            FreezeProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().pitch, 7.0f, 1.0e-3f);

            auto text = [&](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("capture", 300.0f), juce::String("300 ms"));
            expectEquals(text("fade", 2500.0f), juce::String("2500 ms"));
            expectEquals(text("pitch", -12.0f), juce::String("-12 HT"));
            expectEquals(text("motion", 0.3f), juce::String("30 %"));
            expectEquals(text("drift", 6.0f), juce::String("6 s"));
            expectEquals(text("lowCut", 80.0f), juce::String("80 Hz"));
            expectEquals(text("highCut", 5000.0f), juce::String("5 kHz"));
            expectEquals(text("level", -3.0f), juce::String("-3 dB"));

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()), child->getName());
            if (auto* freeze = dynamic_cast<FreezeEditor*>(editor.get()))
            {
                freeze->spectrum.refresh();
                // A click lets go of the freeze, a second takes it again.
                freeze->spectrum.toggleFreeze();
                expect(processor.state.getRawParameterValue("freeze")->load() < 0.5f, "clicked off");
                processor.state.getParameter("freeze")->setValueNotifyingHost(1.0f);
            }
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                // A pad frozen an octave down with a melody over it, as the README shows it.
                FreezeProcessor shown;
                shown.setCurrentProgram(3);
                Signal pad((std::size_t) (6.0 * kRate), 0.0f);
                addChord(pad, 0.0, 2.0, { 45, 57, 60, 64 });
                juce::Random random(3);
                addPluck(pad, 5.5, 0.5, 76, 0.4f, random);
                render(shown, pad, pad, { { 1.5, "freeze", 1.0f } });
                editor.reset(shown.createEditor());
                if (auto* freeze = dynamic_cast<FreezeEditor*>(editor.get()))
                    freeze->spectrum.refresh();
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Freeze.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }

        beginTest("CPU: a frozen stereo pad with every option on costs little");
        {
            Engine engine;
            Settings s;
            s.pitch = 7.0f;
            s.shimmer = 0.5f;
            s.motion = 0.6f;
            s.blur = 0.5f;
            s.lowCutHz = 100.0f;
            s.highCutHz = 8000.0f;
            engine.update(s);
            engine.prepare(kRate);
            Signal chord((std::size_t) (10.0 * kRate), 0.0f);
            addChord(chord, 0.0, 10.0, { 45, 57, 60, 64 });
            runEngine(engine, slice(chord, 0.0, 1.0), slice(chord, 0.0, 1.0));
            engine.setFrozen(true);
            const auto start = std::chrono::steady_clock::now();
            runEngine(engine, chord, chord);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            logMessage("real-time factor " + juce::String(seconds / 10.0, 4));
            expectLessThan(seconds / 10.0, 0.05);
        }

        presets();
    }

    void presets()
    {
        // For listening: TONWERK_FREEZE_WAV_DIR=<dir> writes each sound's take and the dry source as WAV files.
        const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_FREEZE_WAV_DIR", {});

        beginTest("Factory sounds: groups, names, and the frozen layer as loud as what it froze");
        {
            // A held A minor pad: frozen at 2.5 s, the dry given way (mix 100 %) to hear the layer alone.
            Signal pad((std::size_t) (11.0 * kRate), 0.0f);
            addChord(pad, 0.0, 11.0, { 45, 57, 60, 64 });
            const double dry = tonwerktest::momentaryMax(slice(pad, 0.5, 2.5), slice(pad, 0.5, 2.5));
            const auto& list = factoryPresets();
            std::set<juce::String> names;
            for (int i = 0; i < (int) list.size(); ++i)
            {
                const auto& preset = list[(std::size_t) i];
                FreezeProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(preset.category, kFreezeCategories) >= 0, "no group: " + sound);
                expect(processor.state.getRawParameterValue("freeze")->load() < 0.5f, "frozen on load: " + sound);
                const double fade = processor.readSettings().fadeMs / 1000.0;
                const auto take = render(processor, pad, pad, { { 0.0, "mix", 1.0f }, { 2.5, "freeze", 1.0f } });
                const double wet = tonwerktest::momentaryMax(slice(take.left, 3.0 + fade, 11.0), slice(take.right, 3.0 + fade, 11.0));
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 22)
                           + juce::String::fromUTF8(preset.category).paddedRight(' ', 14) + juce::String(wet - dry, 1)
                           + " dB against dry, peak " + juce::String(peak, 2));
                expectWithinAbsoluteError(wet - dry, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
            }
        }

        if (wavDir.isEmpty())
            return;

        beginTest("Listening: each factory sound on a pad with a melody over it, and a sung voice");
        {
            // Two bars of A minor with an arpeggio; frozen at the end of bar two. Then only a melody plays, over the
            // pad; the freeze lets go at 12 s. The rhythmic sounds get a chord a bar (Am, F, C, G) under the melody.
            juce::Random random(7);
            Signal source((std::size_t) (16.0 * kRate), 0.0f), changing((std::size_t) (16.0 * kRate), 0.0f);
            addChord(source, 0.0, 4.0, { 45, 57, 60, 64 });
            for (int step = 0; step < 16; ++step)
                addPluck(source, step * 0.25, 1.0, std::array<int, 4> { 69, 72, 76, 72 }[(std::size_t) step % 4], 0.25f, random);
            const std::array<std::initializer_list<int>, 4> chords { { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } } };
            for (int bar = 0; bar < 6; ++bar)
                addChord(changing, bar * 2.0, bar * 2.0 + 2.0, chords[(std::size_t) bar % 4]);
            const std::array<int, 12> melody { 76, 74, 72, 74, 76, 79, 81, 79, 76, 74, 72, 69 };
            for (std::size_t n = 0; n < melody.size(); ++n)
            {
                addPluck(source, 4.0 + 0.66 * (double) n, 1.5, melody[n], 0.35f, random);
                addPluck(changing, 4.0 + 0.66 * (double) n, 1.5, melody[n], 0.35f, random);
            }
            const juce::File dir(wavDir);
            writeWav(source, source, dir.getChildFile("00 Quelle.wav"));
            writeWav(changing, changing, dir.getChildFile("00 Quelle (Akkordfolge).wav"));
            const auto& list = factoryPresets();
            for (int i = 1; i < (int) list.size(); ++i)
            {
                FreezeProcessor processor;
                processor.setCurrentProgram(i);
                const bool rhythmic = std::strcmp(list[(std::size_t) i].category, "Rhythmisch") == 0;
                const auto take = rhythmic ? render(processor, changing, changing, { { 0.2, "freeze", 1.0f }, { 12.0, "freeze", 0.0f } })
                                           : render(processor, source, source, { { 3.9, "freeze", 1.0f }, { 12.0, "freeze", 0.0f } });
                writeWav(take.left, take.right,
                         dir.getChildFile(juce::String(i).paddedLeft('0', 2) + " " + processor.getProgramName(i) + ".wav"));
            }

            // A voice: three sung notes, the second frozen, the third sung over the choir it makes.
            Signal voice((std::size_t) (10.0 * kRate), 0.0f);
            addVowel(voice, 0.0, 1.5, 57, 0.3f);
            addVowel(voice, 1.6, 3.4, 60, 0.3f);
            addVowel(voice, 4.5, 6.5, 64, 0.3f);
            addVowel(voice, 6.6, 8.0, 62, 0.3f);
            writeWav(voice, voice, dir.getChildFile("20 Quelle Stimme.wav"));
            const std::array<const char*, 3> voiceSounds { "Engelschor", "Paulstretch", "Geisterstimme" };
            for (std::size_t v = 0; v < voiceSounds.size(); ++v)
            {
                const auto sound = juce::String::fromUTF8(voiceSounds[v]);
                FreezeProcessor processor;
                for (int i = 0; i < (int) list.size(); ++i)
                    if (processor.getProgramName(i) == sound)
                        processor.setCurrentProgram(i);
                const auto take = render(processor, voice, voice, { { 3.0, "freeze", 1.0f }, { 9.0, "freeze", 0.0f } });
                writeWav(take.left, take.right, dir.getChildFile(juce::String(21 + (int) v) + " Stimme " + sound + ".wav"));
            }
        }
    }
};

static FreezeTests freezeTests;
