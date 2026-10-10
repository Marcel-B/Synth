#include "DxPresets.h"
#include "DxProcessor.h"
#include "GranularPresets.h"
#include "GranularProcessor.h"
#include "PluginProcessor.h"
#include "PresetCategories.h"
#include "PresetLibrary.h"
#include "WavetablePresets.h"
#include "WavetableProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <set>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Note
{
    double on, off;
    int note;
    int velocity = 100;
};

struct Take
{
    std::vector<float> left, right;
};

bool roomPlaying(const juce::AudioProcessor& processor)
{
    if (const auto* synth = dynamic_cast<const tonwerk::TonwerkSynthProcessor*>(&processor))
        return synth->roomPlaying();
    if (const auto* dx = dynamic_cast<const tonwerkdx::DxProcessor*>(&processor))
        return dx->roomPlaying();
    if (const auto* granular = dynamic_cast<const tonwerkgrain::GranularProcessor*>(&processor))
        return granular->roomPlaying();
    return true;
}

/** Plays `notes` for `length` seconds on a processor whose program is already chosen. */
Take render(juce::AudioProcessor& processor, const std::vector<Note>& notes, double length)
{
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    // After the program: the synths load their room for the current sound here.
    processor.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(2, kBlock);
    // The hall builds the room on a background thread; silence until it plays, and through the convolution's 50 ms
    // crossfade to it. Otherwise a sound would sound with more or less of its hall depending on the machine's speed.
    juce::MidiBuffer none;
    for (int waited = 0; ! roomPlaying(processor) && waited < 5000; ++waited)
    {
        processor.processBlock(buffer, none);
        juce::Thread::sleep(1);
    }
    for (int block = 0; block < (int) (0.1 * kRate) / kBlock; ++block)
        processor.processBlock(buffer, none);
    const auto total = (std::size_t) (length * kRate);
    Take take { std::vector<float>(total), std::vector<float>(total) };
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
    processor.releaseResources();
    return take;
}

/**
 * ITU-R BS.1770's K-weighting at 48 kHz, its published coefficients: a shelf of about +4 dB above 2 kHz and a
 * highpass near 40 Hz, so a bass counts about as loud as it sounds rather than by its energy alone.
 */
std::vector<double> kWeighted(const std::vector<float>& signal)
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double process(double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    Biquad highpass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    std::vector<double> out(signal.size());
    for (std::size_t i = 0; i < signal.size(); ++i)
        out[i] = highpass.process(shelf.process(signal[i]));
    return out;
}

/** EBU R128's momentary loudness (400 ms, every 100 ms) at its loudest, in LUFS: how loud a sound comes across. */
double loudness(const Take& take)
{
    const auto left = kWeighted(take.left);
    const auto right = kWeighted(take.right);
    const auto window = (std::size_t) (0.4 * kRate);
    const auto hop = (std::size_t) (0.1 * kRate);
    double loudest = 0.0;
    for (std::size_t at = 0; at + window <= left.size(); at += hop)
    {
        double sum = 0.0;
        for (std::size_t i = at; i < at + window; ++i)
            sum += left[i] * left[i] + right[i] * right[i];
        loudest = std::max(loudest, sum / (double) window);
    }
    return -0.691 + 10.0 * std::log10(std::max(loudest, 1.0e-12));
}

float peakOf(const Take& take)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < take.left.size(); ++i)
        peak = std::max({ peak, std::abs(take.left[i]), std::abs(take.right[i]) });
    return peak;
}

bool is(const char* category, const char* name) { return std::strcmp(category, name) == 0; }

/**
 * What a sound is played and measured with: a short phrase of its group at 120 BPM, as it is mostly played. A
 * bassline around C2, chords for pads and keys, an arpeggio for plucks, a melody for leads and bells.
 */
std::vector<Note> phrase(const char* category, double& length)
{
    std::vector<Note> notes;
    auto line = [&notes](std::initializer_list<int> pitches, double start, double step, double gate) {
        double at = start;
        for (const int pitch : pitches)
        {
            notes.push_back({ at, at + gate, pitch });
            at += step;
        }
    };
    auto chord = [&notes](std::initializer_list<int> pitches, double on, double off) {
        for (const int pitch : pitches)
            notes.push_back({ on, off, pitch });
    };
    length = 5.0;
    if (is(category, "Bässe"))
    {
        line({ 36, 36, 48, 36, 39, 41, 43, 46 }, 0.0, 0.25, 0.2);
        chord({ 36 }, 2.0, 3.5);
    }
    else if (is(category, "Leads"))
    {
        notes = { { 0.0, 0.45, 72 }, { 0.5, 0.75, 75 }, { 0.75, 1.0, 77 }, { 1.0, 1.7, 79 },
                  { 1.75, 2.0, 77 }, { 2.0, 2.25, 75 }, { 2.25, 2.5, 74 }, { 2.5, 3.75, 72 } };
    }
    else if (is(category, "Flächen"))
    {
        chord({ 48, 60, 63, 67 }, 0.0, 2.0);
        chord({ 44, 60, 63, 67 }, 2.0, 4.0);
        length = 6.5;
    }
    else if (is(category, "Tasten"))
    {
        for (const double at : { 0.0, 0.75, 1.5 })
            chord({ 48, 58, 63, 67 }, at, at + 0.4);
        chord({ 53, 60, 63, 68 }, 2.0, 3.5);
    }
    else if (is(category, "Plucks"))
    {
        line({ 60, 63, 67, 72, 67, 63, 60, 63, 67, 72, 75, 72, 67, 63, 67, 72 }, 0.0, 0.125, 0.1);
        line({ 56, 60, 63, 68, 63, 60, 56, 60, 63, 68, 72, 68, 63, 60, 63, 68 }, 2.0, 0.125, 0.1);
    }
    else if (is(category, "Glocken"))
    {
        line({ 72, 79, 75, 84 }, 0.0, 0.5, 0.4);
        chord({ 72, 79 }, 2.0, 2.5);
    }
    else if (is(category, "Bläser & Streicher"))
    {
        chord({ 60, 63, 67 }, 0.0, 1.5);
        chord({ 58, 62, 65 }, 1.5, 3.0);
    }
    else
    {
        chord({ 60 }, 0.0, 2.0);
        length = 4.0;
    }
    return notes;
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

/** One instrument's factory sounds, by program number. */
struct Instrument
{
    juce::String name;
    int count;
    std::function<std::unique_ptr<juce::AudioProcessor>()> make;
    std::function<const char*(int)> category;
    /** Sounds that keep their level: Tonwerk's own starting sounds, which match the browser, and Init. */
    std::function<bool(int)> exempt;
    /** Semitones the phrase moves, so a sound that transposes itself is heard where it is meant to sound. */
    std::function<int(int)> shift = [](int) { return 0; };
    /** The level its sounds are matched to, in LUFS (momentary, at its loudest). */
    double target = -12.0;
};

std::vector<Instrument> instruments()
{
    std::vector<Instrument> list;
    for (const auto edition : { tonwerk::Edition::analog, tonwerk::Edition::fm })
    {
        // Kept for the lambdas: the factory presets' categories are juce::Strings.
        const auto presets = std::make_shared<juce::Array<tonwerk::PresetLibrary::Preset>>(
            tonwerk::PresetLibrary::factoryPresets(edition));
        list.push_back({ tonwerk::TonwerkSynthProcessor::editionName(edition), presets->size(),
                         [edition] { return std::make_unique<tonwerk::TonwerkSynthProcessor>(edition); },
                         [presets](int i) { return presets->getReference(i).category.toRawUTF8(); },
                         [](int i) { return i < 4; } });
    }
    list.push_back({ "Tonwerk DX", (int) tonwerkdx::factoryPresets().size(),
                     [] { return std::make_unique<tonwerkdx::DxProcessor>(); },
                     [](int i) { return tonwerkdx::factoryPresets()[(std::size_t) i].category; },
                     [](int) { return false; },
                     [](int i) { return -tonwerkdx::factoryPresets()[(std::size_t) i].patch.transpose; },
                     // 4 dB lower: a voice ends at half of full scale at 100 % volume, so a chord struck hard stays
                     // under it (DxTests). A louder engine would make every saved project louder.
                     -16.0 });
    list.push_back({ "Tonwerk Wavetable", (int) tonwerkwave::factoryPresets().size(),
                     [] { return std::make_unique<tonwerkwave::WavetableProcessor>(); },
                     [](int i) { return tonwerkwave::factoryPresets()[(std::size_t) i].category; },
                     [](int i) { return i == 0; } });
    list.push_back({ "Tonwerk Granular", (int) tonwerkgrain::factoryPresets().size(),
                     [] { return std::make_unique<tonwerkgrain::GranularProcessor>(); },
                     [](int i) { return tonwerkgrain::factoryPresets()[(std::size_t) i].category; },
                     [](int i) { return i == 0; } });
    return list;
}
} // namespace

/**
 * The factory sounds of all five instruments, side by side: each in a group of the menu, every name once, and all
 * about equally loud the way they are played, so switching presets does not jump in level.
 */
class PresetTests : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest("Factory sounds", "Presets") {}

    /** How far a sound may be off its instrument's level, in dB. */
    static constexpr double kTolerance = 2.0;
    /**
     * How far below an effect may sit: often a short hit, which a 400 ms window hears as quieter than it is. Raised to
     * the level, it would peak over full scale.
     */
    static constexpr double kEffectsBelow = 6.0;

    void runTest() override
    {
        // For listening: TONWERK_PRESET_WAV_DIR=<dir> writes each sound's phrase as a WAV file.
        const auto wavDir = juce::SystemStats::getEnvironmentVariable("TONWERK_PRESET_WAV_DIR", {});
        for (const auto& instrument : instruments())
        {
            beginTest(instrument.name + ": groups, names and levels");
            std::set<juce::String> names;
            for (int i = 0; i < instrument.count; ++i)
            {
                const char* group = instrument.category(i);
                auto processor = instrument.make();
                processor->setCurrentProgram(i);
                const auto sound = processor->getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(tonwerkui::categoryIndex(group) >= 0 || instrument.exempt(i), "no group: " + sound);

                double length = 0.0;
                auto notes = phrase(group, length);
                for (auto& note : notes)
                    note.note += instrument.shift(i);
                const auto take = render(*processor, notes, length);
                const double level = loudness(take);
                const float peak = peakOf(take);
                bool finite = true;
                for (std::size_t n = 0; n < take.left.size(); ++n)
                    finite = finite && std::isfinite(take.left[n]) && std::isfinite(take.right[n]);
                logMessage(juce::String(i).paddedLeft('0', 2) + " " + sound.paddedRight(' ', 26)
                           + juce::String::fromUTF8(group).paddedRight(' ', 20) + juce::String(level, 1)
                           + " LUFS, peak " + juce::String(peak, 2) + (instrument.exempt(i) ? "  (kept)" : ""));
                expect(finite, sound);
                if (! instrument.exempt(i))
                {
                    const double below = is(group, "Effekte") ? kEffectsBelow : kTolerance;
                    expectGreaterThan(level, instrument.target - below, sound);
                    expectLessThan(level, instrument.target + kTolerance, sound);
                    // Below full scale with delay and hall, chords included.
                    expectLessThan(peak, 1.0f, sound);
                }

                if (wavDir.isNotEmpty())
                    writeWav(take, juce::File(wavDir).getChildFile(instrument.name).getChildFile(
                                       juce::String(i + 1).paddedLeft('0', 2) + " "
                                       + sound.replaceCharacters("/&", "-+") + ".wav"));
            }
        }
    }
};

static PresetTests presetTests;
