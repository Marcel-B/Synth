#include "LfoEditor.h"
#include "LfoEngine.h"
#include "LfoPresets.h"
#include "LfoProcessor.h"

#include "PresetCategories.h"

#include <juce_core/juce_core.h>

#include <set>

using namespace tonwerklfo;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** A controller message as it came out, in samples from the start. */
struct Sent
{
    long long at;
    int controller;
    int value;
    int channel;
};

/** A host at 120 BPM, playing from `ppq` or standing. */
class Head : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

void set(LfoProcessor& p, const char* id, float value)
{
    auto* parameter = p.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/**
 * Runs the plugin for `samples` in blocks as a host would, `in` going in at its samples; returns the controllers it
 * sent and, in `passed`, everything else that came out.
 */
std::vector<Sent> run(LfoProcessor& p, long long samples, const std::vector<std::pair<long long, juce::MidiMessage>>& in = {},
                      Head* head = nullptr, std::vector<juce::MidiMessage>* passed = nullptr)
{
    p.setPlayHead(head);
    p.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(0, kBlock);
    juce::MidiBuffer midi;
    std::vector<Sent> out;
    for (long long at = 0; at < samples; at += kBlock)
    {
        const int count = (int) std::min<long long>(kBlock, samples - at);
        buffer.setSize(0, count);
        midi.clear();
        for (const auto& [when, message] : in)
            if (when >= at && when < at + count)
                midi.addEvent(message, (int) (when - at));
        p.processBlock(buffer, midi);
        for (const auto event : midi)
        {
            const auto message = event.getMessage();
            if (message.isController())
                out.push_back({ at + event.samplePosition, message.getControllerNumber(), message.getControllerValue(),
                                message.getChannel() });
            else if (passed != nullptr)
                passed->push_back(message);
        }
        if (head != nullptr && head->playing)
            head->ppq += count / kRate * 2.0;
    }
    p.setPlayHead(nullptr);
    return out;
}

/** The 14-bit values in order, one per coarse/fine pair. */
std::vector<int> fineValues(const std::vector<Sent>& sent, int controller)
{
    std::vector<int> values;
    int coarse = 0;
    for (const auto& s : sent)
        if (s.controller == controller)
            coarse = s.value;
        else if (s.controller == controller + 32)
            values.push_back(coarse * 128 + s.value);
    return values;
}
} // namespace

class LfoTests : public juce::UnitTest
{
public:
    LfoTests() : juce::UnitTest("Tonwerk LFO", "LFO") {}

    void runTest() override
    {
        beginTest("A free sine sweeps the whole controller in 14 bits");
        {
            LfoProcessor p;
            set(p, "sync", 0.0f);
            set(p, "rate", 1.0f);
            const auto sent = run(p, (long long) kRate);
            const auto values = fineValues(sent, 20);
            expectGreaterThan((int) values.size(), 500);
            expectLessOrEqual((int) values.size(), (int) (kRate / kStep) + 1);
            expectEquals(*std::max_element(values.begin(), values.end()), kMaxFine);
            expectEquals(*std::min_element(values.begin(), values.end()), 0);
            // Every pair is a coarse half on 20, then a fine half on 52, at the same sample, on channel 1.
            for (std::size_t i = 0; i + 1 < sent.size(); i += 2)
            {
                expectEquals(sent[i].controller, 20);
                expectEquals(sent[i + 1].controller, 52);
                expect(sent[i].at == sent[i + 1].at && sent[i].channel == 1);
            }
            // A quarter of the way, the sine is at its top.
            for (std::size_t i = 0; i + 1 < sent.size(); i += 2)
                if (sent[i].at == 12032)
                    expectGreaterThan(sent[i].value * 128 + sent[i + 1].value, 16300);
        }

        beginTest("Seven bits on a controller above 31, and nothing while the value stands");
        {
            LfoProcessor p;
            set(p, "controller", 74.0f);
            set(p, "sync", 0.0f);
            set(p, "rate", 0.5f);
            const auto sent = run(p, (long long) kRate);
            for (const auto& s : sent)
                expectEquals(s.controller, 74);
            // A 0.5 Hz sine crosses 128 steps up and down in a second: at most about 128 messages, never one per step.
            expectLessThan((int) sent.size(), 140);
            expectGreaterThan((int) sent.size(), 100);

            LfoProcessor still;
            set(still, "depth", 0.0f);
            const auto once = run(still, (long long) kRate);
            expectEquals((int) once.size(), 2);
            expectEquals(fineValues(once, 20).front(), 8192);
        }

        beginTest("Synced, it sits where the song is");
        {
            // A rising saw over a bar: half a bar in it is in the middle, three quarters in at 75 %.
            for (const auto& [ppq, expected] : { std::pair { 2.0, 8192 }, { 3.0, 12288 }, { 6.0, 8192 } })
            {
                LfoProcessor p;
                set(p, "shape", (float) Shape::sawUp);
                set(p, "division", (float) tonwerkwobble::kWhole);
                Head head;
                head.ppq = ppq;
                head.playing = true;
                const auto values = fineValues(run(p, 64, {}, &head), 20);
                expect(! values.empty());
                if (! values.empty())
                    expectWithinAbsoluteError(values.front(), expected, 3);
            }
        }

        beginTest("Start note: the first key of a phrase starts the cycle at its sample");
        {
            LfoProcessor p;
            set(p, "shape", (float) Shape::sawUp);
            set(p, "start", (float) Start::note);
            set(p, "division", (float) tonwerkwobble::kWhole);
            std::vector<juce::MidiMessage> passed;
            const auto sent = run(p, 4 * kBlock,
                                  { { 300, juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100) },
                                    { 600, juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100) },
                                    { 700, juce::MidiMessage::noteOff(1, 60) } },
                                  nullptr, &passed);
            // At sample 300 the saw starts at its bottom; the second key, held over the first, does not restart it.
            bool restarted = false;
            for (std::size_t i = 0; i + 1 < sent.size(); i += 2)
                if (sent[i].at == 300)
                {
                    restarted = true;
                    expectEquals(sent[i].value * 128 + sent[i + 1].value, 0);
                }
            expect(restarted);
            // The keys pass through.
            expectEquals((int) passed.size(), 3);
        }

        beginTest("Factory presets: groups, names, and they never touch the controller");
        {
            const auto& presets = factoryPresets();
            std::set<juce::String> names;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                const auto& preset = presets[(std::size_t) i];
                expect(names.insert(juce::String::fromUTF8(preset.name)).second);
                expect(i == 0 || tonwerkui::categoryIndex(preset.category, kLfoCategories) >= 0);
                for (const auto& [id, value] : preset.values)
                    expect(juce::String(id) != "controller" && juce::String(id) != "channel", preset.name);
                LfoProcessor p;
                p.setCurrentProgram(i);
                expect(! run(p, kBlock).empty(), preset.name);
            }
        }

        beginTest("The state keeps the knobs; values show whole numbers with units");
        {
            LfoProcessor p;
            set(p, "controller", 74.0f);
            auto text = [&p](const char* id) {
                auto* parameter = p.state.getParameter(id);
                return parameter->getText(parameter->getValue(), 32);
            };
            expectEquals(text("controller"), juce::String("CC 74"));
            expectEquals(text("depth"), juce::String("100 %"));
            expectEquals(text("phase"), juce::String::fromUTF8("0 °"));
            expectEquals(text("division"), juce::String("1/4"));
            juce::MemoryBlock saved;
            p.getStateInformation(saved);
            LfoProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(loaded.readSettings().controller, 74);
        }

        beginTest("The editor fits its panels");
        {
            LfoProcessor p;
            run(p, 4 * kBlock);
            std::unique_ptr<juce::AudioProcessorEditor> created(p.createEditor());
            auto* editor = dynamic_cast<LfoEditor*>(created.get());
            expect(editor != nullptr);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            editor->wave.view.refresh();
            expect(editor->wave.view.caption().startsWith(juce::String::fromUTF8("CC 20 · 14 BIT · KANAL 1 · 1/4")));
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::File file = juce::File(dir).getChildFile("lfo.png");
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
};

static LfoTests lfoTests;
