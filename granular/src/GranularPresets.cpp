#include "GranularPresets.h"

namespace tonwerkgrain
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float choir = 0, glass = 1, strings = 2, plucks = 3, organ = 4, wind = 5, metal = 6, sine = 7, rhythm = 8;
constexpr float flat = 1, struck = 2;
constexpr float octaves = 1, fifths = 2;
constexpr float low12 = 0, low24 = 1, high12 = 2, band = 3;
constexpr float toPosition = 1, toDensity = 4, toPitch = 5, toCutoff = 6, toWidth = 8;
constexpr float triangle = 1, square = 4, random = 5;
constexpr float half = 3, eighth = 9;
constexpr float mono = 1;
// The delay's note values (Tonwerk's list): a quarter and a dotted eighth.
constexpr float delayQuarter = 7, delayDottedEighth = 9;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },
        // The choir's vowels drifting by: long grains, the position scanning slowly forward and swaying.
        { "Chorwolke", "Flächen",
          { { "source", choir }, { "position", 0.1f }, { "spray", 0.25f }, { "scan", 0.15f }, { "grainSize", 220 },
            { "density", 30 }, { "jitter", 0.5f }, { "width", 0.9f }, { "scatter", 12 },
            { "env1Attack", 900 }, { "env1Release", 2500 },
            { "lfo1Rate", 0.07f }, { "lfo1Target", toPosition }, { "lfo1Amount", 0.15f },
            { "fxReverbMix", 0.35f }, { "fxReverbDecay", 5000 }, { "master", -13 } } },
        // Glass strikes from all over the source, an octave up or down at random, in a long hall.
        { "Glasnebel", "Flächen",
          { { "source", glass }, { "position", 0.5f }, { "spray", 0.9f }, { "grainSize", 300 }, { "density", 25 },
            { "scatter", 1200 }, { "scatterMode", octaves }, { "width", 1 },
            { "filterOn", 1 }, { "filterMode", low12 }, { "cutoff", 6000 },
            { "env1Attack", 1200 }, { "env1Release", 3000 },
            { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "fxDelayFeedback", 0.45f }, { "fxReverbMix", 0.45f }, { "fxReverbDecay", 6000 }, { "master", -18 } } },
        { "Streicherwolke", "Flächen",
          { { "source", strings }, { "position", 0.4f }, { "spray", 0.3f }, { "scan", 0.25f }, { "grainSize", 180 },
            { "density", 40 }, { "width", 0.8f }, { "scatter", 8 },
            { "env1Attack", 700 }, { "env1Release", 1800 }, { "fxReverbMix", 0.3f }, { "master", -14 } } },
        // Organ grains an octave apart under a slowly breathing lowpass.
        { "Orgelnebel", "Flächen",
          { { "source", organ }, { "position", 0.3f }, { "spray", 0.5f }, { "scan", 0.1f }, { "grainSize", 250 },
            { "density", 24 }, { "scatter", 1200 }, { "scatterMode", octaves },
            { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 2500 }, { "resonance", 0.2f },
            { "env1Attack", 1500 }, { "env1Release", 3000 },
            { "lfo1Rate", 0.2f }, { "lfo1Target", toCutoff }, { "lfo1Amount", 0.1f },
            { "fxReverbMix", 0.4f }, { "fxReverbDecay", 5000 }, { "master", -16 } } },
        // Sine grains on fifths and octaves: a shimmering cloud with nothing but the grains to colour it.
        { "Eisfläche", "Flächen",
          { { "source", sine }, { "grainSize", 400 }, { "density", 30 }, { "spray", 0.2f }, { "scatter", 1900 },
            { "scatterMode", fifths }, { "width", 1 }, { "env1Attack", 2000 }, { "env1Release", 4000 },
            { "fxDelayMix", 0.25f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayQuarter },
            { "fxDelayFeedback", 0.5f }, { "fxReverbMix", 0.5f }, { "fxReverbDecay", 7000 }, { "master", -20 } } },
        { "Dunkle Fläche", "Flächen",
          { { "source", strings }, { "grainSize", 300 }, { "density", 30 }, { "spray", 0.4f }, { "scan", 0.05f },
            { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 700 }, { "resonance", 0.15f }, { "keytrack", 0.5f },
            { "env1Attack", 1500 }, { "env1Release", 3000 },
            { "lfo1Shape", triangle }, { "lfo1Rate", 0.1f }, { "lfo1Target", toCutoff }, { "lfo1Amount", 0.15f },
            { "fxReverbMix", 0.4f }, { "master", -14 } } },
        // The choir slowed to a crawl: long, flat grains read one after another.
        { "Zeitlupe", "Flächen",
          { { "source", choir }, { "scan", 0.08f }, { "position", 0.0f }, { "grainSize", 450 }, { "density", 18 },
            { "spray", 0.05f }, { "jitter", 0.2f }, { "window", flat },
            { "env1Attack", 800 }, { "env1Release", 3000 },
            { "fxReverbMix", 0.4f }, { "fxReverbDecay", 6000 }, { "master", -16 } } },
        // Wind through a narrow band that follows the keys: noise made into a note.
        { "Windharfe", "Flächen",
          { { "source", wind }, { "grainSize", 200 }, { "density", 40 }, { "spray", 0.6f }, { "width", 1 },
            { "filterOn", 1 }, { "filterMode", band }, { "cutoff", 262 }, { "resonance", 0.85f }, { "keytrack", 1 },
            { "env1Attack", 1000 }, { "env1Release", 2500 }, { "fxReverbMix", 0.4f }, { "master", 6 } } },

        // Short struck grains of glass anywhere in the source and anywhere in two octaves.
        { "Kornregen", "Effekte",
          { { "source", glass }, { "window", struck }, { "grainSize", 60 }, { "density", 50 }, { "jitter", 1 },
            { "spray", 1 }, { "scatter", 2400 }, { "width", 1 }, { "env1Attack", 10 }, { "env1Release", 2000 },
            { "fxDelayMix", 0.3f }, { "fxReverbMix", 0.5f }, { "master", -10 } } },
        // One spot of the rhythm held still: a frozen hit.
        { "Gefroren", "Effekte",
          { { "source", rhythm }, { "position", 0.3f }, { "spray", 0.02f }, { "grainSize", 90 }, { "density", 30 },
            { "jitter", 0.1f }, { "fxReverbMix", 0.2f }, { "master", 4 } } },
        // The rhythm backwards, crumbling: most grains reversed, the filter jumping at random.
        { "Zerfall", "Effekte",
          { { "source", rhythm }, { "scan", -0.5f }, { "reverse", 0.6f }, { "grainSize", 150 }, { "density", 25 },
            { "spray", 0.2f }, { "jitter", 0.6f }, { "filterOn", 1 }, { "filterMode", low12 }, { "cutoff", 3000 },
            { "lfo1Shape", random }, { "lfo1Rate", 4 }, { "lfo1Target", toCutoff }, { "lfo1Amount", 0.2f },
            { "fxDelayMix", 0.3f }, { "fxReverbMix", 0.35f }, { "master", -4 } } },
        { "Metallschwarm", "Effekte",
          { { "source", metal }, { "grainSize", 80 }, { "density", 60 }, { "spray", 0.8f }, { "scatter", 700 },
            { "jitter", 0.8f }, { "width", 1 },
            { "lfo1Rate", 0.3f }, { "lfo1Target", toDensity }, { "lfo1Amount", 0.3f },
            { "fxReverbMix", 0.4f }, { "fxReverbDecay", 4000 }, { "master", -10 } } },
        // Wind that does not follow the keys, the lowpass rising and falling like gusts.
        { "Sturm", "Effekte",
          { { "source", wind }, { "keyFollow", 0 }, { "grainSize", 250 }, { "density", 40 }, { "spray", 1 },
            { "width", 1 }, { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 800 }, { "resonance", 0.4f },
            { "lfo1Shape", triangle }, { "lfo1Rate", 0.15f }, { "lfo1Target", toCutoff }, { "lfo1Amount", 0.35f },
            { "env1Attack", 1500 }, { "env1Release", 3000 }, { "fxReverbMix", 0.3f }, { "master", 4 } } },
        // Steady grains of a sixteenth at 120 BPM over the rhythm, the position jumping on the eighths.
        { "Stotterband", "Effekte",
          { { "source", rhythm }, { "grainSize", 125 }, { "density", 8 }, { "jitter", 0 }, { "window", flat },
            { "scan", 1 }, { "spray", 0 }, { "lfo1Shape", square }, { "lfo1Sync", 1 }, { "lfo1Division", eighth },
            { "lfo1Target", toPosition }, { "lfo1Amount", 0.25f }, { "fxReverbMix", 0.15f }, { "master", 2 } } },

        { "Kornlead", "Leads",
          { { "source", strings }, { "voiceMode", mono }, { "glide", 80 }, { "grainSize", 60 }, { "density", 80 },
            { "spray", 0.05f }, { "jitter", 0.2f }, { "width", 0.3f },
            { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 4000 },
            { "env1Attack", 10 }, { "env1Release", 300 },
            { "lfo1Rate", 5 }, { "lfo1Target", toPitch }, { "lfo1Amount", 0.015f },
            { "fxDelayMix", 0.2f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "master", -8 } } },
        // The choir's vowels swept on the half notes: a lead that talks.
        { "Vokal-Lead", "Leads",
          { { "source", choir }, { "voiceMode", mono }, { "glide", 60 }, { "grainSize", 80 }, { "density", 70 },
            { "spray", 0.03f }, { "lfo1Shape", triangle }, { "lfo1Sync", 1 }, { "lfo1Division", half },
            { "lfo1Target", toPosition }, { "lfo1Amount", 0.4f }, { "position", 0.5f },
            { "env1Attack", 20 }, { "env1Release", 400 }, { "fxDelayMix", 0.2f }, { "master", -4 } } },

        // Strings in short grains under a lowpass that the second envelope opens with each note.
        { "Kornbass", "Bässe",
          { { "source", strings }, { "grainSize", 50 }, { "density", 90 }, { "spray", 0.02f }, { "jitter", 0.1f },
            { "width", 0.2f }, { "voiceMode", mono }, { "glide", 30 },
            { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 500 }, { "resonance", 0.3f },
            { "env2Attack", 0 }, { "env2Decay", 250 }, { "env2Sustain", 0 }, { "env2Release", 100 },
            { "env2Target", toCutoff }, { "env2Amount", 0.4f },
            { "env1Attack", 2 }, { "env1Decay", 400 }, { "env1Sustain", 0.8f }, { "env1Release", 120 },
            { "fxReverbMix", 0 }, { "master", 1 } } },
        { "Rauer Bass", "Bässe",
          { { "source", metal }, { "grainSize", 70 }, { "density", 60 }, { "spray", 0.05f }, { "width", 0.2f },
            { "voiceMode", mono }, { "filterOn", 1 }, { "filterMode", low24 }, { "cutoff", 900 }, { "resonance", 0.25f },
            { "env2Attack", 0 }, { "env2Decay", 300 }, { "env2Sustain", 0 }, { "env2Release", 100 },
            { "env2Target", toCutoff }, { "env2Amount", 0.3f },
            { "env1Attack", 2 }, { "env1Release", 120 }, { "fxReverbMix", 0 }, { "master", 4 } } },

        // Organ grains with a quick wobble across the stereo field, as a rotary speaker turns it.
        { "Orgelkörner", "Tasten",
          { { "source", organ }, { "grainSize", 100 }, { "density", 40 }, { "spray", 0.05f },
            { "env1Attack", 5 }, { "env1Decay", 300 }, { "env1Sustain", 0.9f }, { "env1Release", 150 },
            { "lfo1Rate", 6 }, { "lfo1Target", toWidth }, { "lfo1Amount", 0.3f }, { "fxReverbMix", 0.2f },
            { "master", -9 } } },
        // The plucks read forward at their own speed: the grains rebuild each pluck's decay, roughened.
        { "Staubpiano", "Tasten",
          { { "source", plucks }, { "position", 0.0f }, { "scan", 1 }, { "grainSize", 120 }, { "density", 40 },
            { "spray", 0.02f }, { "jitter", 0.6f }, { "filterOn", 1 }, { "filterMode", low12 }, { "cutoff", 5000 },
            { "env1Attack", 2 }, { "env1Decay", 1200 }, { "env1Sustain", 0.2f }, { "env1Release", 400 },
            { "fxReverbMix", 0.25f }, { "master", -1 } } },

        { "Kornzupfer", "Plucks",
          { { "source", plucks }, { "position", 0.0f }, { "scan", 1 }, { "spray", 0 }, { "grainSize", 80 },
            { "density", 60 }, { "env1Attack", 1 }, { "env1Decay", 350 }, { "env1Sustain", 0 }, { "env1Release", 250 },
            { "fxDelayMix", 0.25f }, { "fxDelaySync", 1 }, { "fxDelayDivision", delayDottedEighth },
            { "fxDelayFeedback", 0.35f }, { "fxReverbMix", 0.2f }, { "master", 2 } } },
        { "Tropfen", "Plucks",
          { { "source", glass }, { "window", struck }, { "grainSize", 150 }, { "density", 15 }, { "jitter", 0.6f },
            { "scatter", 1200 }, { "scatterMode", octaves },
            { "env1Attack", 1 }, { "env1Decay", 500 }, { "env1Sustain", 0 }, { "env1Release", 400 },
            { "fxDelayMix", 0.3f }, { "fxReverbMix", 0.3f }, { "master", -4 } } },

        { "Kristall", "Glocken",
          { { "source", glass }, { "scan", 1 }, { "spray", 0.02f }, { "grainSize", 200 }, { "density", 30 },
            { "scatter", 1200 }, { "scatterMode", octaves },
            { "env1Attack", 1 }, { "env1Decay", 3000 }, { "env1Sustain", 0 }, { "env1Release", 2500 },
            { "fxReverbMix", 0.45f }, { "fxReverbDecay", 5000 }, { "master", -12 } } },
        { "Glockenstaub", "Glocken",
          { { "source", metal }, { "window", struck }, { "grainSize", 250 }, { "density", 25 }, { "spray", 0.5f },
            { "scatter", 1900 }, { "scatterMode", fifths },
            { "filterOn", 1 }, { "filterMode", high12 }, { "cutoff", 300 },
            { "env1Attack", 5 }, { "env1Decay", 2500 }, { "env1Sustain", 0 }, { "env1Release", 2000 },
            { "fxReverbMix", 0.4f }, { "master", -10 } } },

        { "Körnige Streicher", "Bläser & Streicher",
          { { "source", strings }, { "position", 0.5f }, { "spray", 0.1f }, { "scan", 0.3f }, { "grainSize", 150 },
            { "density", 45 }, { "width", 0.9f },
            { "env1Attack", 250 }, { "env1Decay", 500 }, { "env1Sustain", 0.9f }, { "env1Release", 700 },
            { "fxReverbMix", 0.3f }, { "master", -13 } } },
        // The choir held on its "a".
        { "Chor Aah", "Bläser & Streicher",
          { { "source", choir }, { "position", 0.075f }, { "spray", 0.06f }, { "grainSize", 180 }, { "density", 40 },
            { "scatter", 10 }, { "width", 0.8f }, { "env1Attack", 400 }, { "env1Release", 1200 },
            { "fxReverbMix", 0.35f }, { "master", -9 } } },
    };
    return presets;
}
} // namespace tonwerkgrain
