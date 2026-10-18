#pragma once

#include <cstddef>
#include <cstdint>

namespace twm
{
/** Frames per table: the position knob morphs through them, as in Tonwerk Wavetable. */
inline constexpr int kFrames = 64;
/**
 * Band-limited copies per frame, one per octave. The plugin keeps 512 harmonics in 10 levels; the module keeps 256 in
 * 9, which halves the memory. Only notes below about 94 Hz lose anything: their top harmonic ends at 256 times the
 * fundamental (10 kHz at 40 Hz) instead of reaching Nyquist.
 */
inline constexpr int kLevels = 9;
inline constexpr int kMaxHarmonics = 256;
inline constexpr int kTables = 10;

/**
 * A wavetable as the plugin builds it (same formulas, same mipmap), stored as int16 to fit the ESP32-S3's PSRAM:
 * kFrames single cycles per level, each 8 samples per highest harmonic (128 to 2048) plus one wrap sample, so the
 * linear interpolation never wraps. All ten tables take 5.7 MB.
 */
class Table
{
public:
    static int harmonics(int level) { return kMaxHarmonics >> level; }
    static int sizeLog2(int level)
    {
        // 8 samples per harmonic: 2^(11 - level), at least 2^7.
        const int s = 11 - level;
        return s < 7 ? 7 : s;
    }
    static int size(int level) { return 1 << sizeLog2(level); }
    /** Samples one table needs, all levels and frames. */
    static std::size_t samples();

    /** The richest level whose top harmonic stays below Nyquist for a note of `cyclesPerSample`. */
    static int levelFor(float cyclesPerSample)
    {
        const float allowed = 0.5f / (cyclesPerSample > 1.0e-9f ? cyclesPerSample : 1.0e-9f);
        int level = 0;
        while (level < kLevels - 1 && (float) harmonics(level) > allowed)
            ++level;
        return level;
    }

    const int16_t* cycle(int level, int frame) const
    {
        return data + offsets[level] + (std::size_t) frame * (std::size_t) (size(level) + 1);
    }

    /**
     * One sample at `phase` (0 to 1) between the frames `position` (0 to kFrames - 1) falls between. Phases arrive
     * as float: a warped phase is computed in float anyway, and 24 bits of mantissa are 10^-4 of a sample at 2048.
     */
    float read(int level, float position, float phase) const
    {
        int frame = (int) position;
        if (frame > kFrames - 2)
            frame = kFrames - 2;
        const float between = position - (float) frame;
        const int n = size(level);
        const int16_t* a = cycle(level, frame);
        const int16_t* b = a + n + 1;
        const float index = phase * (float) n;
        int i = (int) index;
        // A warped phase can round up to exactly 1; that is the wrap sample's place, not one past it.
        if (i > n - 1)
            i = n - 1;
        const float t = index - (float) i;
        const float sa = (float) a[i] + (float) (a[i + 1] - a[i]) * t;
        const float sb = (float) b[i] + (float) (b[i + 1] - b[i]) * t;
        return (sa + (sb - sa) * between) * kFromInt;
    }

    int16_t* data = nullptr;
    std::size_t offsets[kLevels] {};

    /** Full scale of the stored samples: a little under 32767, so a poorer level's Gibbs overshoot still fits. */
    static constexpr float kToInt = 30000.0f;
    static constexpr float kFromInt = 1.0f / kToInt;
};

/** The ten tables of Tonwerk Wavetable's menu, in its order, in one block of memory. */
class Bank
{
public:
    using Allocator = void* (*) (std::size_t bytes);

    /**
     * Raise this whenever a formula or the layout changes: the firmware keeps the computed tables in flash and
     * computes them again only when this differs from the copy's.
     */
    static constexpr uint32_t kVersion = 1;

    /** Bytes all tables take together. */
    static std::size_t bytes() { return (std::size_t) kTables * Table::samples() * sizeof(int16_t); }
    /** Points the tables into `block` (bytes() long), without computing anything. */
    static void layout(int16_t* block);
    /** Computes every table from its formula into the block given to layout(). */
    static void compute();
    /** layout() and compute() into memory from `allocate`; false when it runs out. */
    static bool build(Allocator allocate);

    static const Table& table(int index)
    {
        return tables[index < 0 ? 0 : index >= kTables ? kTables - 1 : index];
    }
    static const char* name(int index);

private:
    static Table tables[kTables];
};
} // namespace twm
