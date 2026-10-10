#include "EffectsChain.h"
#include "GranularEditor.h"
#include "GranularEngine.h"
#include "GranularPresets.h"
#include "GranularProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkgrain;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

double noteHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

/** The amplitude of `hz` in `signal` between `from` and `to` (Goertzel). */
float amplitudeAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < to; ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (to - from));
}

/**
 * How strong `hz` is in `signal` between `from` and `to`, as an amplitude: the power at `hz` averaged over Hann-windowed
 * frames of about 85 ms (Welch). Grains start at random places of the source, so their phases do not line up and one
 * long measurement would come out at random; the average over frames does not.
 */
float strengthAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    constexpr std::size_t kFrame = 4096;
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    double power = 0.0;
    int frames = 0;
    for (std::size_t start = from; start + kFrame <= to; start += kFrame / 2)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t n = 0; n < kFrame; ++n)
        {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (double) n / kFrame);
            const double x = signal[start + n] * hann;
            re += x * std::cos(w * (double) n);
            im += x * std::sin(w * (double) n);
        }
        power += re * re + im * im;
        ++frames;
    }
    // A Hann window keeps half the amplitude of a steady sine.
    return frames == 0 ? 0.0f : (float) (4.0 * std::sqrt(power / frames) / (double) kFrame);
}

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) (to - from));
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

std::size_t at(double seconds) { return (std::size_t) (seconds * kRate); }

struct Note
{
    double on, off;
    int note;
};

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

using Knobs = std::initializer_list<std::pair<const char*, float>>;

void set(GranularProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/**
 * Plays `notes` on `processor` for `length` seconds. Without a program the path is plain: no hall, no master cut,
 * full velocity at full level.
 */
Rendered play(GranularProcessor& processor, Knobs knobs, std::vector<Note> notes, double length, int program = -1,
              std::function<void(int)> afterBlock = {})
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    if (program >= 0)
        processor.setCurrentProgram(program);
    else
    {
        set(processor, "fxReverbMix", 0.0f);
        set(processor, "master", 0.0f);
        set(processor, "velocityAmount", 0.0f);
    }
    for (const auto& [id, value] : knobs)
        set(processor, id, value);
    processor.prepareToPlay(kRate, kBlock);

    const auto total = at(length);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    const auto start = std::chrono::steady_clock::now();
    int block = 0;
    for (std::size_t from = 0; from < total; from += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - from);
        juce::MidiBuffer midi;
        for (const auto& n : notes)
        {
            const auto on = at(n.on);
            const auto off = at(n.off);
            if (on >= from && on < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, (juce::uint8) 127), (int) (on - from));
            if (off >= from && off < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), (int) (off - from));
        }
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + from);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + from);
        if (afterBlock)
            afterBlock(block++);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

Rendered play(Knobs knobs, std::vector<Note> notes, double length, int program = -1)
{
    GranularProcessor processor;
    return play(processor, knobs, std::move(notes), length, program);
}

Rendered hold(Knobs knobs, int note, double length)
{
    return play(knobs, { { 0.0, length + 1.0, note } }, length);
}

/** The middle of both channels. */
std::vector<float> mid(const Rendered& r)
{
    std::vector<float> m(r.left.size());
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (r.left[i] + r.right[i]);
    return m;
}

bool finite(const Rendered& r)
{
    for (std::size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite(r.left[i]) || ! std::isfinite(r.right[i]))
            return false;
    return true;
}

constexpr float kSine = 7.0f;
constexpr float kGlass = 1.0f;

/** A WAV file of a sine, for loading as the user's source. */
juce::File writeSine(double hz, double seconds, double rate)
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("tonwerk-granular-test-" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    auto writer = juce::WavAudioFormat().createWriterFor(
        stream, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    juce::AudioBuffer<float> audio(2, (int) (seconds * rate));
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const float x = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate);
        audio.setSample(0, i, x);
        audio.setSample(1, i, x);
    }
    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    return file;
}
} // namespace

class GranularTests : public juce::UnitTest
{
public:
    GranularTests() : juce::UnitTest("Tonwerk Granular", "Granular") {}

    void runTest() override
    {
        beginTest("Every source is filled, about equally loud, and the tuned ones sound at their root");
        {
            const auto& bank = SourceBank::instance();
            for (int i = 0; i < SourceBank::kBuiltIn; ++i)
            {
                const auto& source = bank.source(i);
                const auto& x = source.levels[0];
                float peak = 0.0f;
                bool ok = true;
                for (float v : x)
                {
                    ok = ok && std::isfinite(v);
                    peak = std::max(peak, std::abs(v));
                }
                const float level = rms(x, 0, x.size());
                logMessage(juce::String(source.name) + ": " + juce::String((double) x.size() / source.sampleRate, 1) + " s, RMS "
                           + juce::String(decibels(level), 1) + " dB, peak " + juce::String(decibels(peak), 1) + " dB");
                expect(ok, source.name);
                expectGreaterThan(x.size(), (std::size_t) 100000, source.name);
                // -12 dB RMS unless the peak would pass -0.5 dB first (the struck sources).
                expectGreaterThan(decibels(level), -20.0f, source.name);
                expectLessThan(decibels(level), -11.5f, source.name);
                expectLessThan(peak, 0.95f, source.name);
                expectEquals((int) source.overview.size(), GrainSource::kOverview);
            }
            const auto& sine = bank.source((int) kSine).levels[0];
            const float root = amplitudeAt(sine, noteHz(60), 0, 48000);
            expectGreaterThan(root, 0.3f);
            expectLessThan(amplitudeAt(sine, noteHz(61), 0, 48000), 0.05f * root);
        }

        beginTest("The lower copies keep the tone and drop what would alias");
        {
            std::vector<float> x(96000);
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (0.3 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / kRate)
                                + 0.3 * std::sin(2.0 * juce::MathConstants<double>::pi * 15000.0 * (double) i / kRate));
            const auto source = GrainSource::fromSamples(x, kRate, 60, "test");
            const auto& half = source.levels[1];
            auto amplitude = [&half](double hz) {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / (kRate / 2.0);
                double re = 0.0, im = 0.0;
                for (std::size_t n = 1000; n < half.size() - 1000; ++n)
                {
                    re += half[n] * std::cos(w * (double) n);
                    im += half[n] * std::sin(w * (double) n);
                }
                return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (half.size() - 2000));
            };
            const float kept = amplitude(1000.0);
            // 15 kHz would fold to 9 kHz at half the rate.
            const float folded = amplitude(9000.0);
            logMessage("1 kHz " + juce::String(decibels(kept), 1) + " dB, 15 kHz folded " + juce::String(decibels(folded), 1) + " dB");
            expectGreaterThan(kept, 0.2f);
            expectLessThan(decibels(folded / kept), -60.0f);
        }

        // Long grains, since a short one blurs its pitch by about the inverse of its length.
        beginTest("A note plays the source at its pitch, and an octave knob moves it an octave");
        {
            for (const auto& [note, octave] : { std::pair { 69, 0 }, std::pair { 57, 1 }, std::pair { 45, 0 } })
            {
                const auto out = mid(hold({ { "source", kSine }, { "octave", (float) octave }, { "density", 50 },
                                            { "grainSize", 400 } },
                                          note, 2.5));
                const double hz = noteHz(note + 12 * octave);
                const float wanted = strengthAt(out, hz, at(0.5), at(2.4));
                const float beside = strengthAt(out, hz * std::pow(2.0, 4.0 / 12.0), at(0.5), at(2.4));
                logMessage(juce::String(hz, 1) + " Hz: " + juce::String(decibels(wanted), 1) + " dB, a third up "
                           + juce::String(decibels(beside), 1) + " dB");
                expectGreaterThan(wanted, 0.05f);
                expectLessThan(beside, 0.1f * wanted);
            }
        }

        beginTest("Without key follow every key plays the source's own pitch");
        {
            for (const int note : { 48, 72 })
            {
                const auto out = mid(hold({ { "source", kSine }, { "keyFollow", 0 }, { "density", 50 } }, note, 1.5));
                const float root = strengthAt(out, noteHz(60), at(0.4), at(1.4));
                const float played = strengthAt(out, noteHz(note), at(0.4), at(1.4));
                expectGreaterThan(root, 10.0f * played);
            }
        }

        beginTest("Scatter on octaves lands on octaves only");
        {
            const auto out = mid(hold({ { "source", kSine }, { "scatter", 1200 }, { "scatterMode", 1 }, { "density", 80 },
                                        { "grainSize", 120 } },
                                      69, 3.0));
            const float a440 = strengthAt(out, 440.0, at(0.5), at(2.9));
            const float a220 = strengthAt(out, 220.0, at(0.5), at(2.9));
            const float a880 = strengthAt(out, 880.0, at(0.5), at(2.9));
            const float between = strengthAt(out, noteHz(66), at(0.5), at(2.9));
            logMessage("220/440/880 Hz: " + juce::String(decibels(a220), 1) + " / " + juce::String(decibels(a440), 1)
                       + " / " + juce::String(decibels(a880), 1) + " dB, a tritone between "
                       + juce::String(decibels(between), 1) + " dB");
            for (const float octave : { a220, a440, a880 })
                expectGreaterThan(octave, 5.0f * between);
        }

        beginTest("A dense cloud is about as loud as a sparse one");
        {
            const auto sparse = mid(hold({ { "source", kSine }, { "density", 8 }, { "grainSize", 120 }, { "jitter", 0 } }, 60, 3.0));
            const auto dense = mid(hold({ { "source", kSine }, { "density", 150 }, { "grainSize", 300 } }, 60, 3.0));
            const float a = decibels(rms(sparse, at(0.5), at(2.9)));
            const float b = decibels(rms(dense, at(0.5), at(2.9)));
            logMessage("Sparse " + juce::String(a, 1) + " dB, dense " + juce::String(b, 1) + " dB RMS");
            expectWithinAbsoluteError(b, a, 4.0f);
        }

        beginTest("Grains come from around the position, and the scan carries it along");
        {
            GranularProcessor processor;
            float lowest = 1.0f, highest = 0.0f;
            int seen = 0;
            play(processor, { { "source", kGlass }, { "position", 0.7f }, { "spray", 0.1f }, { "density", 60 } },
                 { { 0.0, 2.0, 60 } }, 1.0, -1, [&](int) {
                     for (auto& place : processor.grainPlaces)
                         if (const float p = place.load(); p >= 0.0f)
                         {
                             lowest = std::min(lowest, p);
                             highest = std::max(highest, p);
                             ++seen;
                         }
                 });
            logMessage("Grains between " + juce::String(lowest, 3) + " and " + juce::String(highest, 3));
            expectGreaterThan(seen, 100);
            expectGreaterThan(lowest, 0.62f);
            expectLessThan(highest, 0.78f);

            GranularProcessor scanning;
            play(scanning, { { "source", kGlass }, { "position", 0.0f }, { "spray", 0.0f }, { "scan", 1.0f } },
                 { { 0.0, 2.0, 60 } }, 1.0);
            // A source of 4 s read at its own speed: a quarter of the way after a second.
            expectWithinAbsoluteError(scanning.shownCentre.load(), 0.25f, 0.02f);
        }

        beginTest("Attack and release take their time");
        {
            const auto out = mid(play({ { "source", kSine }, { "density", 60 }, { "env1Attack", 500 }, { "env1Release", 500 } },
                                      { { 0.0, 1.5, 60 } }, 3.0));
            const float early = rms(out, at(0.05), at(0.15));
            const float full = rms(out, at(1.0), at(1.4));
            const float after = rms(out, at(2.2), at(2.4));
            expectLessThan(early, 0.5f * full);
            expectGreaterThan(full, 0.05f);
            expectLessThan(after, 0.05f * full);
        }

        beginTest("An LFO on the pitch makes a vibrato, both ways");
        {
            const auto out = mid(hold({ { "source", kSine }, { "density", 100 }, { "grainSize", 40 }, { "lfo1Rate", 2 },
                                        { "lfo1Target", 5 }, { "lfo1Amount", 1.0f / 12.0f } },
                                      69, 2.0));
            // A semitone either way: both neighbours are heard as much as the note itself, roughly.
            const float up = strengthAt(out, noteHz(70), at(0.2), at(1.95));
            const float down = strengthAt(out, noteHz(68), at(0.2), at(1.95));
            const float far = strengthAt(out, noteHz(73), at(0.2), at(1.95));
            expectGreaterThan(up, 4.0f * far);
            expectGreaterThan(down, 4.0f * far);
        }

        beginTest("Every source, window, filter and direction stays finite and in bounds");
        {
            for (int source = 0; source < SourceBank::kBuiltIn; ++source)
                for (int window = 0; window < 3; ++window)
                {
                    const auto out = play({ { "source", (float) source }, { "window", (float) window }, { "reverse", 0.5f },
                                            { "scan", -1.5f }, { "spray", 1 }, { "scatter", 2400 }, { "density", 200 },
                                            { "grainSize", (float) (5 + 300 * window) }, { "filterOn", 1 },
                                            { "filterMode", (float) ((window + source) % 4) }, { "resonance", 1 },
                                            { "cutoff", 3000 }, { "width", 1 } },
                                          { { 0.0, 0.8, 24 }, { 0.0, 0.8, 108 }, { 0.1, 0.8, 60 } }, 1.0);
                    expect(finite(out));
                    float peak = 0.0f;
                    for (std::size_t i = 0; i < out.left.size(); ++i)
                        peak = std::max({ peak, std::abs(out.left[i]), std::abs(out.right[i]) });
                    // Three notes at full resonance; this is about blowing up, not about level.
                    expectLessThan(peak, 16.0f);
                }
        }

        beginTest("A file of the user's becomes the source and comes back with the project");
        {
            const auto file = writeSine(330.0, 3.0, 44100.0);
            GranularProcessor processor;
            expect(processor.loadFile(file));
            expectEquals(juce::roundToInt(processor.state.getRawParameterValue("source")->load()), SourceBank::kUserFile);
            // Recorded at 44.1 kHz, played at 48: the rate is allowed for. C4 plays the file as it is.
            const Knobs knobs { { "density", 50 }, { "grainSize", 400 } };
            auto out = mid(play(processor, knobs, { { 0.0, 3.0, 60 } }, 2.5));
            const float wanted = strengthAt(out, 330.0, at(0.5), at(2.4));
            expectGreaterThan(wanted, 0.05f);
            expectLessThan(strengthAt(out, 330.0 * 48000.0 / 44100.0, at(0.5), at(2.4)), 0.2f * wanted);

            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            GranularProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.userFile().getFullPathName(), file.getFullPathName());
            expectEquals(juce::roundToInt(loaded.state.getRawParameterValue("source")->load()), SourceBank::kUserFile);
            out = mid(play(loaded, knobs, { { 0.0, 3.0, 60 } }, 2.5));
            expectGreaterThan(strengthAt(out, 330.0, at(0.5), at(2.4)), 0.05f);
            file.deleteFile();

            // Without a file the entry stays silent.
            const auto none = mid(hold({ { "source", (float) SourceBank::kUserFile } }, 60, 0.5));
            expectLessThan(rms(none, 0, none.size()), 1.0e-6f);
        }

        beginTest("The delay and hall take a stereo source as the mono one when both sides are the same");
        {
            tonwerk::EffectsChain a, b;
            a.prepare(kRate, kBlock);
            b.prepare(kRate, kBlock);
            tonwerk::Effects fx;
            fx.delay.mix = 0.5f;
            juce::AudioBuffer<float> outA(2, kBlock), outB(2, kBlock);
            std::vector<float> in(kBlock), copy(kBlock);
            float worst = 0.0f;
            for (int block = 0; block < 200; ++block)
            {
                for (int i = 0; i < kBlock; ++i)
                    in[(std::size_t) i] = copy[(std::size_t) i] = (float) std::sin(0.01 * (block * kBlock + i));
                a.process(fx, in.data(), outA, 0, kBlock);
                b.process(fx, in.data(), copy.data(), outB, 0, kBlock);
                for (int i = 0; i < kBlock; ++i)
                    worst = std::max(worst, std::abs(outA.getSample(0, i) - outB.getSample(1, i)));
            }
            expectLessThan(worst, 1.0e-6f);
        }

        beginTest("Every factory sound plays, and names only parameters that exist");
        {
            GranularProcessor processor;
            const auto& presets = factoryPresets();
            expectEquals(processor.getNumPrograms(), (int) presets.size());
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                for (const auto& [id, value] : presets[(std::size_t) i].values)
                    expect(processor.state.getParameter(id) != nullptr, id);
                const auto out = play({}, { { 0.0, 1.0, 48 }, { 0.0, 1.0, 55 } }, 1.5, i);
                expect(finite(out), presets[(std::size_t) i].name);
                expectGreaterThan(rms(out.left, 0, out.left.size()), 1.0e-3f, presets[(std::size_t) i].name);
            }
        }

        beginTest("Eight notes of a dense cloud stay well inside real time");
        {
            std::vector<Note> chord;
            for (int note : { 48, 52, 55, 59, 60, 64, 67, 71 })
                chord.push_back({ 0.0, 4.0, note });
            const auto out = play({ { "source", 2 }, { "density", 200 }, { "grainSize", 400 }, { "filterOn", 1 },
                                    { "fxReverbMix", 0.3f } },
                                  chord, 4.0);
            const double realtime = 4.0 / out.seconds;
            logMessage("8 voices of 40 grains each, filter and hall: " + juce::String(realtime, 1) + " times real time");
            expectGreaterThan(realtime, 3.0);
        }

        beginTest("The processor keeps its settings and shows whole numbers with units");
        {
            GranularProcessor processor;
            set(processor, "position", 0.42f);
            set(processor, "lfo2Target", 6.0f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            GranularProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            const auto s = loaded.readSettings();
            expectWithinAbsoluteError(s.position, 0.42f, 1.0e-4f);
            expect(s.lfo[1].route.target == Target::cutoff);

            auto text = [&processor](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("grainSize", 120.3f), juce::String("120 ms"));
            expectEquals(text("density", 20.0f), juce::String("20 Hz"));
            expectEquals(text("scan", -0.5f), juce::String("-50 %"));
            expectEquals(text("scatter", 1200.0f), juce::String("1200 ct"));
            expectEquals(text("fxReverbDecay", 3000.0f), juce::String("3000 ms"));
            expectEquals(text("fxDelayFeedback", 0.35f), juce::String("35 %"));
            expectEquals(text("master", -6.0000002f), juce::String("-6 dB"));
        }

        beginTest("The editor fits its panels");
        {
            GranularProcessor processor;
            play(processor, { { "source", 0 }, { "spray", 0.3f } }, { { 0.0, 2.0, 60 } }, 0.5);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expectEquals(editor->getWidth(), GranularEditor::kWidth);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Granular.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static GranularTests granularTests;
