#include "PhysicalPresets.h"

namespace tonwerkphys
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float strike = 1, bow = 2, blow = 3;
constexpr float closedTube = 1, openTube = 2, bar = 3, bell = 4, membrane = 5, bowl = 6;
constexpr float guitar = 1, violin = 2, box = 3, soundboard = 4;
constexpr float toPitch = 1, toPressure = 2, toCutoff = 7, toAmp = 9;
constexpr float low12 = 0;
constexpr float triangle = 1;
constexpr float mono = 1;
// The delay's note values (Tonwerk's list): a dotted eighth.
constexpr float delayDottedEighth = 9;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // Plucked strings: a soft thumb near the middle and a guitar body, a bright pick near the bridge.
        { "Nylongitarre", "Plucks",
          { { "hardness", 0.35f }, { "noise", 0.15f }, { "position", 0.15f }, { "decay", 3500 }, { "damping", 0.55f },
            { "release", 250 }, { "body", guitar }, { "bodyMix", 0.6f }, { "master", -1 } } },
        { "Stahlsaite", "Plucks",
          { { "hardness", 0.75f }, { "noise", 0.3f }, { "position", 0.1f }, { "decay", 5000 }, { "damping", 0.2f },
            { "inharmonicity", 0.03f }, { "release", 400 }, { "body", guitar }, { "bodyMix", 0.4f }, { "master", 0 } } },
        // Harp strings have no dampers: they ring on after the finger leaves.
        { "Harfe", "Plucks",
          { { "hardness", 0.4f }, { "noise", 0.1f }, { "position", 0.3f }, { "decay", 6000 }, { "damping", 0.45f },
            { "release", 1500 }, { "fxReverbMix", 0.3f }, { "fxReverbDecay", 4000 }, { "master", -15 } } },
        { "Pizzicato", "Plucks",
          { { "hardness", 0.3f }, { "noise", 0.05f }, { "position", 0.25f }, { "decay", 700 }, { "damping", 0.6f },
            { "release", 150 }, { "body", violin }, { "bodyMix", 0.8f }, { "master", -3 } } },
        // Plucked tines: the bar's tuned overtones, short and wooden.
        { "Kalimba", "Plucks",
          { { "resonator", bar }, { "hardness", 0.55f }, { "position", 0.3f }, { "decay", 1800 }, { "damping", 0.5f },
            { "release", 1800 }, { "body", box }, { "bodyMix", 0.3f }, { "master", -7 } } },
        // Plucked close to the bridge, nasal; the wheel bends the string as a koto player presses it.
        { "Koto", "Plucks",
          { { "hardness", 0.7f }, { "noise", 0.2f }, { "position", 0.08f }, { "decay", 2500 }, { "damping", 0.35f },
            { "release", 800 }, { "body", box }, { "bodyMix", 0.5f }, { "wheelTarget", toPitch },
            { "wheelAmount", 0.17f }, { "master", 1 } } },

        // A felt hammer on a stiff string over a soundboard: the harder the key, the harder the felt.
        { "Hammerklavier", "Tasten",
          { { "exciter", strike }, { "hardness", 0.55f }, { "noise", 0.1f }, { "position", 0.12f },
            { "decay", 9000 }, { "damping", 0.45f }, { "inharmonicity", 0.12f }, { "release", 400 },
            { "body", soundboard }, { "bodyMix", 0.5f }, { "velocityAmount", 0.7f }, { "dynamics", 0.7f },
            { "width", 0.6f }, { "fxReverbMix", 0.2f }, { "master", -10 } } },
        { "Hackbrett", "Tasten",
          { { "exciter", strike }, { "hardness", 0.8f }, { "noise", 0.2f }, { "position", 0.1f }, { "decay", 5000 },
            { "damping", 0.25f }, { "inharmonicity", 0.05f }, { "release", 2500 }, { "body", box },
            { "bodyMix", 0.5f }, { "master", -9 } } },
        // A quill plucking right at the end of the string: thin, bright and short once the key is up.
        { "Kielklavier", "Tasten",
          { { "hardness", 0.9f }, { "noise", 0.1f }, { "position", 0.07f }, { "decay", 3000 }, { "damping", 0.2f },
            { "release", 200 }, { "body", soundboard }, { "bodyMix", 0.4f }, { "master", -1 } } },
        { "Funk-Clavi", "Tasten",
          { { "hardness", 0.85f }, { "noise", 0.15f }, { "position", 0.06f }, { "decay", 900 }, { "damping", 0.3f },
            { "release", 60 }, { "dynamics", 0.5f }, { "master", 5 } } },

        { "Marimba", "Glocken",
          { { "resonator", bar }, { "exciter", strike }, { "hardness", 0.45f }, { "noise", 0 }, { "position", 0.25f },
            { "decay", 1400 }, { "damping", 0.5f }, { "release", 1400 }, { "body", box }, { "bodyMix", 0.4f },
            { "master", 0 } } },
        // The bars ring long under a damper pedal, the motor's tremolo on the level.
        { "Vibraphon", "Glocken",
          { { "resonator", bar }, { "exciter", strike }, { "hardness", 0.6f }, { "noise", 0 }, { "position", 0.25f },
            { "decay", 6000 }, { "damping", 0.4f }, { "release", 300 }, { "lfo1Target", toAmp },
            { "lfo1Amount", 0.35f }, { "lfo1Rate", 5.0f }, { "fxReverbMix", 0.25f }, { "master", -8 } } },
        { "Kirchenglocke", "Glocken",
          { { "resonator", bell }, { "exciter", strike }, { "hardness", 0.8f }, { "noise", 0.1f },
            { "position", 0.3f }, { "decay", 12000 }, { "damping", 0.35f }, { "release", 6000 }, { "octave", -1 },
            { "fxReverbMix", 0.35f }, { "fxReverbDecay", 5000 }, { "master", -16 } } },
        // A bar stretched out of tune, as the long tubes of an orchestra's chimes are.
        { "Röhrenglocke", "Glocken",
          { { "resonator", bar }, { "exciter", strike }, { "hardness", 0.85f }, { "noise", 0.05f },
            { "position", 0.1f }, { "decay", 7000 }, { "damping", 0.3f }, { "inharmonicity", 0.35f },
            { "release", 3000 }, { "fxReverbMix", 0.25f }, { "master", -6 } } },
        { "Glasschale", "Glocken",
          { { "resonator", bowl }, { "exciter", strike }, { "hardness", 0.3f }, { "noise", 0 }, { "decay", 15000 },
            { "damping", 0.3f }, { "release", 8000 }, { "fxReverbMix", 0.3f }, { "master", -3 } } },
        { "Spieluhr", "Glocken",
          { { "resonator", bar }, { "hardness", 0.9f }, { "noise", 0 }, { "decay", 2500 }, { "damping", 0.3f },
            { "inharmonicity", 0.1f }, { "release", 2500 }, { "octave", 1 }, { "body", box }, { "bodyMix", 0.3f },
            { "master", -4 } } },

        // A bow near the bridge with a violin's body and a singer's vibrato.
        { "Geige", "Bläser & Streicher",
          { { "exciter", bow }, { "pressure", 0.55f }, { "noise", 0.15f }, { "position", 0.12f },
            { "env1Attack", 120 }, { "env1Decay", 300 }, { "env1Sustain", 0.85f }, { "env1Release", 250 },
            { "decay", 2000 }, { "damping", 0.35f }, { "release", 200 }, { "body", violin }, { "bodyMix", 0.8f },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.012f }, { "lfo1Rate", 5.5f }, { "dynamics", 0.4f },
            { "fxReverbMix", 0.2f }, { "master", -17 } } },
        { "Cello", "Bläser & Streicher",
          { { "exciter", bow }, { "pressure", 0.6f }, { "noise", 0.15f }, { "position", 0.14f }, { "octave", -1 },
            { "env1Attack", 150 }, { "env1Decay", 300 }, { "env1Sustain", 0.85f }, { "env1Release", 300 },
            { "decay", 2500 }, { "damping", 0.4f }, { "release", 250 }, { "body", violin }, { "bodyMix", 0.7f },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.01f }, { "lfo1Rate", 5.0f }, { "dynamics", 0.4f },
            { "fxReverbMix", 0.2f }, { "master", -15 } } },
        // A reed on a tube closed at the mouth: only the odd overtones, the clarinet's hollow sound.
        { "Klarinette", "Bläser & Streicher",
          { { "resonator", closedTube }, { "exciter", blow }, { "pressure", 0.55f }, { "noise", 0.1f },
            { "env1Attack", 40 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f }, { "env1Release", 120 },
            { "decay", 1500 }, { "damping", 0.5f }, { "release", 100 }, { "dynamics", 0.4f }, { "master", -6 } } },
        { "Schalmei", "Bläser & Streicher",
          { { "resonator", closedTube }, { "exciter", blow }, { "pressure", 0.75f }, { "hardness", 0.8f },
            { "noise", 0.05f }, { "env1Attack", 30 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f },
            { "env1Release", 100 }, { "decay", 1500 }, { "damping", 0.35f }, { "release", 100 },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.006f }, { "lfo1Rate", 5.0f }, { "master", -6 } } },
        // Breath through an open pipe: the tube picks its notes out of the noise.
        { "Hauchflöte", "Bläser & Streicher",
          { { "resonator", openTube }, { "exciter", blow }, { "pressure", 0.7f }, { "hardness", 0.55f },
            { "env1Attack", 80 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f }, { "env1Release", 150 },
            { "decay", 1200 }, { "damping", 0.55f }, { "release", 150 }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.008f }, { "lfo1Rate", 5.0f }, { "fxReverbMix", 0.25f }, { "master", -17 } } },
        { "Panflöte", "Bläser & Streicher",
          { { "resonator", openTube }, { "exciter", blow }, { "pressure", 0.8f }, { "hardness", 0.4f },
            { "env1Attack", 60 }, { "env1Decay", 100 }, { "env1Sustain", 1.0f }, { "env1Release", 200 },
            { "decay", 2500 }, { "damping", 0.7f }, { "release", 200 }, { "fxReverbMix", 0.3f }, { "master", -16 } } },

        { "Zupfbass", "Bässe",
          { { "hardness", 0.3f }, { "noise", 0.1f }, { "position", 0.2f }, { "decay", 2500 }, { "damping", 0.6f },
            { "release", 120 }, { "body", box }, { "bodyMix", 0.3f }, { "fxReverbMix", 0.05f }, { "master", 2 } } },
        { "Gestrichener Bass", "Bässe",
          { { "exciter", bow }, { "pressure", 0.65f }, { "noise", 0.1f }, { "position", 0.15f },
            { "env1Attack", 60 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f }, { "env1Release", 150 },
            { "decay", 2000 }, { "damping", 0.45f }, { "release", 150 }, { "body", violin }, { "bodyMix", 0.5f },
            { "fxReverbMix", 0.05f }, { "master", 0 } } },
        // A plucked tube: hollow, only the odd overtones.
        { "Röhrenbass", "Bässe",
          { { "resonator", closedTube }, { "hardness", 0.6f }, { "noise", 0.1f }, { "decay", 1200 },
            { "damping", 0.5f }, { "release", 100 }, { "fxReverbMix", 0.05f }, { "master", 3 } } },
        // One string sliding from note to note, as on a bass without frets.
        { "Gleitbass", "Bässe",
          { { "hardness", 0.35f }, { "noise", 0.05f }, { "position", 0.18f }, { "decay", 3000 },
            { "damping", 0.65f }, { "release", 150 }, { "voiceMode", mono }, { "glide", 80 },
            { "fxReverbMix", 0.05f }, { "master", 4 } } },

        { "Saitenlead", "Leads",
          { { "exciter", bow }, { "pressure", 0.7f }, { "noise", 0.1f }, { "position", 0.1f },
            { "env1Attack", 40 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f }, { "env1Release", 150 },
            { "decay", 2000 }, { "damping", 0.3f }, { "release", 150 }, { "voiceMode", mono }, { "glide", 60 },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.015f }, { "lfo1Rate", 5.5f },
            { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "fxDelayFeedback", 0.35f }, { "master", -10 } } },
        { "Rohrblattlead", "Leads",
          { { "resonator", closedTube }, { "exciter", blow }, { "pressure", 0.7f }, { "hardness", 0.7f },
            { "noise", 0.1f }, { "env1Attack", 30 }, { "env1Decay", 200 }, { "env1Sustain", 0.9f },
            { "env1Release", 120 }, { "decay", 1500 }, { "damping", 0.4f }, { "release", 100 },
            { "voiceMode", mono }, { "glide", 50 }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.012f },
            { "lfo1Rate", 5.0f }, { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 },
            { "fxDelayDivision", delayDottedEighth }, { "fxDelayFeedback", 0.3f }, { "master", -2 } } },

        // A bow drawn round the rim of a glass bowl: it swells and sings.
        { "Gestrichene Schale", "Flächen",
          { { "resonator", bowl }, { "exciter", bow }, { "pressure", 0.6f }, { "hardness", 0.4f },
            { "env1Attack", 1200 }, { "env1Decay", 500 }, { "env1Sustain", 1.0f }, { "env1Release", 2000 },
            { "decay", 12000 }, { "damping", 0.3f }, { "release", 4000 }, { "width", 0.7f },
            { "fxReverbMix", 0.4f }, { "fxReverbDecay", 5000 }, { "master", -19 } } },
        // Wind across strings: breath on the string, rising and falling with a slow LFO.
        { "Äolsharfe", "Flächen",
          { { "exciter", blow }, { "pressure", 0.6f }, { "hardness", 0.35f }, { "position", 0.3f },
            { "env1Attack", 1500 }, { "env1Decay", 500 }, { "env1Sustain", 1.0f }, { "env1Release", 2500 },
            { "decay", 8000 }, { "damping", 0.4f }, { "release", 3000 }, { "width", 0.7f },
            { "lfo1Target", toPressure }, { "lfo1Amount", 0.3f }, { "lfo1Rate", 0.2f }, { "lfo1Shape", triangle },
            { "fxReverbMix", 0.45f }, { "fxReverbDecay", 5000 }, { "master", -21 } } },
        { "Gestrichenes Metall", "Flächen",
          { { "resonator", bar }, { "exciter", bow }, { "pressure", 0.5f }, { "hardness", 0.5f },
            { "env1Attack", 800 }, { "env1Decay", 300 }, { "env1Sustain", 1.0f }, { "env1Release", 1500 },
            { "decay", 6000 }, { "damping", 0.5f }, { "release", 2000 }, { "filterOn", 1 }, { "filterMode", low12 },
            { "cutoff", 5000 }, { "fxReverbMix", 0.35f }, { "fxReverbDecay", 4500 }, { "master", -21 } } },

        { "Pauke", "Effekte",
          { { "resonator", membrane }, { "exciter", strike }, { "hardness", 0.5f }, { "noise", 0.05f },
            { "position", 0.3f }, { "decay", 2500 }, { "damping", 0.6f }, { "release", 2500 }, { "octave", -1 },
            { "fxReverbMix", 0.2f }, { "master", 0 } } },
        { "Tom", "Effekte",
          { { "resonator", membrane }, { "exciter", strike }, { "hardness", 0.65f }, { "noise", 0.1f },
            { "position", 0.15f }, { "decay", 700 }, { "damping", 0.7f }, { "release", 700 }, { "master", 5 } } },
        { "Holzblock", "Effekte",
          { { "resonator", bar }, { "exciter", strike }, { "hardness", 0.95f }, { "noise", 0.1f }, { "decay", 400 },
            { "damping", 0.5f }, { "inharmonicity", 0.2f }, { "release", 150 }, { "octave", 1 }, { "master", 6 } } },
        { "Gong", "Effekte",
          { { "resonator", bell }, { "exciter", strike }, { "hardness", 0.3f }, { "noise", 0.05f },
            { "inharmonicity", 0.6f }, { "decay", 15000 }, { "damping", 0.25f }, { "release", 8000 },
            { "octave", -2 }, { "fxReverbMix", 0.3f }, { "master", -5 } } },
        // The filter on the wheel: a plucked string that opens up as the wheel goes forward.
        { "Saitenregen", "Effekte",
          { { "hardness", 0.8f }, { "noise", 0.4f }, { "position", 0.07f }, { "decay", 8000 }, { "damping", 0.15f },
            { "inharmonicity", 0.08f }, { "release", 4000 }, { "filterOn", 1 }, { "cutoff", 1500 },
            { "wheelTarget", toCutoff }, { "wheelAmount", 0.4f }, { "width", 1.0f },
            { "fxDelayMix", 0.35f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "fxDelayFeedback", 0.55f }, { "fxReverbMix", 0.35f }, { "master", 1 } } },
    };
    return presets;
}
} // namespace tonwerkphys
