#include "ZonePresets.h"

#include "ZoneEngine.h"

namespace tonwerkzone
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float bell = (float) Shape::bell, lows = (float) Shape::low, highs = (float) Shape::high;
constexpr float wide = (float) Mode::wide;
constexpr float sidechain = (float) Source::sidechain;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Defaults: a bell at 1 kHz an octave wide, -24 dB at 4:1, 5 ms attack, 100 ms release, at most 24 dB.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The sibilants around 6.5 kHz turned down the moment they come, let go before the next vowel.
        { "De-Esser", "Gesang",
          { { "frequency", 6500 }, { "width", 18 }, { "threshold", -34 }, { "ratio", 6 }, { "attack", 0.5f },
            { "release", 60 }, { "range", 12 }, { "output", 0 } } },
        // The whole voice ducks a little on an "s", as the classic wide-band de-essers do.
        { "De-Esser breit", "Gesang",
          { { "shape", highs }, { "frequency", 5000 }, { "mode", wide }, { "threshold", -32 }, { "ratio", 4 },
            { "attack", 0.5f }, { "release", 60 }, { "range", 8 }, { "output", 0 } } },
        { "Nasal weg", "Gesang",
          { { "frequency", 1000 }, { "width", 10 }, { "threshold", -30 }, { "ratio", 3 }, { "attack", 5 },
            { "release", 120 }, { "range", 6 }, { "output", 0 } } },

        { "Härte zähmen", "Mix",
          { { "frequency", 3000 }, { "width", 14 }, { "threshold", -32 }, { "ratio", 3 }, { "attack", 2 },
            { "release", 80 }, { "range", 8 }, { "output", 0 } } },
        { "Mulm weg", "Mix",
          { { "frequency", 300 }, { "width", 16 }, { "threshold", -26 }, { "ratio", 3 }, { "attack", 10 },
            { "release", 150 }, { "range", 8 }, { "output", 0 } } },
        { "Becken zähmen", "Mix",
          { { "shape", highs }, { "frequency", 8000 }, { "threshold", -34 }, { "ratio", 4 }, { "attack", 1 },
            { "release", 80 }, { "range", 10 }, { "output", 0 } } },

        // A boom that only some notes of a bass line hit: those come down, the others stay.
        { "Dröhnen bändigen", "Bass",
          { { "frequency", 110 }, { "width", 8 }, { "threshold", -20 }, { "ratio", 4 }, { "attack", 10 },
            { "release", 150 }, { "range", 10 }, { "output", 0 } } },
        { "Sub glätten", "Bass",
          { { "shape", lows }, { "frequency", 90 }, { "threshold", -18 }, { "ratio", 6 }, { "attack", 10 },
            { "release", 120 }, { "range", 12 }, { "output", 0 } } },
        { "Growl-Spitzen", "Bass",
          { { "frequency", 1500 }, { "width", 24 }, { "threshold", -30 }, { "ratio", 4 }, { "attack", 1 },
            { "release", 60 }, { "range", 10 }, { "output", 0 } } },

        // Put the kick on the side chain: only the bass's lows make room for it, the growl above stays.
        { "Kick-Platz im Bass", "Sidechain",
          { { "source", sidechain }, { "shape", lows }, { "frequency", 120 }, { "threshold", -30 }, { "ratio", 10 },
            { "attack", 1 }, { "release", 120 }, { "range", 18 }, { "output", 0 } } },
        // Voice on the side chain: the pad's presence range gives way while it sings.
        { "Platz für Gesang", "Sidechain",
          { { "source", sidechain }, { "frequency", 2500 }, { "width", 24 }, { "threshold", -36 }, { "ratio", 3 },
            { "attack", 5 }, { "release", 200 }, { "range", 6 }, { "output", 0 } } },
    };
    return presets;
}
} // namespace tonwerkzone
