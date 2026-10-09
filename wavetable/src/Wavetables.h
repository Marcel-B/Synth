#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace tonwerkwave
{
/** Frames per table: the position knob morphs through them. */
inline constexpr int kFrames = 64;
/** Band-limited copies per frame, one per octave: level L holds the harmonics up to kMaxHarmonics >> L. */
inline constexpr int kLevels = 10;
inline constexpr int kMaxHarmonics = 512;

/**
 * A wavetable: kFrames single cycles, each stored once per octave with the harmonics that octave can play without
 * aliasing (a mipmap, made by cutting the spectrum, so a high note simply has fewer harmonics). Every table here is
 * computed from a formula when the plugin loads; none is copied from another synth.
 *
 * A level with few harmonics needs few samples: its cycle is 8 samples per highest harmonic, at least 256 and at
 * most 2048, which keeps a table at under 2 MB. Each stored cycle has one extra sample, a copy of its first, so the
 * linear interpolation never wraps.
 */
class Wavetable
{
public:
    std::string name;

    static int harmonics(int level) { return kMaxHarmonics >> level; }
    static int size(int level)
    {
        const int n = 8 * harmonics(level);
        return n < 256 ? 256 : n > 2048 ? 2048 : n;
    }

    /** The first sample of `frame` at `level`. */
    const float* cycle(int level, int frame) const
    {
        return data.data() + offsets[(std::size_t) level] + (std::size_t) frame * (std::size_t) (size(level) + 1);
    }

    /**
     * The level for a note of `cyclesPerSample` (frequency / sample rate): the richest one whose top harmonic stays
     * below Nyquist.
     */
    static int levelFor(double cyclesPerSample)
    {
        // Harmonics allowed below Nyquist; each level down halves them.
        const double allowed = 0.5 / std::max(cyclesPerSample, 1.0e-9);
        int level = 0;
        while (level < kLevels - 1 && (double) harmonics(level) > allowed)
            ++level;
        return level;
    }

    /** One sample at `phase` (0 to 1) between the frames `position` (0 to kFrames - 1) falls between. */
    float read(int level, float position, double phase) const
    {
        const int frame = std::min((int) position, kFrames - 2);
        const float between = position - (float) frame;
        const float* a = cycle(level, frame);
        const float* b = a + size(level) + 1;
        const int n = size(level);
        const double index = phase * n;
        // A warped phase can round up to exactly 1; that is the wrap sample's place, not one past it.
        const int i = std::min((int) index, n - 1);
        const float t = (float) (index - i);
        const float sa = a[i] + (a[i + 1] - a[i]) * t;
        const float sb = b[i] + (b[i + 1] - b[i]) * t;
        return sa + (sb - sa) * between;
    }

    std::vector<float> data;
    std::array<std::size_t, kLevels> offsets {};
};

/** The tables every instance shares; made once, on first use (on the message thread, from a constructor). */
class WavetableBank
{
public:
    static const WavetableBank& instance();

    int count() const { return (int) tables.size(); }
    const Wavetable& table(int index) const
    {
        return tables[(std::size_t) std::clamp(index, 0, count() - 1)];
    }
    /** The names in the order of the oscillator's table menu; part of saved projects, so only ever appended to. */
    static std::vector<std::string> names();

private:
    WavetableBank();
    std::vector<Wavetable> tables;
};
} // namespace tonwerkwave
