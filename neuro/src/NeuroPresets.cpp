#include "NeuroPresets.h"

#include "NeuroEngine.h"

namespace tonwerkneuro
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float bandpass = (float) FilterType::bandpass, notch = (float) FilterType::notch,
                comb = (float) FilterType::comb;
constexpr float hard = (float) DistortionMode::hard, fold = (float) DistortionMode::fold,
                tube = (float) DistortionMode::tube;
constexpr float triangle = (float) LfoShape::triangle, rampUp = (float) LfoShape::rampUp,
                square = (float) LfoShape::square;
// `kRatios` indices.
constexpr float ratio1 = 2, ratio1half = 3, ratio2 = 4, ratio3 = 5, ratio4 = 6;
// `tonwerkwave::kDivisions` indices.
constexpr float whole = 2, quarter = 6, eighth = 9, eighthTriplet = 10, sixteenth = 11;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    // Percent as 0 to 1, times in ms, menus as indices; master in dB, leveled to -12 LUFS (PresetTests).
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // The Reese of drum and bass: two saws beating against each other, the sub under them.
        { "Klassik-Reese", "Bässe",
          { { "detune", 20 }, { "width", 0.7f }, { "sub", 0.6f }, { "cutoff", 700 }, { "envCutoff", 0 },
            { "drive", 0.2f }, { "master", -3 } } },
        { "Dunkle Reese", "Bässe",
          { { "detune", 12 }, { "cutoff", 260 }, { "resonance", 0.2f }, { "envCutoff", 0.1f }, { "sub", 0.7f },
            { "master", -4 } } },
        { "Hoover-Reese", "Bässe",
          { { "detune", 45 }, { "width", 1.0f }, { "cutoff", 1500 }, { "envCutoff", 0 }, { "glide", 120 },
            { "drive", 0.35f }, { "master", -6 } } },
        { "Röhren-Reese", "Bässe",
          { { "detune", 16 }, { "driveMode", tube }, { "drive", 0.6f }, { "cutoff", 900 }, { "resonance", 0.35f },
            { "master", -7 } } },

        // Neuro: FM for the grit, folded, and a band moving through it.
        { "Neuro-Growl", "Bässe",
          { { "reese", 0.3f }, { "fm", 0.8f }, { "ratio", ratio2 }, { "fmIndex", 0.45f }, { "feedback", 0.3f },
            { "driveMode", fold }, { "drive", 0.55f }, { "filterType", bandpass }, { "cutoff", 700 },
            { "resonance", 0.5f }, { "envCutoff", 0 }, { "lfoCutoff", 0.45f }, { "lfoFm", 0.35f },
            { "lfoDivision", eighth }, { "master", -11 } } },
        { "Kamm-Neuro", "Bässe",
          { { "reese", 0.4f }, { "fm", 0.6f }, { "ratio", ratio3 }, { "fmIndex", 0.3f }, { "filterType", comb },
            { "cutoff", 300 }, { "resonance", 0.7f }, { "envCutoff", 0 }, { "lfoCutoff", 0.3f },
            { "lfoShape", triangle }, { "lfoDivision", quarter }, { "drive", 0.4f }, { "master", -9 } } },
        { "FM-Biss", "Bässe",
          { { "reese", 0.2f }, { "fm", 0.9f }, { "ratio", ratio4 }, { "fmIndex", 0.15f }, { "envFm", 0.8f },
            { "modDecay", 150 }, { "drive", 0.5f }, { "cutoff", 1200 }, { "envCutoff", 0.3f }, { "master", -10 } } },

        // Wobbles on the song's grid; the LFO starts closed with each note.
        { "Wobble Achtel", "Bässe",
          { { "reese", 0.6f }, { "fm", 0.4f }, { "fmIndex", 0.3f }, { "cutoff", 180 }, { "resonance", 0.5f },
            { "envCutoff", 0 }, { "lfoCutoff", 0.8f }, { "lfoDivision", eighth }, { "drive", 0.4f },
            { "master", -7 } } },
        { "Wobble Triolen", "Bässe",
          { { "reese", 0.6f }, { "fm", 0.4f }, { "fmIndex", 0.3f }, { "cutoff", 180 }, { "resonance", 0.5f },
            { "envCutoff", 0 }, { "lfoCutoff", 0.8f }, { "lfoDivision", eighthTriplet }, { "drive", 0.4f },
            { "master", -7 } } },
        { "Riddim-Stotter", "Bässe",
          { { "reese", 0.5f }, { "fm", 0.5f }, { "ratio", ratio1 }, { "fmIndex", 0.35f }, { "driveMode", hard },
            { "drive", 0.6f }, { "cutoff", 150 }, { "resonance", 0.4f }, { "envCutoff", 0 }, { "lfoShape", square },
            { "lfoCutoff", 0.7f }, { "lfoDivision", sixteenth }, { "master", -8 } } },
        // "Yoi": a notch sweeping through FM, so the bass talks.
        { "Yoi-Bass", "Bässe",
          { { "reese", 0.4f }, { "fm", 0.7f }, { "ratio", ratio1half }, { "fmIndex", 0.4f }, { "filterType", notch },
            { "cutoff", 400 }, { "resonance", 0.6f }, { "envCutoff", 0 }, { "lfoCutoff", 0.6f }, { "lfoFm", 0.3f },
            { "lfoDivision", quarter }, { "drive", 0.5f }, { "master", -12 } } },
        { "Sub-Druck", "Bässe",
          { { "reese", 0.3f }, { "sub", 1.0f }, { "cutoff", 160 }, { "envCutoff", 0 }, { "width", 0.3f },
            { "master", -3 } } },

        { "Neuro-Lead", "Leads",
          { { "reese", 0.5f }, { "fm", 0.6f }, { "ratio", ratio1half }, { "fmIndex", 0.25f }, { "sub", 0 },
            { "cutoff", 2200 }, { "resonance", 0.3f }, { "envCutoff", 0.2f }, { "drive", 0.45f }, { "glide", 60 },
            { "master", -10 } } },
        // Over a whole bar the filter climbs from closed to open: a riser on one held note.
        { "Bass-Riser", "Effekte",
          { { "reese", 0.7f }, { "fm", 0.3f }, { "cutoff", 120 }, { "resonance", 0.6f }, { "envCutoff", 0 },
            { "lfoShape", rampUp }, { "lfoCutoff", 1.0f }, { "lfoDivision", whole }, { "drive", 0.4f },
            { "master", -8 } } },
    };
    return presets;
}
} // namespace tonwerkneuro
