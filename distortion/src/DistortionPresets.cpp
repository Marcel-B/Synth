#include "DistortionPresets.h"

namespace tonwerkdistortion
{
const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Percent as 0 to 1, input and level in whole dB. The levels are set so each sound matches its dry source.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },
        // Just breaking up: the clean note with a little hair when struck hard.
        { "Leicht angezerrt", "Gitarre", { { "input", -12 }, { "distortion", 0.1f }, { "tone", 0.6f }, { "level", -11 } } },
        { "Crunch", "Gitarre", { { "input", -3 }, { "distortion", 0.3f }, { "tone", 0.5f }, { "level", -10 } } },
        // Chords stay readable: medium gain, the mids a little forward of the pedal's scoop.
        { "Rock-Rhythmus", "Gitarre", { { "distortion", 0.55f }, { "tone", 0.45f }, { "level", -10 } } },
        // Darker and softer, for slow bends.
        { "Blues-Solo", "Gitarre", { { "input", -3 }, { "distortion", 0.45f }, { "tone", 0.3f }, { "level", -7 } } },
        { "Solo-Lead", "Gitarre", { { "input", 3 }, { "distortion", 0.85f }, { "tone", 0.6f }, { "level", -12 } } },
        // Everything up: the scoop and the fizz are the sound.
        { "Metal", "Gitarre", { { "input", 9 }, { "distortion", 1.0f }, { "tone", 0.7f }, { "level", -14 } } },
        { "Fuzz-Wand", "Gitarre", { { "input", 18 }, { "distortion", 1.0f }, { "tone", 0.35f }, { "level", -8 } } },

        // A bass keeps its low end only with the tone towards dark: the bright side is a high-pass.
        { "Bass-Knurren", "Bass", { { "input", -6 }, { "distortion", 0.35f }, { "tone", 0.2f }, { "level", -5 } } },
        { "Bass-Fuzz", "Bass", { { "input", 6 }, { "distortion", 0.9f }, { "tone", 0.3f }, { "level", -6 } } },

        // A synth's line level is hotter than a guitar's; the input brings it to where the pedal is gentle.
        { "Synth-Wärme", "Synths", { { "input", -15 }, { "distortion", 0.15f }, { "tone", 0.45f }, { "level", -2 } } },
        { "Acid-Biss", "Synths", { { "distortion", 0.75f }, { "tone", 0.6f }, { "level", 0 } } },
        { "Lo-Fi-Lead", "Synths", { { "input", 6 }, { "distortion", 1.0f }, { "tone", 0.55f }, { "level", -1 } } },

        // Drums: a little grit on the bus, or smashed flat.
        { "Drum-Dreck", "Drums", { { "input", -12 }, { "distortion", 0.25f }, { "tone", 0.55f }, { "level", -12 } } },
        { "Zertrümmert", "Drums", { { "input", 6 }, { "distortion", 1.0f }, { "tone", 0.6f }, { "level", -15 } } },

        // The bright side thins the voice out like a cheap speaker.
        { "Megafon", "Gesang", { { "input", -6 }, { "distortion", 0.5f }, { "tone", 0.9f }, { "level", -10 } } },
        { "Schrei", "Gesang", { { "input", 3 }, { "distortion", 0.8f }, { "tone", 0.6f }, { "level", -6 } } },

        // The modes for electronic sounds (mode: 1 tube, 2 clipper, 3 folder, 4 rectifier).
        // A little tube on a pad or a bass: even harmonics, the top rounded off.
        { "Röhren-Wärme", "Synths", { { "mode", 1 }, { "distortion", 0.25f }, { "tone", 0.4f }, { "level", 2 } } },
        { "Röhren-Bass", "Bass", { { "mode", 1 }, { "distortion", 0.55f }, { "tone", 0.4f }, { "level", -4 } } },
        // Kick and loop flattened at the top for loudness, the tone left flat.
        { "Kick-Clipper", "Drums", { { "mode", 2 }, { "distortion", 0.3f }, { "level", 1 } } },
        { "Hart geclippt", "Drums", { { "mode", 2 }, { "distortion", 0.7f }, { "tone", 0.6f }, { "level", -5 } } },
        // West Coast: folds that open as the note gets louder, so velocity and envelopes play the timbre.
        { "West-Coast-Falter", "Synths", { { "mode", 3 }, { "distortion", 0.45f }, { "tone", 0.45f }, { "level", -3 } } },
        { "Falter-Bass", "Bass", { { "mode", 3 }, { "distortion", 0.7f }, { "tone", 0.35f }, { "level", -8 } } },
        // The octave above a lead, and the spit of a gated fuzz on a bass.
        { "Oktav-Fuzz", "Synths", { { "mode", 4 }, { "distortion", 0.6f }, { "tone", 0.55f }, { "level", 2 } } },
        { "Gleichrichter-Bass", "Bass", { { "mode", 4 }, { "distortion", 0.8f }, { "tone", 0.3f }, { "level", -4 } } },
    };
    return presets;
}
} // namespace tonwerkdistortion
