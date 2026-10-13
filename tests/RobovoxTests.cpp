#include "RobovoxEditor.h"
#include "RobovoxEngine.h"
#include "RobovoxProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkrobovox;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

using Signal = std::vector<float>;

/**
 * A sung vowel: a band-limited sawtooth at `hz` (from `hz2` on after `switchAt` seconds) through the formants of "a",
 * peaking near 0.5.
 */
Signal vowel(double hz, double seconds, double hz2 = 0.0, double switchAt = 1.0e9)
{
    struct Resonator
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        Resonator(double f, double bw)
        {
            const double w = 2.0 * kPi * f / kRate, alpha = std::sin(w) * std::sinh(std::log(2.0) / 2.0 * bw / f * w / std::sin(w));
            b0 = alpha / (1 + alpha);
            b2 = -b0;
            a1 = -2.0 * std::cos(w) / (1 + alpha);
            a2 = (1 - alpha) / (1 + alpha);
        }
        double process(double x)
        {
            const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Resonator f1(730, 90), f2(1090, 110), f3(2440, 160);
    Signal out((std::size_t) (seconds * kRate));
    double phase = 0.0;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate, f = t >= switchAt ? hz2 : hz;
        phase = std::fmod(phase + f / kRate, 1.0);
        double source = 0.0;
        for (int k = 1; k * f < 6000.0; ++k)
            source += std::sin(2.0 * kPi * k * phase) / k;
        out[n] = (float) (0.6 * (f1.process(source) + 0.5 * f2.process(source) + 0.25 * f3.process(source)));
    }
    return out;
}

Signal run(Engine& engine, const Signal& in)
{
    Signal out(in.size());
    for (std::size_t at = 0; at < in.size(); at += kBlock)
        engine.process(in.data() + at, out.data() + at, (int) std::min<std::size_t>(kBlock, in.size() - at));
    return out;
}

/** The amplitude of `hz` in `signal[from, to)` (Goertzel over a Hann window). */
double amplitudeAt(const Signal& signal, double hz, std::size_t from, std::size_t to)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double w = 2.0 * kPi * hz / kRate;
    for (std::size_t n = from; n < to; ++n)
    {
        const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * (double) (n - from) / (double) (to - from));
        re += h * signal[n] * std::cos(w * (double) n);
        im += h * signal[n] * std::sin(w * (double) n);
        sum += h;
    }
    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

/** How much of `signal[from, to)` lies on the harmonics of `hz` rather than halfway between them, in dB. */
double combContrast(const Signal& signal, double hz, std::size_t from, std::size_t to)
{
    double on = 0.0, between = 0.0;
    for (int k = 1; k * hz < 2500.0; ++k)
    {
        on += std::pow(amplitudeAt(signal, k * hz, from, to), 2.0);
        between += std::pow(amplitudeAt(signal, (k + 0.5) * hz, from, to), 2.0);
    }
    return 10.0 * std::log10(on / (between + 1.0e-20));
}

double rms(const Signal& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return std::sqrt(sum / (double) (to - from));
}

double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
} // namespace

class RobovoxTests : public juce::UnitTest
{
public:
    RobovoxTests() : juce::UnitTest("Tonwerk Robovox", "Robovox") {}

    void runTest() override
    {
        beginTest("Notes snap to the scale, hold through a wobble and move only when heard twice");
        {
            // D minor: D E F G A B(flat) C.
            expectEquals(nearestInScale(62.4, 2, Scale::minor), 62);
            expectEquals(nearestInScale(63.4, 2, Scale::minor), 64);
            expectEquals(nearestInScale(65.8, 2, Scale::minor), 65);
            expectEquals(nearestInScale(66.2, 2, Scale::minor), 67);
            expectEquals(nearestInScale(70.9, 2, Scale::minor), 70);
            expectEquals(nearestInScale(61.2, 2, Scale::chromatic), 61);
            Quantiser q;
            expectEquals(q.next(57.1, 2, Scale::minor), 57);
            // Just past the boundary to B flat: the hysteresis keeps A.
            expectEquals(q.next(57.6, 2, Scale::minor), 57);
            // Clearly B flat, but heard once: still A; twice: B flat.
            expectEquals(q.next(58.0, 2, Scale::minor), 57);
            expectEquals(q.next(58.0, 2, Scale::minor), 58);
        }

        beginTest("The pitch tracker reads a voice's range within 10 cents and stays quiet on noise and silence");
        {
            for (const double f : { 82.0, 110.0, 220.0, 440.0, 880.0 })
            {
                PitchTracker tracker;
                tracker.prepare(kRate);
                const auto voice = vowel(f, 0.2);
                for (const float x : voice)
                    tracker.push(x);
                const double read = tracker.analyse();
                expectWithinAbsoluteError(1200.0 * std::log2(read / f), 0.0, 10.0, juce::String(f) + " Hz");
            }
            PitchTracker tracker;
            tracker.prepare(kRate);
            for (int n = 0; n < 9600; ++n)
                tracker.push(0.0f);
            expectEquals(tracker.analyse(), 0.0);
            juce::Random random(1);
            for (int n = 0; n < 9600; ++n)
                tracker.push(random.nextFloat() - 0.5f);
            expectEquals(tracker.analyse(), 0.0);
        }

        beginTest("The robot sings two octaves under the voice, on a steady comb of harmonics");
        {
            Engine engine;
            engine.prepare(kRate);
            engine.update({});
            // A3 is in D minor: the carrier plays A1, 55 Hz.
            const auto out = run(engine, vowel(220.0, 1.5));
            expectEquals(engine.carrierNote(), 33);
            const auto from = (std::size_t) (0.5 * kRate), to = (std::size_t) (1.4 * kRate);
            const double contrast = combContrast(out, 55.0, from, to);
            logMessage("Harmonics of 55 Hz stand " + juce::String(contrast, 1) + " dB over the gaps between them");
            expectGreaterThan(contrast, 15.0);
            // And the voice's own pitch is gone: its odd multiples of 55 Hz are no stronger than the even ones.
            expectGreaterThan(amplitudeAt(out, 165.0, from, to), 0.2 * amplitudeAt(out, 220.0, from, to));
        }

        beginTest("Without glide the carrier steps to the next note within a syllable");
        {
            Engine engine;
            engine.prepare(kRate);
            engine.update({});
            // A3, then G3 from 0.8 s: the carrier goes from A1 to G1.
            const auto voice = vowel(220.0, 1.6, 196.0, 0.8);
            Signal out(voice.size());
            int changedAt = -1;
            for (std::size_t at = 0; at < voice.size(); at += kBlock)
            {
                engine.process(voice.data() + at, out.data() + at, kBlock);
                if (changedAt < 0 && engine.carrierNote() == 31)
                    changedAt = (int) at;
            }
            const double after = (changedAt - 0.8 * kRate) / kRate * 1000.0;
            logMessage("The carrier changed " + juce::String(after, 0) + " ms after the voice");
            expectGreaterThan(changedAt, (int) (0.8 * kRate));
            expectLessThan(after, 60.0);
            const auto latency = (std::size_t) engine.latency();
            expectGreaterThan(combContrast(out, hz(31), (std::size_t) (1.0 * kRate) + latency, voice.size()), 15.0);
        }

        beginTest("MIDI sets the carrier's note, whatever the voice sings");
        {
            Engine engine;
            engine.prepare(kRate);
            Settings settings;
            settings.mode = Mode::midi;
            engine.update(settings);
            engine.setMidiNote(40);
            const auto out = run(engine, vowel(220.0, 1.0));
            expectEquals(engine.carrierNote(), 40);
            expectGreaterThan(combContrast(out, hz(40), (std::size_t) (0.4 * kRate), out.size()), 15.0);
        }

        beginTest("About as loud as the voice, silent when it is, and its dry path lines up with the latency");
        {
            Engine engine;
            engine.prepare(kRate);
            engine.update({});
            auto voice = vowel(196.0, 1.5);
            voice.resize(voice.size() + (std::size_t) kRate, 0.0f);
            const auto out = run(engine, voice);
            const auto latency = (std::size_t) engine.latency();
            const double change = 20.0 * std::log10(rms(out, 24000 + latency, 72000 + latency) / rms(voice, 24000, 72000));
            logMessage("Level against the voice: " + juce::String(change, 1) + " dB");
            expectWithinAbsoluteError(change, 0.0, 4.0);
            expectLessThan(rms(out, (std::size_t) (2.0 * kRate), out.size()), 1.0e-5);

            Settings dry;
            dry.mix = 0.0f;
            engine.prepare(kRate);
            engine.update(dry);
            const auto through = run(engine, voice);
            for (std::size_t n = latency; n < voice.size(); n += 997)
                expectWithinAbsoluteError(through[n], voice[n - latency], 1.0e-6f);
        }

        beginTest("Formants move with the formant knob, not the carrier");
        {
            auto centroid = [](const Signal& s) {
                double weighted = 0.0, total = 0.0;
                for (double f = 100.0; f < 4000.0; f += 50.0)
                {
                    const double a = amplitudeAt(s, f, 24000, 60000);
                    weighted += a * f;
                    total += a;
                }
                return weighted / total;
            };
            const auto voice = vowel(220.0, 1.5);
            Engine engine;
            engine.prepare(kRate);
            engine.update({});
            const double plain = centroid(run(engine, voice));
            Settings up;
            up.formant = 7.0f;
            engine.prepare(kRate);
            engine.update(up);
            const auto raised = run(engine, voice);
            logMessage("Spectral centre " + juce::String(plain, 0) + " Hz, +7 semitones " + juce::String(centroid(raised), 0) + " Hz");
            expectGreaterThan(centroid(raised), plain * 1.2);
            expectEquals(engine.carrierNote(), 33);
        }

        beginTest("The CPU it costs");
        {
            Engine engine;
            engine.prepare(kRate);
            engine.update({});
            const auto voice = vowel(220.0, 10.0);
            const auto start = std::chrono::steady_clock::now();
            const auto out = run(engine, voice);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double realtime = 10.0 / seconds;
            logMessage("Runs " + juce::String(realtime, 0) + " times faster than real time");
            // Generous, for debug builds and busy CI machines; the log shows the real figure.
            expectGreaterThan(realtime, 5.0);
            expect(std::isfinite(out.back()));
        }

        beginTest("At 44.1 and 96 kHz the carrier is in tune and the frame keeps its length in time");
        {
            for (const double rate : { 44100.0, 96000.0 })
            {
                Engine engine;
                engine.prepare(rate);
                engine.update({});
                logMessage(juce::String(rate, 0) + " Hz: latency " + juce::String(engine.latency()) + " samples");
                expectWithinAbsoluteError(engine.latency() / rate, 1536.0 / 48000.0, 0.004);
                // The voice made at 48 kHz and played at `rate` sings 220 * rate / 48000 Hz.
                const auto voice = vowel(220.0, 1.0);
                Signal out(voice.size());
                engine.process(voice.data(), out.data(), (int) voice.size());
                const double sung = 220.0 * rate / kRate;
                const int expected = nearestInScale(69.0 + 12.0 * std::log2(sung / 440.0), 2, Scale::minor) - 24;
                expectEquals(engine.carrierNote(), expected);
            }
        }

        beginTest("The processor: settings in the project, whole numbers, MIDI and the editor");
        {
            RobovoxProcessor processor;
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            expectEquals(processor.getLatencySamples(), 1536);
            processor.state.getParameter("formant")->setValueNotifyingHost(processor.state.getParameter("formant")->convertTo0to1(5.0f));
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            RobovoxProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().formant, 5.0f, 1.0e-3f);

            auto text = [&](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("formant", -3.0000001f), juce::String("-3 HT"));
            expectEquals(text("bandwidth", 250.0f), juce::String("250 Hz"));
            expectEquals(text("glide", 120.0f), juce::String("120 ms"));
            expectEquals(text("sibilance", 0.6f), juce::String("60 %"));
            expectEquals(text("output", -6.0f), juce::String("-6 dB"));
            expectEquals(text("octave", -2.0f), juce::String("-2 Okt"));
            expectEquals(noteName(60), juce::String("C3"));
            expectEquals(noteName(26), juce::String("D0"));

            // MIDI mode: a note on in the middle of a block sets the carrier, and both sides get the robot.
            processor.state.getParameter("mode")->setValueNotifyingHost(1.0f);
            const auto voice = vowel(220.0, 1.0);
            juce::AudioBuffer<float> buffer(2, kBlock);
            for (std::size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
            {
                juce::MidiBuffer midi;
                if (at == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.8f), 100);
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, voice.data() + at, kBlock);
                processor.processBlock(buffer, midi);
            }
            expectEquals(processor.carrierNote.load(), 45);
            expect(std::equal(buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock, buffer.getReadPointer(1)));
            expectGreaterThan(buffer.getMagnitude(0, 0, kBlock), 0.01f);

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()), child->getName());
            if (auto* robovox = dynamic_cast<RobovoxEditor*>(editor.get()))
            {
                robovox->pitch.view.refresh();
                expectEquals(robovox->pitch.view.carrierText(), juce::String("A1"));
            }
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Robovox.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }

        beginTest("A side chain bus, so Logic offers its menu for the MIDI track, whose audio changes nothing");
        {
            auto run = [&](bool sideNoise) {
                RobovoxProcessor processor;
                expectEquals(processor.getBusCount(true), 2);
                expectEquals(processor.getTotalNumInputChannels(), 4);
                processor.prepareToPlay(kRate, kBlock);
                processor.state.getParameter("mode")->setValueNotifyingHost(1.0f);
                const auto voice = vowel(220.0, 0.5);
                juce::AudioBuffer<float> buffer(4, kBlock);
                juce::Random random(7);
                std::vector<float> out;
                for (std::size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
                {
                    juce::MidiBuffer midi;
                    if (at == 0)
                        midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.8f), 0);
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.copyFrom(ch, 0, voice.data() + at, kBlock);
                    for (int ch = 2; ch < 4; ++ch)
                        for (int i = 0; i < kBlock; ++i)
                            buffer.setSample(ch, i, sideNoise ? random.nextFloat() * 2.0f - 1.0f : 0.0f);
                    processor.processBlock(buffer, midi);
                    out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock);
                }
                return out;
            };
            const auto quiet = run(false);
            const auto noisy = run(true);
            expect(quiet == noisy);
            expectGreaterThan(*std::max_element(quiet.begin(), quiet.end()), 0.01f);
        }
    }
};

static RobovoxTests robovoxTests;
