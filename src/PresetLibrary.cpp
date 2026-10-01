#include "PresetLibrary.h"

namespace tonwerk
{
PresetLibrary::PresetLibrary(juce::File presetFolder) : folder(std::move(presetFolder)) { reload(); }

juce::File PresetLibrary::defaultFolder()
{
    // ~/Library/Application Support/Tonwerk Synth on the Mac, ~/.config/Tonwerk Synth on Linux.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile("Tonwerk Synth");
}

juce::Array<PresetLibrary::Preset> PresetLibrary::factoryPresets()
{
    struct Entry
    {
        const char* name;
        Kind kind;
    };
    // The names say what Tonwerk plays them on: lead for the melody, pads for chords, and the guide tones.
    const Entry analog[] = { { "Analog Lead", Kind::melody },
                             { "Analog Pad", Kind::chords },
                             { "Analog Bass", Kind::bass },
                             { "Analog Leitton", Kind::guideTones } };
    const Entry fm[] = { { "FM Blech", Kind::melody },
                         { "FM E-Piano", Kind::chords },
                         { "FM Bass", Kind::bass },
                         { "FM Leitton", Kind::guideTones } };
    juce::Array<Preset> result;
    for (const auto& entry : analog)
    {
        Patch patch;
        patch.engine = Engine::analog;
        patch.analog = defaultAnalog(entry.kind);
        result.add({ entry.name, patchToJson(patch), "factory" });
    }
    for (const auto& entry : fm)
    {
        Patch patch;
        patch.engine = Engine::fm;
        patch.fm = defaultFm(entry.kind);
        result.add({ entry.name, patchToJson(patch), "factory" });
    }
    return result;
}

void PresetLibrary::reload()
{
    presets.clear();
    const auto json = juce::JSON::parse(presetFile());
    if (! json.isArray())
        return;
    for (const auto& entry : *json.getArray())
    {
        const auto name = entry.getProperty("name", {}).toString();
        const auto patch = entry.getProperty("patch", {});
        if (name.isNotEmpty() && patch.isObject())
            presets.add({ name, patch, entry.getProperty("source", "file").toString() });
    }
}

void PresetLibrary::save() const
{
    juce::Array<juce::var> list;
    for (const auto& preset : presets)
    {
        auto entry = new juce::DynamicObject();
        entry->setProperty("name", preset.name);
        entry->setProperty("source", preset.source);
        entry->setProperty("patch", preset.patch);
        list.add(entry);
    }
    folder.createDirectory();
    presetFile().replaceWithText(juce::JSON::toString(juce::var(list)));
}

int PresetLibrary::add(const juce::Array<NamedPatch>& sounds, const juce::String& source)
{
    reload();
    for (const auto& sound : sounds)
    {
        Preset preset { sound.name, sound.patch, source };
        bool replaced = false;
        for (auto& existing : presets)
        {
            if (existing.name.equalsIgnoreCase(sound.name))
            {
                existing = preset;
                replaced = true;
                break;
            }
        }
        if (! replaced)
            presets.add(preset);
    }
    std::sort(presets.begin(), presets.end(), [](const Preset& a, const Preset& b) {
        return a.name.compareNatural(b.name) < 0;
    });
    save();
    return sounds.size();
}

bool PresetLibrary::remove(const juce::String& name)
{
    reload();
    for (int i = 0; i < presets.size(); ++i)
    {
        if (presets.getReference(i).name.equalsIgnoreCase(name))
        {
            presets.remove(i);
            save();
            return true;
        }
    }
    return false;
}

juce::String PresetLibrary::tonwerkUrl() const
{
    return juce::JSON::parse(settingsFile()).getProperty("tonwerkUrl", {}).toString();
}

void PresetLibrary::setTonwerkUrl(const juce::String& url)
{
    auto settings = new juce::DynamicObject();
    settings->setProperty("tonwerkUrl", url.trim());
    folder.createDirectory();
    settingsFile().replaceWithText(juce::JSON::toString(juce::var(settings)));
}

juce::Result PresetLibrary::fetchFromTonwerk(const juce::String& baseUrl, juce::Array<NamedPatch>& sounds, int timeoutMs)
{
    auto base = baseUrl.trim();
    while (base.endsWithChar('/'))
        base = base.dropLastCharacters(1);
    if (! base.startsWithIgnoreCase("http://") && ! base.startsWithIgnoreCase("https://"))
        return juce::Result::fail("Die Adresse muss mit https:// oder http:// beginnen.");

    int status = 0;
    const juce::URL url(base + "/api/logic/synths/presets");
    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                            .withConnectionTimeoutMs(timeoutMs)
                                            .withStatusCode(&status));
    if (stream == nullptr)
        return juce::Result::fail("Tonwerk ist unter " + base + " nicht erreichbar.");
    const auto text = stream->readEntireStreamAsString();
    if (status != 200)
        return juce::Result::fail("Tonwerk antwortet mit Status " + juce::String(status) + ".");
    if (! juce::JSON::parse(text).isArray())
        return juce::Result::fail(juce::String::fromUTF8("Die Antwort ist keine Liste von Klängen. Ist das die Adresse von Tonwerk?"));
    sounds = namedPatchesFromJson(text, {});
    return juce::Result::ok();
}
} // namespace tonwerk
