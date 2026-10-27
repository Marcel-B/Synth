#include "RumblePresets.h"

#include "RumbleEngine.h"

namespace tonwerkrumble
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float echo = (float) Space::echo, echoHall = (float) Space::echoHall;
constexpr float tube = (float) Curve::tube, hard = (float) Curve::hard, fold = (float) Curve::fold;
// `kDivisions` indices.
constexpr float quarter = 7, dottedEighth = 9, eighth = 10, sixteenth = 13;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Percent as 0 to 1, times in s or ms, levels in dB.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The kick into a long hall, driven and cut to the low end, ducked by the kick itself.
        { "Klassisch", "Techno", { { "rumble", 5 } } },
        { "Lagerhalle", "Techno",
          { { "decay", 4.0f }, { "size", 0.9f }, { "drive", 24 }, { "curve", tube }, { "cutoff", 120 },
            { "rumble", 5 } } },
        { "Bunker", "Techno",
          { { "decay", 3.0f }, { "drive", 20 }, { "cutoff", 90 }, { "resonance", 0.55f }, { "lowCut", 30 },
            { "rumble", 5 } } },
        { "Rollend", "Techno",
          { { "space", echoHall }, { "echoDivision", sixteenth }, { "feedback", 0.5f }, { "decay", 1.8f },
            { "cutoff", 150 }, { "duckRelease", 160 }, { "rumble", 4 } } },
        { "Weich und tief", "Techno",
          { { "drive", 8 }, { "cutoff", 110 }, { "resonance", 0.2f }, { "duck", 0.8f }, { "duckRelease", 260 },
            { "rumble", 7 } } },

        // Harder clipping and a brighter filter: the rumble growls.
        { "Industrie", "Hart",
          { { "drive", 32 }, { "curve", hard }, { "cutoff", 200 }, { "resonance", 0.4f }, { "duckRelease", 150 },
            { "rumble", 2 } } },
        { "Gefaltet", "Hart",
          { { "drive", 24 }, { "curve", fold }, { "cutoff", 220 }, { "resonance", 0.35f }, { "duckFilter", 0.5f },
            { "rumble", 5 } } },
        { "Dreck", "Hart",
          { { "tone", 2000 }, { "drive", 36 }, { "curve", hard }, { "decay", 2.0f }, { "cutoff", 260 },
            { "duckRelease", 120 }, { "rumble", 2 } } },

        // Echoes and long rooms, slow to come back.
        { "Dub-Echo", "Dub",
          { { "space", echo }, { "echoDivision", dottedEighth }, { "feedback", 0.7f }, { "drive", 12 },
            { "curve", tube }, { "cutoff", 180 }, { "duck", 0.75f }, { "rumble", 0 } } },
        { "Nebel", "Dub",
          { { "decay", 6.0f }, { "size", 1.0f }, { "predelay", 20 }, { "drive", 10 }, { "cutoff", 160 },
            { "duck", 0.6f }, { "duckRelease", 400 }, { "rumble", 1 } } },

        // Ducked on the song's grid: the rumble swells into the offbeat whatever the kick does.
        { "Viertel-Pumpe", "Raster",
          { { "duckSync", 1 }, { "duckDivision", quarter }, { "duck", 1.0f }, { "duckRelease", 450 },
            { "duckFilter", 0.6f }, { "rumble", 4 } } },
        { "Achtel-Pumpe", "Raster",
          { { "duckSync", 1 }, { "duckDivision", eighth }, { "duck", 0.9f }, { "duckRelease", 220 },
            { "drive", 24 }, { "curve", tube }, { "rumble", 3 } } },
    };
    return presets;
}
} // namespace tonwerkrumble
