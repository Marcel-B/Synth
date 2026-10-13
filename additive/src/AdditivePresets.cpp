#include "AdditivePresets.h"

namespace tonwerkadd
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float sine = 0, saw = 1, square = 2, triangle = 3, pulse = 4, organ = 5, hollow = 6, bright = 7,
                octaves = 8, glass = 9, primes = 10, randomSpectrum = 11;
constexpr float aligned = 0;
constexpr float toPitch = 1, toMorph = 2, toTilt = 3, toOddEven = 4, toPartials = 6, toComb = 7, toCutoff = 8,
                toVowel = 10, toAmp = 11;
constexpr float bandpass = 2;
constexpr float triangleLfo = 1, rampUp = 3, randomLfo = 5;
constexpr float mono = 1;
constexpr float vowelA = 0, vowelE = 1, vowelO = 3, vowelU = 4;
// Tonwerk Wavetable's note values for the LFOs: a bar, an eighth.
constexpr float lfoBar = 2, lfoEighth = 9;
// The delay's note values (Tonwerk's list): a dotted eighth.
constexpr float delayDottedEighth = 9;
// A vibrato's depth as a share of the pitch route's octave: about a sixth of a semitone either way.
constexpr float vibrato = 0.014f;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // Two spectra the slow LFO blends back and forth, the partials beating gently against each other.
        { "Morph-Fläche", "Flächen",
          { { "spectrumA", bright }, { "spectrumB", hollow }, { "morph", 0.5f }, { "beating", 6 }, { "width", 0.8f },
            { "tilt", -2 }, { "env1Attack", 800 }, { "env1Release", 1500 }, { "lfo1Rate", 0.1f },
            { "lfo1Target", toMorph }, { "lfo1Amount", 0.5f }, { "fxReverbMix", 0.25f }, { "fxReverbDecay", 4000 },
            { "master", -15 } } },
        // Notches moving through the partials: a phaser without a filter's phase smear.
        { "Kammfläche", "Flächen",
          { { "combDepth", 0.85f }, { "combSpacing", 6 }, { "beating", 4 }, { "width", 0.7f }, { "tilt", -1 },
            { "env1Attack", 600 }, { "env1Release", 1500 }, { "lfo1Shape", triangleLfo }, { "lfo1Rate", 0.15f },
            { "lfo1Target", toComb }, { "lfo1Amount", 0.5f }, { "fxReverbMix", 0.2f }, { "master", -11 } } },
        { "Glasfläche", "Flächen",
          { { "spectrumA", glass }, { "stretch", 0.2f }, { "beating", 8 }, { "width", 0.9f }, { "tilt", -2 },
            { "env1Attack", 1200 }, { "env1Release", 2500 }, { "fxReverbMix", 0.3f }, { "fxReverbDecay", 5000 },
            { "master", -18 } } },
        // A choir's open A: the formants fixed in hertz over the partials, each voice a little off the others.
        { "Chor Aah", "Flächen",
          { { "vowel", vowelA }, { "formantDepth", 0.9f }, { "beating", 12 }, { "width", 0.8f },
            { "env1Attack", 400 }, { "env1Release", 1200 }, { "lfo1Rate", 5.0f }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.004f }, { "lfo2Shape", randomLfo }, { "lfo2Rate", 0.3f }, { "lfo2Target", toVowel },
            { "lfo2Amount", 0.04f }, { "fxReverbMix", 0.25f }, { "fxReverbDecay", 4000 }, { "master", -9 } } },
        { "Chor Uuh", "Flächen",
          { { "vowel", vowelU }, { "formantDepth", 1.0f }, { "beating", 10 }, { "width", 0.8f },
            { "env1Attack", 500 }, { "env1Release", 1200 }, { "lfo1Rate", 5.0f }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.004f }, { "fxReverbMix", 0.25f }, { "fxReverbDecay", 4000 }, { "master", -15 } } },
        { "Dunkle Fläche", "Flächen",
          { { "spectrumA", triangle }, { "spectrumB", saw }, { "morph", 0.3f }, { "tilt", -4 }, { "beating", 7 },
            { "width", 0.7f }, { "filterOn", 1 }, { "cutoff", 900 }, { "resonance", 0.2f }, { "env1Attack", 1500 },
            { "env1Release", 3000 }, { "lfo1Rate", 0.08f }, { "lfo1Target", toCutoff }, { "lfo1Amount", 0.1f },
            { "fxReverbMix", 0.25f }, { "fxReverbDecay", 5000 }, { "master", -16 } } },
        // The partials come and go: the LFO counts them up from a dozen to all 64 and back.
        { "Obertonwelle", "Flächen",
          { { "partials", 32 }, { "beating", 5 }, { "width", 0.8f }, { "env1Attack", 700 }, { "env1Release", 1500 },
            { "lfo1Shape", triangleLfo }, { "lfo1Rate", 0.07f }, { "lfo1Target", toPartials }, { "lfo1Amount", 0.4f },
            { "fxReverbMix", 0.2f }, { "master", -12 } } },

        // A drawbar organ: the partials start together, no velocity, a little of the rotor's wobble.
        { "Zugriegelorgel", "Tasten",
          { { "spectrumA", organ }, { "phases", aligned }, { "width", 0.3f }, { "env1Decay", 50 },
            { "env1Release", 60 }, { "velocityAmount", 0 }, { "dynamics", 0 }, { "lfo1Rate", 6.5f },
            { "lfo1Target", toPitch }, { "lfo1Amount", 0.003f }, { "fxReverbMix", 0.12f }, { "master", -12 } } },
        { "Kirchenorgel", "Tasten",
          { { "spectrumA", organ }, { "spectrumB", octaves }, { "morph", 0.5f }, { "width", 0.6f },
            { "env1Attack", 60 }, { "env1Release", 600 }, { "velocityAmount", 0 }, { "dynamics", 0 },
            { "fxReverbMix", 0.3f }, { "fxReverbDecay", 6000 }, { "master", -20 } } },
        // A tine's fundamental with a bright, quickly fading bell above it, and the amp's tremolo.
        { "Additiv-Piano E", "Tasten",
          { { "spectrumA", sine }, { "spectrumB", glass }, { "morph", 0.3f }, { "stretch", 0.1f },
            { "spectralOn", 1 }, { "spectralDecay", 1500 }, { "spectralDamping", 0.8f }, { "env1Decay", 3000 },
            { "env1Sustain", 0 }, { "env1Release", 400 }, { "velocityAmount", 0.5f }, { "dynamics", 0.7f },
            { "lfo1Rate", 4.5f }, { "lfo1Target", toAmp }, { "lfo1Amount", 0.15f }, { "master", -2 } } },
        // Struck strings: the high partials die first, slightly stretched like a piano's stiff strings.
        { "Additiv-Flügel", "Tasten",
          { { "tilt", -3 }, { "stretch", 0.15f }, { "spectralOn", 1 }, { "spectralDecay", 4000 },
            { "spectralDamping", 0.9f }, { "env1Attack", 2 }, { "env1Decay", 6000 }, { "env1Sustain", 0 },
            { "env1Release", 300 }, { "velocityAmount", 0.5f }, { "dynamics", 0.7f }, { "fxReverbMix", 0.15f },
            { "master", -5 } } },
        { "Cembalo", "Tasten",
          { { "spectrumA", pulse }, { "tilt", 2 }, { "stretch", 0.05f }, { "spectralOn", 1 },
            { "spectralDecay", 2500 }, { "spectralDamping", 0.4f }, { "env1Decay", 4000 }, { "env1Sustain", 0 },
            { "env1Release", 150 }, { "velocityAmount", 0 }, { "dynamics", 0.2f }, { "master", -6 } } },

        // Bells: partials off their harmonic places, the high ones fading long before the hum.
        { "Röhrenglocke", "Glocken",
          { { "spectrumA", primes }, { "stretch", 0.5f }, { "spectralOn", 1 }, { "spectralDecay", 6000 },
            { "spectralDamping", 0.7f }, { "env1Decay", 8000 }, { "env1Sustain", 0 }, { "env1Release", 3000 },
            { "fxReverbMix", 0.25f }, { "master", -8 } } },
        { "Glockenspiel", "Glocken",
          { { "spectrumA", glass }, { "stretch", 0.4f }, { "spectralOn", 1 }, { "spectralDecay", 1500 },
            { "spectralDamping", 1.0f }, { "env1Decay", 3000 }, { "env1Sustain", 0 }, { "env1Release", 1500 },
            { "octave", 1 }, { "fxReverbMix", 0.2f }, { "master", -2 } } },
        { "Kristall", "Glocken",
          { { "spectrumA", octaves }, { "stretch", 0.3f }, { "beating", 10 }, { "width", 0.9f }, { "spectralOn", 1 },
            { "spectralDecay", 3000 }, { "spectralDamping", 0.6f }, { "env1Decay", 5000 }, { "env1Sustain", 0 },
            { "env1Release", 3000 }, { "fxReverbMix", 0.3f }, { "fxReverbDecay", 5000 }, { "master", -9 } } },
        // Squeezed partials that beat against each other, as a gamelan's paired instruments do.
        { "Gamelan", "Glocken",
          { { "spectrumA", primes }, { "stretch", -0.4f }, { "beating", 15 }, { "spectralOn", 1 },
            { "spectralDecay", 2500 }, { "spectralDamping", 0.6f }, { "env1Decay", 4000 }, { "env1Sustain", 0 },
            { "env1Release", 2000 }, { "fxReverbMix", 0.2f }, { "master", -4 } } },

        { "Orgelbass", "Bässe",
          { { "spectrumA", organ }, { "partials", 8 }, { "phases", aligned }, { "tilt", -2 }, { "width", 0 },
            { "env1Decay", 50 }, { "env1Release", 60 }, { "velocityAmount", 0 }, { "master", -3 } } },
        // A saw through the spectral lowpass with a snapping envelope, like a classic synth bass.
        { "Sägebass", "Bässe",
          { { "phases", aligned }, { "width", 0 }, { "filterOn", 1 }, { "cutoff", 350 }, { "resonance", 0.35f },
            { "env1Attack", 2 }, { "env1Decay", 400 }, { "env1Sustain", 0.8f }, { "env1Release", 80 },
            { "env2Attack", 1 }, { "env2Decay", 250 }, { "env2Sustain", 0 }, { "env2Target", toCutoff },
            { "env2Amount", 0.4f }, { "dynamics", 0.3f }, { "master", -3 } } },
        { "Sub-Bass", "Bässe",
          { { "spectrumA", sine }, { "spectrumB", triangle }, { "morph", 0.3f }, { "partials", 6 },
            { "phases", aligned }, { "width", 0 }, { "env1Release", 80 }, { "velocityAmount", 0 }, { "master", -4 } } },
        // Vowels in time with the song: the LFO walks the formants through A, E, I on every eighth.
        { "Sprechbass", "Bässe",
          { { "vowel", vowelE }, { "formantDepth", 1.0f }, { "phases", aligned }, { "width", 0 },
            { "lfo1Sync", 1 }, { "lfo1Division", lfoEighth }, { "lfo1Target", toVowel }, { "lfo1Amount", 0.25f },
            { "env1Release", 100 }, { "master", 5 } } },
        { "Kammbass", "Bässe",
          { { "spectrumA", square }, { "combDepth", 0.9f }, { "combSpacing", 3 }, { "phases", aligned },
            { "width", 0 }, { "env2Attack", 1 }, { "env2Decay", 400 }, { "env2Sustain", 0 }, { "env2Target", toComb },
            { "env2Amount", 0.5f }, { "env1Release", 80 }, { "master", -1 } } },

        { "Obertonlead", "Leads",
          { { "spectrumA", bright }, { "tilt", -1 }, { "phases", aligned }, { "width", 0.2f }, { "voiceMode", mono },
            { "glide", 60 }, { "lfo1Rate", 5.5f }, { "lfo1Target", toPitch }, { "lfo1Amount", vibrato },
            { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "master", -6 } } },
        // An O that opens to an A with every note, as a wah pedal would.
        { "Vokallead", "Leads",
          { { "vowel", vowelO }, { "formantDepth", 0.85f }, { "phases", aligned }, { "width", 0.2f },
            { "voiceMode", mono }, { "glide", 40 }, { "env2Attack", 150 }, { "env2Decay", 600 }, { "env2Sustain", 0.3f },
            { "env2Target", toVowel }, { "env2Amount", -0.75f }, { "lfo1Rate", 5.5f }, { "lfo1Target", toPitch },
            { "lfo1Amount", vibrato }, { "master", -1 } } },
        // The LFO trades odd partials for even ones: hollow, then full, then hollow again.
        { "Hohllead", "Leads",
          { { "spectrumA", hollow }, { "oddEven", 0.5f }, { "phases", aligned }, { "width", 0.3f },
            { "voiceMode", mono }, { "glide", 30 }, { "lfo1Rate", 1.5f }, { "lfo1Target", toOddEven },
            { "lfo1Amount", 0.5f }, { "lfo2Rate", 5.5f }, { "lfo2Target", toPitch }, { "lfo2Amount", vibrato },
            { "master", -6 } } },

        // The high partials die within a few hundred milliseconds, as a plucked string's do.
        { "Obertonzupfer", "Plucks",
          { { "spectralOn", 1 }, { "spectralDecay", 400 }, { "spectralDamping", 1.0f }, { "phases", aligned },
            { "env1Decay", 1200 }, { "env1Sustain", 0 }, { "env1Release", 200 }, { "fxDelayMix", 0.2f },
            { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth }, { "master", 3 } } },
        { "Glaszupfer", "Plucks",
          { { "spectrumA", glass }, { "stretch", 0.2f }, { "spectralOn", 1 }, { "spectralDecay", 600 },
            { "spectralDamping", 0.8f }, { "env1Decay", 800 }, { "env1Sustain", 0 }, { "env1Release", 300 },
            { "fxReverbMix", 0.2f }, { "master", 0 } } },
        { "Additiv-Harfe", "Plucks",
          { { "spectrumA", triangle }, { "spectrumB", saw }, { "morph", 0.4f }, { "spectralOn", 1 },
            { "spectralDecay", 2000 }, { "spectralDamping", 0.9f }, { "env1Decay", 3000 }, { "env1Sustain", 0 },
            { "env1Release", 1500 }, { "fxReverbMix", 0.25f }, { "master", -7 } } },

        // Brass: the high partials come in after the fundamental, so the note opens as it is blown.
        { "Blechbläser", "Bläser & Streicher",
          { { "tilt", -2 }, { "spectralOn", 1 }, { "bloom", 150 }, { "spectralDecay", 20000 },
            { "spectralSustain", 1 }, { "env1Attack", 40 }, { "env1Release", 200 }, { "velocityAmount", 0.5f },
            { "lfo1Rate", 5.0f }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.004f }, { "fxReverbMix", 0.15f },
            { "master", -10 } } },
        { "Streicher", "Bläser & Streicher",
          { { "tilt", -2 }, { "beating", 10 }, { "width", 0.9f }, { "vowel", vowelE }, { "formantDepth", 0.3f },
            { "spectralOn", 1 }, { "bloom", 250 }, { "spectralDecay", 20000 }, { "spectralSustain", 1 },
            { "env1Attack", 250 }, { "env1Release", 600 }, { "lfo1Rate", 5.5f }, { "lfo1Target", toPitch },
            { "lfo1Amount", 0.004f }, { "fxReverbMix", 0.2f }, { "master", -10 } } },
        { "Klarinette", "Bläser & Streicher",
          { { "spectrumA", hollow }, { "tilt", -2 }, { "phases", aligned }, { "width", 0.2f }, { "spectralOn", 1 },
            { "bloom", 60 }, { "spectralDecay", 20000 }, { "spectralSustain", 1 }, { "env1Attack", 50 },
            { "env1Release", 150 }, { "lfo1Rate", 5.0f }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.003f },
            { "fxReverbMix", 0.15f }, { "master", -10 } } },
        { "Flöte", "Bläser & Streicher",
          { { "spectrumA", sine }, { "spectrumB", triangle }, { "morph", 0.5f }, { "partials", 8 }, { "tilt", -6 },
            { "phases", aligned }, { "width", 0.2f }, { "env1Attack", 80 }, { "env1Release", 150 },
            { "lfo1Rate", 4.5f }, { "lfo1Target", toAmp }, { "lfo1Amount", 0.06f }, { "fxReverbMix", 0.2f },
            { "master", -10 } } },

        // From one partial to all 64 over four seconds: hold the key.
        { "Obertonleiter", "Effekte",
          { { "partials", 1 }, { "phases", aligned }, { "env2Attack", 4000 }, { "env2Sustain", 1 },
            { "env2Target", toPartials }, { "env2Amount", 1.0f }, { "env1Release", 800 }, { "fxDelayMix", 0.2f },
            { "fxReverbMix", 0.2f }, { "master", -6 } } },
        { "Glitzerregen", "Effekte",
          { { "spectrumA", randomSpectrum }, { "stretch", 0.3f }, { "beating", 20 }, { "width", 1.0f },
            { "combDepth", 1.0f }, { "combSpacing", 5 }, { "lfo1Shape", randomLfo }, { "lfo1Rate", 8.0f },
            { "lfo1Target", toComb }, { "lfo1Amount", 1.0f }, { "env1Attack", 300 }, { "env1Release", 2000 },
            { "fxDelayMix", 0.3f }, { "fxDelaySync", 1 }, { "fxReverbMix", 0.3f }, { "master", -6 } } },
        { "Bandpass-Sweep", "Effekte",
          { { "spectrumA", bright }, { "filterOn", 1 }, { "filterMode", bandpass }, { "cutoff", 1000 },
            { "resonance", 0.3f }, { "lfo1Sync", 1 }, { "lfo1Division", lfoBar }, { "lfo1Shape", rampUp },
            { "lfo1Target", toCutoff }, { "lfo1Amount", 0.25f }, { "env1Release", 800 }, { "fxDelayMix", 0.2f },
            { "master", 6 } } },
        { "Spektralmorph", "Effekte",
          { { "spectrumA", sine }, { "spectrumB", randomSpectrum }, { "lfo1Sync", 1 }, { "lfo1Division", lfoBar },
            { "lfo1Shape", triangleLfo }, { "lfo1Target", toMorph }, { "lfo1Amount", 1.0f }, { "lfo2Rate", 0.3f },
            { "lfo2Target", toTilt }, { "lfo2Amount", 0.3f }, { "width", 0.9f }, { "env1Release", 1000 },
            { "fxReverbMix", 0.25f }, { "master", -8 } } },
    };
    return presets;
}
} // namespace tonwerkadd
