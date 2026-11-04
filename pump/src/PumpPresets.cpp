#include "PumpPresets.h"

#include "PumpEngine.h"

namespace tonwerkpump
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float trigger = 0, compressor = 1, tempo = 2;
constexpr float input = 1;
constexpr float lows = 1, highs = 2;
constexpr float half = 1, quarter = kQuarter, eighth = 6, eighthTriplet = 7, sixteenth = 8;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The kick on the side chain plays the curve: down at once, back over 220 ms, every hit the same.
        { "Kick-Pumpe", "Sidechain",
          { { "mode", trigger }, { "threshold", -30 }, { "depth", 18 }, { "attack", 0.001f }, { "release", 0.22f },
            { "shape", 0.3f }, { "keyLowpass", 150 }, { "output", 1 } } },
        { "Sanftes Ducking", "Sidechain",
          { { "mode", trigger }, { "threshold", -30 }, { "depth", 8 }, { "attack", 0.003f }, { "release", 0.3f },
            { "shape", -0.3f }, { "keyLowpass", 150 }, { "output", 1 } } },
        // Deep and held a moment, rising late: the big room sound.
        { "EDM-Pumpe", "Sidechain",
          { { "mode", trigger }, { "threshold", -30 }, { "depth", 30 }, { "attack", 0.001f }, { "hold", 0.02f },
            { "release", 0.3f }, { "shape", 0.7f }, { "keyLowpass", 150 }, { "output", 3 } } },
        // Only below 150 Hz: the bass makes room for the kick, the rest of it stays.
        { "Bass unter Kick", "Sidechain",
          { { "mode", trigger }, { "threshold", -30 }, { "depth", 24 }, { "attack", 0.001f }, { "release", 0.14f },
            { "shape", 0.2f }, { "keyLowpass", 150 }, { "band", lows }, { "split", 150 }, { "output", 0 } } },
        // Ghost notes duck a little, the downbeat all the way.
        { "Dynamische Pumpe", "Sidechain",
          { { "mode", trigger }, { "threshold", -36 }, { "depth", 20 }, { "attack", 0.002f }, { "release", 0.25f },
            { "shape", 0.3f }, { "dynamics", 1.0f }, { "keyLowpass", 150 }, { "output", 2 } } },

        // No key needed: the curve on the song's grid.
        { "Viertel-Pumpe", "Tempo",
          { { "mode", tempo }, { "division", quarter }, { "depth", 18 }, { "attack", 0.001f }, { "release", 0.35f },
            { "shape", 0.3f }, { "output", 3 } } },
        { "Achtel-Pumpe", "Tempo",
          { { "mode", tempo }, { "division", eighth }, { "depth", 12 }, { "attack", 0.001f }, { "release", 0.18f },
            { "shape", 0.2f }, { "output", 2 } } },
        { "Halbe atmen", "Tempo",
          { { "mode", tempo }, { "division", half }, { "depth", 10 }, { "attack", 0.01f }, { "release", 0.8f },
            { "shape", -0.5f }, { "output", 1 } } },
        // Sixteenths chopped hard: a gate more than a pump.
        { "Sechzehntel-Gate", "Tempo",
          { { "mode", tempo }, { "division", sixteenth }, { "depth", 30 }, { "attack", 0.0f }, { "hold", 0.03f },
            { "release", 0.04f }, { "shape", 1.0f }, { "output", 3 } } },
        { "Triolen-Pumpe", "Tempo",
          { { "mode", tempo }, { "division", eighthTriplet }, { "depth", 14 }, { "attack", 0.001f },
            { "release", 0.12f }, { "shape", 0.4f }, { "output", 3 } } },

        // The classic: a sidechain compressor on the kick, its release the pump.
        { "Klassischer Sidechain", "Kompressor",
          { { "mode", compressor }, { "threshold", -18 }, { "ratio", 8 }, { "depth", 24 }, { "attack", 0.001f },
            { "release", 0.15f }, { "keyLowpass", 150 }, { "output", 4 } } },
        // Voice on the side chain: the pad steps back while it sings.
        { "Platz für Gesang", "Kompressor",
          { { "mode", compressor }, { "threshold", -30 }, { "ratio", 3 }, { "depth", 8 }, { "attack", 0.01f },
            { "release", 0.4f }, { "output", 6 } } },
        { "Harter Sidechain", "Kompressor",
          { { "mode", compressor }, { "threshold", -20 }, { "ratio", 20 }, { "depth", 30 }, { "attack", 0.0003f },
            { "hold", 0.03f }, { "release", 0.2f }, { "keyLowpass", 150 }, { "output", 6 } } },

        // On a drum bus without a side chain: its own kick pumps it, the click let through by 5 ms.
        { "Selbst-Pumpe", "Kreativ",
          { { "mode", trigger }, { "source", input }, { "threshold", -12 }, { "depth", 10 }, { "attack", 0.005f },
            { "release", 0.15f }, { "keyLowpass", 150 }, { "output", 1 } } },
        // Only above 800 Hz: the air breathes on the beat, the low end stands.
        { "Höhen-Pumpe", "Kreativ",
          { { "mode", tempo }, { "division", quarter }, { "depth", 15 }, { "attack", 0.002f }, { "release", 0.3f },
            { "shape", 0.3f }, { "band", highs }, { "split", 800 }, { "output", 1 } } },
    };
    return presets;
}
} // namespace tonwerkpump
