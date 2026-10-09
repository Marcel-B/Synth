#include "PatchJson.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_core/juce_core.h>

using namespace tonwerk;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** Plays `note` for `blocks` and lets it go; returns the loudest sample on either side. */
float play(TonwerkSynthProcessor& processor, int note, int blocks, bool& finite)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        if (i == blocks / 2)
            midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        processor.processBlock(buffer, midi);
        midi.clear();
        peak = std::max({ peak, buffer.getMagnitude(0, 0, kBlock), buffer.getMagnitude(1, 0, kBlock) });
        for (int s = 0; s < kBlock; ++s)
            finite = finite && std::isfinite(buffer.getSample(0, s)) && std::isfinite(buffer.getSample(1, s));
    }
    // Silence for the tails, so the next sound starts clean.
    juce::MidiBuffer none;
    for (int i = 0; i < 1600; ++i)
        processor.processBlock(buffer, none);
    return peak;
}

int countParameters(TonwerkSynthProcessor& processor, const juce::String& prefix)
{
    int count = 0;
    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            count += withId->paramID.startsWith(prefix) ? 1 : 0;
    return count;
}
} // namespace

class EditionTests : public juce::UnitTest
{
public:
    EditionTests() : juce::UnitTest("Analog, FM and Synth", "Plugin") {}

    void runTest() override
    {
        beginTest("Each plugin has only its engine's parameters and the effects");
        {
            TonwerkSynthProcessor analog(Edition::analog), fm(Edition::fm), combined(Edition::combined);
            expect(analog.state.getParameter("engine") == nullptr);
            expect(fm.state.getParameter("engine") == nullptr);
            expect(combined.state.getParameter("engine") != nullptr);
            expectEquals(countParameters(analog, "fm."), 0);
            expectEquals(countParameters(fm, "fm."), countParameters(combined, "fm."));
            expect(fm.state.getParameter("filter.cutoff") == nullptr);
            expect(analog.state.getParameter("filter.cutoff") != nullptr);
            for (auto* p : { &analog, &fm })
                expectEquals(countParameters(*p, "fx."), countParameters(combined, "fx."));
            expectEquals(analog.getParameters().size() + fm.getParameters().size() - countParameters(analog, "fx.") + 1,
                         combined.getParameters().size(), "together they are Tonwerk Synth's");
            expectEquals(analog.getName(), juce::String("Tonwerk Analog"));
            expectEquals(fm.getName(), juce::String("Tonwerk FM"));
            expectEquals(combined.getName(), juce::String("Tonwerk Synth"));
        }

        beginTest("Values show as whole numbers with their units");
        {
            TonwerkSynthProcessor combined;
            auto text = [&](const char* id, float value) {
                auto* parameter = combined.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("ampEnv.attack", 0.35f), juce::String("350 ms"));
            expectEquals(text("ampEnv.release", 1.2f), juce::String("1200 ms"));
            expectEquals(text("filter.cutoff", 1800.4f), juce::String("1800 Hz"));
            expectEquals(text("lfo.rate", 5.5f), juce::String("5.5 Hz"));
            expectEquals(text("lfo.rate", 12.0f), juce::String("12 Hz"));
            expectEquals(text("filter.envAmount", 1.5f), juce::String("18 HT"));
            expectEquals(text("osc1.level", 0.7f), juce::String("70 %"));
            expectEquals(text("fold.symmetry", -0.5f), juce::String("-50 %"));
            expectEquals(text("osc2.detune", 7.0f), juce::String("7 ct"));
            expectEquals(text("osc1.octave", -1.0f), juce::String("-1 Okt"));
            expectEquals(text("fm.op1.ratio", 2.0f), juce::String::fromUTF8("2 ×"));
            expectEquals(text("fm.op2.ratio", 3.5f), juce::String::fromUTF8("3.5 ×"));
            expectEquals(text("filter.resonance", 14.0f), juce::String("14"));
            auto value = [&](const char* id, const char* typed) {
                auto* parameter = combined.state.getParameter(id);
                return parameter->convertFrom0to1(parameter->getValueForText(typed));
            };
            expectWithinAbsoluteError(value("ampEnv.attack", "350 ms"), 0.35f, 1.0e-3f);
            expectWithinAbsoluteError(value("ampEnv.attack", "1.5 s"), 1.5f, 1.0e-3f);
            expectWithinAbsoluteError(value("osc1.level", "40 %"), 0.4f, 1.0e-3f);
        }

        beginTest("Tonwerk FM plays FM, whatever its parameters leave out");
        {
            TonwerkSynthProcessor fm(Edition::fm);
            fm.setPlayConfigDetails(0, 2, kRate, kBlock);
            fm.prepareToPlay(kRate, kBlock);
            bool finite = true;
            expectGreaterThan(play(fm, 60, 40, finite), 0.05f);
            expect(finite);
            expectEquals(fm.currentPatchJson().getProperty("engine", {}).toString(), juce::String("fm"));
            fm.releaseResources();
        }

        beginTest("A plugin takes only its own engine's sounds");
        {
            TonwerkSynthProcessor analog(Edition::analog), fm(Edition::fm), combined;
            const auto fmSound = juce::JSON::parse(R"({ "engine": "fm", "volume": 0.4 })");
            const auto analogSound = juce::JSON::parse(R"({ "volume": 0.3 })");
            expect(! analog.applyPatch(fmSound, "Glocke"));
            expect(analog.presetName() != "Glocke");
            expect(analog.applyPatch(analogSound, "Bass"));
            expectWithinAbsoluteError((float) analog.currentPatchJson().getProperty("volume", {}), 0.3f, 1.0e-3f);
            expect(fm.applyPatch(fmSound, "Glocke"));
            expect(! fm.applyPatch(analogSound, "Bass"));
            expect(combined.applyPatch(fmSound, "Glocke") && combined.applyPatch(analogSound, "Bass"));
        }

        beginTest("A plugin saves and restores its own state");
        {
            TonwerkSynthProcessor analog(Edition::analog);
            analog.applyPatch(juce::JSON::parse(R"({ "filter": { "cutoff": 700 } })"), "Dunkel");
            juce::MemoryBlock saved;
            analog.getStateInformation(saved);
            TonwerkSynthProcessor restored(Edition::analog);
            restored.setStateInformation(saved.getData(), (int) saved.getSize());
            expectEquals(restored.presetName(), juce::String("Dunkel"));
            expectWithinAbsoluteError((float) restored.currentPatchJson()["filter"].getProperty("cutoff", {}), 700.0f, 0.5f);
            // Another plugin's state is not taken for its own.
            TonwerkSynthProcessor fm(Edition::fm);
            fm.setStateInformation(saved.getData(), (int) saved.getSize());
            expect(fm.presetName() != "Dunkel");
        }

        for (const auto edition : { Edition::analog, Edition::fm })
        {
            TonwerkSynthProcessor processor(edition);
            beginTest("Every factory sound of " + processor.getName() + " sounds and does not clip");
            processor.setPlayConfigDetails(0, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            for (int i = 0; i < processor.getNumPrograms(); ++i)
            {
                processor.setCurrentProgram(i);
                // The reverb's room is loaded on the timer, which the tests do not run.
                processor.prepareToPlay(kRate, kBlock);
                bool finite = true;
                const float peak = play(processor, 48, 120, finite);
                expect(finite, processor.getProgramName(i));
                expectGreaterThan(peak, 0.05f, processor.getProgramName(i));
                // Tonwerk's own bass peaks at 1.18 on this note, as in the browser; none goes beyond.
                expectLessThan(peak, 1.2f, processor.getProgramName(i));
                logMessage(processor.getProgramName(i) + ": peak " + juce::String(peak, 2));
            }
            processor.releaseResources();
        }

        beginTest("The editors fit their panels; Tonwerk Synth's follows the engine");
        {
            const auto shots = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
            auto snapshot = [&shots](juce::Component& editor, const juce::String& file) {
                // For a look at them: TONWERK_EDITOR_PNG_DIR=/path writes one PNG per editor.
                if (shots.isEmpty())
                    return;
                const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
                juce::FileOutputStream out { juce::File(shots).getChildFile(file + ".png") };
                out.setPosition(0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream(image, out);
            };
            for (const auto edition : { Edition::analog, Edition::fm })
            {
                TonwerkSynthProcessor processor(edition);
                // A held note, so the oscilloscope has something to show.
                processor.setPlayConfigDetails(0, 2, kRate, kBlock);
                processor.prepareToPlay(kRate, kBlock);
                juce::AudioBuffer<float> buffer(2, kBlock);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8) 100), 0);
                for (int i = 0; i < 20; ++i)
                {
                    processor.processBlock(buffer, midi);
                    midi.clear();
                }
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
                for (auto* child : editor->getChildren())
                    if (auto* scope = dynamic_cast<ScopeView*>(child))
                        scope->refresh();
                const auto engine = edition == Edition::fm ? Engine::fm : Engine::analog;
                auto* synthEditor = dynamic_cast<TonwerkSynthEditor*>(editor.get());
                expect(synthEditor != nullptr);
                expectEquals(editor->getWidth(), TonwerkSynthEditor::kWidth);
                expectEquals(editor->getHeight(), synthEditor->heightFor(engine));
                for (auto* child : editor->getChildren())
                    if (child->isVisible())
                        expect(editor->getLocalBounds().contains(child->getBounds()), child->getName());
                logMessage(processor.getName() + ": " + juce::String(editor->getWidth()) + " x "
                           + juce::String(editor->getHeight()));
                snapshot(*editor, processor.getName());
            }
            TonwerkSynthProcessor combined;
            std::unique_ptr<juce::AudioProcessorEditor> editor(combined.createEditor());
            auto* synthEditor = dynamic_cast<TonwerkSynthEditor*>(editor.get());
            expectEquals(editor->getHeight(), synthEditor->heightFor(Engine::analog));
            snapshot(*editor, "Tonwerk Synth Analog");
            // On the message thread the editor hears of the change at once.
            combined.state.getParameter("engine")->setValueNotifyingHost(1.0f);
            expectEquals(editor->getHeight(), synthEditor->heightFor(Engine::fm));
            snapshot(*editor, "Tonwerk Synth FM");
        }
    }
};

static EditionTests editionTests;
