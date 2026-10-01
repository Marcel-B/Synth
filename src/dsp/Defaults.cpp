#include "Patch.h"

namespace tonwerk
{
// The starting sounds per kind of track from Tonwerk's synth.ts and fm.ts, value for value.

namespace
{
Oscillator osc(Wave wave, int octave, float detune, float level)
{
    Oscillator result;
    result.wave = wave;
    result.octave = octave;
    result.detune = detune;
    result.level = level;
    return result;
}

Operator op(float ratio, float level, Envelope env, float velocity = 0.7f, float detune = 0.0f)
{
    return { ratio, detune, level, velocity, env };
}
} // namespace

AnalogPatch defaultAnalog(Kind kind)
{
    AnalogPatch patch; // The lead, which the melody plays.
    switch (kind)
    {
        case Kind::melody:
            break;
        case Kind::chords:
            patch.osc1 = osc(Wave::sawtooth, 0, -8.0f, 0.5f);
            patch.osc2 = osc(Wave::sawtooth, 0, 8.0f, 0.5f);
            patch.filter = { FilterType::lowpass, 1400.0f, 1.0f, 1.0f, 0.3f };
            patch.filterEnv = { 0.2f, 1.0f, 0.5f, 0.6f };
            patch.ampEnv = { 0.08f, 0.8f, 0.8f, 0.5f };
            patch.lfo = { Wave::triangle, 0.4f, AnalogLfoTarget::filter, 0.15f };
            patch.volume = 0.45f;
            break;
        case Kind::bass:
            patch.osc1 = osc(Wave::sawtooth, 0, 0.0f, 0.7f);
            patch.osc2 = osc(Wave::square, -1, 0.0f, 0.5f);
            patch.filter = { FilterType::lowpass, 380.0f, 5.0f, 2.0f, 0.3f };
            patch.filterEnv = { 0.005f, 0.25f, 0.15f, 0.1f };
            patch.ampEnv = { 0.005f, 0.3f, 0.85f, 0.08f };
            patch.lfo = { Wave::sine, 5.0f, AnalogLfoTarget::pitch, 0.0f };
            patch.volume = 0.9f;
            break;
        case Kind::guideTones:
            patch.osc1 = osc(Wave::sine, 0, 0.0f, 0.8f);
            patch.osc2 = osc(Wave::triangle, 1, 0.0f, 0.2f);
            patch.filter = { FilterType::lowpass, 4000.0f, 0.0f, 0.0f, 0.0f };
            patch.filterEnv = { 0.01f, 0.5f, 1.0f, 0.3f };
            patch.ampEnv = { 0.03f, 0.5f, 0.8f, 0.3f };
            patch.lfo = { Wave::sine, 5.0f, AnalogLfoTarget::pitch, 0.0f };
            patch.volume = 0.5f;
            break;
    }
    return patch;
}

FmPatch defaultFm(Kind kind)
{
    FmPatch patch; // The brass, which the melody plays.
    switch (kind)
    {
        case Kind::melody:
            break;
        case Kind::chords: // The classic electric piano.
            patch.algorithm = 5;
            patch.feedback = 0.0f;
            patch.ops = { op(1, 0.8f, { 0.002f, 2.5f, 0.0f, 0.6f }),
                          op(1, 0.5f, { 0.002f, 1.2f, 0.1f, 0.5f }, 0.8f),
                          op(1, 0.35f, { 0.002f, 0.8f, 0.0f, 0.4f }, 0.8f, 3.0f),
                          op(14, 0.35f, { 0.002f, 0.25f, 0.0f, 0.2f }, 0.9f) };
            patch.lfo = { Wave::sine, 5.0f, FmLfoTarget::pitch, 0.0f };
            patch.volume = 0.6f;
            break;
        case Kind::bass:
            patch.algorithm = 3;
            patch.feedback = 0.6f;
            patch.ops = { op(1, 0.9f, { 0.002f, 0.8f, 0.7f, 0.08f }),
                          op(1, 0.5f, { 0.002f, 0.25f, 0.2f, 0.1f }),
                          op(3, 0.15f, { 0.002f, 0.15f, 0.0f, 0.1f }),
                          op(1, 0.3f, { 0.002f, 0.4f, 0.3f, 0.1f }) };
            patch.lfo = { Wave::sine, 5.0f, FmLfoTarget::pitch, 0.0f };
            patch.volume = 0.85f;
            break;
        case Kind::guideTones:
            patch.algorithm = 7;
            patch.feedback = 0.0f;
            patch.ops = { op(1, 0.8f, { 0.03f, 0.5f, 0.8f, 0.3f }),
                          op(2, 0.15f, { 0.03f, 0.5f, 0.8f, 0.3f }),
                          op(1, 0.0f, { 0.03f, 0.5f, 0.8f, 0.3f }),
                          op(1, 0.0f, { 0.03f, 0.5f, 0.8f, 0.3f }) };
            patch.lfo = { Wave::sine, 5.0f, FmLfoTarget::pitch, 0.0f };
            patch.volume = 0.5f;
            break;
    }
    return patch;
}
} // namespace tonwerk
