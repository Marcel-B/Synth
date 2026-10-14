#include "ShimmerPresets.h"

#include "ShimmerEngine.h"

namespace tonwerkshimmer
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float fifth = 1, octaveFifth = 3, twoOctaves = 4, octaveDown = 5;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        { "Klassischer Shimmer", "Shimmer",
          { { "size", 0.8f }, { "decay", 6 }, { "shimmer", 0.5f }, { "mix", 0.35f }, { "output", -1 } } },
        // Much of the loop shifted, the highs kept: the tail climbs into a bright choir.
        { "Engelschor", "Shimmer",
          { { "size", 0.9f }, { "decay", 10 }, { "damping", 9000 }, { "shimmer", 0.75f }, { "modulation", 0.5f },
            { "mix", 0.4f }, { "output", -1 } } },
        { "Quinten-Shimmer", "Shimmer",
          { { "size", 0.8f }, { "decay", 7 }, { "shimmer", 0.5f }, { "interval", fifth }, { "mix", 0.35f },
            { "output", -1 } } },
        { "Oktave und Quinte", "Shimmer",
          { { "size", 0.85f }, { "decay", 8 }, { "shimmer", 0.45f }, { "interval", octaveFifth }, { "damping", 8000 },
            { "mix", 0.35f }, { "output", -1 } } },
        { "Doppel-Oktave", "Shimmer",
          { { "size", 0.7f }, { "decay", 5 }, { "shimmer", 0.3f }, { "interval", twoOctaves }, { "damping", 10000 },
            { "mix", 0.3f }, { "output", -1 } } },

        // Plain reverbs: no shimmer.
        { "Großer Hall", "Hall",
          { { "size", 0.9f }, { "decay", 4 }, { "shimmer", 0 }, { "mix", 0.3f }, { "output", -1 } } },
        { "Kathedrale", "Hall",
          { { "size", 1.0f }, { "decay", 9 }, { "predelay", 60 }, { "shimmer", 0 }, { "damping", 5000 },
            { "mix", 0.35f }, { "output", -1 } } },
        { "Heller Raum", "Hall",
          { { "size", 0.35f }, { "decay", 1.4f }, { "shimmer", 0 }, { "damping", 12000 }, { "predelay", 10 },
            { "mix", 0.25f }, { "output", -1 } } },
        { "Dunkle Halle", "Hall",
          { { "size", 0.8f }, { "decay", 5 }, { "shimmer", 0 }, { "damping", 2200 }, { "predelay", 30 },
            { "mix", 0.35f }, { "output", -1 } } },

        // Long and wandering, mostly wet: the source turns into a pad.
        { "Endlose Fläche", "Ambient",
          { { "size", 1.0f }, { "decay", 30 }, { "shimmer", 0.3f }, { "modulation", 0.7f }, { "damping", 6000 },
            { "mix", 0.5f }, { "output", -1 } } },
        { "Wolke", "Ambient",
          { { "size", 1.0f }, { "decay", 15 }, { "shimmer", 0.2f }, { "modulation", 0.9f }, { "predelay", 120 },
            { "mix", 0.45f }, { "output", -1 } } },
        { "Sternenstaub", "Ambient",
          { { "size", 0.75f }, { "decay", 12 }, { "shimmer", 0.6f }, { "interval", twoOctaves }, { "damping", 14000 },
            { "lowCut", 300 }, { "mix", 0.4f }, { "output", -1 } } },

        // An octave down each round: the tail sinks into the dark.
        { "Unterwelt", "Effekte",
          { { "size", 0.9f }, { "decay", 8 }, { "shimmer", 0.5f }, { "interval", octaveDown }, { "lowCut", 40 },
            { "damping", 4000 }, { "mix", 0.4f }, { "output", -1 } } },
        // Late and clear of the notes, as a guitarist's shimmer pedal.
        { "Gitarren-Shimmer", "Effekte",
          { { "size", 0.8f }, { "decay", 6 }, { "shimmer", 0.55f }, { "predelay", 80 }, { "lowCut", 200 },
            { "mix", 0.3f }, { "output", 0 } } },
        { "Kristall", "Effekte",
          { { "size", 0.5f }, { "decay", 3 }, { "shimmer", 0.6f }, { "interval", twoOctaves }, { "damping", 16000 },
            { "lowCut", 400 }, { "mix", 0.3f }, { "output", 0 } } },
    };
    return presets;
}
} // namespace tonwerkshimmer
