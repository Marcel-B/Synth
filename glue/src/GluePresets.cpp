#include "GluePresets.h"

#include "GlueEngine.h"

namespace tonwerkglue
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float ratio1_5 = 0, ratio2 = 1, ratio4 = 2, ratio10 = 3, ratio20 = 4;
constexpr float attack0_1 = 0, attack1 = 2, attack3 = 3, attack10 = 4, attack30 = 5;
constexpr float release0_1 = 0, release0_3 = 1, release0_6 = 2, releaseAuto = kAutoRelease;
constexpr float sidechain = 1;
constexpr float half = 1, quarter = kQuarter, eighth = kEighth, sixteenth = 7;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The console's bus setting: slow attack lets the hits through, auto release breathes with the song.
        { "Mix-Kleber", "Bus",
          { { "threshold", -14 }, { "ratio", ratio2 }, { "attack", attack30 }, { "release", releaseAuto },
            { "makeup", 2 }, { "output", -2 } } },
        { "Sanfter Bus", "Bus",
          { { "threshold", -18 }, { "ratio", ratio1_5 }, { "attack", attack30 }, { "release", releaseAuto },
            { "knee", 12 }, { "makeup", 1 }, { "output", 0 } } },
        { "Dichter Bus", "Bus",
          { { "threshold", -16 }, { "ratio", ratio4 }, { "attack", attack10 }, { "release", release0_3 },
            { "makeup", 4 }, { "output", -2 } } },
        // The kick no longer drives the whole mix down: the detector hears from 120 Hz up.
        { "Mastering-Glue", "Bus",
          { { "threshold", -12 }, { "ratio", ratio1_5 }, { "attack", attack30 }, { "release", releaseAuto },
            { "scHighpass", 120 }, { "makeup", 1 }, { "output", -1 } } },

        // 30 ms attack: each hit's front passes before the compressor grabs, so the drums snap.
        { "Drum-Punch", "Drums",
          { { "threshold", -18 }, { "ratio", ratio4 }, { "attack", attack30 }, { "release", release0_1 },
            { "makeup", 4 }, { "output", -3 } } },
        // Crushed hard and mixed under the dry: body without losing the hits.
        { "Parallel-Drums", "Drums",
          { { "threshold", -30 }, { "ratio", ratio10 }, { "attack", attack1 }, { "release", release0_1 },
            { "makeup", 10 }, { "mix", 0.4f }, { "output", 1 } } },
        { "Raum-Quetsche", "Drums",
          { { "threshold", -30 }, { "ratio", ratio20 }, { "attack", attack0_1 }, { "release", release0_1 },
            { "knee", 0 }, { "makeup", 12 }, { "mix", 0.5f }, { "output", 2 } } },

        // The pump without a kick: every quarter the mix ducks and swells back, as under a four-to-the-floor.
        { "Viertel-Pumpen", "Pumpen",
          { { "threshold", 0 }, { "pump", 1 }, { "pumpDivision", quarter }, { "pumpDepth", 12 },
            { "pumpLength", 0.6f }, { "pumpCurve", 0.5f }, { "output", 4 } } },
        { "Achtel-Pumpen", "Pumpen",
          { { "threshold", 0 }, { "pump", 1 }, { "pumpDivision", eighth }, { "pumpDepth", 9 }, { "pumpLength", 0.7f },
            { "pumpCurve", 0.3f }, { "output", 4 } } },
        { "EDM-Ducking", "Pumpen",
          { { "threshold", 0 }, { "pump", 1 }, { "pumpDivision", quarter }, { "pumpDepth", 24 },
            { "pumpLength", 0.5f }, { "pumpCurve", 0.8f }, { "output", 4 } } },
        { "Sanftes Atmen", "Pumpen",
          { { "threshold", 0 }, { "pump", 1 }, { "pumpDivision", half }, { "pumpDepth", 6 }, { "pumpLength", 0.9f },
            { "pumpCurve", 0.2f }, { "output", 2 } } },
        { "Stotter-Pumpen", "Pumpen",
          { { "threshold", 0 }, { "pump", 1 }, { "pumpDivision", sixteenth }, { "pumpDepth", 15 },
            { "pumpLength", 0.8f }, { "pumpCurve", 0.6f }, { "output", 6 } } },

        // Put the kick (or any track) on the side chain: the rest ducks under it.
        { "Kick-Ducking", "Sidechain",
          { { "source", sidechain }, { "threshold", -30 }, { "ratio", ratio10 }, { "attack", attack0_1 },
            { "release", release0_3 }, { "output", 0 } } },
        { "Bass unter Kick", "Sidechain",
          { { "source", sidechain }, { "threshold", -24 }, { "ratio", ratio4 }, { "attack", attack1 },
            { "release", release0_1 }, { "output", 0 } } },
        // Voice on the side chain: the pad or guitar makes room while it sings, comes back in the gaps.
        { "Platz für Gesang", "Sidechain",
          { { "source", sidechain }, { "threshold", -30 }, { "ratio", ratio2 }, { "attack", attack3 },
            { "release", release0_6 }, { "scHighpass", 150 }, { "output", 0 } } },
    };
    return presets;
}
} // namespace tonwerkglue
