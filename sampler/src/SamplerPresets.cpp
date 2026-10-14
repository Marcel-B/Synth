#include "SamplerPresets.h"

namespace tonwerksampler
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float vocalAh = 0, vocalChops = 1, breakbeat = 2, piano = 3, strings = 4, bell = 5, eightOhEight = 6,
                choir = 7, guitar = 8;
constexpr float slices = 1;
constexpr float oneShot = 1;
constexpr float forward = 1, pingPong = 2;
constexpr float transients = 1;
constexpr float low12 = 1, low24 = 2, band12 = 4;
constexpr float triangle = 1, random = 5;
constexpr float toPitch = 1, toCutoff = 2, toPan = 4;
constexpr float legato = 2;
// The delay's note values (Tonwerk's list).
constexpr float delayDottedEighth = 9, delayEighth = 10;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // The 808 recorded at C1, gliding between notes the way trap basses do.
        { "808 Glide", "Bässe",
          { { "source", eightOhEight }, { "root", 36 }, { "voiceMode", legato }, { "glide", 70 },
            { "ampRelease", 120 }, { "velocity", 0.2f }, { "fxReverbMix", 0.0f }, { "master", -8 } } },
        { "808 Dreck", "Bässe",
          { { "source", eightOhEight }, { "root", 36 }, { "lofi", 0.35f }, { "filter", low24 }, { "cutoff", 2500 },
            { "ampRelease", 100 }, { "velocity", 0.2f }, { "fxReverbMix", 0.0f }, { "master", -8 } } },
        // The nylon string an octave down, its pluck closed by the filter envelope.
        { "Saitenbass", "Bässe",
          { { "source", guitar }, { "root", 48 }, { "filter", low24 }, { "cutoff", 400 }, { "envAmount", 0.45f },
            { "filterEnvDecay", 300 }, { "ampRelease", 120 }, { "fxReverbMix", 0.0f }, { "master", 2 } } },

        // The sung "ah" looped in its held part, a slow vibrato and a glide.
        { "Vokal-Lead", "Leads",
          { { "source", vocalAh }, { "loop", forward }, { "loopStart", 0.35f }, { "loopEnd", 0.8f },
            { "crossfade", 150 }, { "ampAttack", 20 }, { "ampRelease", 300 }, { "lfoTarget", toPitch },
            { "lfoAmount", 0.015f }, { "lfoRate", 5.5f }, { "voiceMode", legato }, { "glide", 80 },
            { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth }, { "master", -8 } } },
        { "Lo-Fi-Gesang", "Leads",
          { { "source", vocalAh }, { "loop", forward }, { "loopStart", 0.35f }, { "loopEnd", 0.8f },
            { "crossfade", 150 }, { "lofi", 0.55f }, { "filter", low12 }, { "cutoff", 3500 }, { "ampRelease", 250 },
            { "master", -7 } } },

        { "Streicherteppich", "Flächen",
          { { "source", strings }, { "loop", forward }, { "loopStart", 0.2f }, { "loopEnd", 0.9f },
            { "crossfade", 400 }, { "ampAttack", 400 }, { "ampRelease", 1200 }, { "fxReverbMix", 0.3f },
            { "fxReverbDecay", 3500 }, { "master", -14 } } },
        { "Chor-Fläche", "Flächen",
          { { "source", choir }, { "loop", forward }, { "loopStart", 0.2f }, { "loopEnd", 0.9f }, { "crossfade", 400 },
            { "ampAttack", 300 }, { "ampRelease", 1500 }, { "fxReverbMix", 0.35f }, { "fxReverbDecay", 4000 },
            { "master", -16 } } },
        // Backwards, so each chord swells into its cut-off start.
        { "Rückwärts-Chor", "Flächen",
          { { "source", choir }, { "reverse", 1 }, { "ampAttack", 50 }, { "ampRelease", 800 },
            { "fxReverbMix", 0.3f }, { "master", -15 } } },
        // The voice swinging back and forth through its loop, wandering between the speakers.
        { "Vokal-Pad", "Flächen",
          { { "source", vocalAh }, { "loop", pingPong }, { "loopStart", 0.3f }, { "loopEnd", 0.7f },
            { "ampAttack", 600 }, { "ampRelease", 1500 }, { "filter", low12 }, { "cutoff", 2500 },
            { "lfoTarget", toPan }, { "lfoAmount", 0.5f }, { "lfoRate", 0.3f }, { "fxReverbMix", 0.3f },
            { "master", -15 } } },

        { "Tine-Piano", "Tasten",
          { { "source", piano }, { "ampRelease", 400 }, { "velocity", 0.7f }, { "fxReverbMix", 0.15f },
            { "master", -14 } } },
        // The piano through a worn sampler and a slow wobble, as lo-fi hip-hop has it.
        { "Lo-Fi-Keys", "Tasten",
          { { "source", piano }, { "lofi", 0.45f }, { "filter", low12 }, { "cutoff", 3500 }, { "lfoTarget", toPitch },
            { "lfoAmount", 0.012f }, { "lfoRate", 0.6f }, { "lfoShape", triangle }, { "ampRelease", 300 },
            { "master", -14 } } },

        { "Nylon", "Plucks", { { "source", guitar }, { "ampRelease", 300 }, { "fxReverbMix", 0.15f }, { "master", -8 } } },
        // The first sung syllable alone, played as notes.
        { "Chop-Melodie", "Plucks",
          { { "source", vocalChops }, { "end", 0.11f }, { "playback", oneShot }, { "fxDelayMix", 0.25f },
            { "fxDelaySync", 1 }, { "fxDelayDivision", delayEighth }, { "master", -5 } } },

        { "Kirchenglocke", "Glocken",
          { { "source", bell }, { "tune", -12 }, { "ampRelease", 2500 }, { "fxReverbMix", 0.3f },
            { "fxReverbDecay", 4000 }, { "master", -16 } } },
        { "Spieluhr", "Glocken",
          { { "source", bell }, { "tune", 12 }, { "ampRelease", 800 }, { "lofi", 0.15f }, { "fxReverbMix", 0.2f },
            { "master", -9 } } },

        { "Pizzicato", "Bläser & Streicher",
          { { "source", strings }, { "start", 0.1f }, { "ampDecay", 450 }, { "ampSustain", 0.0f },
            { "ampRelease", 150 }, { "filter", low12 }, { "cutoff", 3000 }, { "master", 3 } } },
        { "Streicher legato", "Bläser & Streicher",
          { { "source", strings }, { "start", 0.05f }, { "loop", forward }, { "loopStart", 0.2f }, { "loopEnd", 0.9f },
            { "crossfade", 400 }, { "ampAttack", 120 }, { "ampRelease", 500 }, { "master", -8 } } },

        // Slices from C1 on, each key a syllable cut at its start.
        { "Vokal-Chops", "Effekte",
          { { "source", vocalChops }, { "mode", slices }, { "slices", 8 }, { "sliceMode", transients },
            { "playback", oneShot }, { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayEighth },
            { "master", 1 } } },
        { "Breakbeat-Slices", "Effekte",
          { { "source", breakbeat }, { "mode", slices }, { "slices", 16 }, { "playback", oneShot },
            { "fxReverbMix", 0.05f }, { "master", 2 } } },
        { "Break rückwärts", "Effekte",
          { { "source", breakbeat }, { "mode", slices }, { "slices", 16 }, { "reverse", 1 }, { "playback", oneShot },
            { "choke", 0 }, { "fxReverbMix", 0.1f }, { "master", 2 } } },
        // The chops through a sweeping bandpass, each a little different.
        { "Funk-Chops", "Effekte",
          { { "source", vocalChops }, { "mode", slices }, { "slices", 8 }, { "sliceMode", transients },
            { "playback", oneShot }, { "filter", band12 }, { "cutoff", 1200 }, { "resonance", 0.5f },
            { "lfoTarget", toCutoff }, { "lfoAmount", 0.4f }, { "lfoShape", random }, { "lfoRate", 6.0f },
            { "master", 6 } } },
    };
    return presets;
}

bool playsSlices(const FactoryPreset& preset)
{
    for (const auto& [id, value] : preset.values)
        if (id == "mode" && value > 0.5f)
            return true;
    return false;
}
} // namespace tonwerksampler
