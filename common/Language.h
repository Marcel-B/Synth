#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>
#include <set>
#include <string>

/**
 * The plugins' language: English unless the system speaks German, or what the user chose with the editors' DE/EN
 * switch. The choice is a file in Application Support/Tonwerk shared by all plugins (in the iPad's and the Mac App
 * Store's sandbox each plugin has its own).
 *
 * The code keeps writing its texts in German; `tr` looks the English up in one table (`English.cpp`) shared by all
 * plugins. A text the table lacks is shown as written and remembered (`untranslated`), so the tests find it.
 */
namespace tonwerkui
{
enum class Language
{
    german,
    english
};

/** The language the plugins speak now. Read once from the saved choice or the system. */
Language language();

/** Saves `chosen` for every plugin and switches to it. */
void chooseLanguage(Language chosen);

/** Reads the saved choice again (another plugin may have changed it); true when the language changed. */
bool refreshLanguage();

/**
 * For tests and screenshots: speak `forced` without saving it. `TONWERK_LANGUAGE=en|de` in the environment does the
 * same from the start.
 */
void forceLanguage(Language forced);

/** A German text (UTF-8) in the current language. */
juce::String tr(const char* german);
juce::String tr(const juce::String& german);

/** Each of `german` in the current language. */
juce::StringArray tr(const juce::StringArray& german);
juce::StringArray tr(std::initializer_list<const char*> german);

/**
 * A name for a function that takes both: a German literal is translated, a juce::String (already in the current
 * language, or put together from translated parts) is taken as it is.
 */
struct Localised
{
    Localised(const char* german) : text(tr(german)) {}
    Localised(juce::String ready) : text(std::move(ready)) {}
    operator const juce::String&() const { return text; }
    friend juce::String operator+(const Localised& a, const char* b) { return a.text + b; }
    friend juce::String operator+(const Localised& a, const juce::String& b) { return a.text + b; }
    juce::String text;
};

/**
 * A text written in either language in the current one: for menu entries and names a processor made before the
 * language changed.
 */
juce::String retranslate(const juce::String& text);

/** The German texts asked for in English that the table lacks, since the start or `clearUntranslated`. */
std::set<std::string> untranslated();
void clearUntranslated();

/** Every English text of the table, for tests that check what an editor shows. */
bool isEnglishText(const juce::String& text);

/**
 * For tests: the first word of `text` that the table knows as German with another English ("Form" for "Shape"), so a
 * name put together from untranslated parts is found; empty when there is none.
 */
juce::String germanWordIn(const juce::String& text);
} // namespace tonwerkui
