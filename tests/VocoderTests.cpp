#include "Platform.h"
#include "Loudness.h"
#include "PresetCategories.h"
#include "VocoderEditor.h"
#include "VocoderEngine.h"
#include "VocoderPresets.h"
#include "VocoderProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkvocoder;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;

using Signal = std::vector<float>;
using Knobs = std::vector<std::pair<const char*, float>>;

float amplitudeAt(const Signal& signal, double hz, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = (std::size_t) (to * kRate);
    const double w = kTwoPi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = a; n < b; ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (b - a));
}

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = std::min(signal.size(), (std::size_t) (to * kRate));
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

Signal sine(double hz, double seconds, float amplitude = 0.3f)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(kTwoPi * hz * (double) n / kRate);
    return out;
}

Signal noise(double seconds, float amplitude = 0.3f, int seed = 3)
{
    juce::Random random(seed);
    Signal out((std::size_t) (seconds * kRate));
    for (auto& v : out)
        v = amplitude * (random.nextFloat() * 2.0f - 1.0f);
    return out;
}

/**
 * Something like speech, as in EffectPresetTests: a voice's harmonics through the formants of a, e, i, o, u, four
 * syllables a second with a consonant's hiss between, peaking at -6 dBFS.
 */
Signal speech(double seconds = 4.5)
{
    struct Formant
    {
        double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        void set(double frequency, double bandwidth)
        {
            const double w = kTwoPi * frequency / kRate;
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
    const int sentence[] = { 0, 2, 1, 3, 0, 4, 1, 2, 3, 0, 1, 4, 2, 0, 3, 1, 2, 0 };
    juce::Random random(5);
    Signal out((std::size_t) (seconds * kRate));
    std::array<Formant, 3> formants;
    double phase = 0.0;
    int vowel = -1;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const auto syllable = std::min(17, (int) (t / 0.25));
        if (sentence[syllable] != vowel)
        {
            vowel = sentence[syllable];
            for (int f = 0; f < 3; ++f)
                formants[(std::size_t) f].set(vowels[vowel][f], 80.0 + 40.0 * f);
        }
        const double within = std::fmod(t, 0.25);
        const double pitch = 190.0 - 50.0 * t / seconds + 3.0 * std::sin(kTwoPi * 5.0 * t);
        phase = std::fmod(phase + pitch / kRate, 1.0);
        double source = 0.0;
        for (int k = 1; k * pitch < 5000.0; ++k)
            source += std::sin(kTwoPi * k * phase) / k;
        const double voiced = within < 0.2 ? std::min({ 1.0, within / 0.02, (0.2 - within) / 0.03 }) : 0.0;
        const double hiss = within >= 0.2 && within < 0.24 ? 0.2 * (random.nextFloat() * 2.0 - 1.0) : 0.0;
        const double sound = source * voiced;
        out[n] = (float) (formants[0].process(sound) + 0.6 * formants[1].process(sound)
                          + 0.3 * formants[2].process(sound) + hiss);
    }
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

struct Note
{
    double on, off;
    int note;
};

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

/** The voice through the engine alone, the notes played on the carrier. */
Take play(const Settings& s, const Signal& voice, const std::vector<Note>& notes)
{
    Engine engine;
    engine.prepare(kRate);
    Take take { Signal(voice.size()), Signal(voice.size()) };
    std::vector<std::tuple<std::size_t, int, int>> events;
    for (const auto& n : notes)
    {
        events.emplace_back((std::size_t) (n.on * kRate), 1, n.note);
        events.emplace_back((std::size_t) (n.off * kRate), 0, n.note);
    }
    std::sort(events.begin(), events.end());
    std::size_t at = 0;
    const auto started = std::chrono::steady_clock::now();
    auto runTo = [&](std::size_t end) {
        end = std::min(end, voice.size());
        if (end > at)
            engine.process(voice.data() + at, take.left.data() + at, take.right.data() + at, (int) (end - at), s);
        at = std::max(at, end);
    };
    for (const auto& [when, kind, note] : events)
    {
        runTo(when);
        if (kind == 1)
            engine.carrier.noteOn(note, s);
        else
            engine.carrier.noteOff(note, s);
    }
    runTo(voice.size());
    take.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return take;
}

Signal sum(const Take& take)
{
    Signal out(take.left.size());
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = 0.5f * (take.left[n] + take.right[n]);
    return out;
}

/** The voice through the processor with a chord held from the start, as from a MIDI track in Logic. */
Take render(VocoderProcessor& processor, const Signal& voice, std::initializer_list<int> chord = { 48, 55, 60, 64 })
{
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Take take { Signal(voice.size()), Signal(voice.size()) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (std::size_t at = 0; at < voice.size(); at += kBlock)
    {
        midi.clear();
        if (at == 0)
            for (const int note : chord)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        const int count = (int) std::min<std::size_t>(kBlock, voice.size() - at);
        buffer.setSize(2, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, voice.data() + at, count);
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

class VocoderTests : public juce::UnitTest
{
public:
    VocoderTests() : juce::UnitTest("Tonwerk Vocoder", "Vocoder") {}

    void runTest() override
    {
        beginTest("The carrier's notes come out where the voice has energy, and nothing without notes or voice");
        {
            Settings s;
            const auto voice = noise(1.0);
            const auto out = sum(play(s, voice, { { 0.0, 1.0, 57 } }));
            // A3 is 220 Hz: its harmonics stand out, what lies between them does not.
            const float harmonic = amplitudeAt(out, 440.0, 0.3, 0.9);
            const float between = amplitudeAt(out, 550.0, 0.3, 0.9);
            logMessage("A3 on noise: 440 Hz " + juce::String(harmonic, 4) + ", 550 Hz " + juce::String(between, 5));
            expectGreaterThan(harmonic, between * 10.0f);

            // No key, no sound: neither the carrier's noise nor the hiss come through alone.
            expectLessThan(rms(sum(play(s, voice, {})), 0.3, 1.0), 1.0e-6f);
            expectLessThan(rms(sum(play(s, Signal(48000, 0.0f), { { 0.0, 1.0, 57 } })), 0.3, 1.0), 1.0e-4f);
        }

        beginTest("The bank follows the voice's spectrum; formant moves it");
        {
            Settings s;
            s.noise = 1.0f; // mostly a flat carrier, so the bank alone shapes the spectrum
            s.sibilance = 0.0f;
            s.bands = 32;
            const auto voice = sine(1000.0, 1.5);
            const auto out = sum(play(s, voice, { { 0.0, 1.5, 24 } }));
            const float at = rms(out, 0.5, 1.5);
            // Measured through a narrow band around each frequency: the noise at 1 kHz against 300 Hz and 3 kHz.
            auto band = [&out](double hz) {
                Biquad a, b;
                a.bandpass(hz, 8.0, kRate);
                b.bandpass(hz, 8.0, kRate);
                Signal f(out.size());
                for (std::size_t n = 0; n < out.size(); ++n)
                    f[n] = b.process(a.process(out[n]));
                return rms(f, 0.5, 1.5);
            };
            const float centre = band(1000.0), low = band(300.0), high = band(3000.0);
            logMessage("1 kHz voice: output " + juce::String(at, 3) + ", near 1 kHz " + juce::String(centre, 4)
                       + ", 300 Hz " + juce::String(low, 5) + ", 3 kHz " + juce::String(high, 5));
            expectGreaterThan(centre, 20.0f * low);
            expectGreaterThan(centre, 20.0f * high);

            s.formant = 12.0f;
            const auto shifted = sum(play(s, voice, { { 0.0, 1.5, 24 } }));
            auto bandOf = [](const Signal& signal, double hz) {
                Biquad a, b;
                a.bandpass(hz, 8.0, kRate);
                b.bandpass(hz, 8.0, kRate);
                Signal f(signal.size());
                for (std::size_t n = 0; n < signal.size(); ++n)
                    f[n] = b.process(a.process(signal[n]));
                return rms(f, 0.5, 1.5);
            };
            logMessage("formant +12: near 2 kHz " + juce::String(bandOf(shifted, 2000.0), 4) + ", near 1 kHz "
                       + juce::String(bandOf(shifted, 1000.0), 5));
            expectGreaterThan(bandOf(shifted, 2000.0), 5.0f * bandOf(shifted, 1000.0));
        }

        beginTest("About as loud as the voice, one note or a chord, any band count");
        {
            const auto voice = speech();
            const double dry = tonwerktest::momentaryMax(voice, voice);
            for (const int bands : kBandCounts)
                for (const bool chord : { false, true })
                {
                    Settings s;
                    s.bands = bands;
                    std::vector<Note> notes { { 0.0, 4.5, 48 } };
                    if (chord)
                        for (const int n : { 55, 60, 64 })
                            notes.push_back({ 0.0, 4.5, n });
                    const auto take = play(s, voice, notes);
                    const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                    logMessage(juce::String(bands) + " bands, " + (chord ? "chord" : "one note") + ": "
                               + juce::String(change, 1) + " dB against the voice");
                    expectWithinAbsoluteError(change, 0.0, 4.0);
                }
        }

        beginTest("Release: the bands let go of the voice in their release time; freeze holds them");
        {
            Settings s;
            s.release = 0.05f;
            s.sibilance = 0.0f;
            Signal voice = noise(1.5);
            std::fill(voice.begin() + 48000, voice.end(), 0.0f);
            const auto out = sum(play(s, voice, { { 0.0, 1.5, 48 } }));
            const float held = rms(out, 0.5, 1.0);
            const float after = rms(out, 1.3, 1.5);
            logMessage("voice stops: " + juce::String(juce::Decibels::gainToDecibels(after / held), 0) + " dB 300 ms on");
            expectLessThan(after, held * 0.01f);

            s.freeze = false;
            // Frozen while the voice sounds: the bands keep their levels after it stops.
            Engine engine;
            engine.prepare(kRate);
            engine.carrier.noteOn(48, s);
            Signal l(voice.size()), r(voice.size());
            engine.process(voice.data(), l.data(), r.data(), 40000, s);
            s.freeze = true;
            engine.process(voice.data() + 40000, l.data() + 40000, r.data() + 40000, (int) voice.size() - 40000, s);
            expectGreaterThan(rms(l, 1.3, 1.5), rms(l, 0.5, 0.8) * 0.5f);
        }

        beginTest("Carrier: notes held to the end of their release, latch holds a chord until the next");
        {
            Settings s;
            s.carrierRelease = 0.05f;
            const auto voice = noise(2.0);
            const auto out = sum(play(s, voice, { { 0.0, 0.5, 48 } }));
            expectLessThan(rms(out, 0.8, 1.0), rms(out, 0.2, 0.4) * 0.01f + 0.004f);

            s.latch = true;
            s.noise = 0.0f;
            const auto latched = sum(play(s, voice, { { 0.0, 0.5, 48 } }));
            expectGreaterThan(rms(latched, 1.0, 1.5), rms(latched, 0.2, 0.4) * 0.5f);
            // A new chord after letting go replaces the latched one: C2 gone, A2 there.
            const auto replaced = sum(play(s, voice, { { 0.0, 0.5, 36 }, { 1.0, 1.2, 45 } }));
            expectGreaterThan(amplitudeAt(replaced, 110.0, 1.4, 1.9), 5.0f * amplitudeAt(replaced, 65.41, 1.4, 1.9));
        }

        beginTest("Sibilance: the voice's hiss passes the bank");
        {
            Settings s;
            Signal hissy = noise(1.0, 0.3f, 9);
            Biquad a, b;
            a.highpass(6000.0, 0.7, kRate);
            b.highpass(6000.0, 0.7, kRate);
            for (auto& v : hissy)
                v = b.process(a.process(v));
            s.sibilance = 0.0f;
            s.noise = 0.0f;
            const float without = rms(sum(play(s, hissy, { { 0.0, 1.0, 36 } })), 0.2, 1.0);
            s.sibilance = 1.0f;
            const float with = rms(sum(play(s, hissy, { { 0.0, 1.0, 36 } })), 0.2, 1.0);
            logMessage("hiss: " + juce::String(without, 4) + " without, " + juce::String(with, 4) + " with");
            expectGreaterThan(with, 2.0f * without);
        }

        beginTest("Width spreads the bands; at 0 both sides are the same");
        {
            Settings s;
            s.width = 0.0f;
            const auto voice = speech(2.0);
            const auto centred = play(s, voice, { { 0.0, 2.0, 48 } });
            float difference = 0.0f;
            for (std::size_t n = 0; n < centred.left.size(); ++n)
                difference = std::max(difference, std::abs(centred.left[n] - centred.right[n]));
            expectLessThan(difference, 1.0e-5f);
            s.width = 1.0f;
            const auto wide = play(s, voice, { { 0.0, 2.0, 48 } });
            Signal side(wide.left.size());
            for (std::size_t n = 0; n < side.size(); ++n)
                side[n] = wide.left[n] - wide.right[n];
            expectGreaterThan(rms(side, 0.2, 2.0), 0.3f * rms(sum(wide), 0.2, 2.0));
        }

        beginTest("32 bands under an eight-note super saw run far inside real time");
        {
            Settings s;
            s.bands = 32;
            s.wave = CarrierWave::superSaw;
            const auto voice = speech(10.0);
            std::vector<Note> notes;
            for (int i = 0; i < 8; ++i)
                notes.push_back({ 0.0, 10.0, 48 + 3 * i });
            const auto take = play(s, voice, notes);
            const double factor = 10.0 / take.seconds;
            logMessage("32 bands, 8 voices: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 10.0);
        }

        beginTest("Factory sounds: groups, names and levels against the voice");
        {
            const auto voice = speech();
            const double dry = tonwerktest::momentaryMax(voice, voice);
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_VOCODER_WAV_DIR", {});
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                VocoderProcessor processor;
                processor.setCurrentProgram(i);
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kVocoderCategories) >= 0,
                       "no group: " + sound);
                const auto take = render(processor, voice);
                const double change = tonwerktest::momentaryMax(take.left, take.right) - dry;
                float peak = 0.0f;
                bool finite = true;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                {
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 24)
                           + juce::String::fromUTF8(presets[(std::size_t) i].category).paddedRight(' ', 12)
                           + juce::String(change, 1) + " dB against dry, peak " + juce::String(peak, 2));
                expect(finite, sound);
                expectWithinAbsoluteError(change, 0.0, 2.0, sound);
                expectLessThan(peak, 1.0f, sound);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
            if (wavDir.isNotEmpty())
                writeWav({ voice, voice }, juce::File(wavDir).getChildFile("00 Stimme trocken.wav"));
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            VocoderProcessor p;
            p.state.getParameter("formant")->setValueNotifyingHost(p.state.getParameter("formant")->convertTo0to1(-5.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("bands"), juce::String("16"));
            expectEquals(text("formant"), juce::String("-5 HT"));
            expectEquals(text("release"), juce::String("60 ms"));
            expectEquals(text("sibilance"), juce::String("50 %"));
            expectEquals(text("octave"), juce::String("0 Okt"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            VocoderProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().formant, -5.0f, 0.01f);
            expectEquals(loaded.readSettings().bands, 16);
        }

        beginTest("The editor shows the bands and the carrier's notes");
        {
            VocoderProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<VocoderEditor>(created.get());
            expect(editor != nullptr);
            // Rendered without releasing, so the notes are still held for the display.
            p.setPlayConfigDetails(2, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            const auto voice = speech(1.0);
            juce::AudioBuffer<float> buffer(2, kBlock);
            for (std::size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
            {
                juce::MidiBuffer midi;
                if (at == 0)
                    for (const int note : { 48, 52, 55 })
                        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, voice.data() + at, kBlock);
                p.processBlock(buffer, midi);
            }
            editor->bank.view.refresh();
            expectEquals(editor->bank.view.notesText(), juce::String("C2 E2 G2"));
            expectGreaterThan(editor->getWidth(), 700);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("vocoder.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static VocoderTests vocoderTests;
