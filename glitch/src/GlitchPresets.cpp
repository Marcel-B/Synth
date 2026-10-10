#include "GlitchPresets.h"

namespace chromeglitch
{
namespace
{
constexpr float eighth = 0, sixteenth = 1, thirtySecond = 2;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Percent as 0 to 1, lengths in ms, pitch in semitones, the grid as its menu index. Unnamed knobs keep their
    // defaults: amount 50 %, stutter 50 % of 50 ms four times, dropouts 30 % of 100 ms, crusher 50 %, pitch 30 % within
    // 7 semitones, sync on the sixteenths with 30 % chaos, mix 100 %.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // Now and then a syllable catches, the voice otherwise clean: for a whole verse.
        { "Leichtes Flackern", "Dezent",
          { { "amount", 0.25f }, { "stutterChance", 0.4f }, { "stutterLength", 40 }, { "repeats", 2 },
            { "dropoutChance", 0.2f }, { "dropoutLength", 40 }, { "crush", 0.2f }, { "pitchChance", 0 } } },
        // Only dropouts, short and off the grid: a loose cable.
        { "Wackelkontakt", "Dezent",
          { { "amount", 0.45f }, { "stutterChance", 0 }, { "dropoutChance", 0.7f }, { "dropoutLength", 30 },
            { "crush", 0.3f }, { "pitchChance", 0 }, { "sync", 0 }, { "chaos", 0.6f } } },
        // A call on a weak network: crushed throughout, words hang and drop.
        { "Funkloch", "Dezent",
          { { "amount", 0.4f }, { "stutterChance", 0.35f }, { "stutterLength", 30 }, { "repeats", 3 },
            { "dropoutChance", 0.5f }, { "dropoutLength", 80 }, { "crush", 0.9f }, { "pitchChance", 0 },
            { "sync", 0 } } },

        // A syllable repeated over an eighth: 60 ms four times is about one eighth at 120 BPM.
        { "Achtel-Stotter", "Rhythmisch",
          { { "amount", 0.7f }, { "stutterChance", 0.7f }, { "stutterLength", 60 }, { "repeats", 4 },
            { "dropoutChance", 0.2f }, { "crush", 0.3f }, { "pitchChance", 0 }, { "division", eighth },
            { "chaos", 0 } } },
        { "Sechzehntel-Roll", "Rhythmisch",
          { { "amount", 0.7f }, { "stutterChance", 0.6f }, { "stutterLength", 30 }, { "repeats", 4 },
            { "dropoutChance", 0.15f }, { "crush", 0.35f }, { "pitchChance", 0.15f }, { "pitchRange", 12 },
            { "division", sixteenth }, { "chaos", 0.1f } } },
        // Each repeat shorter than the last: the stutter speeds up into the next word.
        { "Stotter-Rampe", "Rhythmisch",
          { { "amount", 0.7f }, { "stutterChance", 0.6f }, { "stutterLength", 80 }, { "repeats", 10 },
            { "accelerate", 0.5f }, { "dropoutChance", 0 }, { "crush", 0.4f }, { "pitchChance", 0 },
            { "division", eighth }, { "chaos", 0 } } },
        // Dropouts on the thirty-seconds: the voice chopped like a gate.
        { "Zerhacker", "Rhythmisch",
          { { "amount", 0.8f }, { "stutterChance", 0 }, { "dropoutChance", 0.6f }, { "dropoutLength", 40 },
            { "crush", 0.2f }, { "pitchChance", 0 }, { "division", thirtySecond }, { "chaos", 0 } } },

        { "Kernschmelze", "Zerstört",
          { { "amount", 1.0f }, { "stutterChance", 0.8f }, { "stutterLength", 50 }, { "repeats", 8 },
            { "accelerate", 0.4f }, { "dropoutChance", 0.4f }, { "dropoutLength", 120 }, { "crush", 0.9f },
            { "pitchChance", 0.6f }, { "pitchRange", 12 }, { "chaos", 0.7f } } },
        // Pitch thrown over two octaves, off any grid.
        { "Cyberpsychose", "Zerstört",
          { { "amount", 0.9f }, { "stutterChance", 0.7f }, { "stutterLength", 35 }, { "repeats", 6 },
            { "dropoutChance", 0.3f }, { "dropoutLength", 60 }, { "crush", 0.7f }, { "pitchChance", 0.8f },
            { "pitchRange", 24 }, { "sync", 0 }, { "chaos", 1.0f } } },
        // Long dropouts and a crusher at full: the implant shutting down.
        { "Systemabsturz", "Zerstört",
          { { "amount", 0.85f }, { "stutterChance", 0.4f }, { "stutterLength", 100 }, { "repeats", 3 },
            { "dropoutChance", 0.7f }, { "dropoutLength", 250 }, { "crush", 1.0f }, { "pitchChance", 0.2f },
            { "pitchRange", 5 }, { "division", eighth }, { "chaos", 0.5f } } },

        // 10 ms slices repeated many times buzz at 100 Hz: a voice turned machine.
        { "Roboterstimme", "Klangeffekte",
          { { "amount", 0.8f }, { "stutterChance", 1.0f }, { "stutterLength", 10 }, { "repeats", 16 },
            { "dropoutChance", 0 }, { "crush", 0.5f }, { "pitchChance", 0 }, { "sync", 0 }, { "chaos", 0 } } },
        // Repeats jump by up to an octave, as if the voice skipped between channels.
        { "Tonhöhen-Sprünge", "Klangeffekte",
          { { "amount", 0.75f }, { "stutterChance", 0.7f }, { "stutterLength", 70 }, { "repeats", 4 },
            { "dropoutChance", 0.1f }, { "crush", 0.2f }, { "pitchChance", 0.9f }, { "pitchRange", 12 },
            { "division", eighth }, { "chaos", 0.2f } } },
        // Half dry: the glitches sit behind the voice instead of replacing it.
        { "Geisterecho", "Klangeffekte",
          { { "amount", 0.8f }, { "stutterChance", 0.8f }, { "stutterLength", 120 }, { "repeats", 3 },
            { "dropoutChance", 0 }, { "crush", 0.6f }, { "pitchChance", 0.4f }, { "pitchRange", 12 },
            { "division", eighth }, { "mix", 0.5f } } },
    };
    return presets;
}
} // namespace chromeglitch
