#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/**
 * The plugins speak German or English: German when the system's language is German, English otherwise. Texts are
 * written in German in the sources and pass `tr` where they are shown (parameter names and choices, panel titles,
 * labels, menus, messages); `englishTexts()` (Translations.cpp) holds their English.
 */
namespace tonwerkui
{
struct Translation
{
    const char* german;
    const char* english;
};

const std::vector<Translation>& englishTexts();

/** True when the texts are shown in English. `TONWERK_LANGUAGE` (de, en) overrides the system's language. */
bool english();
/** Overrides the language for the rest of the process (tests, screenshots). */
void setEnglish(bool);

/** A UTF-8 text in the plugins' language; a text without an English entry comes back as it is. */
juce::String tr(const char* text);
juce::String tr(const juce::String& text);
juce::StringArray tr(const juce::StringArray& texts);

/** A parameter's text function whose texts (a value with its unit, "Aus") come out in the plugins' language. */
template <typename Function>
auto translated(Function function)
{
    return [function](auto value, int length) { return tr(juce::String(function(value, length))); };
}
} // namespace tonwerkui
