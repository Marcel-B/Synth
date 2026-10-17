#include "RobovoxPresets.h"

namespace tonwerkrobovox
{
const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Menus as the index of their entry (mode: Stimme, MIDI; key: C = 0 ... H = 11; scale: Chromatisch, Dur, Moll,
    // Pentatonik; wave: Sägezahn, Rechteck, Puls), percent as 0 to 1. The output levels match each sound to the voice.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },
        // Marcel's example: the carrier steps through the bass notes, two octaves under a man's voice, three under a
        // woman's, and the lowest harmonics stay up as a bass would.
        { "Neonstadt (Sänger)", "Tief", { { "bandwidth", 200 }, { "bass", 0.6f } } },
        { "Neonstadt (Sängerin)", "Tief", { { "octave", -3 }, { "bandwidth", 200 }, { "bass", 0.6f }, { "output", 2 } } },
        { "Dämon", "Tief", { { "octave", -3 }, { "formant", -5 }, { "bass", 0.8f }, { "sibilance", 0.3f }, { "output", 5 } } },
        // The carrier from a MIDI track in Logic's side chain: the bass line sings.
        { "MIDI-Bass", "Tief", { { "mode", 1 }, { "bass", 0.6f } } },

        // At the voice's own pitch: a classic vocoder with the notes snapped to the scale.
        { "Klassischer Vocoder", "Roboter", { { "octave", 0 }, { "scale", 0 }, { "bandwidth", 300 }, { "bass", 0.1f }, { "output", -3 } } },
        { "Harte Korrektur", "Roboter", { { "octave", 0 }, { "key", 0 }, { "scale", 1 }, { "bass", 0.0f }, { "output", -4 } } },
        { "Helm-Roboter", "Roboter", { { "octave", -1 }, { "wave", 1 }, { "formant", 2 }, { "bass", 0.2f }, { "output", -3 } } },
        { "Halb Mensch", "Roboter", { { "octave", 0 }, { "mix", 0.5f }, { "bass", 0.0f } } },

        { "Chip-Stimme", "Hell", { { "octave", 1 }, { "wave", 2 }, { "formant", 7 }, { "bass", 0.0f }, { "output", -5 } } },
        { "Androidin", "Hell", { { "octave", 0 }, { "formant", 4 }, { "bandwidth", 160 }, { "bass", 0.0f }, { "output", -4 } } },

        // A narrow pulse and wide bands: a voice from a cheap speaker.
        { "Funkgerät", "Effekte", { { "octave", 0 }, { "wave", 2 }, { "bandwidth", 600 }, { "sibilance", 1.0f }, { "bass", 0.0f }, { "output", -3 } } },
        { "Gleitender Roboter", "Effekte", { { "octave", -1 }, { "glide", 120 }, { "scale", 0 }, { "bass", 0.3f }, { "output", -2 } } },
    };
    return presets;
}
} // namespace tonwerkrobovox
