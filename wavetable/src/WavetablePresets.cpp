#include "WavetablePresets.h"

namespace tonwerkwave
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float syncTable = 1, pwmTable = 2, vowel = 3, fmGrowl = 4;
constexpr float warpSync = 1, warpFm = 4;
constexpr float low12 = 0, low24 = 1, high12 = 2, comb = 5;
constexpr float sine = 0, triangle = 1, square = 4, random = 5;
constexpr float quarter = 6, eighth = 9, eighthTriplet = 10, sixteenth = 11, sixteenthTriplet = 12;
constexpr float lfo1 = 1, lfo2 = 2, env2 = 4, macro1 = 10;
constexpr float aPosition = 1, aWarp = 2, aPitch = 3, cutoffTarget = 13;
constexpr float mono = 1, legato = 2;
constexpr float soft = 0, hard = 1, tube = 3;
// Positions in the basic table: sine, triangle, saw, square.
constexpr float sawPosition = 2.0f / 3.0f, squarePosition = 1.0f;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
        // The classic: a saw and a square an octave down through a closed, resonant lowpass, opened by an LFO on the
        // eighths. Makro 1 opens the filter further.
        { "Wobble 1/8",
          { { "aPosition", sawPosition }, { "aUnison", 3 }, { "aDetune", 15 }, { "aWidth", 0.3f },
            { "bOn", 1 }, { "bPosition", squarePosition }, { "bOctave", -1 }, { "bLevel", 0.5f },
            { "subOn", 1 }, { "subOctave", 0 },
            { "filterMode", low24 }, { "cutoff", 120 }, { "resonance", 0.45f }, { "filterDrive", 0.3f },
            { "lfo1Division", eighth },
            { "mod1Source", lfo1 }, { "mod1Target", cutoffTarget }, { "mod1Amount", 0.55f },
            { "mod2Source", macro1 }, { "mod2Target", cutoffTarget }, { "mod2Amount", 0.3f },
            { "voiceMode", legato }, { "glide", 60 }, { "env1Release", 80 },
            { "distOn", 1 }, { "distMode", soft }, { "distDrive", 0.45f }, { "master", -8 } } },
        // Triplet wobble: the LFO moves the FM table as well as the filter, so the tone talks while it opens.
        { "Wobble Triolen",
          { { "aTable", fmGrowl }, { "aPosition", 0.3f }, { "aUnison", 2 }, { "aDetune", 10 },
            { "subOn", 1 }, { "subOctave", 0 },
            { "filterMode", low24 }, { "cutoff", 200 }, { "resonance", 0.35f },
            { "lfo1Shape", triangle }, { "lfo1Division", eighthTriplet },
            { "mod1Source", lfo1 }, { "mod1Target", cutoffTarget }, { "mod1Amount", 0.5f },
            { "mod2Source", lfo1 }, { "mod2Target", aPosition }, { "mod2Amount", 0.5f },
            { "mod3Source", macro1 }, { "mod3Target", cutoffTarget }, { "mod3Amount", 0.3f },
            { "voiceMode", legato }, { "glide", 50 }, { "env1Release", 80 },
            { "distOn", 1 }, { "distMode", tube }, { "distDrive", 0.55f }, { "master", -9 } } },
        // Two saws a few cents apart beating against each other, a slow drift on the filter, a clean sub.
        { "Reese",
          { { "aPosition", sawPosition }, { "aUnison", 4 }, { "aDetune", 22 }, { "aWidth", 0.5f },
            { "bOn", 1 }, { "bPosition", sawPosition }, { "bFine", 9 }, { "bLevel", 0.7f },
            { "subOn", 1 }, { "subOctave", 0 }, { "subLevel", 0.55f },
            { "filterMode", low24 }, { "cutoff", 700 }, { "resonance", 0.15f },
            { "lfo2Sync", 0 }, { "lfo2Rate", 0.3f }, { "lfo2Retrigger", 0 },
            { "mod1Source", lfo2 }, { "mod1Target", cutoffTarget }, { "mod1Amount", 0.15f }, { "mod1Bipolar", 1 },
            { "mod2Source", macro1 }, { "mod2Target", cutoffTarget }, { "mod2Amount", 0.4f },
            { "voiceMode", legato }, { "glide", 120 }, { "env1Release", 150 },
            { "distOn", 1 }, { "distMode", soft }, { "distDrive", 0.3f }, { "distMix", 0.6f }, { "master", -9 } } },
        // The vowel table swept from a to u on the quarters with FM from a sine: "yoi".
        { "Growl Yoi",
          { { "aTable", vowel }, { "aWarpMode", warpFm }, { "aWarp", 0.35f }, { "bOctave", 1 },
            { "subOn", 1 }, { "subOctave", 0 }, { "subLevel", 0.5f },
            { "filterMode", low12 }, { "cutoff", 3000 }, { "resonance", 0.2f },
            { "lfo1Division", quarter },
            { "mod1Source", lfo1 }, { "mod1Target", aPosition }, { "mod1Amount", 0.9f },
            { "mod2Source", lfo1 }, { "mod2Target", aWarp }, { "mod2Amount", 0.4f },
            { "mod3Source", macro1 }, { "mod3Target", aWarp }, { "mod3Amount", 0.4f },
            { "voiceMode", legato }, { "glide", 50 }, { "env1Release", 80 },
            { "distOn", 1 }, { "distMode", tube }, { "distDrive", 0.6f }, { "master", -10 } } },
        // FM table and FM from a saw, through a comb tuned to the note: metallic, hollow, moving on the sixteenths.
        { "Growl FM",
          { { "aTable", fmGrowl }, { "aPosition", 0.2f }, { "aUnison", 2 }, { "aDetune", 8 }, { "aWidth", 0.4f },
            { "aWarpMode", warpFm }, { "aWarp", 0.2f }, { "bPosition", sawPosition },
            { "subOn", 1 }, { "subOctave", 0 }, { "subLevel", 0.5f },
            { "filterMode", comb }, { "cutoff", 262 }, { "resonance", 0.6f }, { "keytrack", 1 },
            { "lfo1Shape", triangle }, { "lfo1Division", sixteenth },
            { "mod1Source", lfo1 }, { "mod1Target", aPosition }, { "mod1Amount", 0.6f },
            { "mod2Source", lfo1 }, { "mod2Target", aWarp }, { "mod2Amount", 0.5f },
            { "mod3Source", macro1 }, { "mod3Target", aPosition }, { "mod3Amount", 0.5f },
            { "voiceMode", legato }, { "glide", 40 }, { "env1Release", 80 },
            { "distOn", 1 }, { "distMode", hard }, { "distDrive", 0.5f }, { "master", -10 } } },
        // A synced saw an octave up, the sync sweeping on the sixteenths, thinned by a highpass: a tearout screech.
        { "Screech",
          { { "aTable", syncTable }, { "aPosition", 0.5f }, { "aOctave", 1 }, { "aUnison", 3 }, { "aDetune", 18 },
            { "aWidth", 0.6f }, { "aWarpMode", warpSync }, { "aWarp", 0.3f },
            { "filterMode", high12 }, { "cutoff", 200 }, { "resonance", 0.2f },
            { "lfo1Shape", triangle }, { "lfo1Division", sixteenth },
            { "mod1Source", lfo1 }, { "mod1Target", aPosition }, { "mod1Amount", 0.5f },
            { "mod2Source", lfo1 }, { "mod2Target", aWarp }, { "mod2Amount", 0.4f },
            { "voiceMode", legato }, { "glide", 30 }, { "env1Release", 60 },
            { "distOn", 1 }, { "distMode", hard }, { "distDrive", 0.7f }, { "master", -12 } } },
        // Riddim: a narrow pulse chopped by a square LFO on sixteenth triplets.
        { "Riddim",
          { { "aTable", pwmTable }, { "aPosition", 0.4f }, { "aUnison", 2 }, { "aDetune", 6 },
            { "subOn", 1 }, { "subOctave", 0 },
            { "filterMode", low24 }, { "cutoff", 150 }, { "resonance", 0.3f },
            { "lfo1Shape", square }, { "lfo1Division", sixteenthTriplet },
            { "mod1Source", lfo1 }, { "mod1Target", cutoffTarget }, { "mod1Amount", 0.45f },
            { "mod2Source", macro1 }, { "mod2Target", cutoffTarget }, { "mod2Amount", 0.3f },
            { "voiceMode", legato }, { "glide", 20 }, { "env1Release", 60 },
            { "distOn", 1 }, { "distMode", hard }, { "distDrive", 0.4f }, { "master", -9 } } },
        // The vowel table moved by a random LFO on the sixteenths: the bass babbles.
        { "Talking Bass",
          { { "aTable", vowel }, { "aUnison", 2 }, { "aDetune", 6 },
            { "subOn", 1 }, { "subOctave", 0 }, { "subLevel", 0.5f },
            { "filterMode", low12 }, { "cutoff", 1500 }, { "resonance", 0.3f },
            { "lfo1Shape", random }, { "lfo1Division", sixteenth },
            { "lfo2Shape", sine }, { "lfo2Division", eighth },
            { "mod1Source", lfo1 }, { "mod1Target", aPosition }, { "mod1Amount", 1.0f },
            { "mod2Source", lfo2 }, { "mod2Target", cutoffTarget }, { "mod2Amount", 0.25f },
            { "voiceMode", legato }, { "glide", 40 }, { "env1Release", 80 },
            { "distOn", 1 }, { "distMode", soft }, { "distDrive", 0.4f }, { "master", -9 } } },
        // A fast fall in pitch from two octaves up: the laser of a drop's fill.
        { "Laser",
          { { "aPosition", sawPosition }, { "aUnison", 3 }, { "aDetune", 10 },
            { "filterMode", low12 }, { "cutoff", 6000 },
            { "env1Decay", 250 }, { "env1Sustain", 0 }, { "env1Release", 100 },
            { "env2Attack", 0 }, { "env2Decay", 180 }, { "env2Sustain", 0 },
            { "mod1Source", env2 }, { "mod1Target", aPitch }, { "mod1Amount", 1.0f },
            { "voiceMode", mono }, { "master", -10 } } },
        // Just the low end: a sine sub with a little sine on top and a touch of tube for small speakers.
        { "Sub Bass",
          { { "aLevel", 0.35f }, { "subOn", 1 }, { "subOctave", 0 }, { "subLevel", 0.8f },
            { "filterMode", low24 }, { "cutoff", 200 },
            { "voiceMode", legato }, { "glide", 40 }, { "env1Release", 60 },
            { "distOn", 1 }, { "distMode", tube }, { "distDrive", 0.2f }, { "distMix", 0.3f }, { "master", -6 } } },
        // Seven and five detuned saws an octave apart, wide: chords for the melodic side of dubstep.
        { "Supersaw",
          { { "aPosition", sawPosition }, { "aUnison", 7 }, { "aDetune", 35 }, { "aBlend", 0.85f }, { "aWidth", 1 },
            { "bOn", 1 }, { "bPosition", sawPosition }, { "bOctave", 1 }, { "bUnison", 5 }, { "bDetune", 25 },
            { "bWidth", 1 }, { "bLevel", 0.4f },
            { "filterMode", low24 }, { "cutoff", 5000 },
            { "env1Attack", 10 }, { "env1Decay", 600 }, { "env1Sustain", 0.8f }, { "env1Release", 400 },
            { "master", -12 } } },
    };
    return presets;
}
} // namespace tonwerkwave
