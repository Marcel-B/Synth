#include "VocoderPresets.h"

namespace tonwerkvocoder
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float bands8 = 0, bands12 = 1, bands24 = 3, bands32 = 4;
constexpr float square = 1, pulse = 2, superSaw = 3;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // Sixteen bands on a saw: the vocoder of late-seventies records.
        { "Studio 1978", "Klassisch", { { "output", 0 } } },
        { "Electro-Funk", "Klassisch",
          { { "bands", bands12 }, { "wave", square }, { "release", 40 }, { "output", 1 } } },
        // Many narrow bands and the hiss up: the synth that speaks the words most clearly.
        { "Sprechender Synth", "Klassisch",
          { { "bands", bands32 }, { "sharpness", 0.7f }, { "sibilance", 0.8f }, { "noise", 0.1f }, { "output", 2 } } },
        { "Weiche Stimme", "Klassisch",
          { { "sharpness", 0.2f }, { "attack", 20 }, { "release", 150 }, { "width", 0.3f }, { "output", 1 } } },

        // Three detuned saws per note, the bands spread wide: a choir from one voice.
        { "Roboterchor", "Chor",
          { { "wave", superSaw }, { "detune", 25 }, { "width", 1.0f }, { "output", -1 } } },
        { "Engelschor", "Chor",
          { { "wave", superSaw }, { "detune", 18 }, { "octave", 1 }, { "carrierAttack", 300 },
            { "carrierRelease", 1500 }, { "release", 200 }, { "width", 1.0f }, { "output", -2 } } },
        { "Tiefer Chor", "Chor",
          { { "wave", superSaw }, { "detune", 12 }, { "octave", -1 }, { "carrierRelease", 800 }, { "width", 0.8f },
            { "output", 0 } } },

        // Few bands on a square and fast followers: the tin voice of a sixties film robot.
        { "Blechstimme", "Roboter",
          { { "bands", bands8 }, { "wave", square }, { "attack", 2 }, { "release", 20 }, { "width", 0.0f },
            { "output", 2 } } },
        { "Androide", "Roboter",
          { { "bands", bands24 }, { "wave", pulse }, { "sharpness", 0.8f }, { "release", 30 }, { "output", 2 } } },
        { "Funkspruch", "Roboter",
          { { "bands", bands8 }, { "sharpness", 0.9f }, { "formant", 5 }, { "sibilance", 0.2f }, { "width", 0.0f },
            { "output", 3 } } },

        // The carrier's bands an octave and a half lower than the voice's: a monster.
        { "Monster", "Effekte", { { "formant", -9 }, { "octave", -1 }, { "output", 0 } } },
        { "Zwerg", "Effekte", { { "formant", 9 }, { "octave", 1 }, { "output", 0 } } },
        // Mostly noise for a carrier: the words whispered, the notes only a shimmer under them.
        { "Flüstern", "Effekte", { { "noise", 1.0f }, { "carrierRelease", 50 }, { "sibilance", 0.8f }, { "output", 0 } } },
        // Slow followers smear the words into a pad that breathes with the voice.
        { "Atmende Fläche", "Effekte",
          { { "wave", superSaw }, { "attack", 80 }, { "release", 800 }, { "carrierAttack", 500 },
            { "carrierRelease", 2000 }, { "width", 1.0f }, { "output", 1 } } },
    };
    return presets;
}
} // namespace tonwerkvocoder
