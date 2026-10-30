#include "ArpProcessor.h"
#include "Language.h"
#include "LfoProcessor.h"
#include "RigCatalogue.h"
#include "RobovoxProcessor.h"
#include "VocoderProcessor.h"

#include <juce_core/juce_core.h>

#include <map>

namespace
{
/** The German texts that change in English, so a shown text equal to one of them was never translated. */
const std::map<juce::String, juce::String>& germanKeys()
{
    static const auto keys = [] {
        std::map<juce::String, juce::String> m;
        for (const auto& t : tonwerkui::englishTexts())
            m.emplace(juce::String::fromUTF8(t.german), juce::String::fromUTF8(t.english));
        return m;
    }();
    return keys;
}

/** Why `text` reads German: an umlaut, or the whole text or one of its words a German key. Empty when it reads English. */
juce::String germanIn(const juce::String& text)
{
    if (text.containsAnyOf(juce::String::fromUTF8("äöüÄÖÜß")))
        return "umlaut";
    const auto& keys = germanKeys();
    if (keys.count(text.trim()) > 0)
        return "\"" + text.trim() + "\"";
    juce::StringArray words;
    words.addTokens(text, " ·/:,()+&-", "");
    for (const auto& word : words)
        if (word.length() > 1 && keys.count(word) > 0 && keys.at(word) != word)
            return "\"" + word + "\"";
    return {};
}

struct Made
{
    juce::String name;
    std::unique_ptr<juce::AudioProcessor> processor;
};

std::vector<Made> everyPlugin()
{
    std::vector<Made> all;
    for (const auto* kinds : { &tonwerkrig::instruments(), &tonwerkrig::effects() })
        for (const auto& kind : *kinds)
            all.push_back({ kind.name, kind.make() });
    all.push_back({ "Tonwerk Robovox", std::make_unique<tonwerkrobovox::RobovoxProcessor>() });
    all.push_back({ "Tonwerk Vocoder", std::make_unique<tonwerkvocoder::VocoderProcessor>() });
    all.push_back({ "Tonwerk Arp", std::make_unique<tonwerkarp::ArpProcessor>() });
    all.push_back({ "Tonwerk LFO", std::make_unique<tonwerklfo::LfoProcessor>() });
    return all;
}

/** Every text a component and its children show as labels, buttons, menus and tooltips. */
void collectTexts(juce::Component& component, juce::StringArray& texts, const juce::StringArray& programs)
{
    if (auto* label = dynamic_cast<juce::Label*>(&component))
        texts.add(label->getText());
    if (auto* button = dynamic_cast<juce::Button*>(&component))
        texts.add(button->getButtonText());
    if (auto* box = dynamic_cast<juce::ComboBox*>(&component))
    {
        texts.add(box->getTextWhenNothingSelected());
        // Preset names are names: they stay as written in every language.
        for (int i = 0; i < box->getNumItems(); ++i)
            if (!programs.contains(box->getItemText(i)))
                texts.add(box->getItemText(i));
    }
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&component))
        texts.add(client->getTooltip());
    for (auto* child : component.getChildren())
        collectTexts(*child, texts, programs);
}

class LanguageTests : public juce::UnitTest
{
public:
    LanguageTests() : juce::UnitTest("Language", "Language") {}

    void runTest() override
    {
        beginTest("English texts are unique and read English");
        {
            std::map<juce::String, int> seen;
            for (const auto& t : tonwerkui::englishTexts())
            {
                const auto german = juce::String::fromUTF8(t.german), english = juce::String::fromUTF8(t.english);
                expect(++seen[german] == 1, "twice: " + german);
                expect(!english.containsAnyOf(juce::String::fromUTF8("äöüÄÖÜß")), "umlaut in " + english);
                // A text may be translated twice (a title, then its panel); that must leave the English as it is.
                const auto again = germanKeys().find(english);
                expect(again == germanKeys().end() || again->second == english, german + " -> " + english + " -> again");
            }
        }

        const bool wasEnglish = tonwerkui::english();
        tonwerkui::setEnglish(true);
        beginTest("Every plugin's parameters and editor speak English");
        {
            for (auto& made : everyPlugin())
            {
                auto& processor = *made.processor;
                for (auto* parameter : processor.getParameters())
                {
                    const auto parameterName = parameter->getName(100);
                    const auto why = germanIn(parameterName);
                    expect(why.isEmpty(), made.name + ": parameter " + parameterName + " (" + why + ")");
                    const int steps = juce::jlimit(2, 64, parameter->getNumSteps());
                    for (int i = 0; i < steps && steps < 64; ++i)
                    {
                        const auto text = parameter->getText((float) i / (float) (steps - 1), 100);
                        const auto reason = germanIn(text + " " + parameter->getLabel());
                        expect(reason.isEmpty(), made.name + ": " + parameterName + " shows " + text + " (" + reason + ")");
                    }
                }
                if (!processor.hasEditor())
                    continue;
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
                // For a look at them: TONWERK_EDITOR_PNG_DIR=/path writes every editor in English.
                if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
                    dir.isNotEmpty())
                {
                    const auto file = juce::File(dir).getChildFile(made.name.replaceCharacter(' ', '-').toLowerCase()
                                                                   + "-en.png");
                    file.deleteFile();
                    juce::FileOutputStream stream(file);
                    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                                              stream);
                }
                juce::StringArray texts, programs;
                for (int i = 0; i < processor.getNumPrograms(); ++i)
                    programs.add(processor.getProgramName(i));
                collectTexts(*editor, texts, programs);
                for (const auto& text : texts)
                {
                    const auto why = germanIn(text);
                    expect(why.isEmpty(), made.name + ": editor shows " + text + " (" + why + ")");
                }
            }
        }

        beginTest("German stays German");
        {
            tonwerkui::setEnglish(false);
            expectEquals(tonwerkui::tr("Zeit"), juce::String("Zeit"));
            expectEquals(tonwerkui::tr("Sättigung"), juce::String::fromUTF8("Sättigung"));
            tonwerkui::setEnglish(true);
            expectEquals(tonwerkui::tr("Sättigung"), juce::String("Saturation"));
            expectEquals(tonwerkui::tr("Kein Eintrag"), juce::String("Kein Eintrag"));
        }
        tonwerkui::setEnglish(wasEnglish);
    }
};

LanguageTests languageTests;
} // namespace
