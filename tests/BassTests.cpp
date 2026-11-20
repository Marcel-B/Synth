#include "Platform.h"
#include "BassEditor.h"
#include "BassEngine.h"
#include "BassPresets.h"
#include "BassProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

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
            expect(tonwerkui::innerEditor<BassEditor>(editor.get()) != nullptr);
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

        beginTest("The filter's picture: flat below the cutoff, a peak at it with resonance, 24 dB an octave above");
        {
            using tonwerkbass::ResponseView;
            expectWithinAbsoluteError(ResponseView::responseDb(0.0f, 1000.0f, 50.0f), 0.0f, 0.1f);
            expectWithinAbsoluteError(ResponseView::responseDb(0.0f, 1000.0f, 16000.0f)
                                          - ResponseView::responseDb(0.0f, 1000.0f, 8000.0f),
                                      -24.0f, 1.0f);
            // With resonance the gain at the cutoff stands well over the gain an octave either side.
            const float peak = ResponseView::responseDb(0.9f, 1000.0f, 1000.0f);
            expectGreaterThan(peak, ResponseView::responseDb(0.9f, 1000.0f, 500.0f) + 6.0f);
            expectGreaterThan(peak, ResponseView::responseDb(0.9f, 1000.0f, 2000.0f) + 6.0f);
            // The oscillator's picture: the sub an octave under the wave, a square under two of its cycles.
            tonwerkbass::Settings s;
            s.wave = tonwerkbass::Wave::saw;
            s.sub = 0.0f;
            expectWithinAbsoluteError(tonwerkbass::WaveView::valueAt(s, 0.125f), -0.5f, 0.001f);
            s.sub = 1.0f;
            expectWithinAbsoluteError(tonwerkbass::WaveView::valueAt(s, 0.125f), 0.5f, 0.001f);
            expectWithinAbsoluteError(tonwerkbass::WaveView::valueAt(s, 0.625f), -1.5f, 0.001f);

            // While a note sounds the processor says where envelope and accent have the cutoff: above its knob.
            BassProcessor p;
            p.setPlayConfigDetails(0, 2, 48000.0, 256);
            p.prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 127), 0);
            p.processBlock(buffer, midi);
            expectGreaterThan(p.shownCutoff.load(), p.readSettings().cutoff * 1.5f);
        }

        beginTest("A switch is a lit key without a word, and a menu of waveforms shows them drawn");
        {
            expect(! tonwerkui::waveIcon("Sinus").isEmpty());
            expect(! tonwerkui::waveIcon("Sawtooth").isEmpty());
            expect(! tonwerkui::waveIcon(juce::String::fromUTF8("Sägezahn")).isEmpty());
            expect(tonwerkui::waveIcon("Cutoff").isEmpty());
            expect(tonwerkui::waveIcon("Sine Bass").isEmpty());
            BassProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            int keys = 0, waves = 0, words = 0;
            std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
                if (auto* toggle = dynamic_cast<juce::ToggleButton*>(&c))
                {
                    ++keys;
                    expect(toggle->getButtonText().isEmpty());
                    expect(toggle->getTitle().isNotEmpty());
                }
                if (auto* box = dynamic_cast<juce::ComboBox*>(&c))
                    (box->getProperties()["waves"] ? waves : words) += 1;
                for (auto* child : c.getChildren())
                    walk(*child);
            };
            walk(*editor);
            // Sync of the LFO and of the delay; wave and LFO shape drawn, the LFO's target and the note values in words.
            expectEquals(keys, 2);
            expectEquals(waves, 2);
            expectGreaterThan(words, 1);
        }

        beginTest("Where a finger chooses, the menus are higher and their lists' rows as high as the box");
        {
            BassProcessor p;
            auto heights = [&](bool touch) {
                const juce::ScopedValueSetter<bool> set(tonwerkui::touchMenus(), touch);
                std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
                int box = 0, row = 0;
                std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
                    auto* menu = dynamic_cast<juce::ComboBox*>(&c);
                    if (menu != nullptr && menu->getProperties()["waves"])
                    {
                        juce::Label label;
                        menu->getLookAndFeel().positionComboBoxText(*menu, label);
                        box = menu->getHeight();
                        row = menu->getLookAndFeel().getOptionsForComboBoxPopupMenu(*menu, label).getStandardItemHeight();
                    }
                    for (auto* child : c.getChildren())
                        walk(*child);
                };
                walk(*editor);
                return std::pair { box, row };
            };
            const auto mouse = heights(false), finger = heights(true);
            expectEquals(mouse.first, 24);
            expectEquals(mouse.second, 24);
            expectEquals(finger.first, tonwerkui::kTouchMenu);
            expectEquals(finger.second, tonwerkui::kTouchMenu);
        }

        beginTest("Inside the iPad app the editor leaves its scope to the app's");
        {
            BassProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> with(p.createEditor());
            auto scopes = [](juce::Component* editor) {
                int count = 0;
                for (auto* child : editor->getChildren())
                    count += dynamic_cast<tonwerkui::ScopeView*>(child) != nullptr && child->isVisible() ? 1 : 0;
                return count;
            };
            expectEquals(scopes(tonwerkui::innerEditor<BassEditor>(with.get())), 1);
            tonwerkui::editorsHaveScope() = false;
            std::unique_ptr<juce::AudioProcessorEditor> without(p.createEditor());
            tonwerkui::editorsHaveScope() = true;
            auto* inner = tonwerkui::innerEditor<BassEditor>(without.get());
            expectEquals(scopes(inner), 0);
            // The room goes back to the knobs: the editor is lower, and nothing is left where the scope was.
            expectLessThan(without->getHeight(), with->getHeight() - 100);
            expectEquals(without->getWidth(), with->getWidth());
            for (auto* child : inner->getChildren())
                if (child->isVisible())
                    expect(inner->getLocalBounds().contains(child->getBounds()));
            // What the scope's caption says of the note comes from the processor, so the app's scope can say it.
            expect(p.scopeNote().isEmpty());
            p.shownNote = 36;
            p.shownAccent = true;
            p.shownSlide = true;
            expect(p.scopeNote().endsWith("C1  ACCENT  SLIDE"), p.scopeNote());
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = without->createComponentSnapshot(without->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("bass-ipad.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }

        beginTest("Pages for a phone's screen, the knobs as large as Tonwerk Analog's");
        {
            BassProcessor p;
            // A page names its parameters by id; one the plugin lacks would be a knob that moves nothing.
            std::set<juce::String> named;
            for (const auto& page : bassPages())
            {
                int places = 0;
                for (const auto& panel : page.panels)
                    for (const auto& item : panel.row)
                    {
                        named.insert(item.id);
                        // Of two controls sharing a place (a rate or its note value) only one shows.
                        places += item.when.isNotEmpty() && item.whenOn ? 0 : 1;
                        expect(p.state.getParameter(item.id) != nullptr, "no parameter " + item.id);
                        if (item.when.isNotEmpty())
                            expect(p.state.getParameter(item.when) != nullptr, "no parameter " + item.when);
                    }
                // More than ten places made Tonwerk Analog's knobs smaller on every page.
                expectLessOrEqual(places, 10, page.title);
            }
            // Nothing of the full editor is out of reach on a phone.
            for (auto* parameter : p.getParameters())
                if (auto* with = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                    expect(named.count(with->paramID) == 1, "no page has " + with->paramID);

            std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
            auto* frame = dynamic_cast<tonwerkui::HostFrame*>(editor.get());
            expect(frame != nullptr && frame->hasCompact() && ! frame->isCompact());
            if (frame == nullptr)
                return;
            // An iPhone in landscape beside the app's rail, without the keys: the size Tonwerk Analog's pages have.
            frame->setCompact(true);
            frame->setBare(true);
            frame->setSize(666, 381);
            auto* compact = tonwerkui::innerEditor<tonwerkui::CompactEditor>(editor.get());
            expect(compact != nullptr && tonwerkui::innerEditor<BassEditor>(editor.get()) == nullptr);
            if (compact == nullptr)
                return;
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            float smallest = 1.0e9f, largest = 0.0f;
            int tabs = 0;
            for (auto* child : compact->getChildren())
                if (auto* tab = dynamic_cast<juce::TextButton*>(child); tab != nullptr && tab->getRadioGroupId() != 0)
                {
                    ++tabs;
                    tab->onClick();
                    smallest = std::min(smallest, compact->slotWidth());
                    largest = std::max(largest, compact->slotWidth());
                    if (dir.isNotEmpty())
                    {
                        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
                        juce::File file = juce::File(dir).getChildFile("bass-seite-" + tab->getButtonText() + ".png");
                        file.deleteFile();
                        juce::FileOutputStream stream(file);
                        juce::PNGImageFormat().writeImageToStream(image, stream);
                    }
                }
            expectEquals(tabs, (int) bassPages().size());
            logMessage("A knob's place on the pages: " + juce::String(smallest, 1) + " points");
            // One size on every page, and Tonwerk Analog's on the same screen (78 points).
            expectWithinAbsoluteError(largest, smallest, 0.01f);
            expectGreaterOrEqual(smallest, 78.0f);
            frame->setCompact(false);
            expect(tonwerkui::innerEditor<BassEditor>(editor.get()) != nullptr);
        }
    }
};

static BassTests bassTests;
