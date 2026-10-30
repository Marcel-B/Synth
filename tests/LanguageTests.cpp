#include "AdditiveProcessor.h"
#include "BassProcessor.h"
#include "DxProcessor.h"
#include "ExpanderProcessor.h"
#include "GranularProcessor.h"
#include "GrooveProcessor.h"
#include "NeuroProcessor.h"
#include "PdProcessor.h"
#include "PhysicalProcessor.h"
#include "PluginProcessor.h"
#include "SamplerProcessor.h"
#include "English.h"
#include "Language.h"
#include "Platform.h"
#include "WavetableProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <regex>
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
        { "Tonwerk Additiv", [] { return std::make_unique<tonwerkadd::AdditiveProcessor>(); } },
        { "Tonwerk Bass", [] { return std::make_unique<tonwerkbass::BassProcessor>(); } },
        { "Tonwerk DX", [] { return std::make_unique<tonwerkdx::DxProcessor>(); } },
        { "Tonwerk Expander", [] { return std::make_unique<tonwerkexpander::ExpanderProcessor>(); } },
        { "Tonwerk Granular", [] { return std::make_unique<tonwerkgrain::GranularProcessor>(); } },
        { "Tonwerk Groovebox", [] { return std::make_unique<tonwerkgroove::GrooveProcessor>(); } },
        { "Tonwerk Neuro", [] { return std::make_unique<tonwerkneuro::NeuroProcessor>(); } },
        { "Tonwerk PD", [] { return std::make_unique<tonwerkpd::PdProcessor>(); } },
        { "Tonwerk Physical", [] { return std::make_unique<tonwerkphys::PhysicalProcessor>(); } },
        { "Tonwerk Sampler", [] { return std::make_unique<tonwerksampler::SamplerProcessor>(); } },
        { "Tonwerk Synth", [] { return std::make_unique<tonwerk::TonwerkSynthProcessor>(tonwerk::Edition::combined); } },
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

        beginTest("Every text the sources hand to tr() has its English");
        {
            // Texts on paths no test walks (a failed file, a message) are found here, by reading the code.
            std::set<std::string> known;
            for (const auto& entry : tonwerkui::english::entries())
                known.insert(entry.german);
            const std::regex call(R"re(\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\))re");
            const std::regex piece(R"re("((?:[^"\\]|\\.)*)")re");
            const juce::File root(TONWERK_SOURCE_DIR);
            int calls = 0;
            for (const auto& file : root.findChildFiles(juce::File::findFiles, true, "*.cpp;*.h"))
            {
                const auto path = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
                if (path.startsWith("build") || path.startsWith("esp32-wavetable") || path.startsWith("tests/")
                    || path.startsWith("common/Language"))
                    continue;
                const auto code = file.loadFileAsString().toStdString();
                for (std::sregex_iterator it(code.begin(), code.end(), call), end; it != end; ++it)
                {
                    std::string text;
                    const auto literals = (*it)[1].str();
                    for (std::sregex_iterator p(literals.begin(), literals.end(), piece), e; p != e; ++p)
                        text += (*p)[1].str();
                    ++calls;
                    expect(text.empty() || known.count(text) > 0,
                           path + ": no English for \"" + juce::String::fromUTF8(text.c_str()) + "\"");
                }
            }
            expect(calls > 100);
        }

        for (const auto& [plugin, make] : plugins())
        {
            beginTest(juce::String("Every text of ") + plugin + " in English");
            // Ids are what projects keep: no language may touch them.
            juce::StringArray germanIds;
            {
                Speaking german(Language::german);
                const auto inGerman = make();
                for (auto* parameter : inGerman->getParameters())
                    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                        germanIds.add(withId->getParameterID());
            }
            Speaking english(Language::english);
            tonwerkui::clearUntranslated();
            auto processor = make();
            juce::StringArray texts;
            juce::StringArray englishIds;
            for (auto* parameter : processor->getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                    englishIds.add(withId->getParameterID());
            expect(englishIds == germanIds, juce::String(plugin) + ": parameter ids differ between the languages");
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
                {
                    expect(! looksGerman(text), juce::String(plugin) + ": still German: " + text);
                    const auto word = tonwerkui::germanWordIn(text);
                    expect(word.isEmpty(), juce::String(plugin) + ": \"" + word + "\" untranslated in " + text);
                }
        }
    }
};

static LanguageTests languageTests;
