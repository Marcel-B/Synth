#include "Parameters.h"
#include "PatchJson.h"
#include "PresetLibrary.h"

#include <juce_core/juce_core.h>

using namespace tonwerk;

namespace
{
/** Equal after a trip through JSON or the parameters, which store floats and normalise them. */
bool same(float a, float b) { return std::abs(a - b) <= 1.0e-4f * std::max(1.0f, std::abs(a)); }

bool samePatch(const Patch& a, const Patch& b)
{
    for (const auto& d : descriptors())
        if (! same(d.get(a), d.get(b)))
        {
            DBG(d.id << ": " << d.get(a) << " != " << d.get(b));
            return false;
        }
    return true;
}

/** Tonwerk's FM bass as its browser saves it, with the effects it adds. */
const char* const kTonwerkFm = R"({
  "engine": "fm", "algorithm": 3, "feedback": 0.6,
  "ops": [
    { "ratio": 1, "detune": 0, "level": 0.9, "velocity": 0.7, "env": { "attack": 0.002, "decay": 0.8, "sustain": 0.7, "release": 0.08 } },
    { "ratio": 1, "detune": 0, "level": 0.5, "velocity": 0.7, "env": { "attack": 0.002, "decay": 0.25, "sustain": 0.2, "release": 0.1 } },
    { "ratio": 3, "detune": 0, "level": 0.15, "velocity": 0.7, "env": { "attack": 0.002, "decay": 0.15, "sustain": 0, "release": 0.1 } },
    { "ratio": 1, "detune": 0, "level": 0.3, "velocity": 0.7, "env": { "attack": 0.002, "decay": 0.4, "sustain": 0.3, "release": 0.1 } }
  ],
  "lfo": { "wave": "sine", "rate": 5, "target": "pitch", "depth": 0 },
  "volume": 0.85,
  "fx": { "delay": { "mix": 0.2, "time": 0.3, "feedback": 0.4, "tone": 3000 }, "reverb": { "mix": 0.25, "decay": 2.5 } }
})";
} // namespace

class PatchJsonTests : public juce::UnitTest
{
public:
    PatchJsonTests() : juce::UnitTest("Patch JSON", "Patches") {}

    void runTest() override
    {
        beginTest("Tonwerk's FM patch reads as written");
        const auto fm = patchFromJson(juce::JSON::parse(kTonwerkFm));
        expect(fm.engine == Engine::fm);
        expectEquals(fm.fm.algorithm, 3);
        expect(same(fm.fm.feedback, 0.6f));
        expect(same(fm.fm.ops[2].ratio, 3.0f));
        expect(same(fm.fm.ops[0].env.decay, 0.8f));
        expect(same(fm.fx.reverb.decay, 2.5f));
        expect(same(fm.fx.delay.tone, 3000.0f));
        Patch expected;
        expected.engine = Engine::fm;
        expected.fm = defaultFm(Kind::bass);
        expected.fx = fm.fx;
        expect(samePatch(fm, expected));

        beginTest("Every factory sound survives a trip through JSON");
        for (const auto& preset : PresetLibrary::factoryPresets())
        {
            const auto patch = patchFromJson(preset.patch);
            const auto again = patchFromJson(juce::JSON::parse(juce::JSON::toString(patchToJson(patch))), patch);
            expect(samePatch(patch, again), preset.name);
        }

        beginTest("Missing and broken fields take the melody's default, ranges are clamped");
        const auto odd = patchFromJson(juce::JSON::parse(R"({
            "osc1": { "wave": "organ", "octave": 7, "detune": -500, "width": 2, "level": "loud" },
            "filter": { "cutoff": 1, "resonance": 99 },
            "lfo": { "target": "filter" }
        })"));
        const auto lead = defaultAnalog(Kind::melody);
        expect(odd.engine == Engine::analog, "a patch without engine is an analog one");
        expect(odd.analog.osc1.wave == lead.osc1.wave);
        expectEquals(odd.analog.osc1.octave, 2);
        expect(same(odd.analog.osc1.detune, -50.0f));
        expect(same(odd.analog.osc1.width, 0.95f));
        expect(same(odd.analog.osc1.level, lead.osc1.level));
        expect(same(odd.analog.filter.cutoff, 20.0f));
        expect(same(odd.analog.filter.resonance, 20.0f));
        expect(odd.analog.lfo.target == AnalogLfoTarget::filter);
        expect(same(odd.analog.volume, lead.volume));
        expect(same(odd.fx.reverb.mix, 0.0f), "sounds from before the effects have none");

        beginTest("Sounds from before the PWM switch keep the LFO on");
        const auto old = patchFromJson(juce::JSON::parse(R"({ "osc2": { "wave": "square", "pwm": 0.5 } })"));
        expect(old.analog.osc2.pwmLfo);

        beginTest("Ratios are halves, as Tonwerk's slider sets them");
        const auto ratio = patchFromJson(juce::JSON::parse(R"({ "engine": "fm", "ops": [ { "ratio": 2.3 } ] })"));
        expect(same(ratio.fm.ops[0].ratio, 2.5f));
        expect(same(ratio.fm.ops[1].ratio, defaultFm(Kind::melody).ops[1].ratio));

        beginTest("Importing one engine leaves the other as it was");
        Patch base;
        base.analog.filter.cutoff = 555.0f;
        const auto imported = patchFromJson(juce::JSON::parse(kTonwerkFm), base);
        expect(same(imported.analog.filter.cutoff, 555.0f));

        beginTest("Preset lists, single entries and bare patches are all read");
        const auto list = namedPatchesFromJson(
            R"([{ "id": 1, "name": "Bass", "patch": { "engine": "fm" }, "updatedAt": "x" },
                { "id": 2, "name": "", "patch": {} },
                { "id": 3, "name": "Broken" }])",
            "file");
        expectEquals(list.size(), 1);
        expectEquals(list[0].name, juce::String("Bass"));
        const auto single = namedPatchesFromJson(R"({ "name": "Pad", "patch": { "volume": 0.5 } })", "file");
        expectEquals(single.size(), 1);
        expectEquals(single[0].name, juce::String("Pad"));
        const auto bare = namedPatchesFromJson(R"({ "engine": "analog", "volume": 0.5 })", "Mein Klang");
        expectEquals(bare.size(), 1);
        expectEquals(bare[0].name, juce::String("Mein Klang"));
        expectEquals(namedPatchesFromJson("not json", "x").size(), 0);
    }
};

class PresetLibraryTests : public juce::UnitTest
{
public:
    PresetLibraryTests() : juce::UnitTest("Preset library", "Patches") {}

    void runTest() override
    {
        const juce::TemporaryFile folder;
        PresetLibrary library(folder.getFile());

        beginTest("Eight factory sounds, four per engine");
        const auto factory = PresetLibrary::factoryPresets();
        expectEquals(factory.size(), 8);
        int fm = 0;
        for (const auto& preset : factory)
            fm += patchFromJson(preset.patch).engine == Engine::fm ? 1 : 0;
        expectEquals(fm, 4);

        beginTest("Imported sounds are kept, sorted, and replaced by name");
        juce::Array<NamedPatch> fromTonwerk;
        fromTonwerk.add({ "Pad", juce::JSON::parse(R"({ "volume": 0.1 })") });
        fromTonwerk.add({ "Bass", juce::JSON::parse(R"({ "volume": 0.2 })") });
        library.add(fromTonwerk, "tonwerk");
        juce::Array<NamedPatch> fromFile;
        fromFile.add({ "pad", juce::JSON::parse(R"({ "volume": 0.3 })") });
        library.add(fromFile, "file");
        PresetLibrary other(folder.getFile());
        expectEquals(other.imported().size(), 2);
        expectEquals(other.imported()[0].name, juce::String("Bass"));
        expectEquals(other.imported()[1].name, juce::String("pad"));
        expectEquals(other.imported()[1].source, juce::String("file"));
        expect(same(patchFromJson(other.imported()[1].patch).analog.volume, 0.3f));

        beginTest("Removing one leaves the rest");
        expect(other.remove("BASS"));
        expect(! other.remove("Bass"));
        library.reload();
        expectEquals(library.imported().size(), 1);

        beginTest("Tonwerk's address is remembered");
        library.setTonwerkUrl(" https://mac.example.ts.net:8443 ");
        expectEquals(PresetLibrary(folder.getFile()).tonwerkUrl(), juce::String("https://mac.example.ts.net:8443"));

        beginTest("An address without scheme is refused before anything is sent");
        juce::Array<NamedPatch> sounds;
        expect(PresetLibrary::fetchFromTonwerk("mac.local", sounds).failed());

        folder.getFile().deleteRecursively();
    }
};

static PatchJsonTests patchJsonTests;
static PresetLibraryTests presetLibraryTests;
