#include "BassPresets.h"

namespace tonwerkbass
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float square = 1;
constexpr float sine = 0, squareLfo = 4, random = 5;
constexpr float toPitch = 1, toVolume = 2;
// LFO note values (Tonwerk Wavetable's list) and the delay's (Tonwerk's).
constexpr float lfoEighth = 9, lfoSixteenth = 11;
constexpr float delayDottedEighth = 9;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // The 303 as it left the box: a saw, the resonance up, the envelope doing the talking.
        { "Acid Klassik", "Bässe",
          { { "cutoff", 400 }, { "resonance", 0.75f }, { "envMod", 0.6f }, { "decay", 500 }, { "master", -3 } } },
        // Resonance near the edge and a short sweep: the squelch, louder still on accents.
        { "Squelch", "Bässe",
          { { "cutoff", 250 }, { "resonance", 0.92f }, { "envMod", 0.8f }, { "decay", 350 }, { "accent", 0.8f },
            { "sweep", 0.8f }, { "master", -4 } } },
        { "Rechteck-Acid", "Bässe",
          { { "wave", square }, { "cutoff", 350 }, { "resonance", 0.7f }, { "envMod", 0.55f }, { "decay", 400 },
            { "master", -7 } } },
        // Through a pedal after the filter, as most acid records were.
        { "Verzerrt 303", "Bässe",
          { { "cutoff", 300 }, { "resonance", 0.8f }, { "envMod", 0.7f }, { "decay", 400 }, { "drive", 0.6f },
            { "master", -6 } } },
        { "Tiefer Rumpel", "Bässe",
          { { "cutoff", 180 }, { "resonance", 0.3f }, { "envMod", 0.25f }, { "decay", 800 }, { "sub", 0.6f },
            { "master", -4 } } },
        // An octave below for the club's subs, the acid above it.
        { "Sub-Acid", "Bässe",
          { { "cutoff", 300 }, { "resonance", 0.7f }, { "sub", 0.5f }, { "accent", 0.3f }, { "master", -4 } } },
        // Long slides between held notes: the line stretches like rubber.
        { "Gummiband", "Bässe",
          { { "slide", 180 }, { "resonance", 0.6f }, { "envMod", 0.4f }, { "decay", 900 }, { "master", -2 } } },
        { "Kurz & trocken", "Bässe",
          { { "ampDecay", 700 }, { "decay", 200 }, { "envMod", 0.7f }, { "resonance", 0.5f }, { "accent", 0.3f }, { "master", 0 } } },
        // The cutoff on an eighth-note LFO over the acid: dubstep's wobble from a 303.
        { "Acid-Wobble", "Bässe",
          { { "resonance", 0.85f }, { "envMod", 0.2f }, { "lfoShape", sine }, { "lfoSync", 1 },
            { "lfoDivision", lfoEighth }, { "lfoAmount", 0.6f }, { "master", -3 } } },
        { "Dub-Acid", "Bässe",
          { { "resonance", 0.8f }, { "fxDelayMix", 0.3f }, { "fxDelaySync", 1 },
            { "fxDelayDivision", delayDottedEighth }, { "fxDelayFeedback", 0.5f }, { "master", -3 } } },
        { "Kellerclub", "Bässe",
          { { "cutoff", 220 }, { "resonance", 0.85f }, { "drive", 0.35f }, { "fxReverbMix", 0.15f },
            { "master", -9 } } },
        { "Rechteck tief", "Bässe",
          { { "wave", square }, { "cutoff", 200 }, { "envMod", 0.3f }, { "resonance", 0.4f }, { "sub", 0.4f },
            { "master", -7 } } },
        // The volume chopped in sixteenths: a gated acid line from held notes.
        { "Zerhackt", "Bässe",
          { { "lfoShape", squareLfo }, { "lfoSync", 1 }, { "lfoDivision", lfoSixteenth }, { "lfoTarget", toVolume },
            { "lfoAmount", 1.0f }, { "resonance", 0.75f }, { "accent", 0.3f }, { "master", -1 } } },

        // An octave up, distorted and echoed: the line that screams over the beat.
        { "Acid-Lead", "Leads",
          { { "tune", 12 }, { "resonance", 0.85f }, { "drive", 0.5f }, { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 },
            { "fxDelayDivision", delayDottedEighth }, { "master", -10 } } },
        { "Schreier", "Leads",
          { { "tune", 12 }, { "cutoff", 900 }, { "resonance", 0.97f }, { "envMod", 0.4f }, { "drive", 0.7f },
            { "master", -10 } } },
        { "Vibrato-Lead", "Leads",
          { { "tune", 12 }, { "cutoff", 1200 }, { "resonance", 0.5f }, { "release", 150 }, { "lfoShape", sine },
            { "lfoRate", 5 }, { "lfoTarget", toPitch }, { "lfoAmount", 0.15f }, { "fxReverbMix", 0.2f },
            { "master", -11 } } },

        { "Acid-Pluck", "Plucks",
          { { "ampDecay", 200 }, { "decay", 120 }, { "cutoff", 600 }, { "resonance", 0.7f }, { "fxReverbMix", 0.2f },
            { "master", -3 } } },

        // The resonance alone, whistling down with the envelope and following the keys.
        { "Pfeifender Filter", "Effekte",
          { { "cutoff", 200 }, { "resonance", 1.0f }, { "envMod", 0.9f }, { "decay", 2000 }, { "keytrack", 1.0f },
            { "fxReverbMix", 0.25f }, { "master", -9 } } },
        // A new cutoff on every sixteenth, as if someone turned the knob at random.
        { "Zufallsfilter", "Effekte",
          { { "resonance", 0.85f }, { "envMod", 0.2f }, { "lfoShape", random }, { "lfoSync", 1 },
            { "lfoDivision", lfoSixteenth }, { "lfoAmount", 0.8f }, { "master", -4 } } },
    };
    return presets;
}
} // namespace tonwerkbass
