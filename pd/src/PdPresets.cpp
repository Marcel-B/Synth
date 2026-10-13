#include "PdPresets.h"

namespace tonwerkpd
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float saw = 0, square = 1, pulse = 2, doubleSine = 3, sawPulse = 4, resSaw = 5, resTriangle = 6,
                resTrapezoid = 7;
/** "Welle 2" counts "Aus" first. */
constexpr float alt(float wave) { return wave + 1; }
constexpr float line2Only = 1, doubled = 2, both = 3;
constexpr float ring = 1, noise = 2;
constexpr float toPitch = 1, toDcw = 2, toDcw1 = 3, toDcw2 = 4, toPitch2 = 5;
constexpr float sine = 0, triangle = 1, squareLfo = 4, random = 5;
constexpr float mono = 1, legato = 2;
// LFO note values (Tonwerk Wavetable's list) and the delay's (Tonwerk's).
constexpr float lfoEighth = 9, lfoSixteenth = 11, lfoThirtySecond = 13;
constexpr float delayDottedEighth = 9;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // The CZ's bass: a saw whose bend snaps shut, over a square an octave down.
        { "CZ-Bass", "Bässe",
          { { "lines", both }, { "line1Depth", 0.85f }, { "line1DcwDecay", 250 }, { "line1DcwSustain", 0.1f },
            { "line1AmpDecay", 1200 }, { "line1AmpSustain", 0.7f }, { "line1AmpRelease", 120 },
            { "line2Wave", square }, { "line2Depth", 0.4f }, { "line2Octave", -1 }, { "line2Level", 0.6f },
            { "line2DcwDecay", 400 }, { "line2DcwSustain", 0.2f }, { "line2AmpRelease", 120 }, { "detuneFine", 0 },
            { "width", 0.1f }, { "master", 1 } } },
        // The resonance sweeping down from high, the CZ's answer to a resonant filter; glides in legato.
        { "Resobass", "Bässe",
          { { "line1Wave", resSaw }, { "line1Depth", 0.7f }, { "line1KeyFollow", 0.0f }, { "line1DcwDecay", 300 },
            { "line1DcwSustain", 0.05f }, { "line1AmpSustain", 0.9f }, { "line1AmpRelease", 100 },
            { "voiceMode", legato }, { "glide", 40 }, { "master", 5 } } },
        { "Pulsbass", "Bässe",
          { { "line1Wave", pulse }, { "line1Depth", 0.8f }, { "line1DcwDecay", 180 }, { "line1DcwSustain", 0.2f },
            { "line1AmpDecay", 600 }, { "line1AmpSustain", 0.6f }, { "line1AmpRelease", 80 }, { "master", 3 } } },
        // The bend on a tempo-locked LFO, two detuned saws: a wobble without a filter.
        { "Wobble PD", "Bässe",
          { { "lines", doubled }, { "detuneFine", 12 }, { "line1Depth", 0.25f }, { "line1KeyFollow", 0.0f },
            { "line1DcwSustain", 1.0f }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 80 },
            { "lfo2Shape", sine }, { "lfo2Sync", 1 }, { "lfo2Division", lfoEighth }, { "lfo2Target", toDcw },
            { "lfo2Amount", 0.75f }, { "voiceMode", mono }, { "width", 0.2f }, { "master", -3 } } },
        // Line 2 a fifth up, ring modulated by line 1: metal in the low end.
        { "Ringbass", "Bässe",
          { { "lines", both }, { "modulation", ring }, { "line1Depth", 0.5f }, { "line1DcwDecay", 300 },
            { "line1DcwSustain", 0.2f }, { "line2Wave", square }, { "line2Depth", 0.3f }, { "line2Level", 0.7f },
            { "detuneSemi", 7 }, { "detuneFine", 0 }, { "line1AmpRelease", 100 }, { "line2AmpRelease", 100 },
            { "width", 0.0f }, { "master", 1 } } },
        // Barely bent: a sine with a breath of overtones, for under everything else.
        { "Sub-Sinus", "Bässe",
          { { "line1Depth", 0.12f }, { "line1KeyFollow", 0.0f }, { "line1DcwSustain", 1.0f },
            { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 80 }, { "velocityAmount", 0.0f }, { "voiceMode", mono },
            { "master", 1 } } },

        // The trapezoid resonance with the wheel on its bend, vibrato fading in.
        { "Sync-Lead", "Leads",
          { { "line1Wave", resTrapezoid }, { "line1Depth", 0.5f }, { "line1DcwSustain", 0.6f },
            { "line1AmpSustain", 1.0f }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.02f }, { "lfo1Fade", 600 },
            { "voiceMode", legato }, { "glide", 60 }, { "fxDelayMix", 0.2f }, { "master", -3 } } },
        { "Doppelsinus-Lead", "Leads",
          { { "lines", doubled }, { "detuneFine", 10 }, { "line1Wave", doubleSine }, { "line1Depth", 0.6f },
            { "line1DcwSustain", 0.5f }, { "line1AmpSustain", 1.0f }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.015f }, { "chorusMix", 0.3f }, { "fxDelayMix", 0.2f }, { "voiceMode", mono },
            { "master", -2 } } },
        // The bend swells in after the note starts, like a filter envelope with a slow attack.
        { "Quaken", "Leads",
          { { "line1Wave", sawPulse }, { "line1Depth", 0.9f }, { "line1DcwAttack", 300 }, { "line1DcwDecay", 800 },
            { "line1DcwSustain", 0.4f }, { "line1AmpSustain", 1.0f }, { "voiceMode", mono }, { "glide", 30 },
            { "master", -2 } } },
        { "Hohler Lead", "Leads",
          { { "lines", both }, { "line1Wave", square }, { "line1Depth", 0.55f }, { "line1DcwSustain", 0.7f },
            { "line1AmpSustain", 1.0f }, { "line2Wave", pulse }, { "line2Octave", 1 }, { "line2Level", 0.4f },
            { "line2Depth", 0.6f }, { "line2AmpSustain", 1.0f }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.02f },
            { "voiceMode", mono }, { "master", -2 } } },
        // The full resonance, its bend jumping on sixteenths.
        { "Kreischen", "Leads",
          { { "line1Wave", resSaw }, { "line1Depth", 0.85f }, { "line1DcwSustain", 0.8f }, { "line1AmpSustain", 1.0f },
            { "lfo2Shape", random }, { "lfo2Sync", 1 }, { "lfo2Division", lfoSixteenth }, { "lfo2Target", toDcw1 },
            { "lfo2Amount", 0.3f }, { "voiceMode", mono }, { "master", 0 } } },

        // Two saws a few cents apart, slow in and out, through the chorus: the CZ's string pad.
        { "CZ-Streicher", "Flächen",
          { { "lines", doubled }, { "detuneFine", 9 }, { "line1Depth", 0.45f }, { "line1DcwAttack", 600 },
            { "line1DcwDecay", 1500 }, { "line1DcwSustain", 0.6f }, { "line1AmpAttack", 400 },
            { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 1200 }, { "chorusMix", 0.6f }, { "fxReverbMix", 0.3f },
            { "master", -13 } } },
        { "Glaswand", "Flächen",
          { { "lines", both }, { "line1Wave", resTriangle }, { "line1Depth", 0.5f }, { "line1DcwSustain", 0.6f },
            { "line1AmpAttack", 800 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 2000 },
            { "line2Wave", resTrapezoid }, { "line2Octave", 1 }, { "line2Depth", 0.4f }, { "line2Level", 0.5f },
            { "line2AmpAttack", 1200 }, { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 2000 }, { "detuneFine", 6 },
            { "lfo2Sync", 0 }, { "lfo2Rate", 0.15f }, { "lfo2Target", toDcw }, { "lfo2Amount", 0.2f },
            { "chorusMix", 0.5f }, { "fxReverbMix", 0.35f }, { "fxReverbDecay", 4000 }, { "master", -13 } } },
        // Line 2 multiplied by noise: air around the tone.
        { "Atemfläche", "Flächen",
          { { "lines", both }, { "modulation", noise }, { "line1Depth", 0.3f }, { "line1DcwSustain", 0.5f },
            { "line1AmpAttack", 600 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 1500 },
            { "line2Wave", sawPulse }, { "line2Depth", 0.5f }, { "line2Level", 0.35f }, { "line2AmpAttack", 900 },
            { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 1500 }, { "chorusMix", 0.4f }, { "fxReverbMix", 0.35f },
            { "master", -11 } } },
        // Saw and square cycle by cycle: the octave below the note joins in.
        { "Wechselfläche", "Flächen",
          { { "lines", doubled }, { "detuneFine", 7 }, { "line1Wave2", alt(square) }, { "line1Depth", 0.4f },
            { "line1DcwAttack", 400 }, { "line1DcwSustain", 0.5f }, { "line1AmpAttack", 300 },
            { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 1000 }, { "chorusMix", 0.5f }, { "fxReverbMix", 0.3f },
            { "master", -14 } } },
        { "Digitalchor", "Flächen",
          { { "lines", both }, { "line1Wave", doubleSine }, { "line1Wave2", alt(resTriangle) }, { "line1Depth", 0.35f },
            { "line1DcwSustain", 0.6f }, { "line1AmpAttack", 500 }, { "line1AmpSustain", 1.0f },
            { "line1AmpRelease", 1500 }, { "line2Wave", resTriangle }, { "line2Depth", 0.3f }, { "line2Level", 0.5f },
            { "line2AmpAttack", 700 }, { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 1500 },
            { "lfo2Sync", 0 }, { "lfo2Rate", 0.3f }, { "lfo2Target", toDcw2 }, { "lfo2Amount", 0.25f },
            { "chorusMix", 0.5f }, { "fxReverbMix", 0.4f }, { "master", -12 } } },
        // Wide, with a dotted delay: rain on neon.
        { "Nachtpanorama", "Flächen",
          { { "lines", both }, { "line1Depth", 0.5f }, { "line1DcwAttack", 900 }, { "line1DcwSustain", 0.4f },
            { "line1AmpAttack", 300 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 1800 },
            { "line2Wave", square }, { "line2Octave", 1 }, { "line2Level", 0.5f }, { "line2AmpAttack", 500 },
            { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 1800 }, { "width", 0.9f }, { "fxDelayMix", 0.3f },
            { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth }, { "fxDelayFeedback", 0.45f },
            { "fxReverbMix", 0.4f }, { "master", -13 } } },

        // A bend that closes as the note dies, a quiet square an octave up for the hammer's ring.
        { "CZ-Piano", "Tasten",
          { { "lines", both }, { "line1Depth", 0.6f }, { "line1DcwDecay", 900 }, { "line1DcwSustain", 0.0f },
            { "line1AmpDecay", 2500 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 400 },
            { "line2Wave", square }, { "line2Octave", 1 }, { "line2Level", 0.3f }, { "line2DcwDecay", 300 },
            { "line2DcwSustain", 0.0f }, { "line2AmpDecay", 1200 }, { "line2AmpSustain", 0.0f },
            { "line2AmpRelease", 300 }, { "detuneFine", 3 }, { "velocityAmount", 0.6f }, { "master", -2 } } },
        // A soft double sine with a short resonant click for the tine.
        { "E-Piano PD", "Tasten",
          { { "lines", both }, { "line1Wave", doubleSine }, { "line1Depth", 0.3f }, { "line1DcwDecay", 600 },
            { "line1DcwSustain", 0.05f }, { "line1AmpDecay", 3000 }, { "line1AmpSustain", 0.2f },
            { "line1AmpRelease", 500 }, { "line2Wave", resTriangle }, { "line2Depth", 0.6f }, { "line2Level", 0.3f },
            { "line2DcwDecay", 80 }, { "line2DcwSustain", 0.0f }, { "line2AmpDecay", 250 },
            { "line2AmpSustain", 0.0f }, { "line2AmpRelease", 200 }, { "detuneFine", 0 }, { "width", 0.2f },
            { "chorusMix", 0.4f }, { "master", -2 } } },
        { "Orgel PD", "Tasten",
          { { "lines", both }, { "line1Wave", square }, { "line1Depth", 0.15f }, { "line1DcwSustain", 1.0f },
            { "line1AmpAttack", 5 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 60 }, { "line2Depth", 0.1f },
            { "line2Octave", 1 }, { "line2Level", 0.5f }, { "line2DcwSustain", 1.0f }, { "line2AmpAttack", 5 },
            { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 60 }, { "detuneFine", 0 }, { "velocityAmount", 0.0f },
            { "dynamics", 0.0f }, { "chorusMix", 0.5f }, { "chorusRate", 5.0f }, { "chorusDepth", 0.3f },
            { "master", -7 } } },
        { "Clavi PD", "Tasten",
          { { "line1Wave", pulse }, { "line1Depth", 0.9f }, { "line1DcwDecay", 200 }, { "line1DcwSustain", 0.2f },
            { "line1AmpDecay", 600 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 80 }, { "master", 2 } } },
        { "Akkordeon", "Tasten",
          { { "lines", doubled }, { "detuneFine", 14 }, { "line1Wave", square }, { "line1Depth", 0.4f },
            { "line1DcwSustain", 1.0f }, { "line1AmpAttack", 40 }, { "line1AmpSustain", 1.0f },
            { "line1AmpRelease", 100 }, { "width", 0.3f }, { "master", -9 } } },
        { "Spielzeugklavier", "Tasten",
          { { "line1Wave", resTrapezoid }, { "line1Depth", 0.3f }, { "line1DcwDecay", 200 }, { "line1DcwSustain", 0.0f },
            { "line1Octave", 1 }, { "line1AmpDecay", 900 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 300 },
            { "master", 0 } } },

        { "PD-Pluck", "Plucks",
          { { "line1Depth", 0.8f }, { "line1DcwDecay", 150 }, { "line1DcwSustain", 0.0f }, { "line1AmpDecay", 400 },
            { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 200 }, { "fxDelayMix", 0.25f }, { "fxDelaySync", 1 },
            { "fxDelayDivision", delayDottedEighth }, { "master", 4 } } },
        { "Resozupf", "Plucks",
          { { "line1Wave", resSaw }, { "line1Depth", 0.9f }, { "line1DcwDecay", 250 }, { "line1DcwSustain", 0.0f },
            { "line1AmpDecay", 700 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 250 }, { "master", 4 } } },
        { "Digitalharfe", "Plucks",
          { { "line1Wave", doubleSine }, { "line1Depth", 0.4f }, { "line1DcwDecay", 300 }, { "line1DcwSustain", 0.0f },
            { "line1AmpDecay", 2500 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 1500 },
            { "fxReverbMix", 0.3f }, { "master", -7 } } },
        // Each note slides up into place from a semitone below, as a koto player's press.
        { "Koto PD", "Plucks",
          { { "line1Wave", sawPulse }, { "line1Depth", 0.7f }, { "line1DcwDecay", 300 }, { "line1DcwSustain", 0.1f },
            { "line1AmpDecay", 1500 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 400 },
            { "pitchEnvAmount", -1 }, { "pitchEnvTime", 80 }, { "master", 0 } } },
        { "Pizzicato PD", "Plucks",
          { { "line1Wave", square }, { "line1Depth", 0.5f }, { "line1DcwDecay", 80 }, { "line1DcwSustain", 0.0f },
            { "line1AmpDecay", 250 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 150 }, { "master", 6 } } },

        // Ring modulation at an uneven ratio: a bell's partials that are not the note's.
        { "Glocke PD", "Glocken",
          { { "lines", both }, { "modulation", ring }, { "line1Depth", 0.1f }, { "line1DcwSustain", 0.0f },
            { "line1AmpDecay", 4000 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 2000 },
            { "line2Wave", square }, { "line2Depth", 0.2f }, { "line2AmpDecay", 4000 }, { "line2AmpSustain", 0.0f },
            { "line2AmpRelease", 2000 }, { "detuneSemi", 6 }, { "detuneFine", 30 }, { "width", 0.0f },
            { "fxReverbMix", 0.3f }, { "master", -7 } } },
        { "Kristall", "Glocken",
          { { "line1Wave", resTriangle }, { "line1Depth", 0.6f }, { "line1DcwDecay", 1500 }, { "line1DcwSustain", 0.1f },
            { "line1AmpDecay", 3000 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 1500 },
            { "chorusMix", 0.4f }, { "fxReverbMix", 0.35f }, { "master", -4 } } },
        { "Röhrenglocke", "Glocken",
          { { "lines", both }, { "modulation", ring }, { "line1Depth", 0.2f }, { "line1AmpDecay", 5000 },
            { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 3000 }, { "line2Depth", 0.15f }, { "line2Octave", 1 },
            { "line2AmpDecay", 5000 }, { "line2AmpSustain", 0.0f }, { "line2AmpRelease", 3000 }, { "detuneSemi", 3 },
            { "detuneFine", 15 }, { "width", 0.0f }, { "fxReverbMix", 0.3f }, { "master", -8 } } },
        { "Spieluhr", "Glocken",
          { { "line1Wave", doubleSine }, { "line1Depth", 0.2f }, { "line1Octave", 1 }, { "line1DcwDecay", 300 },
            { "line1DcwSustain", 0.0f }, { "line1AmpDecay", 1200 }, { "line1AmpSustain", 0.0f },
            { "line1AmpRelease", 800 }, { "fxReverbMix", 0.3f }, { "master", -3 } } },

        { "PD-Bläser", "Bläser & Streicher",
          { { "lines", doubled }, { "detuneFine", 8 }, { "line1Depth", 0.75f }, { "line1DcwAttack", 60 },
            { "line1DcwDecay", 400 }, { "line1DcwSustain", 0.55f }, { "line1AmpAttack", 20 },
            { "line1AmpSustain", 0.9f }, { "line1AmpRelease", 200 }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.015f }, { "lfo1Fade", 500 }, { "master", -8 } } },
        { "Sologeige", "Bläser & Streicher",
          { { "line1Depth", 0.6f }, { "line1DcwAttack", 150 }, { "line1DcwSustain", 0.7f }, { "line1AmpAttack", 120 },
            { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 250 }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.025f }, { "lfo1Fade", 300 }, { "voiceMode", legato }, { "glide", 30 },
            { "fxReverbMix", 0.25f }, { "master", -4 } } },
        // A square barely bent: the hollow, odd overtones of a clarinet.
        { "Holzbläser", "Bläser & Streicher",
          { { "line1Wave", square }, { "line1Depth", 0.4f }, { "line1DcwAttack", 60 }, { "line1DcwSustain", 0.45f },
            { "line1AmpAttack", 50 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 150 },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.015f }, { "master", -5 } } },
        { "Tutti", "Bläser & Streicher",
          { { "lines", both }, { "line1Depth", 0.55f }, { "line1DcwAttack", 200 }, { "line1DcwSustain", 0.6f },
            { "line1AmpAttack", 200 }, { "line1AmpSustain", 1.0f }, { "line1AmpRelease", 600 },
            { "line2Wave", sawPulse }, { "line2Depth", 0.5f }, { "line2Octave", -1 }, { "line2Level", 0.5f },
            { "line2AmpAttack", 250 }, { "line2AmpSustain", 1.0f }, { "line2AmpRelease", 600 }, { "detuneFine", 5 },
            { "chorusMix", 0.4f }, { "fxReverbMix", 0.3f }, { "master", -8 } } },

        // Two octaves of fall and the full resonance: a shot.
        { "Laser PD", "Effekte",
          { { "line1Wave", resSaw }, { "line1Depth", 1.0f }, { "line1KeyFollow", 0.0f }, { "line1DcwSustain", 1.0f },
            { "line1AmpDecay", 500 }, { "line1AmpSustain", 0.0f }, { "line1AmpRelease", 200 },
            { "pitchEnvAmount", 24 }, { "pitchEnvTime", 300 }, { "master", 6 } } },
        { "Rauschwind", "Effekte",
          { { "lines", line2Only }, { "modulation", noise }, { "line2Wave", resTriangle }, { "line2Depth", 0.5f },
            { "line2DcwSustain", 1.0f }, { "line2AmpAttack", 1000 }, { "line2AmpSustain", 1.0f },
            { "line2AmpRelease", 2000 }, { "lfo2Shape", triangle }, { "lfo2Sync", 0 }, { "lfo2Rate", 0.2f },
            { "lfo2Target", toDcw2 }, { "lfo2Amount", 0.4f }, { "fxReverbMix", 0.4f }, { "master", -6 } } },
        // Line 2 jumps around on sixteenths, ring modulated: a radio between stations.
        { "Funkstörung", "Effekte",
          { { "lines", both }, { "modulation", ring }, { "line1Depth", 0.4f }, { "line1DcwSustain", 0.5f },
            { "line1AmpSustain", 1.0f }, { "line2Wave", pulse }, { "line2Depth", 0.6f }, { "line2AmpSustain", 1.0f },
            { "lfo2Shape", random }, { "lfo2Sync", 1 }, { "lfo2Division", lfoSixteenth }, { "lfo2Target", toPitch2 },
            { "lfo2Amount", 0.5f }, { "width", 0.0f }, { "master", -3 } } },
        { "Arcade", "Effekte",
          { { "line1Wave", pulse }, { "line1Depth", 0.9f }, { "line1DcwSustain", 1.0f }, { "line1AmpSustain", 1.0f },
            { "line1AmpRelease", 100 }, { "lfo2Shape", squareLfo }, { "lfo2Sync", 1 },
            { "lfo2Division", lfoThirtySecond }, { "lfo2Target", toPitch }, { "lfo2Amount", 0.25f }, { "master", 1 } } },
    };
    return presets;
}
} // namespace tonwerkpd
