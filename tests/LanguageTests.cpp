#include "English.h"
#include "Language.h"
#include "Platform.h"
#include "WavetableProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace
{
using tonwerkui::Language;

/** Speaks `language` while it lives, then the one before. */
struct Speaking
{
    explicit Speaking(Language language) : before(tonwerkui::language()) { tonwerkui::forceLanguage(language); }
    ~Speaking() { tonwerkui::forceLanguage(before); }
    Language before;
};

/** German's own letters: an English text with one of them was missed. */
bool looksGerman(const juce::String& text) { return text.containsAnyOf(juce::String::fromUTF8("äöüÄÖÜß")); }

/** Every text the components under `root` show: labels, buttons, menu entries, tooltips. */
void collectTexts(juce::Component& root, juce::StringArray& texts)
{
    if (auto* label = dynamic_cast<juce::Label*>(&root))
        texts.add(label->getText());
    if (auto* button = dynamic_cast<juce::Button*>(&root))
        texts.add(button->getButtonText());
    if (auto* box = dynamic_cast<juce::ComboBox*>(&root))
    {
        texts.add(box->getTextWhenNothingSelected());
        for (int i = 0; i < box->getNumItems(); ++i)
            texts.add(box->getItemText(i));
    }
    if (auto* tip = dynamic_cast<juce::TooltipClient*>(&root))
        texts.add(tip->getTooltip());
    for (auto* child : root.getChildren())
        collectTexts(*child, texts);
}

using Make = std::function<std::unique_ptr<juce::AudioProcessor>()>;

/** The plugins whose texts are translated; each new one joins the list. */
std::vector<std::pair<const char*, Make>> plugins()
{
    return {
        { "Tonwerk Wavetable", [] { return std::make_unique<tonwerkwave::WavetableProcessor>(); } },
    };
}
} // namespace

class LanguageTests : public juce::UnitTest
{
public:
    LanguageTests() : juce::UnitTest("Language", "Language") {}

    void runTest() override
    {
        beginTest("The table names each German text once");
        {
            std::set<std::string> seen;
            for (const auto& entry : tonwerkui::english::entries())
                expect(seen.insert(entry.german).second, juce::String::fromUTF8(entry.german) + " twice");
        }

        beginTest("German stays as written, English comes from the table");
        {
            {
                Speaking german(Language::german);
                expectEquals(tonwerkui::tr("Lernen abbrechen"), juce::String("Lernen abbrechen"));
                expectEquals(tonwerkui::retranslate("Cancel learning"), juce::String("Lernen abbrechen"));
            }
            Speaking english(Language::english);
            expectEquals(tonwerkui::tr("Lernen abbrechen"), juce::String("Cancel learning"));
            expectEquals(tonwerkui::retranslate("Lernen abbrechen"), juce::String("Cancel learning"));
            expectEquals(tonwerkui::retranslate("Cancel learning"), juce::String("Cancel learning"));
        }

        for (const auto& [plugin, make] : plugins())
        {
            beginTest(juce::String("Every text of ") + plugin + " in English");
            Speaking english(Language::english);
            tonwerkui::clearUntranslated();
            auto processor = make();
            juce::StringArray texts;
            for (auto* parameter : processor->getParameters())
            {
                texts.add(parameter->getName(100));
                texts.addArray(parameter->getAllValueStrings());
                texts.add(parameter->getText(parameter->getValue(), 100));
            }
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                juce::Image image(juce::Image::RGB, editor->getWidth(), editor->getHeight(), true);
                juce::Graphics g(image);
                editor->paintEntireComponent(g, false);
                collectTexts(*editor, texts);
                if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {});
                    dir.isNotEmpty())
                {
                    juce::FileOutputStream out(juce::File(dir).getChildFile(juce::String(plugin) + " English.png"));
                    if (out.openedOk() && out.setPosition(0) && out.truncate().wasOk())
                        juce::PNGImageFormat().writeImageToStream(image, out);
                }
            }
            // Sound names stay as they are: projects and Tonwerk Rig find sounds by them.
            juce::StringArray programs;
            for (int i = 0; i < processor->getNumPrograms(); ++i)
                programs.add(processor->getProgramName(i));
            for (const auto& missing : tonwerkui::untranslated())
                expect(false, juce::String(plugin) + ": no English for \"" + juce::String::fromUTF8(missing.c_str()) + "\"");
            for (const auto& text : texts)
                if (! programs.contains(text))
                    expect(! looksGerman(text), juce::String(plugin) + ": still German: " + text);
        }
    }
};

static LanguageTests languageTests;
