#include "Compact.h"
#include "Platform.h"
#include "PatchJson.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <set>

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
        beginTest("The editors fit their panels; Tonwerk Synth's follows the engine");
        {
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
                auto* synthEditor = tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get());
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
            auto* synthEditor = tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get());
            expectEquals(editor->getHeight(), synthEditor->heightFor(Engine::analog));
            snapshot(*editor, "Tonwerk Synth Analog");
            // On the message thread the editor hears of the change at once.
            combined.state.getParameter("engine")->setValueNotifyingHost(1.0f);
            expectEquals(editor->getHeight(), synthEditor->heightFor(Engine::fm));
            snapshot(*editor, "Tonwerk Synth FM");
        }

        beginTest("On the iPad the header has the sounds, but no buttons to load, save or delete them");
        {
            auto buttons = [this, &snapshot](bool withFiles) {
                tonwerkui::soundFilesInHeader() = withFiles;
                TonwerkSynthProcessor analog(Edition::analog);
                std::unique_ptr<juce::AudioProcessorEditor> editor(analog.createEditor());
                auto* synthEditor = tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get());
                int shown = 0, menus = 0;
                for (auto* child : synthEditor->getChildren())
                {
                    // The header's buttons carry a text; the keyboard's and the sections' are not direct children.
                    if (auto* button = dynamic_cast<juce::TextButton*>(child))
                        shown += button->isVisible() && button->getButtonText().isNotEmpty() ? 1 : 0;
                    menus += dynamic_cast<juce::ComboBox*>(child) != nullptr && child->isVisible() ? 1 : 0;
                }
                expectEquals(menus, 1);
                if (! withFiles)
                    snapshot(*editor, "Tonwerk Analog iPad");
                return shown;
            };
            const bool before = tonwerkui::soundFilesInHeader();
            expectEquals(buttons(true), 4);
            expectEquals(buttons(false), 0);
            tonwerkui::soundFilesInHeader() = before;
        }

        beginTest("Tonwerk Analog's mix ends the last row, and the envelopes share the middle one");
        {
            TonwerkSynthProcessor analog(Edition::analog);
            std::unique_ptr<juce::AudioProcessorEditor> editor(analog.createEditor());
            auto* synthEditor = tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get());
            std::vector<juce::Rectangle<int>> panels;
            for (auto* child : synthEditor->getChildren())
                if (dynamic_cast<tonwerkui::Section*>(child) != nullptr && child->isVisible())
                    panels.push_back(child->getBounds());
            expectEquals((int) panels.size(), 10);
            std::sort(panels.begin(), panels.end(), [](const auto& a, const auto& b) {
                return a.getY() != b.getY() ? a.getY() < b.getY() : a.getX() < b.getX();
            });
            // Rows of four, three and three panels; no two overlap, and the rows have air between them.
            expectEquals(panels[3].getY(), panels[0].getY());
            expectEquals(panels[6].getY(), panels[4].getY());
            expectEquals(panels[9].getY(), panels[7].getY());
            expect(panels[4].getY() > panels[0].getBottom() + tonwerkui::kGap);
            expect(panels[7].getY() > panels[4].getBottom() + tonwerkui::kGap);
            // The two envelopes, with their drawings, take most of the middle row.
            expect(panels[4].getWidth() + panels[5].getWidth() > synthEditor->getWidth() * 3 / 4);
            expectEquals(panels[9].getRight(), panels[3].getRight());
        }

        beginTest("Tonwerk Analog has pages for a phone's screen, every knob the same size");
        {
            TonwerkSynthProcessor analog(Edition::analog), combined(Edition::combined);
            // A page names its parameters by id; one the plugin lacks would be a knob that moves nothing.
            int knobs = 0;
            for (const auto& page : analogPages())
                for (const auto& panel : page.panels)
                    for (const auto& item : panel.row)
                    {
                        ++knobs;
                        expect(analog.state.getParameter(item.id) != nullptr, "no parameter " + item.id);
                        if (item.when.isNotEmpty())
                            expect(analog.state.getParameter(item.when) != nullptr, "no parameter " + item.when);
                    }
            expect(knobs > 40);

            std::unique_ptr<juce::AudioProcessorEditor> editor(analog.createEditor());
            auto* frame = dynamic_cast<tonwerkui::HostFrame*>(editor.get());
            expect(frame != nullptr && frame->hasCompact() && ! frame->isCompact());
            // Tonwerk Synth, with both engines, keeps its one editor at any size.
            std::unique_ptr<juce::AudioProcessorEditor> other(combined.createEditor());
            expect(! dynamic_cast<tonwerkui::HostFrame&>(*other).hasCompact());

            // An iPhone in landscape beside the app's rail, without the keys: the size the pages were cut for.
            frame->setCompact(true);
            frame->setBare(true);
            frame->setSize(666, 381);
            auto* compact = tonwerkui::innerEditor<tonwerkui::CompactEditor>(editor.get());
            expect(compact != nullptr);
            if (compact == nullptr)
                return;
            expect(tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get()) == nullptr);
            juce::Array<juce::TextButton*> tabs;
            for (auto* child : compact->getChildren())
            {
                if (auto* tab = dynamic_cast<juce::TextButton*>(child); tab != nullptr && tab->getRadioGroupId() != 0)
                    tabs.add(tab);
                // Bare, the app has the sounds in its own list: the header's menu is gone.
                if (dynamic_cast<juce::ComboBox*>(child) != nullptr)
                    expect(! child->isVisible());
            }
            expectEquals(tabs.size(), (int) analogPages().size());
            float smallest = 1.0e9f, largest = 0.0f;
            for (auto* tab : tabs)
            {
                tab->onClick();
                smallest = std::min(smallest, compact->slotWidth());
                largest = std::max(largest, compact->slotWidth());
                snapshot(*editor, "Tonwerk Analog Seite " + tab->getButtonText());
            }
            logMessage("A knob's place on the pages: " + juce::String(smallest, 1) + " points");
            // Marcel's least on his iPhone, and one size on every page.
            expectGreaterOrEqual(smallest, 60.0f * tonwerkui::CompactEditor::kSmallest);
            expectWithinAbsoluteError(largest, smallest, 0.01f);

            // With its header (an AUv3 in a small host view) the sounds are back.
            frame->setBare(false);
            int menus = 0;
            for (auto* child : tonwerkui::innerEditor<tonwerkui::CompactEditor>(editor.get())->getChildren())
                menus += dynamic_cast<juce::ComboBox*>(child) != nullptr && child->isVisible() ? 1 : 0;
            expectEquals(menus, 1);

            frame->setCompact(false);
            expect(tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get()) != nullptr);
        }

        beginTest("Tonwerk FM has pages for a phone's screen: the algorithm above Haupt, the operators behind one tab");
        {
            TonwerkSynthProcessor fm(Edition::fm);
            // A page names its parameters by id; one the plugin lacks would be a knob that moves nothing.
            std::set<juce::String> named;
            const auto count = [&](const tonwerkui::CompactPage& page) {
                int places = 0;
                for (const auto& panel : page.panels)
                    for (const auto& item : panel.row)
                    {
                        named.insert(item.id);
                        // Of two controls sharing a place (a rate or its note value) only one shows.
                        places += item.when.isNotEmpty() && item.whenOn ? 0 : 1;
                        expect(fm.state.getParameter(item.id) != nullptr, "no parameter " + item.id);
                        if (item.when.isNotEmpty())
                            expect(fm.state.getParameter(item.when) != nullptr, "no parameter " + item.when);
                    }
                // More than ten places made Tonwerk Analog's knobs smaller on every page.
                expectLessOrEqual(places, 10, page.title);
            };
            int chosen = 0;
            for (const auto& page : fmPages(fm.state))
            {
                count(page);
                for (const auto& choice : page.choices)
                    count(choice);
                chosen += (int) page.choices.size();
            }
            expectEquals(chosen, 4);
            // Nothing of the full editor is out of reach on a phone.
            for (auto* parameter : fm.getParameters())
                if (auto* with = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                    expect(named.count(with->paramID) == 1, "no page has " + with->paramID);

            std::unique_ptr<juce::AudioProcessorEditor> editor(fm.createEditor());
            auto* frame = dynamic_cast<tonwerkui::HostFrame*>(editor.get());
            expect(frame != nullptr && frame->hasCompact() && ! frame->isCompact());
            if (frame == nullptr)
                return;
            // An iPhone in landscape beside the app's rail, without the keys: the size Tonwerk Analog's pages have.
            frame->setCompact(true);
            frame->setBare(true);
            frame->setSize(666, 381);
            auto* compact = tonwerkui::innerEditor<tonwerkui::CompactEditor>(editor.get());
            expect(compact != nullptr && tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get()) == nullptr);
            if (compact == nullptr)
                return;
            std::function<juce::Component*(juce::Component&, const std::function<bool(juce::Component&)>&)> shown =
                [&shown](juce::Component& c, const std::function<bool(juce::Component&)>& is) -> juce::Component* {
                if (! c.isVisible())
                    return nullptr;
                if (is(c))
                    return &c;
                for (auto* child : c.getChildren())
                    if (auto* found = shown(*child, is))
                        return found;
                return nullptr;
            };
            const auto isChart = [](juce::Component& c) { return dynamic_cast<AlgorithmView*>(&c) != nullptr; };
            const auto isEnvelope = [](juce::Component& c) { return dynamic_cast<EnvelopeView*>(&c) != nullptr; };
            float smallest = 1.0e9f, largest = 0.0f;
            int charts = 0, envelopes = 0;
            std::vector<juce::TextButton*> tabList;
            for (auto* child : compact->getChildren())
                if (auto* tab = dynamic_cast<juce::TextButton*>(child); tab != nullptr && tab->getRadioGroupId() != 0)
                    tabList.push_back(tab);
            expectEquals((int) tabList.size(), (int) fmPages(fm.state).size());
            for (auto* tab : tabList)
            {
                tab->onClick();
                const auto measure = [&] {
                    smallest = std::min(smallest, compact->slotWidth());
                    largest = std::max(largest, compact->slotWidth());
                };
                const auto choices = compact->shownChoices();
                if (choices.empty())
                {
                    measure();
                    if (auto* chart = shown(*compact, isChart))
                    {
                        ++charts;
                        // Above the knobs, across the page, high enough for the four boxes and their lines.
                        expectGreaterOrEqual(chart->getHeight(), 110);
                        expectGreaterOrEqual(chart->getWidth(), 600);
                    }
                    snapshot(*editor, "Tonwerk FM Seite " + tab->getButtonText());
                    continue;
                }
                expectEquals((int) choices.size(), 4);
                for (std::size_t i = 0; i < choices.size(); ++i)
                {
                    choices[i]->onClick();
                    measure();
                    expect(choices[i]->getToggleState());
                    if (auto* envelope = shown(*compact, isEnvelope))
                    {
                        ++envelopes;
                        expectGreaterOrEqual(envelope->getHeight(), 90);
                    }
                    snapshot(*editor, "Tonwerk FM Seite " + tab->getButtonText() + " " + choices[i]->getButtonText());
                }
            }
            expectEquals(charts, 1);
            expectEquals(envelopes, 4);
            logMessage("A knob's place on the pages: " + juce::String(smallest, 1) + " points");
            // One size on every page, and Tonwerk Analog's on the same screen (78 points).
            expectWithinAbsoluteError(largest, smallest, 0.01f);
            expectGreaterOrEqual(smallest, 78.0f);
            frame->setCompact(false);
            expect(tonwerkui::innerEditor<TonwerkSynthEditor>(editor.get()) != nullptr);
        }
    }
};

static EditionTests editionTests;
