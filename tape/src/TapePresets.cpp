#include "TapePresets.h"

namespace tonwerktape
{
const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Percent as 0 to 1, tone in Hz, levels in whole dB, type as its menu entry (Spulenband, Chrom, Ferro,
    // Diktiergerät). The output levels match each sound to its dry source.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // A well kept machine: the saturation and the bump, the wobble barely there.
        { "Studio-Maschine", "Spulenband", { { "type", 0 }, { "drive", 0.3f }, { "tone", 20000 }, { "wow", 0.03f }, { "flutter", 0.05f }, { "noise", -84 }, { "output", -1 } } },
        { "Mastering-Band", "Spulenband", { { "type", 0 }, { "drive", 0.15f }, { "tone", 20000 }, { "wow", 0.0f }, { "flutter", 0.03f }, { "noise", -90 }, { "output", -1 } } },
        // Recorded too hot: the transients rounded off, the highs squashed first.
        { "Heißes Band", "Spulenband", { { "type", 0 }, { "input", 6 }, { "drive", 0.75f }, { "tone", 18000 }, { "wow", 0.05f }, { "flutter", 0.08f }, { "noise", -78 }, { "output", -5 } } },
        // Smashed tape under the dry signal: weight without losing the attack.
        { "Parallel-Band", "Spulenband", { { "type", 0 }, { "input", 6 }, { "drive", 1.0f }, { "tone", 16000 }, { "wow", 0.02f }, { "flutter", 0.05f }, { "noise", -80 }, { "mix", 0.5f }, { "output", -2 } } },

        { "Chromkassette", "Kassette", { { "type", 1 }, { "drive", 0.35f }, { "tone", 15000 }, { "wow", 0.12f }, { "flutter", 0.15f }, { "noise", -70 }, { "output", -1 } } },
        { "Ferro-Kassette", "Kassette", { { "type", 2 }, { "drive", 0.5f }, { "tone", 10000 }, { "wow", 0.2f }, { "flutter", 0.25f }, { "noise", -62 }, { "output", -1 } } },
        // A portable player on batteries: more wobble, duller heads.
        { "Walkman", "Kassette", { { "type", 2 }, { "drive", 0.45f }, { "tone", 8000 }, { "wow", 0.45f }, { "flutter", 0.5f }, { "noise", -58 }, { "output", -1 } } },
        { "Mixtape 1991", "Kassette", { { "type", 2 }, { "drive", 0.55f }, { "tone", 7000 }, { "wow", 0.3f }, { "flutter", 0.35f }, { "noise", -56 }, { "dropouts", 0.15f }, { "output", -1 } } },

        { "Lo-Fi Hip-Hop", "Lo-Fi", { { "type", 2 }, { "drive", 0.6f }, { "tone", 6000 }, { "wow", 0.35f }, { "flutter", 0.2f }, { "noise", -52 }, { "output", -1 } } },
        // Slow, deep wow for keys and pads: the chords sway.
        { "Chillhop-Keys", "Lo-Fi", { { "type", 2 }, { "drive", 0.4f }, { "tone", 5500 }, { "wow", 0.6f }, { "flutter", 0.15f }, { "noise", -56 }, { "output", -1 } } },
        { "Vaporwave", "Lo-Fi", { { "type", 1 }, { "drive", 0.3f }, { "tone", 9000 }, { "wow", 0.8f }, { "flutter", 0.1f }, { "noise", -60 }, { "output", -1 } } },

        // The tape got caught in the mechanism: everything the machine can do wrong.
        { "Bandsalat", "Kaputt", { { "type", 2 }, { "drive", 0.6f }, { "tone", 6000 }, { "wow", 1.0f }, { "flutter", 0.8f }, { "noise", -48 }, { "dropouts", 0.7f }, { "output", -1 } } },
        { "Diktiergerät", "Kaputt", { { "type", 3 }, { "drive", 0.7f }, { "tone", 6000 }, { "wow", 0.3f }, { "flutter", 0.6f }, { "noise", -44 }, { "output", 3 } } },
        { "Dachbodenfund", "Kaputt", { { "type", 2 }, { "drive", 0.5f }, { "tone", 4500 }, { "wow", 0.6f }, { "flutter", 0.5f }, { "noise", -46 }, { "dropouts", 0.4f }, { "output", -1 } } },
        { "Seekrank", "Kaputt", { { "type", 1 }, { "drive", 0.3f }, { "wow", 1.0f }, { "flutter", 0.0f }, { "noise", -66 }, { "output", -1 } } },
    };
    return presets;
}
} // namespace tonwerktape
