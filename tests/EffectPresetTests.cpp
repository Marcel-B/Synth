#include "DistortionPresets.h"
#include "DistortionProcessor.h"
#include "GlitchPresets.h"
#include "GlitchProcessor.h"
#include "Loudness.h"
#include "PresetCategories.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <set>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kPi = 3.14159265358979323846;
constexpr double kBpm = 120.0;
/** Every source peaks here, about where a recorded track sits: -6 dBFS. */
constexpr float kSourcePeak = 0.5f;

using Signal = std::vector<float>;

Signal silence(double seconds) { return Signal((std::size_t) (seconds * kRate), 0.0f); }

void normalise(Signal& signal, float peak)
{
    float highest = 0.0f;
    for (const float sample : signal)
        highest = std::max(highest, std::abs(sample));
    if (highest > 0.0f)
        for (auto& sample : signal)
            sample *= peak / highest;
}

double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

/** A plucked string (Karplus-Strong) added into `out` at `start` seconds, for `seconds`. */
void pluck(Signal& out, double start, double seconds, int midi, float velocity, juce::Random& random,
           float brightness = 0.5f)
{
    const auto period = std::max(2, (int) std::round(kRate / hz(midi)));
    std::vector<float> line((std::size_t) period);
    // A pick's burst of noise, softened: duller strings start with less of the top.
    float smooth = 0.0f;
    for (auto& sample : line)
    {
        smooth += brightness * ((random.nextFloat() * 2.0f - 1.0f) - smooth);
        sample = smooth * velocity;
    }
    const auto from = (std::size_t) (start * kRate);
    const auto count = (std::size_t) (seconds * kRate);
    std::size_t at = 0;
    for (std::size_t n = 0; n < count && from + n < out.size(); ++n)
    {
        const auto nextAt = (at + 1) % line.size();
        const float sample = line[at];
        line[at] = 0.996f * 0.5f * (sample + line[nextAt]);
        // A short release at the end, as a muted string.
        const float gate = n + 2000 > count ? (float) (count - n) / 2000.0f : 1.0f;
        out[from + n] += sample * gate;
        at = nextAt;
    }
}

/** A guitar at 120 BPM: a power chord struck on the quarters, a riff, then a held chord. */
Signal guitar()
{
    juce::Random random(7);
    auto out = silence(5.0);
    auto strum = [&](double at, std::initializer_list<int> notes, double seconds) {
        double offset = 0.0;
        for (const int note : notes)
        {
            pluck(out, at + offset, seconds, note, 1.0f, random);
            offset += 0.012;
        }
    };
    for (const double at : { 0.0, 0.5, 1.0 })
        strum(at, { 40, 47, 52 }, 0.45);
    double at = 1.5;
    for (const int note : { 57, 60, 62, 64, 62, 60 })
    {
        pluck(out, at, 0.24, note, 1.0f, random);
        at += 0.25;
    }
    strum(3.0, { 45, 52, 57, 61, 64 }, 2.0);
    normalise(out, kSourcePeak);
    return out;
}

/** A bass on the eighths around E1, picked rather dark. */
Signal bass()
{
    juce::Random random(11);
    auto out = silence(5.0);
    double at = 0.0;
    for (const int note : { 28, 28, 40, 28, 31, 33, 35, 38, 28, 28, 40, 28, 33, 31, 28, 26 })
    {
        pluck(out, at, 0.23, note, 1.0f, random, 0.25f);
        at += 0.25;
    }
    pluck(out, 4.0, 1.0, 28, 1.0f, random, 0.25f);
    normalise(out, kSourcePeak);
    return out;
}

/** A synth line: a sawtooth through a gentle lowpass on the sixteenths, then a held note. */
Signal synth()
{
    auto out = silence(5.0);
    const int notes[] = { 48, 48, 60, 48, 51, 48, 55, 58 };
    double phase = 0.0, low = 0.0;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const auto step = (int) (t / 0.125);
        const int note = t < 4.0 ? notes[step % 8] : 48;
        const bool on = t >= 4.0 ? t < 4.9 : std::fmod(t, 0.125) < 0.1;
        phase = std::fmod(phase + hz(note) / kRate, 1.0);
        const double saw = on ? 2.0 * phase - 1.0 : 0.0;
        low += 0.25 * (saw - low);
        out[n] = (float) low;
    }
    normalise(out, kSourcePeak);
    return out;
}

/** Two bars of a beat at 120 BPM: kick, snare, closed hats on the eighths. */
Signal drums()
{
    juce::Random random(3);
    auto out = silence(4.5);
    auto add = [&out](double at, double seconds, const std::function<float(double)>& voice) {
        const auto from = (std::size_t) (at * kRate);
        for (std::size_t n = 0; n < (std::size_t) (seconds * kRate) && from + n < out.size(); ++n)
            out[from + n] += voice((double) n / kRate);
    };
    for (int beat = 0; beat < 8; ++beat)
    {
        const double at = beat * 0.5;
        if (beat % 2 == 0)
            add(at, 0.4, [](double t) {
                // A sine falling from 150 to 50 Hz.
                const double phase = 2.0 * kPi * (50.0 * t + 100.0 * 0.03 * (1.0 - std::exp(-t / 0.03)));
                return (float) (std::sin(phase) * std::exp(-t / 0.12));
            });
        else
            add(at, 0.25, [&random](double t) {
                const double body = std::sin(2.0 * kPi * 190.0 * t) * std::exp(-t / 0.05);
                const double rattle = (random.nextFloat() * 2.0 - 1.0) * std::exp(-t / 0.07);
                return (float) (0.5 * body + 0.6 * rattle);
            });
        for (const double half : { 0.0, 0.25 })
        {
            double previous = 0.0;
            add(at + half, 0.06, [&random, &previous](double t) {
                // Noise minus its last sample: only the top remains.
                const double noise = random.nextFloat() * 2.0 - 1.0;
                const double high = noise - previous;
                previous = noise;
                return (float) (0.15 * high * std::exp(-t / 0.015));
            });
        }
    }
    normalise(out, kSourcePeak);
    return out;
}

/**
 * Something like speech: a voice's harmonics through the formants of a, e, i, o, u, four syllables a second with
 * a breath of noise between, the pitch falling as a sentence does. Enough for the glitches to have syllables to catch.
 */
Signal voice()
{
    struct Formant
    {
        double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        void set(double frequency, double bandwidth)
        {
            // RBJ's bandpass with 0 dB at its peak.
            const double w = 2.0 * kPi * frequency / kRate;
            const double alpha = std::sin(w) * std::sinh(std::log(2.0) / 2.0 * (bandwidth / frequency) * w / std::sin(w));
            const double a0 = 1.0 + alpha;
            b0 = alpha / a0;
            b2 = -alpha / a0;
            a1 = -2.0 * std::cos(w) / a0;
            a2 = (1.0 - alpha) / a0;
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
    const double vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                  { 570, 840, 2410 },  { 300, 870, 2240 } };
    const int sentence[] = { 0, 2, 1, 3, 0, 4, 1, 2, 3, 0, 1, 4, 2, 0, 3, 1 };
    juce::Random random(5);
    auto out = silence(4.5);
    std::array<Formant, 3> formants;
    double phase = 0.0;
    int vowel = -1;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const auto syllable = std::min(15, (int) (t / 0.25));
        if (sentence[syllable] != vowel)
        {
            vowel = sentence[syllable];
            for (int f = 0; f < 3; ++f)
                formants[(std::size_t) f].set(vowels[vowel][f], 80.0 + 40.0 * f);
        }
        const double within = std::fmod(t, 0.25);
        const double pitch = 190.0 - 50.0 * t / 4.5 + 3.0 * std::sin(2.0 * kPi * 5.0 * t);
        phase = std::fmod(phase + pitch / kRate, 1.0);
        double source = 0.0;
        for (int k = 1; k * pitch < 5000.0; ++k)
            source += std::sin(2.0 * kPi * k * phase) / k;
        // Voiced for 200 ms with soft edges, then a consonant's hiss.
        const double voiced = within < 0.2 ? std::min({ 1.0, within / 0.02, (0.2 - within) / 0.03 }) : 0.0;
        const double hiss = within >= 0.2 && within < 0.24 ? 0.2 * (random.nextFloat() * 2.0 - 1.0) : 0.0;
        const double sound = source * voiced;
        out[n] = (float) (formants[0].process(sound) + 0.6 * formants[1].process(sound)
                          + 0.3 * formants[2].process(sound) + hiss);
        if (t > 4.0)
            out[n] = 0.0f;
    }
    normalise(out, kSourcePeak);
    return out;
}

struct Take
{
    Signal left, right;
    int glitches = 0;
};

/** Runs a mono source (on both sides) through a processor whose program is chosen, the host playing at 120 BPM. */
Take render(juce::AudioProcessor& processor, const Signal& source)
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
    auto* glitch = dynamic_cast<chromeglitch::ChromeGlitchProcessor*>(&processor);
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
        head.ppq += count * kBpm / 60.0 / kRate;
        if (glitch != nullptr && glitch->glitchStarted.exchange(false))
            ++take.glitches;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    // The distortion's oversampler delays its output; compare like with like.
    const auto latency = (std::size_t) processor.getLatencySamples();
    take.left.erase(take.left.begin(), take.left.begin() + (long) latency);
    take.right.erase(take.right.begin(), take.right.begin() + (long) latency);
    return take;
}

double loudness(const Take& take) { return tonwerktest::momentaryMax(take.left, take.right); }
double loudness(const Signal& source) { return tonwerktest::momentaryMax(source, source); }

float peakOf(const Take& take)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < take.left.size(); ++i)
        peak = std::max({ peak, std::abs(take.left[i]), std::abs(take.right[i]) });
    return peak;
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

juce::String fileName(int index, const juce::String& sound)
{
    return juce::String(index).paddedLeft('0', 2) + " " + sound.replaceCharacters("/&", "-+") + ".wav";
}

/** The source a distortion sound is made for, by its group. */
const Signal& sourceFor(const juce::String& group)
{
    static const Signal guitarTake = guitar(), bassTake = bass(), synthTake = synth(), drumTake = drums(),
                        voiceTake = voice();
    if (group == "Bass")
        return bassTake;
    if (group == "Synths")
        return synthTake;
    if (group == "Drums")
        return drumTake;
    if (group == "Gesang")
        return voiceTake;
    return guitarTake;
}
} // namespace

/**
 * The effects' factory sounds: each in a group of its menu, every name once, and each as loud as what goes in, so
 * switching a sound or the effect on and off does not jump in level. The distortion plays the source of its group
 * (guitar, bass, synth, drums, voice, all peaking at -6 dBFS), the glitch a voice with the host playing.
 */
class EffectPresetTests : public juce::UnitTest
{
public:
    EffectPresetTests() : juce::UnitTest("Effect factory sounds", "Presets") {}

    /** How far a sound may be off its dry source, in dB. */
    static constexpr double kTolerance = 2.0;
    /** How much quieter than the voice a glitch sound may come across: it has no level of its own to set. */
    static constexpr double kGlitchBelow = 3.0;

    void runTest() override
    {
        // For listening: TONWERK_PRESET_WAV_DIR=<dir> writes each sound's take, and the dry sources, as WAV files.
        const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});

        beginTest("Tonwerk Distortion: groups, names and levels");
        {
            const auto& presets = tonwerkdistortion::factoryPresets();
            std::set<juce::String> names;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                const auto& preset = presets[(std::size_t) i];
                const juce::String group = juce::String::fromUTF8(preset.category);
                tonwerkdistortion::DistortionProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(preset.category, tonwerkui::kDistortionCategories) >= 0,
                       "no group: " + sound);

                const auto& source = sourceFor(group);
                const auto take = render(processor, source);
                const double dry = loudness(source), wet = loudness(take);
                const float peak = peakOf(take);
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 20)
                           + group.paddedRight(' ', 10) + juce::String(wet - dry, 1) + " dB against dry, peak "
                           + juce::String(peak, 2) + (i == 0 ? "  (Init, kept)" : ""));
                if (i > 0)
                {
                    expectWithinAbsoluteError(wet - dry, 0.0, kTolerance, sound);
                    expectLessThan(peak, 1.0f, sound);
                }
                if (wavDir.isNotEmpty())
                    writeWav(take.left, take.right,
                             juce::File(wavDir).getChildFile("Tonwerk Distortion").getChildFile(fileName(i + 1, sound)));
            }
        }

        beginTest("Chrome Glitch: groups, names, glitches and levels");
        {
            const auto& presets = chromeglitch::factoryPresets();
            const auto& source = sourceFor("Gesang");
            std::set<juce::String> names;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                const auto& preset = presets[(std::size_t) i];
                chromeglitch::ChromeGlitchProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(preset.category, tonwerkui::kGlitchCategories) >= 0,
                       "no group: " + sound);

                const auto take = render(processor, source);
                const double change = loudness(take) - loudness(source);
                const float peak = peakOf(take);
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 20)
                           + juce::String::fromUTF8(preset.category).paddedRight(' ', 14) + juce::String(take.glitches)
                           + " glitches, " + juce::String(change, 1) + " dB against dry, peak " + juce::String(peak, 2));
                // Every sound is heard within a few bars, none louder than the voice. Dropouts take some of it away,
                // which is what they are for, but none hollows it out.
                expectGreaterThan(take.glitches, 3, sound);
                expectLessThan(change, kTolerance, sound);
                expectGreaterThan(change, -kGlitchBelow, sound);
                expectLessThan(peak, kSourcePeak * 1.1f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take.left, take.right,
                             juce::File(wavDir).getChildFile("Chrome Glitch").getChildFile(fileName(i + 1, sound)));
            }
        }

        if (wavDir.isNotEmpty())
            for (const auto* group : { "Gitarre", "Bass", "Synths", "Drums", "Gesang" })
            {
                const auto& source = sourceFor(group);
                writeWav(source, source,
                         juce::File(wavDir).getChildFile("Quellen").getChildFile(juce::String(group) + ".wav"));
            }

        beginTest("A sound sets every knob it does not name back to its default, and a project remembers it");
        {
            tonwerkdistortion::DistortionProcessor processor;
            processor.state.getParameter("tone")->setValueNotifyingHost(1.0f);
            const auto& presets = tonwerkdistortion::factoryPresets();
            int metal = -1;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (juce::String(presets[(std::size_t) i].name) == "Metal")
                    metal = i;
            expect(metal > 0);
            processor.setCurrentProgram(metal);
            expectWithinAbsoluteError(processor.readSettings().distortion, 1.0f, 1.0e-4f);
            processor.setCurrentProgram(0);
            expectWithinAbsoluteError(processor.readSettings().distortion, 0.5f, 1.0e-4f);
            expectWithinAbsoluteError(processor.readSettings().tone, 0.5f, 1.0e-4f);

            processor.setCurrentProgram(metal);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            tonwerkdistortion::DistortionProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.getCurrentProgram(), metal);

            chromeglitch::ChromeGlitchProcessor glitch;
            glitch.setCurrentProgram(glitch.getNumPrograms() - 1);
            glitch.getStateInformation(saved);
            chromeglitch::ChromeGlitchProcessor glitchLoaded;
            glitchLoaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(glitchLoaded.getCurrentProgram(), glitch.getNumPrograms() - 1);
            expectEquals(glitchLoaded.getProgramName(glitchLoaded.getCurrentProgram()),
                         glitch.getProgramName(glitch.getNumPrograms() - 1));
        }
    }
};

static EffectPresetTests effectPresetTests;
