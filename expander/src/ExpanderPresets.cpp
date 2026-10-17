#include "ExpanderPresets.h"

#include "ExpanderEngine.h"

namespace tonwerkexpander
{
namespace
{
// Menu indices, named so the sounds below read as what they are.
constexpr float pulse = (float) Wave::pulse, both = (float) Wave::both;
constexpr float mono = (float) VoiceMode::mono, unison = (float) VoiceMode::unison;
constexpr float sine = (float) LfoShape::sine;
// The filter's mode.
constexpr float notch = 0.5f, highpass = 1.0f;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    // Percent as 0 to 1, times in ms, menus as indices; master in dB, leveled to -12 LUFS (PresetTests).
    static const std::vector<FactoryPreset> presets {
        { "Init", "", {} },

        // A saw and a pulse an octave down, the envelope snapping the filter shut.
        { "Expander-Bass", "Bässe",
          { { "osc2Wave", pulse }, { "osc2Octave", -1 }, { "osc2Width", 0.4f }, { "osc2Level", 0.7f },
            { "osc2Fine", 0 }, { "cutoff", 260 }, { "resonance", 0.35f }, { "envCutoff", 0.5f },
            { "filterDecay", 250 }, { "filterSustain", 0 }, { "ampSustain", 0.9f }, { "ampRelease", 80 },
            { "keytrack", 0.3f }, { "voiceMode", mono }, { "glide", 30 }, { "master", 0 } } },
        // Oscillator 2 synced to 1, the envelope pulling it up and down: the bite comes from the sync.
        { "Sync-Bass", "Bässe",
          { { "osc1Level", 0.3f }, { "osc2Level", 0.9f }, { "sync", 1 }, { "envPitch", 19 }, { "cutoff", 700 },
            { "envCutoff", 0.3f }, { "filterDecay", 300 }, { "filterSustain", 0 }, { "ampRelease", 80 },
            { "voiceMode", mono }, { "master", 1 } } },
        // Between lowpass and notch the bass turns hollow.
        { "Notch-Bass", "Bässe",
          { { "osc1Wave", both }, { "osc2Octave", -1 }, { "mode", 0.35f }, { "resonance", 0.55f }, { "cutoff", 420 },
            { "envCutoff", 0.4f }, { "filterDecay", 400 }, { "filterSustain", 0.1f }, { "ampRelease", 100 },
            { "voiceMode", mono }, { "master", 3 } } },

        { "Sync-Lead", "Leads",
          { { "osc1Level", 0.3f }, { "osc2Level", 1.0f }, { "sync", 1 }, { "envPitch", 24 }, { "cutoff", 3000 },
            { "envCutoff", 0.2f }, { "filterDecay", 600 }, { "filterSustain", 0.2f }, { "lfoPitch", 20 },
            { "lfoDelay", 400 }, { "voiceMode", mono }, { "glide", 40 }, { "fxDelayMix", 0.2f }, { "master", -3 } } },
        // All eight voices on one key, fanned out across the field.
        { "Unison-Lead", "Leads",
          { { "voiceMode", unison }, { "unisonDetune", 25 }, { "spread", 0.8f }, { "cutoff", 2500 },
            { "resonance", 0.3f }, { "envCutoff", 0.25f }, { "glide", 50 }, { "master", -3 } } },
        { "Bandpass-Lead", "Leads",
          { { "osc1Wave", pulse }, { "osc1Width", 0.3f }, { "bandpass", 1 }, { "cutoff", 1400 }, { "resonance", 0.6f },
            { "envCutoff", 0.3f }, { "lfoPitch", 15 }, { "lfoDelay", 300 }, { "voiceMode", mono }, { "glide", 25 },
            { "master", -3 } } },

        // Two pulses whose width the LFO moves, slow in and out: the classic eight-voice pad.
        { "Acht Stimmen", "Flächen",
          { { "osc1Wave", pulse }, { "osc2Wave", pulse }, { "lfoPwm", 0.6f }, { "lfoRate", 0.6f }, { "ampAttack", 600 },
            { "ampRelease", 1200 }, { "ampSustain", 1.0f }, { "cutoff", 2000 }, { "envCutoff", 0.1f }, { "drift", 0.5f },
            { "spread", 0.8f }, { "fxReverbMix", 0.25f }, { "master", -15 } } },
        { "Notch-Fläche", "Flächen",
          { { "mode", notch }, { "resonance", 0.5f }, { "cutoff", 800 }, { "envCutoff", 0 }, { "lfoCutoff", 0.3f },
            { "lfoRate", 0.2f }, { "lfoShape", sine }, { "ampAttack", 800 }, { "ampSustain", 1.0f },
            { "ampRelease", 1500 }, { "spread", 0.9f }, { "fxReverbMix", 0.3f }, { "master", -14 } } },
        // The highpass leaves only the top of the saws: glassy, thin, for above a mix.
        { "Hochpass-Glas", "Flächen",
          { { "mode", highpass }, { "cutoff", 700 }, { "resonance", 0.4f }, { "envCutoff", 0 }, { "keytrack", 1.0f },
            { "osc2Octave", 1 }, { "ampAttack", 400 }, { "ampSustain", 1.0f }, { "ampRelease", 1500 },
            { "fxDelayMix", 0.25f }, { "fxReverbMix", 0.3f }, { "master", -13 } } },

        { "Poly-Tasten", "Tasten",
          { { "cutoff", 900 }, { "envCutoff", 0.45f }, { "filterDecay", 500 }, { "filterSustain", 0.2f },
            { "ampDecay", 900 }, { "ampSustain", 0.5f }, { "ampRelease", 300 }, { "velocity", 0.7f },
            { "master", -5 } } },

        { "Bandpass-Pluck", "Plucks",
          { { "osc1Wave", both }, { "bandpass", 1 }, { "cutoff", 900 }, { "resonance", 0.3f }, { "envCutoff", 0.35f },
            { "filterDecay", 220 }, { "filterSustain", 0 }, { "ampDecay", 400 }, { "ampSustain", 0 },
            { "ampRelease", 300 }, { "fxDelayMix", 0.25f }, { "master", 3 } } },

        // The filter opens a little after the level: brass swells.
        { "Synth-Brass", "Bläser & Streicher",
          { { "osc2Fine", 9 }, { "filterAttack", 80 }, { "filterDecay", 500 }, { "filterSustain", 0.5f },
            { "envCutoff", 0.5f }, { "cutoff", 500 }, { "ampAttack", 40 }, { "ampSustain", 1.0f },
            { "ampRelease", 250 }, { "velocity", 0.6f }, { "master", -7 } } },
        { "Streicher", "Bläser & Streicher",
          { { "osc2Fine", 12 }, { "lfoPitch", 8 }, { "lfoShape", sine }, { "ampAttack", 300 }, { "ampSustain", 1.0f },
            { "ampRelease", 900 }, { "cutoff", 2500 }, { "envCutoff", 0 }, { "mode", 0.15f }, { "resonance", 0.1f },
            { "drift", 0.6f }, { "spread", 0.9f }, { "fxReverbMix", 0.3f }, { "master", -13 } } },

        // A slow sync sweep up and down on every key.
        { "Sync-Sirene", "Effekte",
          { { "osc1Level", 0.2f }, { "osc2Level", 1.0f }, { "sync", 1 }, { "envPitch", 36 }, { "filterAttack", 1500 },
            { "filterDecay", 1500 }, { "filterSustain", 0 }, { "cutoff", 4000 }, { "envCutoff", 0 },
            { "ampSustain", 1.0f }, { "ampRelease", 600 }, { "fxDelayMix", 0.3f }, { "master", -7 } } },
    };
    return presets;
}
} // namespace tonwerkexpander
