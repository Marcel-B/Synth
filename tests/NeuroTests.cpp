#include "Platform.h"
#include "NeuroEditor.h"
#include "NeuroEngine.h"
#include "NeuroPresets.h"
#include "NeuroProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkneuro;

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
    int velocity = 100;
};

struct Stereo
{
    Signal left, right;
};

/** The engine alone, stereo, at `kRate`: no master, no effects; `transport` at 120 BPM, stopped. */
Stereo play(const Settings& s, const std::vector<Note>& notes, double length, Transport transport = {})
{
    Engine engine;
    engine.prepare(kRate);
    const auto total = (std::size_t) (length * kRate);
    Stereo out { Signal(total, 0.0f), Signal(total, 0.0f) };
    std::vector<std::tuple<std::size_t, int, int>> events;
    for (const auto& n : notes)
    {
        events.emplace_back((std::size_t) (n.on * kRate), 1, n.note);
        events.emplace_back((std::size_t) (n.off * kRate), 0, n.note);
    }
    std::sort(events.begin(), events.end());
    std::size_t at = 0;
    auto run = [&](std::size_t until) {
        while (at < until)
        {
            const int count = (int) std::min<std::size_t>(64, until - at);
            engine.render(out.left.data() + at, out.right.data() + at, count, s, transport);
            at += (std::size_t) count;
        }
    };
    for (const auto& [when, kind, note] : events)
    {
        run(std::min(when, total));
        if (kind == 1)
            engine.noteOn(note, s);
        else
            engine.noteOff(note);
    }
    run(total);
    return out;
}

/** The cutoff the filter stands at, every 64 samples, for `length` seconds of one held note. */
std::vector<float> cutoffTrack(const Settings& s, double length, Transport transport = {})
{
    Engine engine;
    engine.prepare(kRate);
    engine.noteOn(36, s);
    std::array<float, 64> left {}, right {};
    std::vector<float> track;
    for (int n = 0; n < (int) (length * kRate) / 64; ++n)
    {
        engine.render(left.data(), right.data(), 64, s, transport);
        track.push_back(engine.cutoffNow());
    }
    return track;
}

float rms(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate), b = (std::size_t) (to * kRate);
    double sum = 0.0;
    for (std::size_t n = a; n < b; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, b - a));
}

/** How bright a stretch sounds: the RMS of its slope against its own RMS (grows with the cutoff). */
float brightness(const Signal& signal, double from, double to)
{
    const auto a = (std::size_t) (from * kRate) + 1, b = (std::size_t) (to * kRate);
    double slope = 0.0, level = 0.0;
    for (std::size_t n = a; n < b; ++n)
    {
        const double d = signal[n] - signal[n - 1];
        slope += d * d;
        level += (double) signal[n] * signal[n];
    }
    return (float) std::sqrt(slope / std::max(1.0e-12, level));
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

void set(NeuroProcessor& processor, const Knobs& knobs)
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

Take render(NeuroProcessor& processor, const std::vector<Note>& notes, double length)
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
                midi.addEvent(juce::MidiMessage::noteOn(1, n.note, (juce::uint8) n.velocity), (int) (on - at));
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

/** Only the FM carrier, its index at 0: a sine whose pitch the zero crossings read. */
Settings sine()
{
    Settings s;
    s.reese = 0.0f;
    s.sub = 0.0f;
    s.fm = 1.0f;
    s.fmIndex = 0.0f;
    s.drive = 0.0f;
    s.cutoff = 18000.0f;
    s.resonance = 0.0f;
    s.envCutoff = 0.0f;
    return s;
}

/** A dubstep bar at 140 BPM: eighths and a held note, on C1 and its neighbours. */
std::vector<Note> riff(int root)
{
    const double eighth = 60.0 / 140.0 / 2.0;
    const std::array<int, 8> pitches { 0, 0, 3, 0, 7, 5, 3, 0 };
    std::vector<Note> notes;
    for (int bar = 0; bar < 2; ++bar)
        for (std::size_t i = 0; i < 8; ++i)
        {
            const double on = (bar * 8 + (int) i) * eighth;
            notes.push_back({ on, on + (i == 7 ? eighth * 0.95 : eighth * 0.8), root + pitches[i] });
        }
    return notes;
}
} // namespace

class NeuroTests : public juce::UnitTest
{
public:
    NeuroTests() : juce::UnitTest("Tonwerk Neuro", "Neuro") {}

    void runTest() override
    {
        beginTest("A key plays its pitch; tune moves it, the sub sits an octave down");
        {
            auto s = sine();
            const auto low = play(s, { { 0.0, 1.0, 36 } }, 1.0);
            const double c1 = frequencyOf(low.left, 0.2, 0.9);
            logMessage("C1: " + juce::String(c1, 2) + " Hz");
            expectWithinAbsoluteError(c1, 65.406, 0.2);
            s.tune = 7;
            const double up = frequencyOf(play(s, { { 0.0, 1.0, 36 } }, 1.0).left, 0.2, 0.9);
            expectWithinAbsoluteError(12.0 * std::log2(up / c1), 7.0, 0.05);

            s = sine();
            s.fm = 0.0f;
            s.sub = 1.0f;
            const double sub = frequencyOf(play(s, { { 0.0, 1.0, 36 } }, 1.0).left, 0.2, 0.9);
            expectWithinAbsoluteError(sub, 65.406 / 2.0, 0.2);
            s.subOctave = 0;
            expectWithinAbsoluteError(frequencyOf(play(s, { { 0.0, 1.0, 36 } }, 1.0).left, 0.2, 0.9), 65.406, 0.2);
        }

        beginTest("Reese: the detuned saws beat, and width moves them apart in stereo");
        {
            Settings s;
            s.sub = 0.0f;
            s.drive = 0.0f;
            s.envCutoff = 0.0f;
            s.cutoff = 300.0f;
            s.resonance = 0.0f;
            // How much the level swings over 40 ms windows: the beating of the two saws.
            auto swing = [](const Signal& x) {
                float low = 1.0e9f, high = 0.0f;
                for (double t = 0.2; t + 0.04 < 1.9; t += 0.02)
                {
                    const float level = rms(x, t, t + 0.04);
                    low = std::min(low, level);
                    high = std::max(high, level);
                }
                return juce::Decibels::gainToDecibels(high / low);
            };
            s.detune = 0.0f;
            const float none = swing(play(s, { { 0.0, 2.0, 36 } }, 2.0).left);
            s.detune = 30.0f;
            const auto beating = play(s, { { 0.0, 2.0, 36 } }, 2.0);
            const float beat = swing(beating.left);
            logMessage("level swing: unison " + juce::String(none, 1) + " dB, 30 ct " + juce::String(beat, 1) + " dB");
            expectLessThan(none, 1.0f);
            expectGreaterThan(beat, 4.0f);

            auto difference = [](const Stereo& x) {
                double d = 0.0, sum = 0.0;
                for (std::size_t n = 0; n < x.left.size(); ++n)
                {
                    d += std::pow(x.left[n] - x.right[n], 2.0);
                    sum += std::pow(x.left[n] + x.right[n], 2.0);
                }
                return std::sqrt(d / std::max(1.0e-12, sum));
            };
            s.width = 0.0f;
            const double mono = difference(play(s, { { 0.0, 1.0, 36 } }, 1.0));
            s.width = 1.0f;
            const double wide = difference(play(s, { { 0.0, 1.0, 36 } }, 1.0));
            logMessage("side against middle: width 0 " + juce::String(mono, 3) + ", width 100 % " + juce::String(wide, 3));
            expectLessThan(mono, 0.001);
            expectGreaterThan(wide, 0.3);
        }

        beginTest("FM: the index and the mod envelope brighten the sine");
        {
            auto s = sine();
            const float plainSine = brightness(play(s, { { 0.0, 1.0, 36 } }, 1.0).left, 0.2, 0.9);
            s.fmIndex = 0.5f;
            s.ratio = 4; // 2
            const float grit = brightness(play(s, { { 0.0, 1.0, 36 } }, 1.0).left, 0.2, 0.9);
            logMessage("brightness: sine " + juce::String(plainSine, 4) + ", index 50 % " + juce::String(grit, 4));
            expectGreaterThan(grit, plainSine * 4.0f);

            s.fmIndex = 0.0f;
            s.envFm = 1.0f;
            s.mod = { 0.001f, 0.15f, 0.0f, 0.1f };
            const auto bite = play(s, { { 0.0, 1.0, 36 } }, 1.0).left;
            expectGreaterThan(brightness(bite, 0.0, 0.05), brightness(bite, 0.6, 0.9) * 3.0f);
        }

        beginTest("The LFO in tempo: an eighth at 140 BPM wobbles the cutoff every 214 ms, from closed with each note");
        {
            Settings s;
            s.envCutoff = 0.0f;
            s.cutoff = 150.0f;
            s.lfoCutoff = 0.8f;
            s.lfoDivision = 9; // 1/8
            Transport transport;
            transport.bpm = 140.0;
            const auto track = cutoffTrack(s, 2.0, transport);
            // Period from the rising passes through the middle of its range.
            const float middle = 150.0f * std::exp2(0.5f * 5.0f * 0.8f);
            std::vector<double> rises;
            for (std::size_t n = 1; n < track.size(); ++n)
                if (track[n - 1] < middle && track[n] >= middle)
                    rises.push_back((double) n * 64.0 / kRate);
            expectGreaterThan((int) rises.size(), 5);
            const double period = (rises.back() - rises.front()) / (double) (rises.size() - 1);
            logMessage("wobble period " + juce::String(period * 1000.0, 1) + " ms");
            expectWithinAbsoluteError(period, 60.0 / 140.0 / 2.0, 0.002);
            // Retriggered: the note starts at the LFO's bottom, the cutoff knob itself.
            expectWithinAbsoluteError(track.front(), 150.0f, 15.0f);

            // Free running at 3 Hz.
            s.lfoSync = false;
            s.lfoRate = 3.0f;
            const auto free = cutoffTrack(s, 2.0);
            rises.clear();
            for (std::size_t n = 1; n < free.size(); ++n)
                if (free[n - 1] < middle && free[n] >= middle)
                    rises.push_back((double) n * 64.0 / kRate);
            expectWithinAbsoluteError((rises.back() - rises.front()) / (double) (rises.size() - 1), 1.0 / 3.0, 0.003);
        }

        beginTest("Glide and legato: a key pressed while another is held slides there");
        {
            auto s = sine();
            s.glide = 0.1f;
            const auto tied = play(s, { { 0.0, 0.55, 36 }, { 0.5, 1.2, 48 } }, 1.2).left;
            const double during = frequencyOf(tied, 0.51, 0.55);
            logMessage("glide: 10 to 50 ms in " + juce::String(during, 1) + " Hz");
            expectGreaterThan(during, 70.0);
            expectLessThan(during, 125.0);
            expectWithinAbsoluteError(frequencyOf(tied, 0.9, 1.15), 130.81, 0.5);
            // Back to the key still held when the newer one goes up.
            const auto back = play(s, { { 0.0, 1.5, 36 }, { 0.3, 0.8, 48 } }, 1.5).left;
            expectWithinAbsoluteError(frequencyOf(back, 1.2, 1.45), 65.406, 0.3);
        }

        beginTest("Every filter and drive at full resonance stays bounded; released, the note ends");
        {
            for (int type = 0; type < (int) kFilterNames.size(); ++type)
                for (int mode = 0; mode < (int) kDriveNames.size(); ++mode)
                {
                    Settings s;
                    s.filterType = (FilterType) type;
                    s.driveMode = (DistortionMode) mode;
                    s.drive = 1.0f;
                    s.resonance = 1.0f;
                    s.fm = 1.0f;
                    s.fmIndex = 1.0f;
                    s.feedback = 1.0f;
                    s.sub = 1.0f;
                    s.lfoCutoff = 1.0f;
                    s.lfoFm = 1.0f;
                    float peak = 0.0f;
                    bool finite = true;
                    const auto out = play(s, riff(36), 4.2);
                    for (std::size_t n = 0; n < out.left.size(); ++n)
                    {
                        finite = finite && std::isfinite(out.left[n]) && std::isfinite(out.right[n]);
                        peak = std::max({ peak, std::abs(out.left[n]), std::abs(out.right[n]) });
                    }
                    const auto label = juce::String(kFilterNames[(std::size_t) type]) + " / " + juce::String(mode);
                    expect(finite, label);
                    expectLessThan(peak, 5.0f, label);
                    // The riff ends at 3.42 s; the release is 120 ms.
                    expectLessThan(rms(out.left, 3.8, 4.2), 0.001f, label);
                }
        }

        beginTest("Every factory sound plays the riff, finite and below full scale");
        {
            // For listening: TONWERK_NEURO_WAV_DIR=<dir> writes each sound's riff as a WAV file.
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_NEURO_WAV_DIR", {});
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                NeuroProcessor p;
                p.setCurrentProgram(i);
                const auto take = render(p, riff(36), 4.0);
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
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " "
                                                                   + p.getProgramName(i).replaceCharacters("/&", "-+")
                                                                   + ".wav"));
            }
        }

        beginTest("A riff with FM, wobble, delay and hall runs far inside real time");
        {
            NeuroProcessor p;
            set(p, { { "fm", 0.6f }, { "fmIndex", 0.5f }, { "feedback", 0.3f }, { "lfoCutoff", 0.5f },
                     { "lfoFm", 0.3f }, { "filterType", 1.0f }, { "fxDelayMix", 0.3f }, { "fxReverbMix", 0.2f } });
            std::vector<Note> notes;
            for (int bar = 0; bar < 6; ++bar)
                for (auto n : riff(36))
                    notes.push_back({ n.on + bar * 3.43, n.off + bar * 3.43, n.note, n.velocity });
            const auto take = render(p, notes, 20.0);
            const double factor = 20.0 / take.seconds;
            logMessage("riff: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 10.0);
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            NeuroProcessor p;
            set(p, { { "cutoff", 812.0f }, { "detune", 25.0f } });
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("cutoff"), juce::String("812 Hz"));
            expectEquals(text("detune"), juce::String("25 ct"));
            expectEquals(text("reese"), juce::String("80 %"));
            expectEquals(text("ampDecay"), juce::String("300 ms"));
            expectEquals(text("tune"), juce::String("0 HT"));
            expectEquals(text("master"), juce::String("-6 dB"));
            expectEquals(text("ratio"), juce::String("1"));
            expectEquals(text("driveMode"), juce::String("Weich"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            NeuroProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().cutoff, 812.0f, 0.5f);
            expectWithinAbsoluteError(loaded.readSettings().detune, 25.0f, 0.1f);
            expectEquals(NeuroProcessor::noteName(36), juce::String("C1"));
        }

        beginTest("The editor shows the scope, the note and the cutoff");
        {
            NeuroProcessor p;
            p.setCurrentProgram(6);
            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            expect(tonwerkui::innerEditor<NeuroEditor>(editor.get()) != nullptr);
            render(p, riff(36), 0.4);
            expectGreaterThan(editor->getWidth(), 700);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("neuro.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static NeuroTests neuroTests;
