#include "SpreadPresets.h"

#include "SpreadEngine.h"

namespace tonwerkspread
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float diffus = diffuse, kamm = comb, baender = bands;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // A little air around the sound, hardly noticed until it is switched off.
        { "Etwas Luft", "Dezent", { { "mode", diffus }, { "amount", 0.3f }, { "time", 10 }, { "output", 0 } } },
        { "Breiter Mix", "Dezent",
          { { "width", 1.3f }, { "amount", 0.15f }, { "bassMono", 150 }, { "motion", 0.1f }, { "output", 0 } } },
        { "Master-Weite", "Dezent",
          { { "width", 1.25f }, { "amount", 0.0f }, { "bassMono", 180 }, { "output", 0 } } },

        // Wide on headphones, still whole on a mono speaker.
        { "Weite Fläche", "Breit",
          { { "mode", diffus }, { "amount", 0.8f }, { "time", 20 }, { "motion", 0.4f }, { "output", -1 } } },
        { "Kamm-Weite", "Breit", { { "mode", kamm }, { "amount", 0.6f }, { "time", 14 }, { "output", -0.5f } } },
        { "Spektral", "Breit", { { "mode", baender }, { "amount", 0.8f }, { "motion", 0.2f }, { "output", -1 } } },
        { "Extrabreit", "Breit",
          { { "width", 1.8f }, { "mode", diffus }, { "amount", 1.0f }, { "time", 25 }, { "motion", 0.5f },
            { "bassMono", 160 }, { "output", -1.5f } } },

        // Starting points per source.
        { "Synth-Lead", "Instrumente",
          { { "mode", kamm }, { "amount", 0.5f }, { "time", 8 }, { "highCut", 12000 }, { "output", 0 } } },
        { "Gesang", "Instrumente",
          { { "mode", diffus }, { "amount", 0.4f }, { "time", 15 }, { "bassMono", 200 }, { "highCut", 9000 },
            { "output", 0 } } },
        { "Schwebendes Pad", "Instrumente",
          { { "mode", baender }, { "amount", 0.7f }, { "motion", 0.8f }, { "output", -0.5f } } },
        { "Gitarre doppeln", "Instrumente",
          { { "mode", kamm }, { "amount", 0.8f }, { "time", 22 }, { "motion", 0.3f }, { "output", -1 } } },
        { "Bass oben breit", "Instrumente",
          { { "mode", diffus }, { "amount", 0.7f }, { "time", 12 }, { "bassMono", 250 }, { "output", -0.5f } } },
        { "Drum-Raum", "Instrumente",
          { { "mode", diffus }, { "amount", 0.5f }, { "time", 6 }, { "bassMono", 150 }, { "motion", 0.0f },
            { "output", 0 } } },

        // Tools for the mix: narrowing, mono lows, a wandering image.
        { "Mono machen", "Werkzeuge", { { "width", 0.0f }, { "amount", 0.0f }, { "bassMono", 20 }, { "output", 0 } } },
        { "Tiefen mono", "Werkzeuge", { { "amount", 0.0f }, { "bassMono", 200 }, { "output", 0 } } },
        { "Wandernd", "Werkzeuge",
          { { "mode", baender }, { "amount", 1.0f }, { "motion", 1.0f }, { "output", -1.5f } } },
    };
    return presets;
}
} // namespace tonwerkspread
