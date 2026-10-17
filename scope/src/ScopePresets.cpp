#include "ScopePresets.h"

#include "ScopeEngine.h"

namespace tonwerkscope
{
namespace
{
// Menu indices, named so the settings below read as what they are.
constexpr float bars = (float) Mode::bars, xy = (float) Mode::xy;
constexpr float freeRunning = (float) Trigger::free;
constexpr float both = (float) Channel::both, side = (float) Channel::side;
constexpr float quarter = 0, bar = 2, fourBars = 4;
constexpr float yellow = 1, red = 2, green = 3;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Defaults: the wave over 20 ms, triggered rising at zero, the sum, auto zoom, half the afterglow, cyan.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // A few cycles of a bass, standing still.
        { "Bass-Welle", "Welle", { { "time", 50 }, { "persistence", 0.6f } } },
        { "Lead-Detail", "Welle", { { "time", 5 }, { "colour", yellow } } },
        // A whole kick or snare from its start: the shape of its envelope.
        { "Drum-Hüllkurve", "Welle", { { "time", 300 }, { "triggerLevel", 0.1f }, { "persistence", 0.3f }, { "colour", red } } },
        { "Phosphor", "Welle", { { "time", 30 }, { "persistence", 0.9f }, { "colour", green } } },
        { "Frei laufend", "Welle", { { "time", 100 }, { "trigger", freeRunning }, { "persistence", 0.7f } } },

        // The song's bars as a waveform, as the arrangement shows it: pumping and ducking at a glance.
        { "Ein Takt", "Takt", { { "mode", bars }, { "window", bar } } },
        { "Vier Takte", "Takt", { { "mode", bars }, { "window", fourBars }, { "colour", yellow } } },
        { "Sidechain-Check", "Takt", { { "mode", bars }, { "window", quarter }, { "colour", red } } },

        // Left against right turned by 45 degrees: mono stands upright, wide spreads sideways.
        { "Goniometer", "Stereo", { { "mode", xy }, { "persistence", 0.7f } } },
        { "Links und Rechts", "Stereo", { { "channel", both }, { "time", 30 } } },
        { "Nur Seite", "Stereo", { { "channel", side }, { "time", 30 }, { "colour", yellow } } },
    };
    return presets;
}
} // namespace tonwerkscope
