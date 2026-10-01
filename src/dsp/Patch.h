#pragma once

#include <array>
#include <cmath>
#include <cstddef>

/**
 * A sound, as Tonwerk's browser synthesizers describe it (YuE-UI, ClientApp/src/logic/synth.ts, fm.ts, effects.ts).
 * The fields, units and ranges are theirs, so a patch saved in Tonwerk plays here unchanged; PatchJson.cpp reads and
 * writes the same JSON. The plugin keeps both engines' settings at once and plays the one `engine` names, so switching
 * back and forth loses nothing.
 */
namespace tonwerk
{
enum class Wave { sine, triangle, sawtooth, square };
enum class FilterType { lowpass, highpass, bandpass };
enum class AnalogLfoTarget { pitch, filter, amp };
enum class FmLfoTarget { pitch, index, amp };
enum class Engine { analog, fm };

/** Times in seconds, sustain as a share of the peak. */
struct Envelope
{
    float attack = 0.01f;
    float decay = 0.3f;
    float sustain = 0.75f;
    float release = 0.15f;
};

struct Oscillator
{
    /** `square` is the pulse wave; at a width of 50 % and nothing moving it, it is the plain square. */
    Wave wave = Wave::sawtooth;
    /** Whole octaves, -2 to 2. */
    int octave = 0;
    /** Cents, -50 to 50. */
    float detune = 0.0f;
    float level = 0.7f;
    /** Duty cycle of the pulse, 0.05 to 0.95. */
    float width = 0.5f;
    /** How far the LFO moves the width, 0 to 1. */
    float pwm = 0.0f;
    bool pwmLfo = true;
    /** How far the amp and the filter envelope move the width, -1 to 1. */
    float pwmAmpEnv = 0.0f;
    float pwmFilterEnv = 0.0f;
};

struct AnalogPatch
{
    Oscillator osc1;
    Oscillator osc2 { Wave::square, 0, 7.0f, 0.35f };
    float noise = 0.0f;
    struct
    {
        FilterType type = FilterType::lowpass;
        /** Hz at middle C. */
        float cutoff = 1800.0f;
        /** The Web Audio biquad's Q: decibels of resonance for low- and highpass, the plain Q for bandpass. */
        float resonance = 2.0f;
        /** Octaves the filter envelope opens the cutoff, -4 to 6. */
        float envAmount = 1.5f;
        float keyTrack = 0.5f;
    } filter;
    Envelope filterEnv { 0.01f, 0.4f, 0.3f, 0.2f };
    Envelope ampEnv { 0.01f, 0.3f, 0.75f, 0.15f };
    struct
    {
        Wave wave = Wave::sine;
        float rate = 5.5f;
        AnalogLfoTarget target = AnalogLfoTarget::pitch;
        float depth = 0.1f;
    } lfo;
    float volume = 0.8f;
};

struct Operator
{
    /** Multiple of the note's frequency, in halves from 0.5 to 16. */
    float ratio = 1.0f;
    float detune = 0.0f;
    /** A carrier's loudness, or how far a modulator bends its target. */
    float level = 0.8f;
    /** How much a soft note takes off this operator. */
    float velocity = 0.7f;
    Envelope env;
};

struct FmPatch
{
    /** 1 to 8, see `kAlgorithms`. */
    int algorithm = 4;
    float feedback = 0.4f;
    std::array<Operator, 4> ops {
        Operator { 1.0f, 0.0f, 0.8f, 0.7f, { 0.03f, 0.4f, 0.8f, 0.2f } },
        Operator { 1.0f, 0.0f, 0.55f, 0.6f, { 0.08f, 0.5f, 0.6f, 0.2f } },
        Operator { 1.0f, 0.0f, 0.3f, 0.7f, { 0.05f, 0.6f, 0.5f, 0.2f } },
        Operator { 2.0f, 0.0f, 0.15f, 0.7f, { 0.05f, 0.6f, 0.5f, 0.2f } },
    };
    struct
    {
        Wave wave = Wave::sine;
        float rate = 5.5f;
        FmLfoTarget target = FmLfoTarget::pitch;
        float depth = 0.1f;
    } lfo;
    float volume = 0.7f;
};

struct Effects
{
    struct
    {
        float mix = 0.0f;
        /** Seconds between repeats, 0.02 to 1.5. */
        float time = 0.35f;
        float feedback = 0.35f;
        /** Hz of the lowpass in the loop. */
        float tone = 4000.0f;
    } delay;
    struct
    {
        float mix = 0.0f;
        /** Seconds to -60 dB, 0.3 to 8. */
        float decay = 2.0f;
    } reverb;
};

struct Patch
{
    Engine engine = Engine::analog;
    AnalogPatch analog;
    FmPatch fm;
    Effects fx;
};

/** A TX81Z algorithm: which operator modulates which (0 is operator 1), and which are heard. */
struct Algorithm
{
    /** UTF-8, as printed on the synths. */
    const char* label;
    int modCount;
    std::array<std::array<int, 2>, 3> mods;
    int carrierCount;
    std::array<int, 4> carriers;
};

inline constexpr std::array<Algorithm, 8> kAlgorithms { {
    { "4→3→2→1", 3, { { { 3, 2 }, { 2, 1 }, { 1, 0 } } }, 1, { 0 } },
    { "(3+4)→2→1", 3, { { { 3, 1 }, { 2, 1 }, { 1, 0 } } }, 1, { 0 } },
    { "(4 + 3→2)→1", 3, { { { 3, 0 }, { 2, 1 }, { 1, 0 } } }, 1, { 0 } },
    { "(4→3 + 2)→1", 3, { { { 3, 2 }, { 2, 0 }, { 1, 0 } } }, 1, { 0 } },
    { "2→1 · 4→3", 2, { { { 3, 2 }, { 1, 0 } } }, 2, { 0, 2 } },
    { "4→(1 · 2 · 3)", 3, { { { 3, 0 }, { 3, 1 }, { 3, 2 } } }, 3, { 0, 1, 2 } },
    { "4→3 · 2 · 1", 1, { { { 3, 2 } } }, 3, { 0, 1, 2 } },
    { "1 · 2 · 3 · 4", 0, {}, 4, { 0, 1, 2, 3 } },
} };

inline bool isCarrier(const Algorithm& algorithm, int op)
{
    for (int i = 0; i < algorithm.carrierCount; ++i)
        if (algorithm.carriers[(std::size_t) i] == op)
            return true;
    return false;
}

/** Whether a setting is off; the patch's values are exact, so a tolerance only keeps the compiler quiet. */
inline bool isZero(float value) { return std::abs(value) < 1.0e-9f; }

/** Tonwerk's track kinds, each with its own starting sound per engine. */
enum class Kind { melody, chords, bass, guideTones };

AnalogPatch defaultAnalog(Kind kind);
FmPatch defaultFm(Kind kind);
} // namespace tonwerk
