#pragma once

namespace tonwerk
{
/** A parameter's range, the same as in Tonwerk's RANGES, FM_RANGES and EFFECT_RANGES. */
struct Range
{
    float min;
    float max;
};

namespace ranges
{
inline constexpr Range octave { -2, 2 };
inline constexpr Range detune { -50, 50 };
inline constexpr Range level { 0, 1 };
inline constexpr Range width { 0.05f, 0.95f };
inline constexpr Range pwm { 0, 1 };
inline constexpr Range pwmEnv { -1, 1 };
inline constexpr Range cutoff { 20, 18000 };
inline constexpr Range resonance { 0, 20 };
inline constexpr Range envAmount { -4, 6 };
inline constexpr Range keyTrack { 0, 1 };
inline constexpr Range attack { 0, 4 };
inline constexpr Range decay { 0.01f, 4 };
inline constexpr Range sustain { 0, 1 };
inline constexpr Range release { 0.01f, 6 };
inline constexpr Range rate { 0.1f, 20 };
inline constexpr Range depth { 0, 1 };
inline constexpr Range volume { 0, 1 };

inline constexpr Range algorithm { 1, 8 };
inline constexpr Range feedback { 0, 1 };
inline constexpr Range ratio { 0.5f, 16 };
inline constexpr Range velocity { 0, 1 };

inline constexpr Range mix { 0, 1 };
inline constexpr Range delayTime { 0.02f, 1.5f };
inline constexpr Range delayFeedback { 0, 0.9f };
inline constexpr Range tone { 500, 12000 };
inline constexpr Range reverbDecay { 0.3f, 8 };
} // namespace ranges
} // namespace tonwerk
