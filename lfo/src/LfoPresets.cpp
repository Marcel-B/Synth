#include "LfoPresets.h"

#include "LfoEngine.h"

namespace tonwerklfo
{
namespace
{
// Menu indices, named so the presets below read as what they are.
constexpr float sine = (float) Shape::sine, triangle = (float) Shape::triangle, sawUp = (float) Shape::sawUp,
                sawDown = (float) Shape::sawDown, square = (float) Shape::square, random = (float) Shape::random;
constexpr float fourBars = 0.0f, twoBars = 1.0f, bar = (float) tonwerkwobble::kWhole, half = 4.0f,
                quarter = (float) tonwerkwobble::kQuarter, eighth = (float) tonwerkwobble::kEighth,
                eighthTriplet = 11.0f, sixteenth = (float) tonwerkwobble::kSixteenth;
constexpr float note = (float) Start::note;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Defaults: a sine on the quarters, the whole range around the middle, controller 20 in 14 bits, on the song.
    // The presets never change the controller or the channel: those belong to the knob the instrument has learnt.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        { "Langsamer Sweep", "Sweeps", { { "shape", triangle }, { "division", fourBars } } },
        { "Atmen", "Sweeps", { { "shape", sine }, { "division", twoBars }, { "depth", 0.5f } } },
        { "Rampe pro Takt", "Sweeps", { { "shape", sawUp }, { "division", bar } } },
        { "Frei schwebend", "Sweeps", { { "shape", sine }, { "sync", 0 }, { "rate", 0.3f }, { "depth", 0.6f } } },

        { "Wobble 1/8", "Rhythmisch", { { "shape", sine }, { "division", eighth }, { "phase", 270 } } },
        { "Wobble Triolen", "Rhythmisch", { { "shape", sine }, { "division", eighthTriplet }, { "phase", 270 } } },
        { "Gate 1/16", "Rhythmisch", { { "shape", square }, { "division", sixteenth } } },
        { "Abfall pro Note", "Rhythmisch", { { "shape", sawDown }, { "division", half }, { "start", note } } },
        { "Pumpen 1/4", "Rhythmisch", { { "shape", sawUp }, { "division", quarter }, { "depth", 0.6f }, { "centre", 0.7f } } },

        { "Zufall 1/16", "Zufall", { { "shape", random }, { "division", sixteenth } } },
        { "Zufall 1/4", "Zufall", { { "shape", random }, { "division", quarter }, { "depth", 0.6f } } },
        { "Zufall pro Takt", "Zufall", { { "shape", random }, { "division", bar }, { "depth", 0.4f } } },
    };
    return presets;
}
} // namespace tonwerklfo
