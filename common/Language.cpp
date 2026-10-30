#include "Language.h"

#include "English.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace tonwerkui
{
namespace
{
juce::File choiceFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile("Tonwerk")
        .getChildFile("language.txt");
}

/** English when the system's language is anything but German. */
Language systemLanguage()
{
    const auto shown = juce::SystemStats::getDisplayLanguage().toLowerCase();
    if (shown.isNotEmpty())
        return shown.startsWith("de") ? Language::german : Language::english;
    return juce::SystemStats::getUserLanguage().toLowerCase().startsWith("de") ? Language::german : Language::english;
}

/** The saved choice, the environment's or the system's language, in that order. */
Language startingLanguage(bool& forced)
{
    const auto environment = juce::SystemStats::getEnvironmentVariable("TONWERK_LANGUAGE", {}).toLowerCase();
    forced = environment == "en" || environment == "de";
    if (forced)
        return environment == "de" ? Language::german : Language::english;
    const auto saved = choiceFile().loadFileAsString().trim().toLowerCase();
    if (saved == "de")
        return Language::german;
    if (saved == "en")
        return Language::english;
    return systemLanguage();
}

struct State
{
    State() { current = (int) startingLanguage(pinned); }
    std::atomic<int> current;
    bool pinned = false;
    std::mutex lock;
    std::set<std::string> missing;
};

State& state()
{
    static State instance;
    return instance;
}

struct Tables
{
    Tables()
    {
        for (const auto& entry : english::entries())
        {
            const char* english = entry.english != nullptr ? entry.english : entry.german;
            jassert(toEnglish.count(entry.german) == 0); // each German text once
            toEnglish.emplace(entry.german, english);
            // Two German texts may share an English one; the first stays its way back.
            toGerman.emplace(english, entry.german);
        }
    }
    std::unordered_map<std::string, std::string> toEnglish;
    std::unordered_map<std::string, std::string> toGerman;
};

const Tables& tables()
{
    static const Tables instance;
    return instance;
}

juce::String translate(const std::string& german, bool record = true)
{
    if (language() == Language::german)
        return juce::String::fromUTF8(german.c_str());
    const auto& map = tables().toEnglish;
    if (const auto found = map.find(german); found != map.end())
        return juce::String::fromUTF8(found->second.c_str());
    // Already English (a text made while the plugin spoke English) passes as it is.
    if (record && tables().toGerman.count(german) == 0 && ! german.empty())
    {
        std::lock_guard<std::mutex> guard(state().lock);
        state().missing.insert(german);
    }
    return juce::String::fromUTF8(german.c_str());
}
} // namespace

Language language() { return (Language) state().current.load(); }

void chooseLanguage(Language chosen)
{
    state().current = (int) chosen;
    const auto file = choiceFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(chosen == Language::german ? "de" : "en");
}

bool refreshLanguage()
{
    if (state().pinned)
        return false;
    bool forced = false;
    const auto now = startingLanguage(forced);
    return state().current.exchange((int) now) != (int) now;
}

void forceLanguage(Language forced)
{
    state().pinned = true;
    state().current = (int) forced;
}

juce::String tr(const char* german) { return translate(german); }

juce::String tr(const juce::String& german) { return translate(german.toStdString()); }

juce::StringArray tr(const juce::StringArray& german)
{
    juce::StringArray result;
    for (const auto& text : german)
        result.add(tr(text));
    return result;
}

juce::StringArray tr(std::initializer_list<const char*> german)
{
    juce::StringArray result;
    for (const auto* text : german)
        result.add(tr(text));
    return result;
}

juce::String retranslate(const juce::String& text)
{
    const auto key = text.toStdString();
    // Menu entries such as note values are the same in both languages and need no entry.
    if (language() == Language::english)
        return translate(key, false);
    const auto& map = tables().toGerman;
    if (const auto found = map.find(key); found != map.end())
        return juce::String::fromUTF8(found->second.c_str());
    return text;
}

std::set<std::string> untranslated()
{
    std::lock_guard<std::mutex> guard(state().lock);
    return state().missing;
}

void clearUntranslated()
{
    std::lock_guard<std::mutex> guard(state().lock);
    state().missing.clear();
}

bool isEnglishText(const juce::String& text) { return tables().toGerman.count(text.toStdString()) > 0; }
} // namespace tonwerkui
