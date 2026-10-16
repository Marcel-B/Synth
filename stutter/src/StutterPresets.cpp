#include "StutterPresets.h"

#include "StutterEngine.h"

namespace tonwerkstutter
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float quarterInterval = 0, halfBar = 1, twoBars = 3, fourBars = 4;
constexpr float eighth = 1, eighthTriplet = 2, sixteenth = 3, sixteenthTriplet = 4, thirtySecond = 5, sixtyFourth = 7;
constexpr float mix = (float) Mode::mix, gate = (float) Mode::gate;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Offsets and lengths in sixteenths, percent as 0 to 1, the filter -1 to 1, pitch in semitones, levels in dB.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The third beat of every bar, a sixteenth repeated through to the fourth.
        { "Sechzehntel auf drei", "Beat Repeat", { { "chance", 1.0f } } },
        { "Achtel-Echo", "Beat Repeat", { { "chance", 1.0f }, { "offset", 12 }, { "gate", 4 }, { "grid", eighth } } },
        { "Triolen-Repeat", "Beat Repeat",
          { { "chance", 0.6f }, { "offset", 8 }, { "gate", 8 }, { "grid", eighthTriplet } } },
        { "Halbtakt-Stotter", "Beat Repeat",
          { { "interval", halfBar }, { "chance", 0.5f }, { "offset", 6 }, { "gate", 2 }, { "grid", sixteenth } } },
        // Every other bar the last beat stutters: a turnaround.
        { "Zweitakt-Ende", "Beat Repeat",
          { { "interval", twoBars }, { "chance", 1.0f }, { "offset", 28 }, { "gate", 4 }, { "grid", sixteenth },
            { "decay", 0.3f } } },

        { "Zufalls-Splitter", "Glitch",
          { { "interval", quarterInterval }, { "chance", 0.3f }, { "offset", 1 }, { "gate", 2 },
            { "grid", thirtySecond }, { "variation", 0.6f } } },
        { "Rückwärts-Fetzen", "Glitch",
          { { "chance", 0.6f }, { "offset", 12 }, { "gate", 4 }, { "grid", eighth }, { "reverse", 1.0f } } },
        { "Bitsplitter", "Glitch",
          { { "interval", halfBar }, { "chance", 0.5f }, { "offset", 4 }, { "gate", 4 }, { "grid", sixtyFourth },
            { "pitch", 12 }, { "output", -2 } } },
        { "Chaos", "Glitch",
          { { "interval", halfBar }, { "chance", 0.6f }, { "offset", 2 }, { "gate", 6 }, { "grid", sixteenthTriplet },
            { "variation", 1.0f }, { "reverse", 0.5f }, { "pitch", -7 } } },

        // At the end of four bars the last beats roll up into the next part.
        { "Snare-Roll", "Fills",
          { { "interval", fourBars }, { "chance", 1.0f }, { "offset", 56 }, { "gate", 8 }, { "grid", sixteenth },
            { "pitch", 5 } } },
        { "Fill-Riser", "Fills",
          { { "interval", fourBars }, { "chance", 1.0f }, { "offset", 48 }, { "gate", 16 }, { "grid", sixteenth },
            { "pitch", 12 }, { "filter", 0.6f } } },
        { "Bandstopp", "Fills",
          { { "interval", fourBars }, { "chance", 1.0f }, { "offset", 60 }, { "gate", 4 }, { "grid", eighth },
            { "pitch", -24 }, { "decay", 0.5f } } },
        { "Abtauchen", "Fills",
          { { "interval", twoBars }, { "chance", 1.0f }, { "offset", 24 }, { "gate", 8 }, { "grid", eighth },
            { "filter", -0.8f }, { "decay", 0.3f } } },

        // Only the repeats come through: a rhythmic gate made from the music itself.
        { "Repeat-Gate", "Effekte",
          { { "interval", quarterInterval }, { "chance", 1.0f }, { "offset", 0 }, { "gate", 3 }, { "grid", sixteenth },
            { "mode", gate }, { "output", -4 } } },
        { "Dub-Wiederholung", "Effekte",
          { { "chance", 0.7f }, { "offset", 12 }, { "gate", 8 }, { "grid", eighthTriplet }, { "mode", mix },
            { "decay", 0.7f }, { "filter", -0.5f }, { "output", -2 } } },
        // Nothing by chance: Halten (to automate in Logic) repeats from the next sixteenth while it is on.
        { "Von Hand", "Effekte", { { "chance", 0.0f }, { "grid", sixteenth } } },
    };
    return presets;
}
} // namespace tonwerkstutter
