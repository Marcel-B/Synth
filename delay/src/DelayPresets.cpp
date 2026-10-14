#include "DelayPresets.h"

#include "DelayEngine.h"

namespace tonwerkdelay
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float stereo = 0, mono = 2;
constexpr float half = 2, quarter = kQuarter, dottedQuarter = 4, dottedEighth = kDottedEighth, eighth = kEighth,
                eighthTriplet = 9, thirtySecond = 13;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        { "Punktierte Achtel", "Rhythmisch",
          { { "division", dottedEighth }, { "feedback", 0.35f }, { "mix", 0.25f }, { "output", 0 } } },
        { "Achtel Ping-Pong", "Rhythmisch",
          { { "division", eighth }, { "feedback", 0.4f }, { "mix", 0.3f }, { "output", -1 } } },
        { "Triolen", "Rhythmisch",
          { { "division", eighthTriplet }, { "feedback", 0.35f }, { "mix", 0.3f }, { "output", -1 } } },
        // One short, bright repeat, as on fifties rock'n'roll vocals.
        { "Slapback", "Rhythmisch",
          { { "mode", mono }, { "sync", 0 }, { "time", 90 }, { "feedback", 0.1f }, { "lowCut", 100 },
            { "highCut", 5000 }, { "mix", 0.3f }, { "output", -2 } } },
        // Left a quarter, right half as long: two rhythms that meet on every beat.
        { "Doppelt", "Rhythmisch",
          { { "mode", stereo }, { "division", quarter }, { "offset", -0.5f }, { "feedback", 0.3f }, { "mix", 0.3f },
            { "output", -2 } } },

        // Dark, saturated, wandering repeats that run on, as from a dub mixing desk's tape echo.
        { "Dub-Echo", "Dub",
          { { "division", dottedQuarter }, { "feedback", 0.7f }, { "lowCut", 300 }, { "highCut", 2500 },
            { "drive", 0.4f }, { "wow", 0.3f }, { "mix", 0.35f }, { "output", -1 } } },
        // The feedback near the top: it keeps going and the saturation keeps it from blowing up.
        { "Endlos-Dub", "Dub",
          { { "division", dottedQuarter }, { "feedback", 0.95f }, { "lowCut", 250 }, { "highCut", 3000 },
            { "drive", 0.6f }, { "wow", 0.4f }, { "mix", 0.3f }, { "output", -1 } } },
        { "Bandecho", "Dub",
          { { "mode", mono }, { "division", dottedEighth }, { "feedback", 0.55f }, { "lowCut", 200 },
            { "highCut", 3500 }, { "drive", 0.5f }, { "wow", 0.5f }, { "mix", 0.3f }, { "output", -1 } } },

        // Two slightly different times and much feedback: the repeats smear into a space.
        { "Weite Wolke", "Raum",
          { { "mode", stereo }, { "division", quarter }, { "offset", 0.13f }, { "feedback", 0.6f },
            { "highCut", 5000 }, { "mix", 0.3f }, { "output", -2 } } },
        // Ducked: the echoes wait for the gaps between the lines.
        { "Gesangs-Delay", "Raum",
          { { "division", quarter }, { "feedback", 0.35f }, { "lowCut", 300 }, { "highCut", 7000 },
            { "duck", 0.7f }, { "mix", 0.35f }, { "output", 0 } } },
        { "Ducking-Fläche", "Raum",
          { { "division", half }, { "feedback", 0.6f }, { "duck", 0.9f }, { "duckRelease", 600 }, { "mix", 0.4f },
            { "output", 0 } } },

        { "Telefon-Echo", "Effekte",
          { { "division", eighth }, { "feedback", 0.5f }, { "lowCut", 800 }, { "highCut", 2000 }, { "mix", 0.35f },
            { "output", -1 } } },
        // Repeats so close they buzz: a metallic flutter on every hit.
        { "Flatter-Echo", "Effekte",
          { { "mode", mono }, { "division", thirtySecond }, { "feedback", 0.6f }, { "mix", 0.3f }, { "output", -2 } } },
        { "Leiernde Kassette", "Effekte",
          { { "division", eighth }, { "feedback", 0.5f }, { "highCut", 3000 }, { "drive", 0.3f }, { "wow", 1.0f },
            { "mix", 0.35f }, { "output", -1 } } },
    };
    return presets;
}
} // namespace tonwerkdelay
