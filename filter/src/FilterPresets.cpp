#include "FilterPresets.h"

#include "FilterEngine.h"

namespace tonwerkfilter
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float lowpass12 = (float) Type::lowpass12, lowpass24 = (float) Type::lowpass24, ladder = (float) Type::ladder,
                highpass24 = (float) Type::highpass24, bandpass = (float) Type::bandpass, notch = (float) Type::notch,
                comb = (float) Type::comb;
constexpr float triangle = (float) Shape::triangle, sawUp = (float) Shape::sawUp, sawDown = (float) Shape::sawDown,
                square = (float) Shape::square, random = (float) Shape::random;
// `kDivisions` indices.
constexpr float fourBars = 0, wholeNote = 2, eighth = 10, eighthTriplet = 11, sixteenth = 13;
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Percent as 0 to 1, the envelope -1 to 1, times in ms, levels in dB.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // The follower opens a peak as the note hits and lets it fall with the note: the wah plays itself.
        { "Funk-Wah", "Auto-Wah",
          { { "type", bandpass }, { "cutoff", 350 }, { "resonance", 0.6f }, { "envAmount", 0.6f }, { "attack", 5 },
            { "release", 120 }, { "output", 8 } } },
        { "Bass-Hüllkurve", "Auto-Wah",
          { { "type", lowpass24 }, { "cutoff", 180 }, { "resonance", 0.5f }, { "envAmount", 0.7f }, { "attack", 3 },
            { "release", 180 }, { "output", 1 } } },
        { "Quaken", "Auto-Wah",
          { { "type", ladder }, { "cutoff", 300 }, { "resonance", 0.7f }, { "envAmount", 0.8f }, { "attack", 2 },
            { "release", 80 }, { "output", 1 } } },
        { "Sanftes Öffnen", "Auto-Wah",
          { { "type", lowpass12 }, { "cutoff", 300 }, { "envAmount", 0.5f }, { "attack", 80 }, { "release", 600 },
            { "output", 1 } } },
        // Negative: the louder the note, the darker; a pad ducks its own brightness.
        { "Umgekehrt", "Auto-Wah",
          { { "type", lowpass24 }, { "cutoff", 6000 }, { "resonance", 0.4f }, { "envAmount", -0.6f },
            { "attack", 10 }, { "release", 300 }, { "output", 1 } } },

        // On the song's grid: the LFO restarts with the bars wherever playback starts.
        { "Achtel-Pumpe", "Rhythmisch",
          { { "type", lowpass24 }, { "cutoff", 700 }, { "resonance", 0.4f }, { "lfoSync", 1 }, { "lfoShape", sawDown },
            { "lfoDivision", eighth }, { "lfoAmount", 0.6f }, { "output", 1 } } },
        { "Sechzehntel-Gate", "Rhythmisch",
          { { "type", ladder }, { "cutoff", 500 }, { "resonance", 0.5f }, { "lfoSync", 1 }, { "lfoShape", square },
            { "lfoDivision", sixteenth }, { "lfoAmount", 0.7f }, { "output", 4 } } },
        { "Zufalls-Treppe", "Rhythmisch",
          { { "type", bandpass }, { "cutoff", 1200 }, { "resonance", 0.55f }, { "lfoSync", 1 }, { "lfoShape", random },
            { "lfoDivision", sixteenth }, { "lfoAmount", 0.6f }, { "spread", 90 }, { "output", 3 } } },
        { "Triolen-Wobble", "Rhythmisch",
          { { "type", ladder }, { "cutoff", 400 }, { "resonance", 0.6f }, { "drive", 6 }, { "lfoSync", 1 },
            { "lfoDivision", eighthTriplet }, { "lfoAmount", 0.7f }, { "output", 0 } } },

        // Slow movements over bars, for breakdowns and builds.
        { "Langsamer Sweep", "Sweeps",
          { { "type", lowpass24 }, { "cutoff", 1200 }, { "resonance", 0.4f }, { "lfoRate", 0.1f },
            { "lfoAmount", 0.8f }, { "output", 1 } } },
        { "Hochpass-Riser", "Sweeps",
          { { "type", highpass24 }, { "cutoff", 300 }, { "resonance", 0.35f }, { "lfoSync", 1 }, { "lfoShape", sawUp },
            { "lfoDivision", fourBars }, { "lfoAmount", 0.9f }, { "output", 1 } } },
        { "Notch-Phaser", "Sweeps",
          { { "type", notch }, { "cutoff", 1000 }, { "resonance", 0.3f }, { "lfoShape", triangle }, { "lfoRate", 0.3f },
            { "lfoAmount", 0.7f }, { "spread", 90 }, { "output", 0 } } },
        { "Stereo-Kreisen", "Sweeps",
          { { "type", bandpass }, { "cutoff", 900 }, { "resonance", 0.3f }, { "lfoSync", 1 },
            { "lfoDivision", wholeNote }, { "lfoAmount", 0.6f }, { "spread", 180 }, { "output", 6 } } },

        // Fixed colours: a band, a resonance, a comb.
        { "Telefon", "Klangfarbe",
          { { "type", bandpass }, { "cutoff", 1500 }, { "resonance", 0.3f }, { "drive", 6 }, { "output", 6 } } },
        { "Dunkler Ladder", "Klangfarbe",
          { { "type", ladder }, { "cutoff", 600 }, { "resonance", 0.5f }, { "drive", 12 }, { "output", 0 } } },
        { "Acid-Kante", "Klangfarbe",
          { { "type", ladder }, { "cutoff", 500 }, { "resonance", 0.85f }, { "drive", 6 }, { "envAmount", 0.7f },
            { "attack", 2 }, { "release", 150 }, { "output", 0 } } },
        { "Kamm-Metall", "Klangfarbe",
          { { "type", comb }, { "cutoff", 220 }, { "resonance", 0.8f }, { "output", -5 } } },
        { "Kamm-Flanger", "Klangfarbe",
          { { "type", comb }, { "cutoff", 500 }, { "resonance", 0.7f }, { "lfoShape", triangle }, { "lfoRate", 0.2f },
            { "lfoAmount", 0.5f }, { "spread", 90 }, { "mix", 0.5f }, { "output", 0 } } },
    };
    return presets;
}
} // namespace tonwerkfilter
