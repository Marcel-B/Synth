#include "SamplerEditor.h"
#include "SamplerEngine.h"
#include "SamplerPresets.h"
#include "SamplerProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerksampler;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;

using Signal = std::vector<float>;
using Knobs = std::vector<std::pair<const char*, float>>;

/** The amplitude of `hz` in `signal` between `from` and `to` (Goertzel). */
float amplitudeAt(const Signal& signal, double hz, std::size_t from, std::size_t to)
{
    const double w = kTwoPi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < to; ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (to - from));
}

float rms(const Signal& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

struct Note
{
    double on, off;
    int note;
    int velocity = 127;
};

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

void set(SamplerProcessor& processor, const Knobs& knobs)
{
    for (const auto& [id, value] : knobs)
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse;
}

/** Plays `notes` for `length` seconds; delay and hall off unless the knobs say otherwise. */
Take render(SamplerProcessor& processor, const std::vector<Note>& notes, double length)
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    const auto total = (std::size_t) (length * kRate);
    Take take { Signal(total), Signal(total) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < total; at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - at);
        juce::MidiBuffer midi;
        for (const auto& n : notes)
        {
            const auto on = (std::size_t) (n.on * kRate);
            const auto off = (std::size_t) (n.off * kRate);
            if (on >= at && on < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, (juce::uint8) n.velocity), (int) (on - at));
            if (off >= at && off < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), (int) (off - at));
        }
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, take.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, take.right.data() + at);
    }
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    processor.releaseResources();
    return take;
}

/** A processor on a file of `samples` at `rate`, with delay and hall off and the filter open. */
std::unique_ptr<SamplerProcessor> withFile(const Signal& samples, double rate, const Knobs& knobs = {})
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("tonwerk-sampler-test-" + juce::String(juce::Random::getSystemRandom().nextInt())
                                        + ".wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = juce::WavAudioFormat().createWriterFor(
            stream, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));
        const float* channels[] = { samples.data() };
        writer->writeFromFloatArrays(channels, 1, (int) samples.size());
    }
    auto processor = std::make_unique<SamplerProcessor>();
    processor->loadFile(file);
    file.deleteFile();
    set(*processor, { { "fxReverbMix", 0.0f }, { "fxDelayMix", 0.0f }, { "velocity", 0.0f }, { "master", 0.0f } });
    set(*processor, knobs);
    return processor;
}

Signal sine(double hz, double seconds, double rate, float amplitude = 0.5f)
{
    Signal out((std::size_t) (seconds * rate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(kTwoPi * hz * (double) n / rate);
    return out;
}

double frequencyOf(const Signal& signal, std::size_t from, std::size_t to)
{
    std::vector<double> crossings;
    for (std::size_t n = from + 1; n < to; ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
            crossings.push_back((double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]));
    if (crossings.size() < 2)
        return 0.0;
    return (double) (crossings.size() - 1) * kRate / (crossings.back() - crossings.front());
}
} // namespace

class SamplerTests : public juce::UnitTest
{
public:
    SamplerTests() : juce::UnitTest("Tonwerk Sampler", "Sampler") {}

    void runTest() override
    {
        beginTest("The built-in sounds are there, finite and peak at -2 dBFS");
        {
            const auto& bank = SampleBank::instance();
            for (int i = 0; i < SampleBank::kBuiltIn; ++i)
            {
                const auto& sample = bank.sample(i);
                expect(sample.length() > (std::size_t) kRate, juce::String(sample.name));
                float peak = 0.0f;
                bool finite = true;
                for (int c = 0; c < sample.channels; ++c)
                    for (const float v : sample.levels[0][(std::size_t) c])
                    {
                        finite = finite && std::isfinite(v);
                        peak = std::max(peak, std::abs(v));
                    }
                expect(finite, juce::String(sample.name));
                expectWithinAbsoluteError(peak, 0.8f, 0.01f, juce::String(sample.name));
                expect(sample.levels[1][0].size() == (sample.length() + 1) / 2);
            }
            expectEquals((int) SampleBank::names().size(), SampleBank::kUserFile + 1);
        }

        beginTest("A key plays the file at its pitch from the root, whatever the file's rate");
        {
            for (const double fileRate : { 44100.0, 48000.0, 96000.0 })
            {
                auto p = withFile(sine(440.0, 2.0, fileRate), fileRate, { { "root", 69 } });
                for (const int note : { 57, 69, 76, 93 })
                {
                    const auto take = render(*p, { { 0.0, 1.0, note } }, 1.0);
                    const double expected = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                    const double measured = frequencyOf(take.left, (std::size_t) (0.1 * kRate), (std::size_t) (0.9 * kRate));
                    expectWithinAbsoluteError(1200.0 * std::log2(measured / expected), 0.0, 2.0,
                                              juce::String(fileRate) + " Hz file, note " + juce::String(note));
                }
            }
            auto p = withFile(sine(440.0, 2.0, 48000.0), 48000.0, { { "root", 69 }, { "tune", 7 }, { "fine", 50 } });
            const auto take = render(*p, { { 0.0, 1.0, 69 } }, 1.0);
            const double measured = frequencyOf(take.left, (std::size_t) (0.1 * kRate), (std::size_t) (0.9 * kRate));
            expectWithinAbsoluteError(1200.0 * std::log2(measured / 440.0), 750.0, 2.0);
        }

        beginTest("Played high, the lower copies keep what folds back small");
        {
            // A sawtooth with every harmonic up to the file's Nyquist, played three octaves up.
            // Four seconds, so half a second is left of it when it plays eight times as fast.
            Signal saw((std::size_t) (4.0 * kRate));
            for (std::size_t n = 0; n < saw.size(); ++n)
                for (int k = 1; k * 110.0 < 23000.0; ++k)
                    saw[n] += 0.3f / (float) k * (float) std::sin(kTwoPi * 110.0 * k * (double) n / kRate);
            auto p = withFile(saw, kRate, { { "root", 45 } });
            // Two semitones over three octaves: the copy at an eighth of the rate is read 1.12 samples a step.
            const auto take = render(*p, { { 0.0, 1.0, 83 } }, 1.0);
            const double f0 = 110.0 * std::pow(2.0, 38.0 / 12.0);
            const auto from = (std::size_t) (0.05 * kRate), to = (std::size_t) (0.4 * kRate);
            const float wanted = amplitudeAt(take.left, f0, from, to);
            // A harmonic above 24 kHz would fold back to 48000 - its frequency.
            float folded = 0.0f;
            for (int h = 1; h * f0 < 48000.0; ++h)
                if (h * f0 > 24000.0)
                    folded = std::max(folded, amplitudeAt(take.left, 48000.0 - h * f0, from, to));
            logMessage("fundamental " + juce::String(wanted, 3) + ", largest fold-back " + juce::String(folded, 5));
            expectLessThan(folded, wanted * 0.03f);
        }

        beginTest("Without a loop the note ends with the sample; a loop holds it while the key is down");
        {
            auto p = withFile(sine(220.0, 0.5, kRate), kRate, { { "root", 57 } });
            auto take = render(*p, { { 0.0, 2.0, 57 } }, 2.0);
            expectGreaterThan(rms(take.left, 0, (std::size_t) (0.4 * kRate)), 0.2f);
            expectLessThan(rms(take.left, (std::size_t) (0.6 * kRate), (std::size_t) (2.0 * kRate)), 1.0e-5f);

            set(*p, { { "loop", 1 }, { "loopStart", 0.3f }, { "loopEnd", 0.9f } });
            take = render(*p, { { 0.0, 2.0, 57 } }, 2.5);
            expectGreaterThan(rms(take.left, (std::size_t) (1.5 * kRate), (std::size_t) (1.9 * kRate)), 0.2f);
            expectLessThan(rms(take.left, (std::size_t) (2.3 * kRate), (std::size_t) (2.5 * kRate)), 1.0e-4f);

            set(*p, { { "loop", 2 } });
            take = render(*p, { { 0.0, 2.0, 57 } }, 2.0);
            expectGreaterThan(rms(take.left, (std::size_t) (1.5 * kRate), (std::size_t) (1.9 * kRate)), 0.2f);
        }

        beginTest("The crossfade hides a loop whose ends do not meet");
        {
            // The loop's length is not a whole number of cycles: without a crossfade, the seam steps.
            auto jump = [](float crossfade) {
                auto p = withFile(sine(100.0, 1.0, kRate, 0.5f), kRate,
                                  { { "root", 60 }, { "loop", 1 }, { "loopStart", 0.3f }, { "loopEnd", 0.7137f },
                                    { "crossfade", crossfade } });
                const auto take = render(*p, { { 0.0, 3.0, 60 } }, 3.0);
                float worst = 0.0f;
                for (std::size_t n = (std::size_t) (0.05 * kRate); n < take.left.size(); ++n)
                    worst = std::max(worst, std::abs(take.left[n] - take.left[n - 1]));
                return worst;
            };
            // A 100 Hz sine at 0.5 moves at most 0.0066 a sample.
            const float hard = jump(0.0f), soft = jump(100.0f);
            logMessage("largest step without crossfade " + juce::String(hard, 4) + ", with " + juce::String(soft, 4));
            expectGreaterThan(hard, 0.05f);
            expectLessThan(soft, 0.01f);
        }

        beginTest("Reverse plays the sample from its end");
        {
            // Rising from silence: forwards quiet first, backwards loud first.
            Signal rising((std::size_t) kRate);
            for (std::size_t n = 0; n < rising.size(); ++n)
                rising[n] = (float) n / (float) rising.size() * 0.5f * (float) std::sin(kTwoPi * 300.0 * (double) n / kRate);
            auto p = withFile(rising, kRate, { { "root", 60 } });
            auto take = render(*p, { { 0.0, 1.0, 60 } }, 1.0);
            expectLessThan(rms(take.left, 0, 4800), rms(take.left, 38400, 43200));
            set(*p, { { "reverse", 1 } });
            take = render(*p, { { 0.0, 1.0, 60 } }, 1.0);
            expectGreaterThan(rms(take.left, 0, 4800), rms(take.left, 38400, 43200));
        }

        beginTest("Slices: a key from C1 on plays its part of the region at the sample's own pitch");
        {
            Signal parts;
            const std::array<double, 4> tones { 200.0, 300.0, 450.0, 675.0 };
            for (const double hz : tones)
            {
                const auto part = sine(hz, 0.5, kRate);
                parts.insert(parts.end(), part.begin(), part.end());
            }
            auto p = withFile(parts, kRate, { { "mode", 1 }, { "slices", 4 }, { "playback", 1 } });
            for (int i = 0; i < 4; ++i)
            {
                const auto take = render(*p, { { 0.0, 0.1, kFirstSliceKey + i } }, 0.7);
                const double measured = frequencyOf(take.left, 2400, 21600);
                expectWithinAbsoluteError(measured, tones[(std::size_t) i], 1.0, "slice " + juce::String(i + 1));
                // One-shot: it plays to its end after the key is up, then stops.
                expectGreaterThan(rms(take.left, 12000, 21600), 0.2f);
                expectLessThan(rms(take.left, 26000, 33000), 1.0e-5f);
            }
            // Keys below C1 or past the last slice play nothing.
            const auto take = render(*p, { { 0.0, 0.3, kFirstSliceKey - 1 }, { 0.0, 0.3, kFirstSliceKey + 4 } }, 0.4);
            expectLessThan(rms(take.left, 0, take.left.size()), 1.0e-6f);
        }

        beginTest("Slicing by transients cuts the breakbeat on its hits");
        {
            const auto& beat = SampleBank::instance().sample(2);
            const auto map = sliceRegion(beat, 0.0, (double) beat.length(), 16, SliceMode::transients);
            expectEquals(map.count, 16);
            int onGrid = 0;
            for (int i = 0; i < map.count; ++i)
            {
                const double seconds = map.bounds[(std::size_t) i] / beat.sampleRate;
                const double off = std::abs(seconds / 0.125 - std::round(seconds / 0.125)) * 0.125;
                onGrid += off < 0.008 ? 1 : 0;
                if (i > 0)
                    expectGreaterThan(map.bounds[(std::size_t) i], map.bounds[(std::size_t) i - 1]);
            }
            expectEquals(onGrid, 16);
            // The sung chops: one slice a syllable, 0.375 s apart.
            const auto& chops = SampleBank::instance().sample(1);
            const auto syllables = sliceRegion(chops, 0.0, (double) chops.length(), 8, SliceMode::transients);
            for (int i = 0; i < 8; ++i)
                expectWithinAbsoluteError(syllables.bounds[(std::size_t) i] / chops.sampleRate, 0.375 * i, 0.03,
                                          "syllable " + juce::String(i + 1));
        }

        beginTest("Choke: a new slice cuts the one sounding; without it they overlap");
        {
            auto p = withFile(sine(300.0, 2.0, kRate), kRate, { { "mode", 1 }, { "slices", 2 }, { "playback", 1 } });
            auto take = render(*p, { { 0.0, 0.1, kFirstSliceKey }, { 0.3, 0.4, kFirstSliceKey + 1 } }, 0.6);
            // Both slices are the same tone: chopped, the level stays; overlapping, the two add up or cancel.
            const float choked = rms(take.left, (std::size_t) (0.35 * kRate), (std::size_t) (0.55 * kRate));
            expectWithinAbsoluteError(choked, 0.5f / std::sqrt(2.0f), 0.02f);
            set(*p, { { "choke", 0 } });
            take = render(*p, { { 0.0, 0.1, kFirstSliceKey }, { 0.3, 0.4, kFirstSliceKey + 1 } }, 0.6);
            const float both = rms(take.left, (std::size_t) (0.35 * kRate), (std::size_t) (0.55 * kRate));
            expect(std::abs(both - choked) > 0.03f);
        }

        beginTest("Legato glides from the held key without starting over");
        {
            auto p = withFile(sine(220.0, 3.0, kRate), kRate, { { "root", 57 }, { "voiceMode", 2 }, { "glide", 50 } });
            const auto take = render(*p, { { 0.0, 1.5, 57 }, { 0.5, 1.5, 69 } }, 1.5);
            expectWithinAbsoluteError(frequencyOf(take.left, 9600, 23000), 220.0, 1.0);
            expectWithinAbsoluteError(frequencyOf(take.left, (std::size_t) (1.0 * kRate), (std::size_t) (1.4 * kRate)),
                                      440.0, 2.0);
            // No new attack: the level does not dip at the second key.
            const float before = rms(take.left, (std::size_t) (0.45 * kRate), (std::size_t) (0.5 * kRate));
            const float after = rms(take.left, (std::size_t) (0.5 * kRate), (std::size_t) (0.505 * kRate));
            expectGreaterThan(after, before * 0.8f);
        }

        beginTest("The filter and its envelope darken and open the sound");
        {
            Signal noise((std::size_t) (2.0 * kRate));
            juce::Random random(3);
            for (auto& v : noise)
                v = random.nextFloat() - 0.5f;
            auto p = withFile(noise, kRate, { { "root", 60 }, { "filter", 2 }, { "cutoff", 500 } });
            const auto take = render(*p, { { 0.0, 1.0, 60 } }, 1.0);
            const float low = amplitudeAt(take.left, 200.0, 4800, 43200);
            const float high = amplitudeAt(take.left, 4000.0, 4800, 43200);
            logMessage("lowpass 500 Hz: 200 Hz " + juce::String(low, 4) + ", 4 kHz " + juce::String(high, 5));
            expectLessThan(high, low * 0.05f);
        }

        beginTest("Every factory sound plays, finite and below full scale");
        {
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                SamplerProcessor p;
                p.setCurrentProgram(i);
                const int note = playsSlices(presets[(std::size_t) i]) ? kFirstSliceKey + 2 : 60;
                const auto take = render(p, { { 0.0, 1.0, note }, { 0.2, 1.0, note + 4 } }, 1.5);
                bool finite = true;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                {
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                expect(finite, p.getProgramName(i));
                expectLessThan(peak, 1.0f, p.getProgramName(i));
                expectGreaterThan(peak, 0.01f, p.getProgramName(i));
            }
        }

        beginTest("Sixteen stereo voices with filter, LFO, lo-fi, delay and hall stay well inside real time");
        {
            SamplerProcessor p;
            set(p, { { "source", 4 }, { "loop", 1 }, { "filter", 2 }, { "cutoff", 2000 }, { "lfoTarget", 2 },
                     { "lofi", 0.3f }, { "fxDelayMix", 0.3f } });
            std::vector<Note> notes;
            for (int i = 0; i < 16; ++i)
                notes.push_back({ 0.0, 9.0, 40 + 3 * i });
            const auto take = render(p, notes, 10.0);
            const double factor = 10.0 / take.seconds;
            logMessage("16 voices: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 8.0);
        }

        beginTest("The state keeps the file and the knobs; values show whole numbers with units");
        {
            auto p = withFile(sine(440.0, 1.0, kRate), kRate, { { "tune", -5 } });
            auto text = [&p](const char* id) {
                auto* parameter = p->state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("root"), juce::String("C3"));
            expectEquals(text("tune"), juce::String("-5 HT"));
            expectEquals(text("cutoff"), juce::String("20000 Hz"));
            expectEquals(text("start"), juce::String("0 %"));
            expectEquals(text("crossfade"), juce::String("50 ms"));
            expectEquals(text("source"), juce::String("Eigene Datei"));
            // The state names the file by its path; this one is gone again, so the source stays but plays silence.
            juce::MemoryBlock saved;
            p->getStateInformation(saved);
            SamplerProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.readSettings().tune, -5);
            expectEquals(loaded.readSettings().source, SampleBank::kUserFile);
            expect(loaded.userFile().getFileName().startsWith("tonwerk-sampler-test-"));
        }

        beginTest("The editor shows the sample, its slices and the play heads");
        {
            SamplerProcessor p;
            p.setCurrentProgram(19);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<SamplerEditor*>(created.get());
            expect(editor != nullptr);
            render(p, { { 0.0, 0.5, kFirstSliceKey } }, 0.2);
            editor->sample.update();
            expectEquals(editor->sample.view.shownSlices().count, 16);
            expectGreaterThan(editor->getWidth(), 800);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("sampler.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static SamplerTests samplerTests;
