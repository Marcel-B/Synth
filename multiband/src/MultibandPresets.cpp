#include "MultibandPresets.h"

namespace tonwerkmultiband
{
const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Defaults: splits at 200 Hz and 2.5 kHz, every band -20 dB at 3:1, attacks 20/10/5 ms, releases 150/120/80 ms.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        // Gentle on the whole mix: low ratios, slow attacks that let the hits through.
        { "Sanfter Master", "Mix",
          { { "lowThreshold", -18 }, { "lowRatio", 2 }, { "lowAttack", 30 }, { "lowRelease", 200 },
            { "midThreshold", -20 }, { "midRatio", 1.5f }, { "midAttack", 20 }, { "midRelease", 150 },
            { "highThreshold", -22 }, { "highRatio", 2 }, { "highAttack", 10 }, { "highRelease", 100 },
            { "output", 1 } } },
        { "Dichter Mix", "Mix",
          { { "lowThreshold", -24 }, { "lowRatio", 4 }, { "midThreshold", -24 }, { "midRatio", 3 },
            { "highThreshold", -26 }, { "highRatio", 3 }, { "highUpward", 0.2f }, { "lowGain", 2 }, { "midGain", 2 },
            { "highGain", 2 }, { "output", 1 } } },
        // Only the lows held in place; the rest passes.
        { "Tiefen zähmen", "Mix",
          { { "lowThreshold", -24 }, { "lowRatio", 4 }, { "lowAttack", 15 }, { "lowRelease", 150 }, { "midOn", 0 },
            { "highOn", 0 }, { "output", 2 } } },
        // The highs lifted from below: quiet detail comes up, loud cymbals stay where they were.
        { "Glanz", "Mix",
          { { "highSplit", 4000 }, { "highThreshold", -30 }, { "highRatio", 2 }, { "highUpward", 0.4f },
            { "lowOn", 0 }, { "midOn", 0 }, { "output", 0 } } },

        // Downward and upward at once, fast: everything pressed to the threshold, as the famous preset does.
        { "Volldruck", "Druck",
          { { "lowThreshold", -30 }, { "lowRatio", 8 }, { "lowUpward", 0.6f }, { "lowAttack", 10 },
            { "lowRelease", 100 }, { "midThreshold", -30 }, { "midRatio", 8 }, { "midUpward", 0.6f },
            { "midAttack", 5 }, { "midRelease", 80 }, { "highThreshold", -32 }, { "highRatio", 8 },
            { "highUpward", 0.6f }, { "highAttack", 2 }, { "highRelease", 60 }, { "output", 8 } } },
        { "Halber Druck", "Druck",
          { { "lowThreshold", -30 }, { "lowRatio", 8 }, { "lowUpward", 0.6f }, { "lowAttack", 10 },
            { "lowRelease", 100 }, { "midThreshold", -30 }, { "midRatio", 8 }, { "midUpward", 0.6f },
            { "midAttack", 5 }, { "midRelease", 80 }, { "highThreshold", -32 }, { "highRatio", 8 },
            { "highUpward", 0.6f }, { "highAttack", 2 }, { "highRelease", 60 }, { "depth", 0.5f }, { "output", 4 } } },
        { "Hyper", "Druck",
          { { "lowThreshold", -36 }, { "lowRatio", 20 }, { "lowUpward", 0.8f }, { "lowAttack", 5 }, { "lowRelease", 60 },
            { "midThreshold", -36 }, { "midRatio", 20 }, { "midUpward", 0.8f }, { "midAttack", 2 }, { "midRelease", 50 },
            { "highThreshold", -38 }, { "highRatio", 20 }, { "highUpward", 0.8f }, { "highAttack", 1 },
            { "highRelease", 40 }, { "output", 12 } } },
        // Squashed hard and mixed under the dry: the body of OTT, the transients of the original.
        { "Paralleldruck", "Druck",
          { { "lowThreshold", -36 }, { "lowRatio", 20 }, { "lowUpward", 1 }, { "midThreshold", -36 },
            { "midRatio", 20 }, { "midUpward", 1 }, { "highThreshold", -38 }, { "highRatio", 20 },
            { "highUpward", 1 }, { "mix", 0.4f }, { "output", 3 } } },

        // 30 ms on the lows lets the kick's front through before the compressor grabs.
        { "Drum-Bus", "Drums",
          { { "lowThreshold", -22 }, { "lowRatio", 4 }, { "lowAttack", 30 }, { "lowRelease", 100 },
            { "midThreshold", -22 }, { "midRatio", 3 }, { "midAttack", 15 }, { "midRelease", 80 },
            { "highThreshold", -28 }, { "highRatio", 2 }, { "highUpward", 0.3f }, { "output", 2 } } },
        { "Raum aufblasen", "Drums",
          { { "lowThreshold", -30 }, { "lowRatio", 6 }, { "lowUpward", 0.5f }, { "lowAttack", 5 },
            { "lowRelease", 200 }, { "midThreshold", -30 }, { "midRatio", 6 }, { "midUpward", 0.5f },
            { "midAttack", 3 }, { "midRelease", 200 }, { "highThreshold", -32 }, { "highRatio", 6 },
            { "highUpward", 0.5f }, { "highAttack", 1 }, { "highRelease", 150 }, { "mix", 0.6f }, { "output", 4 } } },

        // The sub held still, the growl's mids and highs lifted so every harmonic speaks.
        { "Bass-Kontrolle", "Bass",
          { { "lowSplit", 120 }, { "lowThreshold", -20 }, { "lowRatio", 8 }, { "lowAttack", 10 },
            { "lowRelease", 80 }, { "midThreshold", -26 }, { "midRatio", 3 }, { "midUpward", 0.3f },
            { "highOn", 0 }, { "output", 2 } } },
        { "Growl nach vorn", "Bass",
          { { "lowSplit", 150 }, { "highSplit", 3000 }, { "lowThreshold", -20 }, { "lowRatio", 4 },
            { "midThreshold", -30 }, { "midRatio", 6 }, { "midUpward", 0.7f }, { "midAttack", 3 },
            { "midRelease", 60 }, { "highThreshold", -32 }, { "highRatio", 6 }, { "highUpward", 0.7f },
            { "highAttack", 1 }, { "highRelease", 50 }, { "output", 3 } } },
        { "Sub festnageln", "Bass",
          { { "lowSplit", 100 }, { "lowThreshold", -24 }, { "lowRatio", 12 }, { "lowAttack", 5 },
            { "lowRelease", 60 }, { "midOn", 0 }, { "highOn", 0 }, { "output", 1 } } },

        // Only the sibilants above 5 kHz, quick in and out.
        { "De-Esser", "Gesang",
          { { "highSplit", 5000 }, { "highThreshold", -34 }, { "highRatio", 6 }, { "highAttack", 1 },
            { "highRelease", 50 }, { "lowOn", 0 }, { "midOn", 0 }, { "output", 0 } } },
        { "Stimme nach vorn", "Gesang",
          { { "lowSplit", 150 }, { "highSplit", 5000 }, { "lowThreshold", -26 }, { "lowRatio", 3 },
            { "midThreshold", -26 }, { "midRatio", 3 }, { "midUpward", 0.4f }, { "midAttack", 8 },
            { "midRelease", 100 }, { "highThreshold", -30 }, { "highRatio", 4 }, { "highAttack", 2 },
            { "output", 4 } } },
    };
    return presets;
}
} // namespace tonwerkmultiband
