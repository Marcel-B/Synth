#include "DxEditor.h"
#include "DxPresets.h"
#include "DxProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>

using namespace tonwerkdx;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

float rms(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n)
        sum += (double) signal[n] * signal[n];
    return (float) std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

float peak(const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    float worst = 0.0f;
    for (std::size_t n = from; n < to; ++n)
        worst = std::max(worst, std::abs(signal[n]));
    return worst;
}

/** The amplitude of `hz` in `signal` between `from` and `to` (Goertzel). */
float amplitudeAt(const std::vector<float>& signal, double hz, std::size_t from, std::size_t to)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < to; ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (to - from));
}

float decibels(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-12f)); }

/** A single operator at full level, steady: the plainest DX patch, a sine. */
DxPatch sine()
{
    DxPatch p;
    p.algorithm = 31;
    for (auto& op : p.ops)
        op.on = false;
    p.ops[0].on = true;
    return p;
}

/** One voice of `p` held for `seconds` at `velocity`, straight from the engine. */
std::vector<float> voice(const DxPatch& p, int note, double seconds, float velocity = 1.0f, double releaseAt = -1.0)
{
    DxVoice v;
    v.prepare(kRate);
    v.start(note, velocity, p, 1);
    std::vector<float> out((std::size_t) (seconds * kRate));
    const auto releaseSample = releaseAt < 0.0 ? out.size() : (std::size_t) (releaseAt * kRate);
    for (std::size_t at = 0; at < out.size(); at += kBlock)
    {
        if (at <= releaseSample && releaseSample < at + kBlock)
            v.release();
        v.render(p, out.data() + at, (int) std::min<std::size_t>(kBlock, out.size() - at), 0.0);
    }
    return out;
}

struct Note
{
    double on, off;
    int note;
    int velocity = 100;
};

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

/** Plays `notes` through a fresh processor after loading `patch` (or factory sound `program`). */
Rendered play(const DxPatch* patch, int program, std::vector<Note> notes, double length)
{
    DxProcessor processor;
    processor.setPlayConfigDetails(0, 2, kRate, kBlock);
    if (program >= 0)
        processor.setCurrentProgram(program);
    if (patch != nullptr)
        processor.applyPatch(*patch, "Test");
    processor.prepareToPlay(kRate, kBlock);

    const auto total = (std::size_t) (length * kRate);
    Rendered out { std::vector<float>(total), std::vector<float>(total) };
    juce::AudioBuffer<float> buffer(2, kBlock);
    const auto start = std::chrono::steady_clock::now();
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
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + at);
        std::copy_n(buffer.getReadPointer(1), count, out.right.data() + at);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

/** A packed DX7 voice (128 bytes) with `name`, `algorithm` and operator 1 at `coarse` with detune `detune`. */
std::vector<std::uint8_t> packedVoice(const std::string& name, int algorithm, int coarse, int detune)
{
    std::vector<std::uint8_t> v(128, 0);
    for (int k = 0; k < 6; ++k)
    {
        auto* o = v.data() + k * 17;
        for (int i = 0; i < 4; ++i)
        {
            o[i] = 99;
            o[4 + i] = i == 3 ? 0 : 99;
        }
        o[12] = (std::uint8_t) (7 << 3); // detune 7 is centre
        o[14] = 99;
        o[15] = (std::uint8_t) (1 << 1); // ratio mode, coarse 1
    }
    // Operator 1 is stored last.
    auto* op1 = v.data() + 5 * 17;
    op1[12] = (std::uint8_t) (((detune + 7) << 3) | 3); // rate scaling 3
    op1[13] = (std::uint8_t) ((5 << 2) | 2);            // velocity 5, AMS 2
    op1[15] = (std::uint8_t) (coarse << 1);
    op1[16] = 50;
    v[110] = (std::uint8_t) algorithm;
    v[111] = (std::uint8_t) (8 | 6); // key sync and feedback 6
    v[112] = 40;
    v[116] = (std::uint8_t) ((4 << 4) | (4 << 1)); // pitch sensitivity 4, sine
    v[117] = 24 + 12;
    for (std::size_t i = 0; i < 10; ++i)
        v[118 + i] = (std::uint8_t) (i < name.size() ? name[i] : ' ');
    return v;
}

std::vector<std::uint8_t> bank()
{
    std::vector<std::uint8_t> data { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
    int sum = 0;
    for (int i = 0; i < 32; ++i)
        for (auto byte : packedVoice("VOICE " + std::to_string(i + 1), i, i % 31 + 1, i % 15 - 7))
        {
            data.push_back(byte);
            sum += byte;
        }
    data.push_back((std::uint8_t) ((128 - (sum & 127)) & 127));
    data.push_back(0xF7);
    return data;
}
} // namespace

class DxTests : public juce::UnitTest
{
public:
    DxTests() : juce::UnitTest("Tonwerk DX", "DX") {}

    void runTest() override
    {
        beginTest("Every algorithm computes from operator 6 down and hears at least one carrier");
        {
            const std::array<int, kAlgorithms> carriers { 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
                                                          1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6 };
            for (int a = 0; a < kAlgorithms; ++a)
            {
                const auto& algorithm = algorithms()[(std::size_t) a];
                expectEquals(algorithm.carrierCount(), carriers[(std::size_t) a], "algorithm " + juce::String(a + 1));
                expect(juce::isPositiveAndBelow(algorithm.feedback, kOperators));
                for (int op = 0; op < kOperators; ++op)
                {
                    bool feeds = false;
                    for (int to = 0; to < kOperators; ++to)
                        if (algorithm.modulates(op, to))
                        {
                            feeds = true;
                            expectGreaterThan(op, to, "algorithm " + juce::String(a + 1));
                        }
                    // Every operator is heard or feeds something: none is left over.
                    expect(feeds || algorithm.isCarrier(op),
                           "algorithm " + juce::String(a + 1) + " operator " + juce::String(op + 1));
                }
            }
        }

        beginTest("The diagram keeps every box apart");
        {
            for (int a = 0; a < kAlgorithms; ++a)
            {
                const auto place = DxAlgorithmView::placesFor(algorithms()[(std::size_t) a]);
                for (int i = 0; i < kOperators; ++i)
                    for (int j = i + 1; j < kOperators; ++j)
                        expect(place[(std::size_t) i].getDistanceFrom(place[(std::size_t) j]) >= 1.0f,
                               "algorithm " + juce::String(a + 1));
                // Modulators sit above what they feed.
                for (int from = 0; from < kOperators; ++from)
                    for (int to = 0; to < kOperators; ++to)
                        if (algorithms()[(std::size_t) a].modulates(from, to))
                            expectGreaterThan(place[(std::size_t) from].y, place[(std::size_t) to].y);
            }
        }

        beginTest("An envelope stage at rate 50 takes about its time");
        {
            OperatorPatch op;
            op.rates = { 50, 99, 99, 99 };
            op.levels = { 99, 99, 99, 0 };
            op.levels[3] = 0;
            DxEnvelope env;
            env.start(op, 0.0);
            const double chunk = 32.0 / kRate;
            double t = 0.0;
            while (env.advance(op, chunk) < 99.0 && t < 60.0)
                t += chunk;
            expectWithinAbsoluteError(t, stageSeconds(50), 0.01);
            logMessage("Rate 50 from 0 to 99: " + juce::String(t, 3) + " s; rate 99: "
                       + juce::String(stageSeconds(99) * 1000.0, 1) + " ms, rate 0: " + juce::String(stageSeconds(0), 1)
                       + " s");
            expectLessThan(stageSeconds(99), 0.005);
            expectGreaterThan(stageSeconds(0), 30.0);
        }

        beginTest("Levels fall 0.75 dB a step");
        {
            auto loud = sine();
            auto soft = sine();
            soft.ops[0].level = 91;
            const auto a = voice(loud, 69, 0.5), b = voice(soft, 69, 0.5);
            expectWithinAbsoluteError(decibels(rms(b, 12000, 24000) / rms(a, 12000, 24000)), -6.0f, 0.2f);
        }

        beginTest("Coarse 1 plays the note, coarse 0 an octave below, fine adds a share");
        {
            for (auto [coarse, fine, hz] : { std::tuple { 1, 0, 440.0 }, std::tuple { 0, 0, 220.0 },
                                             std::tuple { 2, 50, 1320.0 } })
            {
                auto p = sine();
                p.ops[0].coarse = coarse;
                p.ops[0].fine = fine;
                const auto out = voice(p, 69, 0.5);
                const float level = rms(out, 6000, 24000) * std::sqrt(2.0f);
                expectWithinAbsoluteError(amplitudeAt(out, hz, 6000, 24000) / level, 1.0f, 0.03f);
            }
        }

        beginTest("A modulator adds overtones; more level, more of them");
        {
            auto p = sine();
            p.algorithm = 0; // 2 -> 1
            p.ops[1].on = true;
            // The share of the energy away from the fundamental.
            auto overtones = [&](int level) {
                p.ops[1].level = level;
                const auto out = voice(p, 57, 0.5);
                const float fundamental = amplitudeAt(out, 220.0, 6000, 24000) / std::sqrt(2.0f);
                const float all = rms(out, 6000, 24000);
                return 1.0f - fundamental * fundamental / (all * all);
            };
            const float none = overtones(0), some = overtones(75), more = overtones(90);
            expectLessThan(none, 0.01f);
            expectGreaterThan(some, 0.05f);
            expectGreaterThan(more, some);
        }

        beginTest("Velocity sensitivity takes off up to about 30 dB on a soft note");
        {
            auto p = sine();
            p.ops[0].velocity = 7;
            const auto hard = voice(p, 69, 0.4, 1.0f), soft = voice(p, 69, 0.4, 0.2f);
            const float drop = decibels(rms(soft, 6000, 19000) / rms(hard, 6000, 19000));
            expectWithinAbsoluteError(drop, -0.8f * 42.0f * 0.75f, 1.0f);
            auto plain = sine();
            const auto even = voice(plain, 69, 0.4, 0.2f);
            expectWithinAbsoluteError(rms(even, 6000, 19000), rms(hard, 6000, 19000), 0.001f);
        }

        beginTest("A released voice ends and goes quiet");
        {
            auto p = sine();
            p.ops[0].rates = { 99, 99, 99, 60 };
            const auto out = voice(p, 69, 2.0, 1.0f, 0.5);
            expectGreaterThan(rms(out, 12000, 24000), 0.1f);
            expectLessThan(peak(out, 80000, 96000), 1.0e-4f);
        }

        beginTest("LFO vibrato moves the pitch by its sensitivity");
        {
            auto p = sine();
            p.lfoWave = LfoWave::square;
            p.lfoSpeed = 10;
            p.lfoPitchDepth = 99;
            p.lfoPitchSens = 7; // an octave at full depth
            const auto out = voice(p, 69, 0.3);
            // The square sits at one end for the first half of its slow cycle: an octave above.
            const float up = amplitudeAt(out, 880.0, 2000, 12000), home = amplitudeAt(out, 440.0, 2000, 12000);
            expectGreaterThan(up, home * 5.0f);
        }

        beginTest("SysEx: a 32-voice bank reads every voice");
        {
            const auto voices = readSysex(bank());
            expectEquals((int) voices.size(), 32);
            for (int i = 0; i < 32; ++i)
            {
                const auto& v = voices[(std::size_t) i];
                expectEquals(juce::String(v.name), "VOICE " + juce::String(i + 1));
                expectEquals(v.patch.algorithm, i);
                expectEquals(v.patch.ops[0].coarse, i % 31 + 1);
                expectEquals(v.patch.ops[0].detune, i % 15 - 7);
                expectEquals(v.patch.ops[0].fine, 50);
                expectEquals(v.patch.ops[0].rateScaling, 3);
                expectEquals(v.patch.ops[0].velocity, 5);
                expectEquals(v.patch.ops[0].ampModSens, 2);
                expectEquals(v.patch.ops[5].coarse, 1);
                expectEquals(v.patch.ops[5].levels[3], 0);
                expectEquals(v.patch.feedback, 6);
                expectEquals(v.patch.lfoSpeed, 40);
                expect(v.patch.lfoWave == LfoWave::sine);
                expectEquals(v.patch.lfoPitchSens, 4);
                expectEquals(v.patch.transpose, 12);
            }
            // The bare 4096 bytes without the frame read the same.
            const auto raw = bank();
            const auto bare = readSysex(std::vector<std::uint8_t>(raw.begin() + 6, raw.begin() + 6 + 4096));
            expectEquals((int) bare.size(), 32);
            expectEquals(juce::String(bare[31].name), juce::String("VOICE 32"));
            // Anything else is no voice.
            expect(readSysex({ 0xF0, 0x7E, 0x00, 0xF7 }).empty());
            expect(readSysex(std::vector<std::uint8_t>(1000, 0)).empty());
        }

        beginTest("SysEx: a single voice");
        {
            std::vector<std::uint8_t> data { 0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B };
            std::vector<std::uint8_t> v(155, 0);
            for (int k = 0; k < 6; ++k)
            {
                auto* o = v.data() + k * 21;
                for (int i = 0; i < 4; ++i)
                    o[i] = 80;
                o[16] = (std::uint8_t) (90 - k);
                o[18] = 1;
                o[20] = 7;
            }
            v[5 * 21 + 18] = 2;      // operator 1: coarse 2
            v[5 * 21 + 20] = 10;     // detune +3
            v[134] = 4;              // algorithm 5
            v[135] = 7;
            const char* voiceName = "SINGLE ONE";
            for (int i = 0; i < 10; ++i)
                v[(std::size_t) (145 + i)] = (std::uint8_t) voiceName[i];
            data.insert(data.end(), v.begin(), v.end());
            data.push_back(0);
            data.push_back(0xF7);
            const auto voices = readSysex(data);
            expectEquals((int) voices.size(), 1);
            expectEquals(juce::String(voices[0].name), juce::String("SINGLE ONE"));
            expectEquals(voices[0].patch.algorithm, 4);
            expectEquals(voices[0].patch.feedback, 7);
            expectEquals(voices[0].patch.ops[0].coarse, 2);
            expectEquals(voices[0].patch.ops[0].detune, 3);
            expectEquals(voices[0].patch.ops[0].level, 85);
            expectEquals(voices[0].patch.ops[5].level, 90);
        }

        beginTest("SysEx files are kept and listed as banks");
        {
            const auto folder = juce::File::createTempFile("dx-sysex");
            const auto source = juce::File::createTempFile(".syx");
            const auto data = bank();
            source.replaceWithData(data.data(), data.size());
            expectEquals(DxProcessor::importSysex(source, folder), 32);
            const auto banks = DxProcessor::banks(folder);
            expectEquals((int) banks.size(), 1);
            expectEquals(banks[0].name, source.getFileNameWithoutExtension());
            expectEquals((int) banks[0].voices.size(), 32);
            const auto junk = juce::File::createTempFile(".syx");
            junk.replaceWithText("kein SysEx");
            expectEquals(DxProcessor::importSysex(junk, folder), 0);
            expectEquals((int) DxProcessor::banks(folder).size(), 1);
            folder.deleteRecursively();
            source.deleteFile();
            junk.deleteFile();
        }

        beginTest("Parameter ids are unique and a patch survives the parameters");
        {
            juce::StringArray ids;
            for (const auto& d : parameters())
            {
                expect(! ids.contains(d.id), d.id);
                ids.add(d.id);
            }
            expectEquals(ids.size(), 10 + 6 * 16 + 8);
            DxProcessor processor;
            for (const auto& preset : factoryPresets())
            {
                processor.applyPatch(preset.patch, juce::String::fromUTF8(preset.name.c_str()));
                const auto back = processor.readPatch();
                for (const auto& d : parameters())
                    expectWithinAbsoluteError(d.get(back), d.get(preset.patch), 1.0e-3f,
                                              juce::String::fromUTF8(preset.name.c_str()) + " " + d.id);
            }
        }

        beginTest("State saves and loads");
        {
            juce::MemoryBlock saved;
            {
                DxProcessor processor;
                processor.setCurrentProgram(3);
                processor.getStateInformation(saved);
            }
            DxProcessor processor;
            processor.setCurrentProgram(0);
            processor.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(processor.readPatch().algorithm, factoryPresets()[3].patch.algorithm);
            expectEquals(processor.presetName(), juce::String::fromUTF8(factoryPresets()[3].name.c_str()));
        }

        beginTest("Every factory sound is heard and keeps below full scale, in a chord too");
        {
            const auto& presets = factoryPresets();
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                const auto sound = juce::String::fromUTF8(presets[(std::size_t) i].name.c_str());
                const auto single = play(nullptr, i, { { 0.0, 1.0, 60 } }, 1.5);
                // Dry: the chord's own peaks, without the hall's and the delay's.
                auto dry = presets[(std::size_t) i].patch;
                dry.fx.delayMix = dry.fx.reverbMix = 0.0f;
                const auto chord = play(&dry, -1, { { 0.0, 1.0, 48 }, { 0.0, 1.0, 60 }, { 0.0, 1.0, 64 }, { 0.0, 1.0, 67, 127 } }, 1.5);
                // The loudest tenth of a second: a pad takes its time, a marimba is gone soon.
                float level = 0.0f;
                for (std::size_t at = 0; at + 4800 <= 48000; at += 4800)
                    level = std::max(level, rms(single.left, at, at + 4800));
                const float loudest = std::max(peak(chord.left, 0, chord.left.size()), peak(chord.right, 0, chord.right.size()));
                logMessage(sound + ": RMS " + juce::String(decibels(level), 1) + " dB, dry chord peak "
                           + juce::String(loudest, 2));
                expectGreaterThan(level, 0.02f, sound);
                expectLessThan(loudest, 0.95f, sound);
                expectEquals(DxProcessor().getProgramName(i), sound);
            }
        }

        beginTest("Sixteen voices of the brass stay well inside real time");
        {
            std::vector<Note> notes;
            for (int i = 0; i < 16; ++i)
                notes.push_back({ 0.0, 3.5, 40 + i * 3 });
            const auto out = play(nullptr, 2, notes, 4.0);
            const double realtime = 4.0 / out.seconds;
            logMessage("16 voices, 6 operators each, delay and hall: " + juce::String(realtime, 1) + "x real time");
            // About 16x in a release build; a debug build reaches less than a fifth of that.
            expectGreaterThan(realtime, 2.0);
        }

        beginTest("The editor fits its panels and switches the detail panel");
        {
            DxProcessor processor;
            processor.setPlayConfigDetails(0, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            {
                juce::AudioBuffer<float> buffer(2, kBlock);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8) 100), 0);
                for (int i = 0; i < 40; ++i)
                {
                    processor.processBlock(buffer, midi);
                    midi.clear();
                }
            }
            std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
            auto* editor = dynamic_cast<DxEditor*>(base.get());
            expect(editor != nullptr);
            logMessage("Editor: " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
            expectLessOrEqual(editor->getHeight(), 800);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            expectEquals((int) editor->menu().size(), (int) factoryPresets().size() + [] {
                int n = 0;
                for (const auto& b : DxProcessor::banks())
                    n += (int) b.voices.size();
                return n;
            }());
            editor->selectOperator(1);
            expectEquals(editor->selectedOperator(), 1);
            int shown = 0;
            for (auto* child : editor->getChildren())
                if (auto* section = dynamic_cast<OperatorSection*>(child); section != nullptr && section->isVisible())
                    ++shown;
            expectEquals(shown, kOperators + 1);
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                editor->selectOperator(0);
                for (auto* child : editor->getChildren())
                    if (auto* view = dynamic_cast<tonwerkui::ScopeView*>(child))
                        view->refresh();
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk DX.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static DxTests dxTests;
