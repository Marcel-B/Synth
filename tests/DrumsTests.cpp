#include "DrumsEditor.h"
#include "DrumsEngine.h"
#include "DrumsPresets.h"
#include "DrumsProcessor.h"
#include "GroovePresets.h"
#include "GrooveProcessor.h"
#include "PresetCategories.h"
#include "Loudness.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkdrums;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

std::size_t at(double seconds) { return (std::size_t) (seconds * kRate); }

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

float peak(const std::vector<float>& signal)
{
    float p = 0.0f;
    for (const float x : signal)
        p = std::max(p, std::abs(x));
    return p;
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

/** The amplitude of the sine at `hz` in a stretch of the signal (a single-bin DFT, Hann windowed). */
float amplitudeAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    double re = 0.0, im = 0.0, window = 0.0;
    const double n = (double) (to - from);
    for (std::size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(6.283185307179586 * (double) (i - from) / n);
        const double phase = 6.283185307179586 * hz * (double) i / kRate;
        re += w * signal[i] * std::cos(phase);
        im += w * signal[i] * std::sin(phase);
        window += w;
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / window);
}

/** The strongest frequency between `low` and `high`, searched in `step` Hz. */
double strongest(const std::vector<float>& signal, double low, double high, double step, std::size_t from,
                 std::size_t to)
{
    double best = low;
    float most = -1.0f;
    for (double hz = low; hz <= high; hz += step)
        if (const float a = amplitudeAt(signal, hz, from, to); a > most)
        {
            most = a;
            best = hz;
        }
    return best;
}

/** One pad hit once on the engine, rendered alone, dry and in the middle. */
template <typename Setup>
std::vector<float> hit(Pad pad, Setup&& setup, double length, float velocity = 1.0f)
{
    Engine engine;
    engine.prepare(kRate);
    Settings s;
    auto& t = s.pad[(std::size_t) pad];
    t.pan = 0.0f;
    setup(t);
    std::vector<float> left(at(length)), right(left.size()), delay(left.size()), reverb(left.size());
    engine.trigger((int) pad, velocity, s);
    engine.render(left.data(), right.data(), delay.data(), reverb.data(), (int) left.size(), s);
    return left;
}

std::vector<float> hit(Pad pad, double length = 1.0) { return hit(pad, [](TrackSettings&) {}, length); }

/** The loudness of a hit as an ear takes it: the loudest 20 ms of its RMS, in dB. */
float loudness(const std::vector<float>& signal)
{
    float most = 0.0f;
    for (std::size_t from = 0; from + at(0.02) <= signal.size(); from += at(0.005))
        most = std::max(most, rms(signal, from, from + at(0.02)));
    return decibels(most);
}

template <typename Processor>
void set(Processor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

/** Runs a processor for `length` seconds with MIDI notes (seconds, note, velocity) on channel 10. */
template <typename Processor>
Rendered run(Processor& processor, double length, std::vector<std::tuple<double, int, int>> notes = {},
             bool waitForRoom = false)
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(2, kBlock);
    if (waitForRoom)
    {
        // The hall builds its room on a background thread; a kit measures the same in every run once it plays.
        juce::MidiBuffer none;
        for (int waited = 0; ! processor.roomPlaying() && waited < 5000; ++waited)
        {
            processor.processBlock(buffer, none);
            juce::Thread::sleep(1);
        }
        for (int block = 0; block < (int) (0.1 * kRate) / kBlock; ++block)
            processor.processBlock(buffer, none);
    }
    const auto total = at(length);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t from = 0; from < total; from += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, total - from);
        juce::MidiBuffer midi;
        for (const auto& [time, note, velocity] : notes)
            if (const auto on = at(time); on >= from && on < from + (std::size_t) count)
                midi.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8) velocity), (int) (on - from));
        buffer.setSize(2, count, false, false, true);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + from);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + from);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

/** No delay or hall, every pad in the middle. */
void plain(DrumsProcessor& processor)
{
    set(processor, "fxReverbMix", 0.0f);
    set(processor, "fxDelayMix", 0.0f);
    for (const auto* pad : kPadIds)
        set(processor, juce::String(pad) + "Pan", 0.0f);
}

bool finite(const Rendered& r)
{
    for (std::size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite(r.left[i]) || ! std::isfinite(r.right[i]))
            return false;
    return true;
}

/**
 * The beat the kits are leveled with: two bars at 120 BPM of a drum part as a General MIDI file carries it, kick,
 * snare, hats, a clap, a crash at the top and a tom fill at the end.
 */
std::vector<std::tuple<double, int, int>> generalMidiBeat()
{
    std::vector<std::tuple<double, int, int>> notes;
    const double sixteenth = 0.125;
    for (int bar = 0; bar < 2; ++bar)
    {
        const double start = bar * 2.0;
        auto note = [&](int step, int pitch, int velocity) {
            notes.emplace_back(start + step * sixteenth, pitch, velocity);
        };
        for (const int step : { 0, 6, 8, 10 })
            note(step, 36, 110);
        for (const int step : { 4, 12 })
            note(step, 38, 105);
        for (int step = 0; step < 16; step += 2)
            note(step, step == 14 ? 46 : 42, step % 4 == 0 ? 95 : 75);
        if (bar == 0)
            note(0, 49, 100);
        else
        {
            note(12, 39, 100);
            note(13, 48, 100);
            note(14, 45, 100);
            note(15, 41, 100);
        }
    }
    return notes;
}

void writeWav(const Rendered& take, const juce::File& file)
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

class DrumsTests : public juce::UnitTest
{
public:
    DrumsTests() : juce::UnitTest("Tonwerk Drums", "Drums") {}

    /** The kits' loudness with the General MIDI beat, as the Groovebox's kits: -16 LUFS momentary at most, ±2 dB. */
    static constexpr double kTarget = -16.0;
    static constexpr double kTolerance = 2.0;

    void runTest() override
    {
        beginTest("Pads sit on General MIDI's drum notes, its neighbouring sounds fall to them");
        {
            const Settings s;
            for (int p = 0; p < kPads; ++p)
                expectEquals(padForNote(kNotes[(std::size_t) p], s.note), p, kPadNames[(std::size_t) p]);
            expectEquals(padForNote(35, s.note), (int) Pad::kick);
            expectEquals(padForNote(40, s.note), (int) Pad::snare);
            expectEquals(padForNote(43, s.note), (int) Pad::tomLow);
            expectEquals(padForNote(47, s.note), (int) Pad::tomMid);
            expectEquals(padForNote(50, s.note), (int) Pad::tomHigh);
            expectEquals(padForNote(57, s.note), (int) Pad::cymbal);
            expectEquals(padForNote(59, s.note), (int) Pad::ride);
            expectEquals(padForNote(62, s.note), (int) Pad::conga);
            expectEquals(padForNote(60 + 12, s.note), -1);
            // The editor's grid holds every pad once.
            std::set<Pad> shown(kGrid.begin(), kGrid.end());
            expectEquals((int) shown.size(), kPads);

            // A pad moved to another note plays there, and that note no longer falls back to General MIDI.
            DrumsProcessor processor;
            plain(processor);
            set(processor, "rimNote", 40.0f);
            run(processor, 1.0,
                { { 0.0, 40, 100 }, { 0.1, 37, 100 }, { 0.2, 35, 100 }, { 0.3, 47, 100 }, { 0.4, 84, 100 } });
            expectEquals(processor.hitCount((int) Pad::rim), 1);
            expectEquals(processor.hitCount((int) Pad::snare), 0);
            expectEquals(processor.hitCount((int) Pad::kick), 1);
            expectEquals(processor.hitCount((int) Pad::tomMid), 1);
        }

        beginTest("Every note of the map plays its pad once, notes off end nothing");
        {
            DrumsProcessor processor;
            plain(processor);
            std::vector<std::tuple<double, int, int>> notes;
            for (int place = 0; place < kPads; ++place)
                notes.emplace_back(0.4 * place, kNotes[(std::size_t) kGrid[(std::size_t) place]], 110);
            const auto out = run(processor, 0.4 * kPads + 1.0, notes);
            for (int p = 0; p < kPads; ++p)
                expectEquals(processor.hitCount(p), 1, kPadNames[(std::size_t) p]);
            expect(finite(out));
            expectLessThan(std::max(peak(out.left), peak(out.right)), 1.0f);
            // Every pad once in the grid's order, to hear them.
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {}); dir.isNotEmpty())
                writeWav(out, juce::File(dir).getChildFile("Tonwerk Drums").getChildFile("00 Pads.wav"));
        }

        beginTest("The Groovebox's eight pads sound exactly like its tracks");
        {
            tonwerkgroove::GrooveProcessor groovebox;
            for (int p = 0; p < tonwerkgroove::kPatterns; ++p)
                groovebox.setPattern(p, {});
            set(groovebox, "seqMode", 0.0f);
            DrumsProcessor drums;
            for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>> {
                     { "snareTune", 3.0f }, { "kickMachine", 1.0f }, { "cymbalMachine", 5.0f }, { "clapDrive", 0.4f },
                     { "hatClosedBits", 8.0f }, { "fxReverbMix", 0.3f } })
            {
                set(groovebox, id, value);
                set(drums, id, value);
            }
            std::vector<std::tuple<double, int, int>> notes;
            for (int t = 0; t < tonwerkgroove::kTracks; ++t)
                notes.emplace_back(0.1 * t, tonwerkgroove::noteForTrack(t), 30 + 12 * t);
            const auto a = run(groovebox, 2.0, notes, true);
            const auto b = run(drums, 2.0, notes, true);
            // The same sound, only quieter by the room a MIDI part needs.
            const float scale = Engine::kOutput / 0.63f;
            float most = 0.0f;
            for (std::size_t i = 0; i < a.left.size(); ++i)
                most = std::max({ most, std::abs(scale * a.left[i] - b.left[i]), std::abs(scale * a.right[i] - b.right[i]) });
            expectLessThan(most, 1.0e-5f);
            expectGreaterThan(rms(b.left, 0, b.left.size()), 0.01f);
        }

        beginTest("The new pads have their own pitch: rim, middle tom, ride, cowbell, conga, claves");
        {
            // The rim's two tones, as the classic analog rim shot's.
            const auto rim = hit(Pad::rim, 0.2);
            expectWithinAbsoluteError(strongest(rim, 300.0, 700.0, 5.0, 0, at(0.04)), 455.0, 15.0);
            expectGreaterThan(amplitudeAt(rim, 455.0 * 3.66, 0, at(0.04)), 0.1f * amplitudeAt(rim, 455.0, 0, at(0.04)));
            // The middle tom between the low and the high one.
            auto pitchOf = [this](Pad pad) {
                const auto tom = hit(pad, 0.6);
                return strongest(tom, 60.0, 260.0, 1.0, at(0.15), at(0.45));
            };
            const double low = pitchOf(Pad::tomLow), mid = pitchOf(Pad::tomMid), high = pitchOf(Pad::tomHigh);
            logMessage("Toms: " + juce::String(low, 0) + ", " + juce::String(mid, 0) + ", " + juce::String(high, 0) + " Hz");
            expectGreaterThan(mid, low + 10.0);
            expectLessThan(mid, high - 10.0);
            // Claves ring high and short.
            const auto clave = hit(Pad::clave, 0.3);
            expectWithinAbsoluteError(strongest(clave, 1500.0, 3500.0, 10.0, 0, at(0.03)), 2500.0, 30.0);
            expectLessThan(rms(clave, at(0.1), at(0.2)), 0.01f * rms(clave, 0, at(0.02)));
            // The cowbell's two squares stand far above what lies between them.
            const auto cowbell = hit(Pad::cowbell, 0.3);
            const float bell = std::min(amplitudeAt(cowbell, 540.0, 0, at(0.1)), amplitudeAt(cowbell, 800.0, 0, at(0.1)));
            expectGreaterThan(bell, 4.0f * amplitudeAt(cowbell, 670.0, 0, at(0.1)));
            // The conga higher than the toms.
            const auto conga = hit(Pad::conga, 0.4);
            expectWithinAbsoluteError(strongest(conga, 200.0, 500.0, 2.0, at(0.05), at(0.2)), 330.0, 15.0);
            // The ride brighter and longer than the closed hat, the shaker only noise up high.
            const auto ride = hit(Pad::ride, 3.0);
            expectGreaterThan(rms(ride, at(1.0), at(1.5)), 0.05f * rms(ride, 0, at(0.1)));
        }

        beginTest("Every pad at its defaults is about as loud as the snare, the kick on top");
        {
            const float snare = loudness(hit(Pad::snare));
            for (int p = 0; p < kPads; ++p)
            {
                const float level = loudness(hit((Pad) p)) - snare;
                logMessage(juce::String(kPadNames[(std::size_t) p]).paddedRight(' ', 14) + juce::String(level, 1) + " dB");
                expectGreaterThan(level, -12.0f, kPadNames[(std::size_t) p]);
                // The kick comes out on top, as in any mix.
                expectLessThan(level, 10.0f, kPadNames[(std::size_t) p]);
            }
        }

        beginTest("The closed and the pedal hat choke the open one");
        {
            for (const int closer : { 42, 44 })
            {
                DrumsProcessor processor;
                plain(processor);
                const auto alone = run(processor, 0.6, { { 0.0, 46, 127 } });
                DrumsProcessor choked;
                plain(choked);
                // The closing hat itself as short as it goes, so what rings later is the open one.
                set(choked, juce::String(closer == 42 ? "hatClosed" : "hatPedal") + "Decay", 10.0f);
                const auto out = run(choked, 0.6, { { 0.0, 46, 127 }, { 0.2, closer, 127 } });
                expectLessThan(rms(out.left, at(0.3), at(0.5)), 0.05f * rms(alone.left, at(0.3), at(0.5)));
            }
        }

        beginTest("Every pad on every machine plays, bounded and finite");
        {
            for (int m = 0; m < kMachines; ++m)
                for (int p = 0; p < kPads; ++p)
                {
                    const auto out = hit((Pad) p, [m](TrackSettings& t) { t.machine = (Machine) m; }, 0.5);
                    bool fine = true;
                    for (const float x : out)
                        fine = fine && std::isfinite(x);
                    const juce::String what = juce::String(tonwerkgroove::kMachineNames[(std::size_t) m]) + " "
                                              + kPadNames[(std::size_t) p];
                    expect(fine, what);
                    expectLessThan(peak(out), 1.0f, what);
                    expectGreaterThan(rms(out, 0, at(0.05)), 1.0e-3f, what);
                }
        }

        beginTest("Velocity sets the level, mute silences the pad");
        {
            const float loud = rms(hit(Pad::conga, [](TrackSettings&) {}, 0.3, 1.0f), 0, at(0.1));
            const float soft = rms(hit(Pad::conga, [](TrackSettings&) {}, 0.3, 0.3f), 0, at(0.1));
            expectWithinAbsoluteError(decibels(soft / loud), decibels(0.3f), 0.5f);
            expectLessThan(rms(hit(Pad::conga, [](TrackSettings& t) { t.mute = true; }, 0.3), 0, at(0.3)), 1.0e-9f);
        }

        beginTest("Notes read as Logic names them, and can be typed");
        {
            DrumsProcessor processor;
            auto* note = processor.state.getParameter("kickNote");
            expectEquals(note->getText(note->convertTo0to1(36.0f), 32), juce::String("C1 (36)"));
            expectEquals(note->getText(note->convertTo0to1(61.0f), 32), juce::String("Cis3 (61)"));
            expectEquals(juce::roundToInt(note->convertFrom0to1(note->getValueForText("D1"))), 38);
            expectEquals(juce::roundToInt(note->convertFrom0to1(note->getValueForText("Fis1"))), 42);
            expectEquals(juce::roundToInt(note->convertFrom0to1(note->getValueForText("H2"))), 59);
            expectEquals(juce::roundToInt(note->convertFrom0to1(note->getValueForText("70"))), 70);
            auto* decay = processor.state.getParameter("rideDecay");
            expectEquals(decay->getText(decay->convertTo0to1(3500.0f), 32), juce::String("3500 ms"));
        }

        beginTest("The state keeps knobs and notes; a kit keeps the notes and the effects switch");
        {
            DrumsProcessor processor;
            processor.setCurrentProgram(3);
            set(processor, "cowbellTune", 5.0f);
            set(processor, "claveNote", 76.0f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            DrumsProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().pad[(std::size_t) Pad::cowbell].tune, 5.0f, 1.0e-3f);
            expectEquals(loaded.readSettings().note[(std::size_t) Pad::clave], 76);
            expectEquals(loaded.presetName(), processor.presetName());

            set(loaded, "fxOn", 0.0f);
            loaded.setCurrentProgram(10);
            expectEquals(loaded.readSettings().note[(std::size_t) Pad::clave], 76);
            expectWithinAbsoluteError(loaded.readSettings().pad[(std::size_t) Pad::cowbell].tune, 0.0f, 1.0e-3f);
            expect(loaded.state.getRawParameterValue("fxOn")->load() < 0.5f);
        }

        beginTest("Kits: Init, a kit per machine, the Groovebox's kits, every one in a group and named once");
        {
            const auto& kits = factoryKits();
            expectEquals(kits.front().name, juce::String("Init"));
            expectEquals((int) kits.size(), kMachines + (int) tonwerkgroove::factoryKits().size() - 1);
            std::set<juce::String> names;
            for (int i = 0; i < (int) kits.size(); ++i)
            {
                const auto& kit = kits[(std::size_t) i];
                expect(names.insert(kit.name).second, "twice: " + kit.name);
                expect(i == 0 || tonwerkui::categoryIndex(kit.category.toRawUTF8(), kDrumsCategories) >= 0, kit.name);
                DrumsProcessor processor;
                for (const auto& [id, value] : kit.values)
                {
                    expect(processor.state.getParameter(id) != nullptr, kit.name + ": " + id);
                    expect(! id.endsWith("Note") && id != "fxOn", kit.name + ": " + id);
                }
            }
            // A Groovebox kit's sound comes over, and the pads it lacks follow their neighbours.
            DrumsProcessor processor;
            for (int i = 0; i < (int) kits.size(); ++i)
                if (kits[(std::size_t) i].name == "Staubiger Sampler")
                    processor.setCurrentProgram(i);
            expectEquals(processor.presetName(), juce::String("Staubiger Sampler"));
            const auto s = processor.readSettings();
            expectLessThan(s.pad[(std::size_t) Pad::hatClosed].bits, 16.0f);
            expectEquals(s.pad[(std::size_t) Pad::shaker].bits, s.pad[(std::size_t) Pad::hatClosed].bits);
        }

        beginTest("Every kit is leveled with a General MIDI beat and stays under full scale");
        {
            const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});
            const auto& kits = factoryKits();
            for (int i = 0; i < (int) kits.size(); ++i)
            {
                const auto& kit = kits[(std::size_t) i];
                DrumsProcessor processor;
                processor.setCurrentProgram(i);
                const auto take = run(processor, 4.5, generalMidiBeat(), true);
                expect(finite(take), kit.name);
                const double level = tonwerktest::momentaryMax(take.left, take.right);
                const float loudest = std::max(peak(take.left), peak(take.right));
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + kit.name.paddedRight(' ', 22)
                           + kit.category.paddedRight(' ', 15) + juce::String(level, 1) + " LUFS, peak "
                           + juce::String(loudest, 2));
                expectGreaterThan(level, kTarget - kTolerance, kit.name);
                expectLessThan(level, kTarget + kTolerance, kit.name);
                expectLessThan(loudest, 1.0f, kit.name);
                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile("Tonwerk Drums").getChildFile(
                                       juce::String(i + 1).paddedLeft('0', 2) + " " + kit.name + ".wav"));
            }
        }

        beginTest("Sixteen pads on every thirty-second note with the hall stay well inside real time");
        {
            DrumsProcessor processor;
            set(processor, "fxReverbMix", 0.5f);
            set(processor, "fxDelayMix", 0.5f);
            set(processor, "drive", 0.5f);
            for (const auto* pad : kPadIds)
            {
                const juce::String p = pad;
                set(processor, p + "Machine", (float) Machine::studio80);
                set(processor, p + "PitchMod", 12.0f);
                set(processor, p + "Filter", 1.0f);
                set(processor, p + "FilterMod", 0.5f);
                set(processor, p + "Drive", 0.5f);
                set(processor, p + "Random", 0.5f);
            }
            std::vector<std::tuple<double, int, int>> notes;
            for (int step = 0; step < 64; ++step)
                for (const int note : kNotes)
                    notes.emplace_back(step * 0.0625, note, 100);
            const auto out = run(processor, 4.0, notes, true);
            const double realtime = 4.0 / out.seconds;
            logMessage("All pads on 1/32 with every shaping, delay and hall: " + juce::String(realtime, 1)
                       + " times real time");
            expect(finite(out));
            expectGreaterThan(realtime, 5.0);
        }

        beginTest("The editor fits its panels");
        {
            DrumsProcessor processor;
            run(processor, 0.3, { { 0.0, 36, 100 } });
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Drums.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static DrumsTests drumsTests;
