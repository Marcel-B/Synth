#include "ExpanderEditor.h"
#include "ExpanderEngine.h"
#include "ExpanderPresets.h"
#include "ExpanderProcessor.h"
#include "HardwarePanel.h"
#include "HardwareSheet.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkexpander;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

using Signal = std::vector<float>;
using Knobs = std::vector<std::pair<const char*, float>>;

struct Note
{
    double on, off;
    int note;
    float velocity = 1.0f;
};

struct Stereo
{
    Signal left, right;
};

/** The engine alone, stereo, at `rate`: no master, no effects, 120 BPM, stopped. */
Stereo play(const Settings& s, const std::vector<Note>& notes, double length, double rate = kRate)
{
    Engine engine;
    engine.prepare(rate);
    const auto total = (std::size_t) (length * rate);
    Stereo out { Signal(total, 0.0f), Signal(total, 0.0f) };
    std::vector<std::tuple<std::size_t, int, int, float>> events;
    for (const auto& n : notes)
    {
        events.emplace_back((std::size_t) (n.on * rate), 1, n.note, n.velocity);
        events.emplace_back((std::size_t) (n.off * rate), 0, n.note, 0.0f);
    }
    std::sort(events.begin(), events.end());
    std::size_t at = 0;
    auto run = [&](std::size_t until) {
        while (at < until)
        {
            const int count = (int) std::min<std::size_t>(64, until - at);
            engine.render(out.left.data() + at, out.right.data() + at, count, s, {});
            at += (std::size_t) count;
        }
    };
    for (const auto& [when, kind, note, velocity] : events)
    {
        run(std::min(when, total));
        if (kind == 1)
            engine.noteOn(note, velocity, s);
        else
            engine.noteOff(note, s);
    }
    run(total);
    return out;
}

float rms(const Signal& signal, double from, double to, double rate = kRate)
{
    const auto a = (std::size_t) (from * rate), b = (std::size_t) (to * rate);
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

/** The pitch from rising zero crossings, in Hz. */
double frequencyOf(const Signal& signal, double from, double to)
{
    std::vector<double> crossings;
    for (auto n = (std::size_t) (from * kRate) + 1; n < (std::size_t) (to * kRate); ++n)
        if (signal[n - 1] < 0.0f && signal[n] >= 0.0f)
            crossings.push_back((double) (n - 1) + signal[n - 1] / (signal[n - 1] - signal[n]));
    if (crossings.size() < 2)
        return 0.0;
    return (double) (crossings.size() - 1) * kRate / (crossings.back() - crossings.front());
}

/** The amplitude of `hz` in a stretch (Goertzel). */
double amplitudeAt(const Signal& signal, double hz, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = (std::size_t) (to * kRate);
    const double w = 2.0 * kPi * hz / kRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t n = a; n < b; ++n)
    {
        // A Hann window so the strong neighbours do not leak in.
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * (double) (n - a) / (double) (b - a));
        const double s0 = signal[n] * window + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double) (b - a) * 4.0;
}

/** Oscillator 1 alone, the filter wide open and fixed, every voice alike: the raw wave. */
Settings plain()
{
    Settings s;
    s.osc2.level = 0.0f;
    s.cutoff = 18000.0f;
    s.resonance = 0.0f;
    s.envCutoff = 0.0f;
    s.keytrack = 0.0f;
    s.drift = 0.0f;
    s.spread = 0.0f;
    s.velocity = 0.0f;
    s.ampEnv = { 0.001f, 0.1f, 1.0f, 0.05f };
    return s;
}

void set(ExpanderProcessor& processor, const Knobs& knobs)
{
    for (const auto& [id, value] : knobs)
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse;
}

struct Take
{
    Signal left, right;
    double seconds = 0.0;
};

Take render(ExpanderProcessor& processor, const std::vector<Note>& notes, double length)
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
            if (off >= at && off < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOff(1, n.note), (int) (off - at));
            if (on >= at && on < at + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, n.velocity), (int) (on - at));
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

/** Two bars at 100 BPM: chords of four held over each other, then a line on top. */
std::vector<Note> phrase()
{
    const double beat = 0.6;
    std::vector<Note> notes;
    const std::array<std::array<int, 4>, 4> chords { { { 48, 55, 60, 63 }, { 44, 51, 56, 60 }, { 46, 53, 58, 62 },
                                                       { 43, 50, 55, 59 } } };
    for (std::size_t c = 0; c < chords.size(); ++c)
        for (const int n : chords[c])
            notes.push_back({ (double) c * 2 * beat, ((double) c * 2 + 1.8) * beat, n, 0.8f });
    const std::array<int, 8> line { 72, 75, 79, 77, 75, 74, 72, 70 };
    for (std::size_t i = 0; i < line.size(); ++i)
        notes.push_back({ 8 * beat + (double) i * 0.5 * beat, 8 * beat + ((double) i + 0.9) * 0.5 * beat, line[i], 0.9f });
    return notes;
}
} // namespace

class ExpanderTests : public juce::UnitTest
{
public:
    ExpanderTests() : juce::UnitTest("Tonwerk Expander", "Expander") {}

    void runTest() override
    {
        beginTest("A key plays its pitch; octave, semitones and fine move oscillator 2");
        {
            auto s = plain();
            const double a4 = frequencyOf(play(s, { { 0.0, 1.0, 69 } }, 1.0).left, 0.2, 0.9);
            logMessage("A4: " + juce::String(a4, 2) + " Hz");
            expectWithinAbsoluteError(a4, 440.0, 0.3);

            s.osc1.level = 0.0f;
            s.osc2.level = 0.8f;
            s.osc2.octave = -1;
            s.osc2.semitones = 7;
            s.osc2.fine = 20.0f;
            const double moved = frequencyOf(play(s, { { 0.0, 1.0, 69 } }, 1.0).left, 0.2, 0.9);
            expectWithinAbsoluteError(12.0 * std::log2(moved / 440.0), -12.0 + 7.0 + 0.2, 0.02);
        }

        beginTest("The pulse holds its width; its offset is taken out");
        {
            auto s = plain();
            s.osc1.wave = Wave::pulse;
            for (const float width : { 0.5f, 0.25f, 0.1f })
            {
                s.osc1.width = width;
                const auto out = play(s, { { 0.0, 1.0, 45 } }, 1.0).left;
                int high = 0, all = 0;
                double mean = 0.0;
                for (auto n = (std::size_t) (0.2 * kRate); n < (std::size_t) (0.9 * kRate); ++n, ++all)
                {
                    high += out[n] > 0.0f ? 1 : 0;
                    mean += out[n];
                }
                const double duty = (double) high / all;
                logMessage("width " + juce::String(width * 100.0f, 0) + " %: duty " + juce::String(duty * 100.0, 1) + " %");
                expectWithinAbsoluteError(duty, (double) width, 0.015);
                expectLessThan(std::abs(mean / all), 0.005);
            }
        }

        beginTest("Sync: oscillator 2 restarts with oscillator 1, so the voice repeats at 1's period");
        {
            // At 44 kHz A4's cycle is exactly 100 samples.
            constexpr double rate = 44000.0;
            auto s = plain();
            s.osc1.level = 0.0f;
            s.osc2.level = 0.8f;
            s.osc2.semitones = 7;
            s.osc2.fine = 13.0f;
            auto mismatch = [](const Signal& x) {
                double d = 0.0, sum = 0.0;
                for (auto n = (std::size_t) (0.3 * rate); n < (std::size_t) (0.8 * rate); ++n)
                {
                    d += std::pow(x[n] - x[n + 100], 2.0);
                    sum += (double) x[n] * x[n];
                }
                return std::sqrt(d / sum);
            };
            const double free = mismatch(play(s, { { 0.0, 1.0, 69 } }, 1.0, rate).left);
            s.sync = true;
            const double synced = mismatch(play(s, { { 0.0, 1.0, 69 } }, 1.0, rate).left);
            logMessage("against one master cycle later: free " + juce::String(free, 3) + ", synced "
                       + juce::String(synced, 4));
            expectGreaterThan(free, 0.3);
            expectLessThan(synced, 0.01);
        }

        beginTest("The steps are smoothed: little folds back below a high saw, synced or not");
        {
            // C7 at 48 kHz: the harmonics above 24 kHz fold back between the real ones. The loudest of those landing
            // below 8 kHz, where they are heard, against the fundamental; a plain saw's would reach about -22 dB.
            auto s = plain();
            const double f = 440.0 * std::exp2((96 - 69) / 12.0);
            auto folded = [&](const Signal& x, double base) {
                const double fundamental = amplitudeAt(x, base, 0.2, 0.7);
                double worst = 0.0;
                for (int k = 2; k < 80; ++k)
                {
                    double alias = std::fmod(k * base, kRate);
                    if (alias > 0.5 * kRate)
                        alias = kRate - alias;
                    const double nearest = std::round(alias / base) * base;
                    if (std::abs(alias - k * base) < 1.0 || alias > 8000.0 || std::abs(alias - nearest) < 40.0)
                        continue;
                    worst = std::max(worst, amplitudeAt(x, alias, 0.2, 0.7));
                }
                return juce::Decibels::gainToDecibels(worst / fundamental);
            };
            const double saw = folded(play(s, { { 0.0, 1.0, 96 } }, 1.0).left, f);
            s.osc1.wave = Wave::pulse;
            s.osc1.width = 0.3f;
            const double pulse = folded(play(s, { { 0.0, 1.0, 96 } }, 1.0).left, f);
            s = plain();
            s.osc1.level = 0.0f;
            s.osc2.level = 0.8f;
            s.osc2.semitones = 5;
            s.sync = true;
            s.osc2.wave = Wave::both;
            const double sync = folded(play(s, { { 0.0, 1.0, 96 } }, 1.0).left, f);
            logMessage("folded back below 8 kHz at C7: saw " + juce::String(saw, 1) + " dB, pulse " + juce::String(pulse, 1)
                       + " dB, sync " + juce::String(sync, 1) + " dB");
            expectLessThan(saw, -36.0);
            expectLessThan(pulse, -36.0);
            expectLessThan(sync, -30.0);
        }

        beginTest("The mode fades lowpass into notch into highpass; the switch makes it a bandpass");
        {
            // A2's harmonics against the raw saw: the 2nd far below the cutoff, the 10th on it, the 30th far above.
            auto s = plain();
            const double root = 110.0;
            auto harmonics = [&](const Settings& settings) {
                const auto x = play(settings, { { 0.0, 1.0, 45 } }, 1.0).left;
                return std::array<double, 3> { amplitudeAt(x, 2 * root, 0.2, 0.9), amplitudeAt(x, 10 * root, 0.2, 0.9),
                                               amplitudeAt(x, 30 * root, 0.2, 0.9) };
            };
            const auto raw = harmonics(s);
            auto db = [&raw](const std::array<double, 3>& h, std::size_t i) {
                return juce::Decibels::gainToDecibels(h[i] / raw[i]);
            };
            s.cutoff = 1100.0f;
            const auto lowpass = harmonics(s);
            s.mode = 0.5f;
            const auto notch = harmonics(s);
            s.mode = 1.0f;
            const auto highpass = harmonics(s);
            s.bandpass = true;
            const auto bandpass = harmonics(s);
            auto line = [&](const char* label, const std::array<double, 3>& h) {
                logMessage(juce::String(label) + ": " + juce::String(db(h, 0), 1) + " / " + juce::String(db(h, 1), 1)
                           + " / " + juce::String(db(h, 2), 1) + " dB");
            };
            line("lowpass", lowpass);
            line("notch", notch);
            line("highpass", highpass);
            line("bandpass", bandpass);
            expectWithinAbsoluteError(db(lowpass, 0), 0.0, 1.0);
            expectLessThan(db(lowpass, 2), -16.0);
            expectWithinAbsoluteError(db(notch, 0), 0.0, 1.0);
            expectLessThan(db(notch, 1), -20.0);
            expectWithinAbsoluteError(db(notch, 2), -1.9, 1.0);
            expectLessThan(db(highpass, 0), -24.0);
            expectWithinAbsoluteError(db(highpass, 2), 0.0, 1.0);
            // Whole at the cutoff, 12 dB an octave either side of it (the prototype gives -8 and -4.5 here).
            expectWithinAbsoluteError(db(bandpass, 1), 0.0, 1.0);
            expectLessThan(db(bandpass, 0), -6.0);
            expectLessThan(db(bandpass, 2), -3.0);
        }

        beginTest("Resonance raises the cutoff's harmonic; full resonance stays bounded on every mode");
        {
            auto s = plain();
            s.cutoff = 1100.0f;
            auto at10 = [&](const Settings& settings) {
                return amplitudeAt(play(settings, { { 0.0, 1.0, 45 } }, 1.0).left, 1100.0, 0.2, 0.9);
            };
            const double flat = at10(s);
            s.resonance = 0.8f;
            const double peak = at10(s);
            logMessage("10th harmonic, resonance 80 % against 0: " + juce::String(juce::Decibels::gainToDecibels(peak / flat), 1) + " dB");
            expectGreaterThan(peak, flat * 3.0);

            for (const bool band : { false, true })
                for (const float mode : { 0.0f, 0.5f, 1.0f })
                {
                    Settings loud;
                    loud.resonance = 1.0f;
                    loud.mode = mode;
                    loud.bandpass = band;
                    loud.osc1.wave = loud.osc2.wave = Wave::both;
                    loud.osc1.level = loud.osc2.level = 1.0f;
                    loud.sync = true;
                    loud.envPitch = 24.0f;
                    loud.lfoCutoff = 1.0f;
                    loud.lfoPwm = 1.0f;
                    loud.envCutoff = 1.0f;
                    std::vector<Note> notes;
                    for (const int n : { 36, 43, 48, 55, 60, 64, 67, 72 })
                        notes.push_back({ 0.0, 1.0, n });
                    const auto out = play(loud, notes, 1.6);
                    float top = 0.0f;
                    bool finite = true;
                    for (std::size_t n = 0; n < out.left.size(); ++n)
                    {
                        finite = finite && std::isfinite(out.left[n]) && std::isfinite(out.right[n]);
                        top = std::max({ top, std::abs(out.left[n]), std::abs(out.right[n]) });
                    }
                    const auto label = juce::String(band ? "bandpass" : "mode ") + juce::String(mode);
                    expect(finite, label);
                    expectLessThan(top, 6.0f, label);
                    // Released at 1 s with a 300 ms release.
                    expectLessThan(rms(out.left, 1.45, 1.6), 0.002f, label);
                }
        }

        beginTest("The filter envelope opens the cutoff; velocity softens and darkens a soft key");
        {
            auto s = plain();
            s.cutoff = 300.0f;
            s.envCutoff = 1.0f;
            s.filterEnv = { 0.001f, 0.2f, 0.0f, 0.1f };
            const auto x = play(s, { { 0.0, 1.0, 45 } }, 1.0).left;
            const double early = amplitudeAt(x, 20 * 110.0, 0.0, 0.08), late = amplitudeAt(x, 20 * 110.0, 0.6, 0.9);
            expectGreaterThan(early, late * 10.0);

            s = plain();
            s.velocity = 1.0f;
            const float full = rms(play(s, { { 0.0, 1.0, 57, 1.0f } }, 1.0).left, 0.2, 0.9);
            const float half = rms(play(s, { { 0.0, 1.0, 57, 0.5f } }, 1.0).left, 0.2, 0.9);
            logMessage("velocity 50 % at full amount: " + juce::String(juce::Decibels::gainToDecibels(half / full), 1) + " dB");
            expectLessThan(juce::Decibels::gainToDecibels(half / full), -6.0f);
            s.velocity = 0.0f;
            expectWithinAbsoluteError(rms(play(s, { { 0.0, 1.0, 57, 0.5f } }, 1.0).left, 0.2, 0.9), full, full * 0.01f);
        }

        beginTest("Eight voices: a ninth key takes the oldest; mono plays one, unison all eight on one key");
        {
            auto s = plain();
            Engine engine;
            engine.prepare(kRate);
            std::array<float, 64> l {}, r {};
            for (int i = 0; i < 9; ++i)
            {
                engine.noteOn(48 + i, 1.0f, s);
                engine.render(l.data(), r.data(), 64, s, {});
            }
            expectEquals(engine.activeVoices(), kVoices);
            expectEquals(engine.newestVoice()->currentNote(), 56);

            Engine monoEngine;
            monoEngine.prepare(kRate);
            s.voiceMode = VoiceMode::mono;
            monoEngine.noteOn(48, 1.0f, s);
            monoEngine.noteOn(55, 1.0f, s);
            monoEngine.render(l.data(), r.data(), 64, s, {});
            expectEquals(monoEngine.activeVoices(), 1);
            expectEquals(monoEngine.newestVoice()->currentNote(), 55);
            monoEngine.noteOff(55, s);
            expectEquals(monoEngine.newestVoice()->currentNote(), 48);

            // Unison as loud as one voice, give or take the beating; wider with spread.
            s = plain();
            const float one = rms(play(s, { { 0.0, 1.0, 45 } }, 1.0).left, 0.2, 0.9);
            s.voiceMode = VoiceMode::unison;
            s.unisonDetune = 20.0f;
            Engine stack;
            stack.prepare(kRate);
            stack.noteOn(45, 1.0f, s);
            stack.render(l.data(), r.data(), 64, s, {});
            expectEquals(stack.activeVoices(), kVoices);
            const float eight = rms(play(s, { { 0.0, 1.0, 45 } }, 1.0).left, 0.2, 0.9);
            logMessage("unison against one voice: " + juce::String(juce::Decibels::gainToDecibels(eight / one), 1) + " dB");
            expectWithinAbsoluteError(juce::Decibels::gainToDecibels(eight / one), 0.0f, 3.0f);
        }

        beginTest("Drift puts each voice a little out of tune with the others; at 0 they agree");
        {
            auto s = plain();
            auto twoPlays = [&](float drift) {
                s.drift = drift;
                // One key twice: the second press takes the next voice.
                const auto x = play(s, { { 0.0, 0.5, 57 }, { 1.0, 1.5, 57 } }, 1.6).left;
                return 1200.0 * std::log2(frequencyOf(x, 1.1, 1.45) / frequencyOf(x, 0.1, 0.45));
            };
            const double none = twoPlays(0.0f), full = twoPlays(1.0f);
            logMessage("between two voices: drift 0 " + juce::String(none, 2) + " ct, drift 100 % " + juce::String(full, 2) + " ct");
            expectLessThan(std::abs(none), 0.1);
            expectGreaterThan(std::abs(full), 1.0);
            expectLessThan(std::abs(full), 20.0);
        }

        beginTest("Glide slides from the last key; the LFO's delay fades its vibrato in");
        {
            auto s = plain();
            s.voiceMode = VoiceMode::mono;
            s.glide = 0.1f;
            const auto tied = play(s, { { 0.0, 0.55, 45 }, { 0.5, 1.2, 57 } }, 1.2).left;
            const double during = frequencyOf(tied, 0.51, 0.55);
            expectGreaterThan(during, 120.0);
            expectLessThan(during, 210.0);
            expectWithinAbsoluteError(frequencyOf(tied, 0.9, 1.15), 220.0, 0.5);

            s = plain();
            s.lfoPitch = 100.0f;
            s.lfoRate = 5.0f;
            s.lfoShape = LfoShape::square;
            s.lfoDelay = 1.0f;
            const auto x = play(s, { { 0.0, 2.0, 57 } }, 2.0).left;
            // How far the pitch swings in 100 ms windows (half a cycle of the LFO each).
            auto swing = [&x](double from, double to) {
                double low = 1.0e9, high = 0.0;
                for (double t = from; t + 0.1 <= to; t += 0.1)
                {
                    const double f = frequencyOf(x, t + 0.01, t + 0.09);
                    low = std::min(low, f);
                    high = std::max(high, f);
                }
                return 1200.0 * std::log2(high / low);
            };
            const double early = swing(0.0, 0.3), late = swing(1.2, 1.9);
            logMessage("vibrato swing: first 300 ms " + juce::String(early, 0) + " ct, after the delay " + juce::String(late, 0) + " ct");
            expectLessThan(early, 80.0);
            expectWithinAbsoluteError(late, 200.0, 20.0);
        }

        beginTest("Every factory sound plays chords and a line, finite and bounded");
        {
            // For listening: TONWERK_EXPANDER_WAV_DIR=<dir> writes each sound's phrase as a WAV file.
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_EXPANDER_WAV_DIR", {});
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                ExpanderProcessor p;
                p.setCurrentProgram(i);
                const auto take = render(p, phrase(), 8.5);
                bool finite = true;
                float peak = 0.0f;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                {
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                    peak = std::max({ peak, std::abs(take.left[n]), std::abs(take.right[n]) });
                }
                expect(finite, p.getProgramName(i));
                // PresetTests holds each under full scale with a phrase of its kind (a pluck arpeggiated); here
                // every sound gets the same four-voice chords and a line, plucks and leads too, so only no blowing up.
                logMessage(p.getProgramName(i) + ": peak " + juce::String(peak, 2));
                expectLessThan(peak, 1.5f, p.getProgramName(i));
                expectGreaterThan(peak, 0.01f, p.getProgramName(i));
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + p.getProgramName(i).replaceCharacters("/&", "-+")
                                                                   + ".wav"));
            }
        }

        beginTest("Eight voices with sync, PWM, delay and hall run far inside real time");
        {
            ExpanderProcessor p;
            set(p, { { "osc1Wave", 2.0f }, { "osc2Wave", 2.0f }, { "sync", 1.0f }, { "envPitch", 12.0f },
                     { "lfoPwm", 0.5f }, { "resonance", 0.6f }, { "fxDelayMix", 0.3f }, { "fxReverbMix", 0.2f },
                     { "ampRelease", 2000.0f } });
            std::vector<Note> notes;
            for (int bar = 0; bar < 4; ++bar)
                for (const int n : { 36, 48, 55, 60, 63, 67, 70, 74 })
                    notes.push_back({ bar * 5.0, bar * 5.0 + 4.0, n });
            const auto take = render(p, notes, 20.0);
            const double factor = 20.0 / take.seconds;
            logMessage("eight voices: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 10.0);
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            ExpanderProcessor p;
            set(p, { { "cutoff", 812.0f }, { "osc2Fine", -9.0f } });
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("cutoff"), juce::String("812 Hz"));
            expectEquals(text("osc2Fine"), juce::String("-9 ct"));
            expectEquals(text("osc1Width"), juce::String("50 %"));
            expectEquals(text("ampDecay"), juce::String("300 ms"));
            expectEquals(text("osc2Semitones"), juce::String("0 HT"));
            expectEquals(text("osc1Octave"), juce::String("0 Okt"));
            expectEquals(text("master"), juce::String("-6 dB"));
            expectEquals(text("mode"), juce::String("LP"));
            expectEquals(text("osc1Wave"), juce::String::fromUTF8("Säge"));
            set(p, { { "mode", 0.5f } });
            expectEquals(text("mode"), juce::String("Notch"));
            set(p, { { "mode", 0.3f } });
            expectEquals(text("mode"), juce::String("30 %"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ExpanderProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().cutoff, 812.0f, 0.5f);
            expectWithinAbsoluteError(loaded.readSettings().osc2.fine, -9.0f, 0.1f);
            expectWithinAbsoluteError(loaded.readSettings().mode, 0.3f, 0.001f);
        }

        beginTest("A patch becomes the hardware module's knob positions as clock hours");
        {
            using namespace hardware;
            expectEquals(juce::String(clockOf(0.0f)), juce::String("7:00"));
            expectEquals(juce::String(clockOf(0.5f)), juce::String("12:00"));
            expectEquals(juce::String(clockOf(1.0f)), juce::String("5:00"));
            expectEquals(juce::String(clockOf(0.25f)), juce::String("9:30"));

            Settings s;
            s.cutoff = std::sqrt(kCutoffLow * kCutoffHigh); // halfway on a logarithmic knob
            s.osc2.wave = Wave::pulse;
            s.osc2.level = 1.0f;
            s.osc1.level = 0.0f;
            s.sync = true;
            s.envPitch = 24.0f;
            s.voiceMode = VoiceMode::mono;
            s.mode = 1.0f;
            s.envCutoff = -0.5f;
            auto panel = panelFor(s);
            expectWithinAbsoluteError(panel.cutoff.position, 0.5f, 0.001f);
            expectEquals(panel.notch.value, std::string("HP"));
            expectWithinAbsoluteError(panel.mixer[1].position, 1.0f, 0.001f);  // all pulse: fully right
            expectWithinAbsoluteError(panel.mixer[0].position, 0.5f, 0.001f);  // off in the middle
            expect(panel.sync);
            expect(panel.vcoSource[1] == VcoSource::env);
            expectWithinAbsoluteError(panel.vcoModulation[1].position, 0.25f, 0.001f); // two of four octaves to FREQ
            expectLessThan(panel.vcfModulation.position, 0.5f);                          // the envelope closes it
            expect(panel.assign == Assign::uni && panel.secondSilent);
            expect(panel.notes.empty() || std::none_of(panel.notes.begin(), panel.notes.end(), [](const std::string& n) {
                       return n.find("acht Stimmen") != std::string::npos;
                   }));

            s.osc1.wave = Wave::both;
            s.lfoPitch = 30.0f;
            s.lfoPwm = 0.5f;
            s.voiceMode = VoiceMode::poly;
            panel = panelFor(s);
            expect(panel.vcoSource[0] == VcoSource::lfo);
            expectLessThan(panel.vcoModulation[0].position, 0.5f); // vibrato wins over PWM
            expect(panel.assign == Assign::duo && ! panel.secondSilent);
            expectGreaterOrEqual((int) panel.notes.size(), 3);     // both waves, vibrato with PWM, eight voices
        }

        beginTest("Every factory sound draws its hardware sheet");
        {
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            ExpanderProcessor p;
            for (int i = 0; i < p.getNumPrograms(); ++i)
            {
                p.setCurrentProgram(i);
                const auto image = drawHardwareSheet(p.readSettings(), p.getProgramName(i));
                expectEquals(image.getWidth(), 1800);
                // The knobs' pointers are white on black: something was drawn on the panel.
                expect(image.getPixelAt(905, 250) != image.getPixelAt(5, 5));
                if (dir.isNotEmpty() && (i == 4 || juce::SystemStats::getEnvironmentVariable("TONWERK_HARDWARE_ALL", {}).isNotEmpty()))
                {
                    const auto fileName = i == 4 ? juce::String("expander-hardware")
                                             : "expander-hardware-" + juce::File::createLegalFileName(p.getProgramName(i));
                    auto file = juce::File(dir).getChildFile(fileName + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream(file);
                    juce::PNGImageFormat().writeImageToStream(image, stream);
                }
            }
        }

        beginTest("The editor shows the scope, the filter's curve and the voices");
        {
            ExpanderProcessor p;
            p.setCurrentProgram(7);
            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            expect(dynamic_cast<ExpanderEditor*>(editor.get()) != nullptr);
            render(p, phrase(), 0.4);
            expectGreaterThan(editor->getWidth(), 700);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("expander.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static ExpanderTests expanderTests;
