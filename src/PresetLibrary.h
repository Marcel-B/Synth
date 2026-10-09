#pragma once

#include "Parameters.h"
#include "PatchJson.h"

#include <juce_core/juce_core.h>

namespace tonwerk
{
/**
 * The sounds the preset menu offers: the plugin's own starting sounds (Tonwerk's defaults per kind of track, for both
 * engines) and the ones imported from Tonwerk or from files. Imported sounds live in one JSON file per user
 * (`presets.json` in the plugin's folder under Application Support), so every instance in every project sees them,
 * as Tonwerk's named sounds are shared by all its tracks. A sound imported again under the same name replaces the old
 * one, so fetching from Tonwerk twice keeps one copy.
 */
class PresetLibrary
{
public:
    struct Preset
    {
        juce::String name;
        juce::var patch;
        /** `factory`, `tonwerk` or `file`; only the menu's grouping uses it. */
        juce::String source;
    };

    /** The library in `folder`; the plugin uses `defaultFolder()`, the tests a temporary one. */
    explicit PresetLibrary(juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    /**
     * The edition's own sounds: Tonwerk's starting sounds of its engine, then this plugin's. Tonwerk Synth has both
     * engines', its first eight in their old order.
     */
    static juce::Array<Preset> factoryPresets(Edition edition = Edition::combined);

    /** The imported sounds the edition plays: Tonwerk Analog's menu leaves out the FM ones, and the other way round. */
    juce::Array<Preset> importedFor(Edition edition) const;

    /** The imported sounds, sorted by name, as last loaded from or saved to the file. */
    const juce::Array<Preset>& imported() const { return presets; }

    /** Reads the file again, since another instance may have imported sounds meanwhile. */
    void reload();

    /** Adds or replaces sounds by name (ignoring case) and saves; returns how many there were. */
    int add(const juce::Array<NamedPatch>& sounds, const juce::String& source);

    bool remove(const juce::String& name);

    /** Tonwerk's address, e.g. `https://mac-mini.tailnet.ts.net:8443`, kept beside the presets. */
    juce::String tonwerkUrl() const;
    void setTonwerkUrl(const juce::String& url);

    /**
     * Fetches Tonwerk's named sounds (`GET /api/logic/synths/presets`). Blocks for up to `timeoutMs`, so it runs on a
     * background thread; the error is a sentence for the user.
     */
    static juce::Result fetchFromTonwerk(const juce::String& baseUrl, juce::Array<NamedPatch>& sounds, int timeoutMs = 10000);

private:
    juce::File presetFile() const { return folder.getChildFile("presets.json"); }
    juce::File settingsFile() const { return folder.getChildFile("settings.json"); }
    void save() const;

    juce::File folder;
    juce::Array<Preset> presets;
};
} // namespace tonwerk
