#include "DrumsPresets.h"

#include "GroovePresets.h"
#include "GrooveProcessor.h"

#include <map>

namespace tonwerkdrums
{
namespace
{
/** What a pad's parameters are called after its id prefix; a kit may set any of them but the note. */
const std::vector<juce::String>& padSuffixes()
{
    static const std::vector<juce::String> suffixes { "Tune",     "Decay",  "Tone",      "Character", "Level",
                                                      "Pan",      "Delay",  "Reverb",    "Mute",      "Attack",
                                                      "Hold",     "PitchMod", "ModDecay", "Filter",   "Cutoff",
                                                      "Resonance", "FilterMod", "Drive",  "Bits",     "Crush",
                                                      "Velocity", "Random", "Machine" };
    return suffixes;
}

/** Whether a Groovebox kit's parameter is one of ours: a pad's sound or the sum's, not its sequencer's. */
bool carriesOver(const juce::String& id)
{
    if (id == "drive" || id == "master")
        return true;
    if (id.startsWith("fx"))
        return id != "fxOn";
    for (int p = 0; p < tonwerkgroove::kTracks; ++p)
        if (id.startsWith(kPadIds[(std::size_t) p]))
        {
            const auto suffix = id.substring((int) std::strlen(kPadIds[(std::size_t) p]));
            for (const auto& known : padSuffixes())
                if (suffix == known)
                    return true;
        }
    return false;
}

/**
 * How the pads the Groovebox lacks follow a kit: each takes from its neighbour on the Groovebox the colour of the kit
 * (drive, crushing, sends), the pedal hat also its closed hat's sound and the ride its cymbal's.
 */
struct Follow
{
    Pad pad, from;
    std::vector<const char*> suffixes;
};

const std::vector<Follow>& follows()
{
    static const std::vector<const char*> colour { "Drive", "Bits", "Crush", "Delay", "Reverb" };
    auto with = [](std::vector<const char*> more) {
        auto all = colour;
        all.insert(all.end(), more.begin(), more.end());
        return all;
    };
    static const std::vector<Follow> list {
        { Pad::rim, Pad::snare, colour },
        { Pad::hatPedal, Pad::hatClosed, with({ "Tune", "Tone", "Character", "Level", "Filter", "Cutoff", "Resonance" }) },
        { Pad::tomMid, Pad::tomLow, colour },
        { Pad::ride, Pad::cymbal, with({ "Tune", "Tone", "Character", "Level" }) },
        { Pad::cowbell, Pad::hatOpen, colour },
        { Pad::conga, Pad::tomHigh, colour },
        { Pad::clave, Pad::snare, colour },
        { Pad::shaker, Pad::hatClosed, colour },
    };
    return list;
}

/**
 * Master in dB per kit so each comes out at the same loudness with the tests' General MIDI beat; the Groovebox
 * leveled its kits with their own patterns.
 */
const std::map<juce::String, float>& masterFor()
{
    static const std::map<juce::String, float> levels {
        { "Deep House", 1.1f },   { "Garage", -0.6f },          { "Berlin Techno", 0.7f },  { "Industrial", 2.9f },
        { "Minimal", -4.2f },     { "Boom Bap", 0.2f },         { "Lo-Fi", 0.8f },          { "Trap", 1.6f },
        { "Drill", 0.3f },        { "Electro Funk", 2.0f },     { "Synthwave", 2.0f },      { "Miami Bass", 0.5f },
        { "Breakbeat", 0.8f },    { "Drum & Bass", 3.5f },      { "Jungle", 2.0f },         { "Halftime", 3.7f },
        { "Riddim", 3.1f },       { "Triolen-Shuffle", -1.7f }, { "Glitch-Perkussion", 3.6f },
        { "Metall-Werkstatt", 5.1f }, { "Gated Eighties", 3.8f }, { "Sub-Boom", 1.9f },
    };
    return levels;
}

DrumKit fromGroovebox(const tonwerkgroove::FactoryKit& kit)
{
    DrumKit drums { juce::String::fromUTF8(kit.name), juce::String::fromUTF8(kit.category), {} };
    std::map<juce::String, float> values;
    for (const auto& [id, value] : kit.values)
        if (carriesOver(id))
            values[id] = value;
    for (const auto& follow : follows())
        for (const auto* suffix : follow.suffixes)
            if (const auto found = values.find(kPadIds[(std::size_t) follow.from] + juce::String(suffix));
                found != values.end())
                values[kPadIds[(std::size_t) follow.pad] + juce::String(suffix)] = found->second;
    for (const auto& [id, value] : values)
        drums.values.emplace_back(id, value);
    return drums;
}

void level(DrumKit& kit)
{
    const auto found = masterFor().find(kit.name);
    if (found == masterFor().end())
        return;
    for (auto& [id, value] : kit.values)
        if (id == "master")
        {
            value += found->second;
            return;
        }
    kit.values.emplace_back("master", found->second);
}
} // namespace

const std::vector<DrumKit>& factoryKits()
{
    static const std::vector<DrumKit> kits = [] {
        std::vector<DrumKit> list;
        list.push_back({ "Init", "", {} });
        // A kit per machine: every pad on it, the sound otherwise as at the start.
        for (int m = 1; m < kMachines; ++m)
        {
            DrumKit kit { juce::String::fromUTF8(tonwerkgroove::kMachineNames[(std::size_t) m]), "Maschinen", {} };
            for (const auto* pad : kPadIds)
                kit.values.emplace_back(juce::String(pad) + "Machine", (float) m);
            list.push_back(std::move(kit));
        }
        const auto& groove = tonwerkgroove::factoryKits();
        for (std::size_t k = 1; k < groove.size(); ++k)
            list.push_back(fromGroovebox(groove[k]));
        for (auto& kit : list)
            level(kit);
        return list;
    }();
    return kits;
}
} // namespace tonwerkdrums
