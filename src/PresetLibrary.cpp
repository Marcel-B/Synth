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

namespace
{
Envelope env(float attack, float decay, float sustain, float release) { return { attack, decay, sustain, release }; }

Operator op(float ratio, float level, Envelope envelope, float velocity = 0.7f)
{
    return { ratio, 0.0f, level, velocity, envelope };
}

/** Sounds of the plugin's own, beyond Tonwerk's four per engine; they also show the wavefolder, S&H and sync. */
std::vector<std::pair<const char*, Patch>> ownAnalog()
{
    std::vector<std::pair<const char*, Patch>> result;
    auto add = [&result](const char* name, auto shape) {
        Patch p;
        p.engine = Engine::analog;
        p.analog = defaultAnalog(Kind::bass);
        p.analog.lfo.depth = 0.0f;
        shape(p);
        result.emplace_back(name, p);
    };
    add("Analog Acid", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.8f };
        a.osc2.level = 0.0f;
        a.filter = { FilterType::lowpass, 320.0f, 14.0f, 3.5f, 0.5f };
        a.filterEnv = env(0.002f, 0.18f, 0.0f, 0.1f);
        a.ampEnv = env(0.002f, 0.25f, 0.8f, 0.06f);
        a.volume = 0.6f;
        p.fx.delay = { 0.2f, 0.35f, 0.3f, 3000.0f, true, 10 };
    });
    add("Analog Wobble", [](Patch& p) {
        auto& a = p.analog;
        a.osc2 = { Wave::square, -1, 0.0f, 0.6f };
        a.filter = { FilterType::lowpass, 500.0f, 8.0f, 0.5f, 0.3f };
        a.ampEnv = env(0.005f, 0.3f, 0.9f, 0.1f);
        a.lfo = { Wave::sine, 4.0f, AnalogLfoTarget::filter, 0.9f, true, 10 };
        a.fold.amount = 0.25f;
        a.volume = 0.6f;
    });
    add("Analog Falt-Lead", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::triangle, 0, 0.0f, 0.8f };
        a.osc2 = { Wave::sine, 1, 0.0f, 0.3f };
        a.fold = { 0.6f, 0.2f, 0.4f };
        a.filter = { FilterType::lowpass, 5000.0f, 2.0f, 1.0f, 0.5f };
        a.filterEnv = env(0.005f, 0.6f, 0.3f, 0.3f);
        a.ampEnv = env(0.01f, 0.4f, 0.8f, 0.25f);
        a.lfo = { Wave::sine, 5.5f, AnalogLfoTarget::pitch, 0.1f };
        a.volume = 0.55f;
        p.fx.delay = { 0.25f, 0.35f, 0.35f, 4000.0f, true, kDottedEighth };
        p.fx.reverb = { 0.2f, 2.5f };
    });
    add("Analog Zufall", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::square, 0, 0.0f, 0.7f, 0.3f };
        a.osc2 = { Wave::sawtooth, 0, 7.0f, 0.4f };
        a.filter = { FilterType::lowpass, 1200.0f, 10.0f, 1.0f, 0.5f };
        a.filterEnv = env(0.005f, 0.2f, 0.2f, 0.2f);
        a.ampEnv = env(0.005f, 0.2f, 0.7f, 0.2f);
        a.sampleHold.sync = true;
        a.sampleHold.division = kSixteenth;
        a.sampleHold.filter = 0.6f;
        a.volume = 0.55f;
        p.fx.reverb = { 0.25f, 2.0f };
    });
    add("Analog Streicher", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::square, 0, 0.0f, 0.6f, 0.5f, 0.4f, true };
        a.osc2 = { Wave::sawtooth, 0, 6.0f, 0.5f };
        a.filter = { FilterType::lowpass, 2200.0f, 1.0f, 0.5f, 0.4f };
        a.filterEnv = env(0.6f, 1.5f, 0.7f, 1.2f);
        a.ampEnv = env(0.5f, 1.0f, 0.85f, 1.2f);
        a.lfo = { Wave::triangle, 0.8f, AnalogLfoTarget::pitch, 0.08f };
        a.volume = 0.45f;
        p.fx.reverb = { 0.35f, 3.5f };
    });
    return result;
}

std::vector<std::pair<const char*, Patch>> ownFm()
{
    std::vector<std::pair<const char*, Patch>> result;
    auto add = [&result](const char* name, auto shape) {
        Patch p;
        p.engine = Engine::fm;
        p.fm.lfo.depth = 0.0f;
        shape(p);
        result.emplace_back(name, p);
    };
    add("FM Glocke", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        p.fm.ops = { op(1.0f, 0.7f, env(0.002f, 4.0f, 0.0f, 3.0f)), op(3.5f, 0.45f, env(0.002f, 2.0f, 0.0f, 2.0f)),
                     op(2.0f, 0.35f, env(0.002f, 3.0f, 0.0f, 2.5f)), op(7.0f, 0.25f, env(0.002f, 1.0f, 0.0f, 1.0f)) };
        p.fm.volume = 0.55f;
        p.fx.reverb = { 0.3f, 3.0f };
    });
    add("FM Marimba", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        p.fm.ops = { op(1.0f, 0.8f, env(0.001f, 0.6f, 0.0f, 0.4f)), op(4.0f, 0.35f, env(0.001f, 0.08f, 0.0f, 0.08f)),
                     op(3.0f, 0.25f, env(0.001f, 0.35f, 0.0f, 0.3f)), op(10.0f, 0.2f, env(0.001f, 0.05f, 0.0f, 0.05f)) };
        p.fm.volume = 0.6f;
        p.fx.reverb = { 0.15f, 1.5f };
    });
    add("FM Orgel", [](Patch& p) {
        p.fm.algorithm = 8;
        p.fm.feedback = 0.2f;
        const auto held = env(0.005f, 0.1f, 1.0f, 0.08f);
        p.fm.ops = { op(0.5f, 0.5f, held, 0.2f), op(1.0f, 0.6f, held, 0.2f), op(2.0f, 0.35f, held, 0.2f),
                     op(3.0f, 0.25f, held, 0.2f) };
        p.fm.lfo = { Wave::sine, 6.0f, FmLfoTarget::pitch, 0.06f };
        p.fm.volume = 0.4f;
        p.fx.reverb = { 0.2f, 2.0f };
    });
    add("FM Zupf", [](Patch& p) {
        p.fm.algorithm = 4;
        p.fm.feedback = 0.3f;
        p.fm.ops = { op(1.0f, 0.85f, env(0.001f, 0.9f, 0.0f, 0.3f)), op(2.0f, 0.4f, env(0.001f, 0.3f, 0.0f, 0.2f)),
                     op(1.0f, 0.3f, env(0.001f, 0.2f, 0.0f, 0.1f)), op(5.0f, 0.2f, env(0.001f, 0.1f, 0.0f, 0.1f)) };
        p.fm.volume = 0.6f;
        p.fx.delay = { 0.2f, 0.35f, 0.35f, 4000.0f, true, kDottedEighth };
    });
    return result;
}
} // namespace

juce::Array<PresetLibrary::Preset> PresetLibrary::factoryPresets(Edition edition)
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
    const bool withAnalog = edition != Edition::fm;
    const bool withFm = edition != Edition::analog;
    juce::Array<Preset> result;
    auto add = [&result](const char* name, const Patch& patch) {
        result.add({ juce::String::fromUTF8(name), patchToJson(patch), "factory" });
    };
    if (withAnalog)
    {
        for (const auto& entry : analog)
        {
            Patch patch;
            patch.engine = Engine::analog;
            patch.analog = defaultAnalog(entry.kind);
            add(entry.name, patch);
        }
    }
    if (withFm)
    {
        for (const auto& entry : fm)
        {
            Patch patch;
            patch.engine = Engine::fm;
            patch.fm = defaultFm(entry.kind);
            add(entry.name, patch);
        }
    }
    // After Tonwerk's, so Tonwerk Synth's programs keep their numbers.
    if (withAnalog)
        for (const auto& [name, patch] : ownAnalog())
            add(name, patch);
    if (withFm)
        for (const auto& [name, patch] : ownFm())
            add(name, patch);
    return result;
}

juce::Array<PresetLibrary::Preset> PresetLibrary::importedFor(Edition edition) const
{
    if (edition == Edition::combined)
        return presets;
    const auto engine = edition == Edition::fm ? Engine::fm : Engine::analog;
    juce::Array<Preset> result;
    for (const auto& preset : presets)
        if (patchFromJson(preset.patch).engine == engine)
            result.add(preset);
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
