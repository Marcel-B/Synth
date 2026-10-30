#include "Platform.h"
#include "ScopeEditor.h"
#include "ScopeEngine.h"
#include "ScopePresets.h"
#include "ScopeProcessor.h"
#include "PresetCategories.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <set>

using namespace tonwerkscope;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;

using Signal = std::vector<float>;

Signal sine(double hz, double seconds, float level, double phase = 0.0)
{
    Signal out((std::size_t) (seconds * kRate));
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = level * (float) std::sin(kTwoPi * hz * (double) n / kRate + phase);
    return out;
}

/** A band-limited sawtooth, as a bass plays it. */
Signal saw(double hz, double seconds, float level)
{
    Signal out((std::size_t) (seconds * kRate), 0.0f);
    for (int k = 1; k * hz < 18000.0; ++k)
        for (std::size_t n = 0; n < out.size(); ++n)
            out[n] += level * (float) (std::sin(kTwoPi * k * hz * (double) n / kRate) / k * 0.6);
    return out;
}

struct PlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        return info;
    }
};

/** Plays `left`/`right` through the processor at 120 BPM from bar 1, and returns what came out. */
std::pair<Signal, Signal> render(ScopeProcessor& processor, const Signal& left, const Signal& right, PlayHead* head = nullptr)
{
    processor.setPlayHead(head);
    processor.setPlayConfigDetails(2, 2, kRate, kBlock);
    processor.prepareToPlay(kRate, kBlock);
    Signal outLeft(left.size()), outRight(right.size());
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (std::size_t at = 0; at < left.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, left.size() - at);
        if (head != nullptr)
            head->ppq = (double) at / kRate * 2.0;
        buffer.setSize(2, count, false, false, true);
        buffer.copyFrom(0, 0, left.data() + at, count);
        buffer.copyFrom(1, 0, right.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, outLeft.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, outRight.data() + at);
    }
    processor.setPlayHead(nullptr);
    return { outLeft, outRight };
}
} // namespace

class ScopeTests : public juce::UnitTest
{
public:
    ScopeTests() : juce::UnitTest("Tonwerk Scope", "Scope") {}

    void runTest() override
    {
        beginTest("The audio passes to the bit, in every mode");
        {
            const auto left = saw(110.0, 1.0, 0.5f), right = sine(330.0, 1.0, 0.3f);
            for (int program = 0; program < (int) factoryPresets().size(); ++program)
            {
                ScopeProcessor processor;
                processor.setCurrentProgram(program);
                PlayHead head;
                const auto [outLeft, outRight] = render(processor, left, right, program % 2 == 0 ? &head : nullptr);
                expect(outLeft == left && outRight == right, processor.getProgramName(program));
            }
        }

        beginTest("The ring hands back the newest samples in order");
        {
            auto ring = std::make_unique<Ring>();
            Signal ramp(70000);
            for (std::size_t n = 0; n < ramp.size(); ++n)
                ramp[n] = (float) n;
            for (std::size_t at = 0; at < ramp.size(); at += 1000)
            {
                const int count = (int) std::min<std::size_t>(1000, ramp.size() - at);
                ring->write(ramp.data() + at, ramp.data() + at, count);
            }
            Signal l(500), r(500);
            ring->read(l.data(), r.data(), 500);
            expectEquals(l.front(), 69500.0f);
            expectEquals(r.back(), 69999.0f);
            expectEquals((int) ring->written(), 70000);
        }

        beginTest("The trigger finds the crossing in its direction, so a held note stands still");
        {
            for (const double phase : { 0.0, 1.0, 2.5, 4.0 })
            {
                const auto wave = sine(100.0, 0.1, 0.5f, phase);
                const int length = 960;
                const int rising = findTrigger(wave.data(), (int) wave.size(), length, 0.0f, Trigger::rising);
                expectLessOrEqual(rising + length, (int) wave.size());
                expect(wave[(std::size_t) rising - 1] < 0.0f && wave[(std::size_t) rising] >= 0.0f);
                const int falling = findTrigger(wave.data(), (int) wave.size(), length, 0.0f, Trigger::falling);
                expect(wave[(std::size_t) falling - 1] > 0.0f && wave[(std::size_t) falling] <= 0.0f);
                const int high = findTrigger(wave.data(), (int) wave.size(), length, 0.25f, Trigger::rising);
                expect(wave[(std::size_t) high - 1] < 0.25f && wave[(std::size_t) high] >= 0.25f);
            }
            // Noise riding on a slow wave: the trigger still finds the wave's crossing, not a wiggle.
            auto noisy = sine(50.0, 0.2, 0.5f);
            juce::Random random(3);
            for (auto& x : noisy)
                x += 0.01f * (random.nextFloat() - 0.5f);
            const int start = findTrigger(noisy.data(), (int) noisy.size(), 960, 0.0f, Trigger::rising);
            const double cycles = (double) start / kRate * 50.0;
            expectWithinAbsoluteError(cycles - std::round(cycles), 0.0, 0.01);
            // Free running ends at the newest sample.
            expectEquals(findTrigger(noisy.data(), (int) noisy.size(), 960, 0.0f, Trigger::free), (int) noisy.size() - 960);
        }

        beginTest("The pitch readout names the note, Logic's way");
        {
            PitchEstimator estimator;
            estimator.prepare(kRate);
            for (const double hz : { 41.2, 55.0, 110.0, 261.6, 440.0, 1000.0 })
            {
                const auto wave = saw(hz, 0.2, 0.4f);
                const float found = estimator.estimate(wave.data(), (int) wave.size());
                logMessage(juce::String(hz, 1) + " Hz saw: " + juce::String(found, 2) + " Hz");
                expectWithinAbsoluteError((double) found, hz, hz * 0.01);
            }
            const auto quiet = sine(440.0, 0.2, 1.0e-5f);
            expectEquals(estimator.estimate(quiet.data(), (int) quiet.size()), 0.0f);
            expectEquals(juce::String(noteName(69)) + juce::String(noteOctave(69)), juce::String("A3"));
            expectEquals(juce::String(noteName(60)) + juce::String(noteOctave(60)), juce::String("C3"));
            expectEquals(juce::String(noteName(33)) + juce::String(noteOctave(33)), juce::String("A0"));
            expectWithinAbsoluteError(noteOf(440.0), 69.0, 1.0e-9);
        }

        beginTest("The tuner reads a note to a fraction of a cent, at any rate");
        {
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                PitchEstimator estimator;
                estimator.prepare(rate);
                double worst = 0.0;
                for (const double hz : { 30.87, 41.2, 55.0, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 466.16, 659.26,
                                         987.77, 1318.51 })
                {
                    // A band-limited saw with every harmonic's phase different: the hardest case for the period.
                    Signal wave((std::size_t) estimator.needed() + 100, 0.0f);
                    for (int k = 1; k * hz < 0.4 * rate; ++k)
                        for (std::size_t n = 0; n < wave.size(); ++n)
                            wave[n] += (float) (0.25 * std::sin(kTwoPi * k * hz * (double) n / rate + 0.3 * k) / k);
                    const float coarse = estimator.estimate(wave.data(), (int) wave.size());
                    const float fine = estimator.refine(wave.data(), (int) wave.size(), coarse);
                    worst = std::max(worst, std::abs(1200.0 * std::log2(fine / hz)));
                }
                logMessage(juce::String(rate, 0) + " Hz: worst " + juce::String(worst, 2) + " cents from 31 Hz to 1.3 kHz");
                expectLessThan(worst, 1.0);
            }
            // The reference moves every note: at A = 442 Hz, 440 Hz is about 8 cents flat.
            const auto a = tuningOf(442.0, 442.0);
            expectEquals(a.note, 69);
            expectWithinAbsoluteError(a.cents, 0.0f, 1.0e-4f);
            const auto flat = tuningOf(440.0, 442.0);
            expectEquals(flat.note, 69);
            expectWithinAbsoluteError(flat.cents, -7.85f, 0.01f);
            // Half a semitone up is the next note, half a cent short of it this one.
            expectEquals(tuningOf(440.0 * std::pow(2.0, 51.0 / 1200.0), 440.0).note, 70);
            expectEquals(tuningOf(440.0 * std::pow(2.0, 49.0 / 1200.0), 440.0).note, 69);
            ScopeProcessor p;
            auto* reference = p.state.getParameter("reference");
            expectEquals(reference->getText(reference->getValue(), 32), juce::String("440 Hz"));
            reference->setValueNotifyingHost(reference->convertTo0to1(442.0f));
            expectEquals(p.readSettings().reference, 442.0f);
        }

        beginTest("The bars sit where the song's beats are");
        {
            // One bar at 120 BPM: loud on the first beat, quiet on the others.
            Signal left((std::size_t) (2.0 * kRate), 0.1f);
            for (std::size_t n = 0; n < (std::size_t) (0.5 * kRate); ++n)
                left[n] = n % 2 == 0 ? 0.8f : -0.8f;
            ScopeProcessor processor;
            processor.setCurrentProgram(6);
            expectEquals(processor.getProgramName(6), juce::String("Ein Takt"));
            PlayHead head;
            render(processor, left, left, &head);
            const auto& bars = processor.bars;
            expectWithinAbsoluteError(bars.maximum(100, 0), 0.8f, 1.0e-6f);
            expectWithinAbsoluteError(bars.minimum(100, 0), -0.8f, 1.0e-6f);
            expectWithinAbsoluteError(bars.maximum(600, 0), 0.1f, 1.0e-6f);
            expectGreaterThan(bars.playheadColumn(), BeatTrace::kColumns - 4);
            expect(processor.playing.load());
            // Stopped, its own clock runs on: the columns keep being written.
            head.playing = false;
            render(processor, Signal((std::size_t) (0.5 * kRate), 0.3f), Signal((std::size_t) (0.5 * kRate), 0.3f), &head);
            expect(! processor.playing.load());
            expectWithinAbsoluteError(bars.maximum(processor.bars.playheadColumn(), 0), 0.3f, 1.0e-6f);
        }

        beginTest("It costs next to nothing on the audio thread");
        {
            ScopeProcessor processor;
            const auto left = saw(110.0, 10.0, 0.5f);
            PlayHead head;
            const auto started = std::chrono::steady_clock::now();
            render(processor, left, left, &head);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            const double factor = 10.0 / seconds;
            logMessage("scope: " + juce::String(factor, 0) + " times faster than real time");
            expectGreaterThan(factor, 100.0);
        }

        beginTest("Presets have groups and names; the state keeps the knobs");
        {
            std::set<juce::String> names;
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                ScopeProcessor processor;
                const auto sound = processor.getProgramName(i);
                expect(names.insert(sound).second, "twice: " + sound);
                expect(i == 0 || tonwerkui::categoryIndex(presets[(std::size_t) i].category, kScopeCategories) >= 0,
                       "no group: " + sound);
            }
            ScopeProcessor p;
            auto* time = p.state.getParameter("time");
            time->setValueNotifyingHost(time->convertTo0to1(50.0f));
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("time"), juce::String("50 ms"));
            expectEquals(text("window"), juce::String("1 Takt"));
            expectEquals(text("triggerLevel"), juce::String("0 %"));
            expectEquals(text("gain"), juce::String("0 dB"));
            p.state.state.setProperty("editorWidth", 1200, nullptr);
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            ScopeProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().timeMs, 50.0f, 0.1f);
            expectEquals((int) loaded.state.state.getProperty("editorWidth"), 1200);
        }

        beginTest("The tuner shows the note and how far it is off");
        {
            ScopeProcessor p;
            p.setCurrentProgram((int) factoryPresets().size() - 1);
            expect(p.readSettings().mode == Mode::tuner);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = tonwerkui::innerEditor<ScopeEditor>(created.get());
            p.setPlayConfigDetails(2, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            // A guitar's A string twelve cents flat, Logic's A1.
            const double hz = 110.0 * std::pow(2.0, -12.0 / 1200.0);
            const auto string = saw(hz, 1.5, 0.4f);
            juce::AudioBuffer<float> buffer(2, 1600);
            juce::MidiBuffer midi;
            for (std::size_t at = 0; at + 1600 <= string.size(); at += 1600)
            {
                buffer.copyFrom(0, 0, string.data() + at, 1600);
                buffer.copyFrom(1, 0, string.data() + at, 1600);
                p.processBlock(buffer, midi);
                editor->screen.refresh();
            }
            const auto shown = editor->screen.tuning();
            logMessage("tuner: note " + juce::String(shown.note) + ", " + juce::String(shown.cents, 2) + " cents");
            expectEquals(shown.note, 45);
            expectWithinAbsoluteError(shown.cents, -12.0f, 1.0f);
            expect(editor->screen.caption().startsWith(juce::String::fromUTF8("STIMMGERÄT")));
            // Silence: the note stays a moment, then goes.
            buffer.clear();
            for (int frame = 0; frame < 60; ++frame)
            {
                p.processBlock(buffer, midi);
                editor->screen.refresh();
            }
            expectEquals(editor->screen.tuning().note, -1);
            // Back to the note for the picture.
            for (std::size_t at = 0; at + 1600 <= string.size(); at += 1600)
            {
                buffer.copyFrom(0, 0, string.data() + at, 1600);
                buffer.copyFrom(1, 0, string.data() + at, 1600);
                p.processBlock(buffer, midi);
                editor->screen.refresh();
            }
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            if (dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File out = juce::File(dir).getChildFile("scope-stimmgeraet.png");
                out.deleteFile();
                juce::FileOutputStream stream(out);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }

        beginTest("The screen draws all three modes and reads out the pitch");
        {
            // A bass saw on the left, a fifth above on the right: something to see in every mode.
            const auto left = saw(110.0, 2.5, 0.45f), right = saw(164.81, 2.5, 0.3f);
            const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            for (const auto& [program, file] : { std::pair { 1, "scope.png" }, { 7, "scope-takt.png" }, { 9, "scope-xy.png" } })
            {
                ScopeProcessor p;
                p.setCurrentProgram(program);
                std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
                auto* editor = tonwerkui::innerEditor<ScopeEditor>(created.get());
                expect(editor != nullptr);
                PlayHead head;
                p.setPlayHead(&head);
                p.setPlayConfigDetails(2, 2, kRate, kBlock);
                p.prepareToPlay(kRate, kBlock);
                juce::AudioBuffer<float> buffer(2, 1600);
                juce::MidiBuffer midi;
                for (std::size_t at = 0; at + 1600 <= left.size(); at += 1600)
                {
                    head.ppq = (double) at / kRate * 2.0;
                    // The drum-like swell in the bars mode: louder on each beat.
                    const float swell = program == 7 ? 0.3f + 0.7f * (float) std::exp(-std::fmod(head.ppq, 1.0) * 4.0) : 1.0f;
                    for (int n = 0; n < 1600; ++n)
                    {
                        buffer.setSample(0, n, swell * left[at + (std::size_t) n]);
                        // The wave mode gets one bass note on both sides, so it stands still; the others the fifth.
                        const float r = program == 1 ? left[at + (std::size_t) n]
                                                     : (program == 9 ? 0.6f * left[at + (std::size_t) n] : 0.0f)
                                                           + right[at + (std::size_t) n];
                        buffer.setSample(1, n, swell * r);
                    }
                    p.processBlock(buffer, midi);
                    editor->screen.refresh();
                }
                p.setPlayHead(nullptr);
                if (program == 1)
                {
                    logMessage("pitch readout: " + juce::String(editor->screen.pitchHz(), 1) + " Hz");
                    expectWithinAbsoluteError(editor->screen.pitchHz(), 110.0f, 1.0f);
                    expect(editor->screen.caption().startsWith("WELLE"));
                }
                if (program == 7)
                    expect(editor->screen.caption().contains("SONG"));
                expectGreaterThan(editor->getWidth(), 600);
                if (dir.isNotEmpty())
                {
                    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                    juce::File out = juce::File(dir).getChildFile(file);
                    out.deleteFile();
                    juce::FileOutputStream stream(out);
                    juce::PNGImageFormat().writeImageToStream(image, stream);
                }
            }
        }
    }
};

static ScopeTests scopeTests;
