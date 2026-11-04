#include "ReverbPresets.h"

#include "ReverbEngine.h"

namespace tonwerkreverb
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float room = 0, hall = 1, plate = 2, cathedral = 3;
constexpr float sixteenth = 5, eighth = 2;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // Small spaces: mostly reflections, a short tail, for drums and voices that should stay close.
        { "Kleiner Raum", "Räume",
          { { "space", room }, { "size", 0.4f }, { "decay", 0.6f }, { "predelay", 5 }, { "early", 0.7f },
            { "mix", 0.25f }, { "output", 0 } } },
        { "Studio-Raum", "Räume",
          { { "space", room }, { "size", 0.7f }, { "decay", 1.0f }, { "predelay", 10 }, { "damping", 7000 },
            { "early", 0.6f }, { "mix", 0.25f }, { "output", 0 } } },
        { "Drum-Room", "Räume",
          { { "space", room }, { "size", 0.6f }, { "decay", 0.8f }, { "bassTime", 0.7f }, { "early", 0.9f },
            { "lowCut", 150 }, { "mix", 0.3f }, { "output", -1 } } },
        { "Holzkammer", "Räume",
          { { "space", room }, { "size", 0.9f }, { "decay", 1.4f }, { "damping", 4500 }, { "early", 0.5f },
            { "diffusion", 0.9f }, { "mix", 0.3f }, { "output", -1 } } },

        // Halls: the classic send reverbs.
        { "Konzertsaal", "Hallen",
          { { "space", hall }, { "size", 0.75f }, { "decay", 2.8f }, { "predelay", 25 }, { "bassTime", 1.3f },
            { "mix", 0.3f }, { "output", 0 } } },
        { "Großer Saal", "Hallen",
          { { "space", hall }, { "size", 1.0f }, { "decay", 4.5f }, { "predelay", 40 }, { "bassTime", 1.2f },
            { "damping", 7000 }, { "mix", 0.35f }, { "output", -1 } } },
        { "Vocal-Hall", "Hallen",
          { { "space", hall }, { "size", 0.65f }, { "decay", 2.2f }, { "sync", 1 }, { "division", sixteenth },
            { "lowCut", 200 }, { "highCut", 10000 }, { "duck", 0.5f }, { "mix", 0.3f }, { "output", 0 } } },
        { "Kathedrale", "Hallen",
          { { "space", cathedral }, { "size", 0.9f }, { "decay", 7.0f }, { "predelay", 50 }, { "bassTime", 1.4f },
            { "damping", 5000 }, { "mix", 0.35f }, { "output", -1 } } },

        // Plates: dense from the first moment, bright, no walls.
        { "Helle Platte", "Platten",
          { { "space", plate }, { "size", 0.6f }, { "decay", 1.8f }, { "damping", 14000 }, { "lowCut", 150 },
            { "mix", 0.25f }, { "output", 0 } } },
        { "Vocal-Platte", "Platten",
          { { "space", plate }, { "size", 0.7f }, { "decay", 2.4f }, { "predelay", 30 }, { "damping", 10000 },
            { "lowCut", 250 }, { "duck", 0.4f }, { "mix", 0.3f }, { "output", 0 } } },
        { "Snare-Platte", "Platten",
          { { "space", plate }, { "size", 0.5f }, { "decay", 1.3f }, { "damping", 12000 }, { "lowCut", 300 },
            { "mix", 0.3f }, { "output", -1 } } },
        { "Dunkle Platte", "Platten",
          { { "space", plate }, { "size", 0.8f }, { "decay", 3.0f }, { "damping", 3500 }, { "highCut", 7000 },
            { "mix", 0.3f }, { "output", 0 } } },

        // Further out: long, ducked or wandering.
        { "Unendliche Weite", "Effekte",
          { { "space", cathedral }, { "size", 1.0f }, { "decay", 18.0f }, { "predelay", 80 }, { "modulation", 0.8f },
            { "damping", 6000 }, { "mix", 0.45f }, { "output", -1 } } },
        { "Pumpender Hall", "Effekte",
          { { "space", hall }, { "size", 0.9f }, { "decay", 3.5f }, { "duck", 1.0f }, { "duckRelease", 250 },
            { "lowCut", 150 }, { "mix", 0.4f }, { "output", 0 } } },
        { "Achtel-Abstand", "Effekte",
          { { "space", plate }, { "size", 0.8f }, { "decay", 2.5f }, { "sync", 1 }, { "division", eighth },
            { "mix", 0.35f }, { "output", -1 } } },
        { "Lo-Fi-Höhle", "Effekte",
          { { "space", cathedral }, { "size", 0.6f }, { "decay", 4.0f }, { "lowCut", 300 }, { "highCut", 3000 },
            { "diffusion", 0.5f }, { "modulation", 0.9f }, { "mix", 0.4f }, { "output", 0 } } },
    };
    return presets;
}
} // namespace tonwerkreverb
