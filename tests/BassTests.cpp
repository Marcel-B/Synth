#include "BassEditor.h"
#include "BassEngine.h"
#include "BassPresets.h"
#include "BassProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkbass;

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

/** The engine alone, mono, at `kRate`: no master, no effects. */
Signal play(const Settings& s, const std::vector<Note>& notes, double length)
{
    Engine engine;
    engine.prepare(kRate);
    const auto total = (std::size_t) (length * kRate);
    Signal out(total, 0.0f);
    // Events in time order, an end before a start on the same sample.
    std::vector<std::tuple<std::size_t, int, int>> events;
    for (const auto& n : notes)
    {
        events.emplace_back((std::size_t) (n.on * kRate), 1, n.note * 128 + n.velocity);
        events.emplace_back((std::size_t) (n.off * kRate), 0, n.note * 128);
    }
    std::sort(events.begin(), events.end());
    std::size_t at = 0;
    for (const auto& [when, kind, code] : events)
    {
        const auto until = std::min(when, total);
        if (until > at)
            engine.render(out.data() + at, (int) (until - at), s, {});
        at = std::max(at, until);
        if (kind == 1)
            engine.noteOn(code / 128, (float) (code % 128) / 127.0f, s);
        else
            engine.noteOff(code / 128);
    }
    if (total > at)
        engine.render(out.data() + at, (int) (total - at), s, {});
    return out;
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

/** A sound whose pitch the zero crossings can read: the filter low and without resonance. */
Settings plain()
{
    Settings s;
    s.cutoff = 150.0f;
    s.resonance = 0.0f;
    s.envMod = 0.0f;
    return s;
}

void set(BassProcessor& processor, const Knobs& knobs)
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

Take render(BassProcessor& processor, const std::vector<Note>& notes, double length)
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

/** An acid line: sixteenths at 125 BPM, accents and slides as on a 303 pattern. */
std::vector<Note> acidLine(int root)
{
    const double step = 60.0 / 125.0 / 4.0;
    const std::array<int, 16> pitches { 0, 0, 12, 0, 3, 0, 15, 0, 0, 7, 0, 12, 10, 0, 3, 5 };
    const std::array<bool, 16> accents { true, false, false, true, false, false, true, false,
                                         true, false, false, true, false, true, false, false };
    const std::array<bool, 16> slides { false, true, false, false, false, false, false, false,
                                        false, true, false, false, true, false, false, false };
    std::vector<Note> notes;
    for (int bar = 0; bar < 2; ++bar)
        for (std::size_t i = 0; i < 16; ++i)
        {
            const double on = (bar * 16 + (int) i) * step;
            // A slide holds the note into the next one.
            const double off = on + (slides[i] ? step * 1.1 : step * 0.5);
            notes.push_back({ on, off, root + pitches[i], accents[i] ? 127 : 100 });
        }
    return notes;
}
} // namespace

class BassTests : public juce::UnitTest
{
public:
    BassTests() : juce::UnitTest("Tonwerk Bass", "Bass") {}

    void runTest() override
    {
        beginTest("A key plays its pitch; tune and fine move it");
        {
            auto s = plain();
            const auto low = play(s, { { 0.0, 1.0, 36 } }, 1.0);
            const double c1 = frequencyOf(low, 0.2, 0.9);
            logMessage("C1: " + juce::String(c1, 2) + " Hz");
            expectWithinAbsoluteError(c1, 65.406, 0.2);
            s.tune = 7;
            s.fine = 50.0f;
            const double up = frequencyOf(play(s, { { 0.0, 1.0, 36 } }, 1.0), 0.2, 0.9);
            expectWithinAbsoluteError(12.0 * std::log2(up / c1), 7.5, 0.05);
        }

        beginTest("Slide: a key pressed while another is held glides there in the slide time, without a new attack");
        {
            auto s = plain();
            s.ampDecay = 10.0f;
            const auto tied = play(s, { { 0.0, 0.55, 36 }, { 0.5, 1.2, 48 } }, 1.2);
            const double during = frequencyOf(tied, 0.505, 0.53);
            const double after = frequencyOf(tied, 0.65, 1.1);
            logMessage("slide: 25 ms in " + juce::String(during, 1) + " Hz, after " + juce::String(after, 1) + " Hz");
            expectGreaterThan(during, 66.0);
            expectLessThan(during, 125.0);
            expectWithinAbsoluteError(after, 130.81, 0.5);
            // The volume goes on falling as it was: no new attack in the tied note.
            expectLessThan(rms(tied, 0.5, 0.6), rms(tied, 0.0, 0.1) * 1.05f);

            const auto apart = play(s, { { 0.0, 0.45, 36 }, { 0.5, 1.2, 48 } }, 1.2);
            expectWithinAbsoluteError(frequencyOf(apart, 0.51, 0.6), 130.81, 1.5);

            // Letting go of the second key slides back to the first, still held.
            const auto back = play(s, { { 0.0, 1.5, 36 }, { 0.3, 0.8, 48 } }, 1.5);
            expectWithinAbsoluteError(frequencyOf(back, 1.0, 1.4), 65.406, 0.3);
        }

        beginTest("Accent: louder and brighter, its sweep shorter");
        {
            Settings s;
            s.cutoff = 300.0f;
            const auto normal = play(s, { { 0.0, 0.4, 36, 100 } }, 0.5);
            const auto hit = play(s, { { 0.0, 0.4, 36, 127 } }, 0.5);
            const float louder = juce::Decibels::gainToDecibels(rms(hit, 0.0, 0.1) / rms(normal, 0.0, 0.1));
            logMessage("accent: " + juce::String(louder, 1) + " dB louder, brightness "
                       + juce::String(brightness(normal, 0.0, 0.05), 3) + " -> "
                       + juce::String(brightness(hit, 0.0, 0.05), 3));
            expectGreaterThan(louder, 2.0f);
            expectGreaterThan(brightness(hit, 0.0, 0.05), brightness(normal, 0.0, 0.05) * 1.2f);
            // The accented sweep is over by 300 ms; the plain one (600 ms) is still open.
            expectLessThan(brightness(hit, 0.3, 0.35) / brightness(hit, 0.0, 0.05),
                           brightness(normal, 0.3, 0.35) / brightness(normal, 0.0, 0.05));

            // Threshold: 127 counts, 100 does not, unless the threshold says so.
            s.accentThreshold = 90;
            const auto lowered = play(s, { { 0.0, 0.4, 36, 100 } }, 0.5);
            expectWithinAbsoluteError(rms(lowered, 0.0, 0.1), rms(hit, 0.0, 0.1), rms(hit, 0.0, 0.1) * 0.1f);
        }

        beginTest("Accent sweep: accents in a row climb higher with more sweep");
        {
            auto run = [](float sweep) {
                Settings s;
                s.cutoff = 200.0f;
                s.envMod = 0.0f;
                s.accent = 1.0f;
                s.sweep = sweep;
                std::vector<Note> notes;
                for (int i = 0; i < 6; ++i)
                    notes.push_back({ i * 0.12, i * 0.12 + 0.06, 36, 127 });
                const auto out = play(s, notes, 0.8);
                return brightness(out, 0.6, 0.64) / brightness(out, 0.0, 0.04);
            };
            const float none = run(0.0f), full = run(1.0f);
            logMessage("sixth accent against the first: sweep 0 " + juce::String(none, 2) + ", sweep 100 % "
                       + juce::String(full, 2));
            expectGreaterThan(full, none * 1.15f);
        }

        beginTest("The filter envelope opens and closes over the decay; resonance stays bounded");
        {
            Settings s;
            s.cutoff = 200.0f;
            s.envMod = 0.8f;
            s.decay = 0.3f;
            const auto out = play(s, { { 0.0, 1.0, 36 } }, 1.0);
            const float early = brightness(out, 0.0, 0.03), late = brightness(out, 0.8, 0.9);
            logMessage("brightness at the start " + juce::String(early, 3) + ", after the decay " + juce::String(late, 3));
            expectGreaterThan(early, late * 2.0f);

            s.resonance = 1.0f;
            s.drive = 1.0f;
            s.sub = 1.0f;
            float peak = 0.0f;
            bool finite = true;
            for (const auto v : play(s, acidLine(36), 4.0))
            {
                finite = finite && std::isfinite(v);
                peak = std::max(peak, std::abs(v));
            }
            logMessage("full resonance, drive and sub: peak " + juce::String(peak, 2));
            expect(finite);
            expectLessThan(peak, 2.0f);
        }

        beginTest("Released, the note ends within its release");
        {
            auto s = plain();
            s.release = 0.02f;
            const auto out = play(s, { { 0.0, 0.3, 36 } }, 0.6);
            expectLessThan(rms(out, 0.42, 0.6), rms(out, 0.1, 0.3) * 0.001f);
        }

        beginTest("Every factory sound plays an acid line, finite and below full scale");
        {
            // For listening: TONWERK_BASS_WAV_DIR=<dir> writes each sound's acid line as a WAV file.
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_BASS_WAV_DIR", {});
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                BassProcessor p;
                p.setCurrentProgram(i);
                const auto take = render(p, acidLine(36), 4.5);
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

        beginTest("An acid line with delay and hall runs far inside real time");
        {
            BassProcessor p;
            set(p, { { "drive", 0.5f }, { "sub", 0.5f }, { "lfoAmount", 0.5f }, { "fxDelayMix", 0.3f },
                     { "fxReverbMix", 0.2f } });
            std::vector<Note> notes;
            for (int bar = 0; bar < 5; ++bar)
                for (auto n : acidLine(36))
                    notes.push_back({ n.on + bar * 3.84, n.off + bar * 3.84, n.note, n.velocity });
            const auto take = render(p, notes, 20.0);
            const double factor = 20.0 / take.seconds;
            logMessage("acid line: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 10.0);
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            BassProcessor p;
            set(p, { { "cutoff", 812.0f }, { "accentThreshold", 96 } });
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("cutoff"), juce::String("812 Hz"));
            expectEquals(text("decay"), juce::String("600 ms"));
            expectEquals(text("resonance"), juce::String("60 %"));
            expectEquals(text("tune"), juce::String("0 HT"));
            expectEquals(text("master"), juce::String("-3 dB"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            BassProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().cutoff, 812.0f, 0.5f);
            expectEquals(loaded.readSettings().accentThreshold, 96);
            expectEquals(BassProcessor::noteName(36), juce::String("C1"));
        }

        beginTest("The editor shows the scope, the note and its accent");
        {
            BassProcessor p;
            p.setCurrentProgram(1);
            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            expect(dynamic_cast<BassEditor*>(editor.get()) != nullptr);
            render(p, acidLine(36), 0.4);
            expectGreaterThan(editor->getWidth(), 700);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("bass.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static BassTests bassTests;
