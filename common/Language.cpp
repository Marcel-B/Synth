#include "Language.h"

#include <atomic>
#include <string>
#include <unordered_map>

namespace tonwerkui
{
namespace
{
bool systemIsGerman()
{
    const auto chosen = juce::SystemStats::getEnvironmentVariable("TONWERK_LANGUAGE", {});
    if (chosen.isNotEmpty())
        return chosen.startsWithIgnoreCase("de");
    // The display language follows the user's preferred languages (macOS, iOS, Windows), the locale otherwise.
    return juce::SystemStats::getDisplayLanguage().startsWithIgnoreCase("de")
           || juce::SystemStats::getUserLanguage().startsWithIgnoreCase("de");
}

std::atomic<int>& chosen()
{
    // -1 until asked: decided once, so a plugin never changes its language while open.
    static std::atomic<int> language { -1 };
    return language;
}

const std::unordered_map<std::string, juce::String>& table()
{
    static const auto map = [] {
        std::unordered_map<std::string, juce::String> m;
        for (const auto& t : englishTexts())
            m.emplace(t.german, juce::String::fromUTF8(t.english));
        return m;
    }();
    return map;
}
} // namespace

bool english()
{
    int language = chosen().load();
    if (language < 0)
    {
        language = systemIsGerman() ? 0 : 1;
        chosen().store(language);
    }
    return language == 1;
}

void setEnglish(bool on) { chosen().store(on ? 1 : 0); }

juce::String tr(const char* text)
{
    if (text == nullptr)
        return {};
    return tr(juce::String::fromUTF8(text));
}

juce::String tr(const juce::String& text)
{
    if (!english())
        return text;
    const auto& known = table();
    if (const auto found = known.find(text.toStdString()); found != known.end())
        return found->second;
    // Names put together from pieces ("Osz 1" + " Welle", a value and its unit) are not in the table whole:
    // their words are, so the longest run of words that is a text takes its English.
    juce::StringArray words;
    words.addTokens(text, " ", "");
    if (words.size() < 2)
        return text;
    juce::StringArray out;
    for (int i = 0; i < words.size();)
    {
        int taken = 1;
        juce::String english = words[i];
        for (int j = words.size(); j > i; --j)
        {
            const auto run = words.joinIntoString(" ", i, j - i);
            if (const auto found = known.find(run.toStdString()); found != known.end())
            {
                english = found->second;
                taken = j - i;
                break;
            }
        }
        out.add(english);
        i += taken;
    }
    return out.joinIntoString(" ");
}

juce::StringArray tr(const juce::StringArray& texts)
{
    if (!english())
        return texts;
    juce::StringArray translated;
    for (const auto& text : texts)
        translated.add(tr(text));
    return translated;
}
} // namespace tonwerkui
