#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Tonwerk Scope's workings, plain C++: what the audio thread keeps for the editor (the last samples of both sides, and
 * the song's last bars as columns of minimum and maximum) and what the editor reads out of it (the trigger point, the
 * pitch). The audio itself passes untouched.
 */
namespace tonwerkscope
{
/** The display's modes; the order of the menu, saved in projects: append only. */
enum class Mode
{
    wave,
    bars,
    xy,
    tuner
};

enum class Trigger
{
    rising,
    falling,
    free
};

/** Which signal the wave and bars modes draw. Menu order: append only. */
enum class Channel
{
    sum,
    left,
    right,
    both,
    side
};

/** The bars mode's window in beats, in the menu's order (append only). */
inline constexpr std::array<double, 5> kWindowBeats { 1.0, 2.0, 4.0, 8.0, 16.0 };
inline constexpr std::array<const char*, 5> kWindowNames { "1/4", "1/2", "1 Takt", "2 Takte", "4 Takte" };

/** The knobs in the parameters' units. */
struct Settings
{
    Mode mode = Mode::wave;
    float timeMs = 20.0f;
    int window = 2; // one bar
    Trigger trigger = Trigger::rising;
    float triggerLevel = 0.0f; // -1 to 1, of full scale before the gain
    Channel channel = Channel::sum;
    float gainDb = 0.0f;
    bool autoGain = true;
    float persistence = 0.5f;
    int colour = 0;
    bool hold = false;
    float reference = 440.0f; // the tuner's A in Hz (Logic's A3)
};

/**
 * The last samples of both sides as atomics: the audio thread writes, the editor reads whenever it likes. A read that
 * straddles a write gets a few samples of two blocks, which is fine for a picture.
 */
class Ring
{
public:
    static constexpr int kSize = 1 << 16;

    void write(const float* left, const float* right, int count)
    {
        auto at = position.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            const auto index = (std::size_t) (at & (kSize - 1));
            data[0][index].store(left[i], std::memory_order_relaxed);
            data[1][index].store(right[i], std::memory_order_relaxed);
            ++at;
        }
        position.store(at, std::memory_order_release);
    }

    /** The newest `count` samples (at most kSize), oldest first. */
    void read(float* left, float* right, int count) const
    {
        count = std::min(count, kSize);
        const auto end = position.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const auto index = (std::size_t) ((end - (std::uint64_t) (count - i)) & (kSize - 1));
            left[i] = data[0][index].load(std::memory_order_relaxed);
            right[i] = data[1][index].load(std::memory_order_relaxed);
        }
    }

    /** How many samples were ever written, so the editor can tell whether anything new came. */
    std::uint64_t written() const { return position.load(std::memory_order_acquire); }

private:
    std::array<std::array<std::atomic<float>, kSize>, 2> data {};
    std::atomic<std::uint64_t> position { 0 };
};

/**
 * The song's last window (a quarter to four bars) as columns of minimum and maximum per side, placed by the beat: the
 * column of a beat position is where that position sits in the window, so a bar always draws in the same place, as a
 * DAW's waveform does. A column is cleared when the beat enters it, so it shows the latest pass.
 */
class BeatTrace
{
public:
    static constexpr int kColumns = 1024;

    /** `beat` is the song position of the block's first sample in quarter notes, `perSample` how far one sample moves it. */
    void write(const float* left, const float* right, int count, double beat, double perSample, double windowBeats)
    {
        for (int i = 0; i < count; ++i)
        {
            double phase = (beat + perSample * i) / windowBeats;
            phase -= std::floor(phase);
            const int column = std::min(kColumns - 1, (int) (phase * kColumns));
            auto& c = columns[(std::size_t) column];
            if (column != current)
            {
                current = column;
                c.minimum[0].store(left[i], std::memory_order_relaxed);
                c.maximum[0].store(left[i], std::memory_order_relaxed);
                c.minimum[1].store(right[i], std::memory_order_relaxed);
                c.maximum[1].store(right[i], std::memory_order_relaxed);
                continue;
            }
            for (std::size_t side = 0; side < 2; ++side)
            {
                const float x = side == 0 ? left[i] : right[i];
                if (x < c.minimum[side].load(std::memory_order_relaxed))
                    c.minimum[side].store(x, std::memory_order_relaxed);
                if (x > c.maximum[side].load(std::memory_order_relaxed))
                    c.maximum[side].store(x, std::memory_order_relaxed);
            }
        }
        playhead.store(current, std::memory_order_relaxed);
    }

    void clear()
    {
        for (auto& c : columns)
            for (std::size_t side = 0; side < 2; ++side)
            {
                c.minimum[side].store(0.0f, std::memory_order_relaxed);
                c.maximum[side].store(0.0f, std::memory_order_relaxed);
            }
        current = -1;
    }

    float minimum(int column, int side) const
    {
        return columns[(std::size_t) column].minimum[(std::size_t) side].load(std::memory_order_relaxed);
    }
    float maximum(int column, int side) const
    {
        return columns[(std::size_t) column].maximum[(std::size_t) side].load(std::memory_order_relaxed);
    }
    /** The column the newest sample went into. */
    int playheadColumn() const { return playhead.load(std::memory_order_relaxed); }

private:
    struct Column
    {
        std::array<std::atomic<float>, 2> minimum {}, maximum {};
    };
    std::array<Column, kColumns> columns {};
    int current = -1;
    std::atomic<int> playhead { 0 };
};

/**
 * Where a trace of `length` samples should start in `x` (of `count` samples) so it stands still: the latest crossing of
 * `level` in the trigger's direction that leaves `length` samples after it. A crossing counts only when the signal then
 * goes clearly past the level before it turns back, so noise riding on a slow wave does not trigger. Without one (or
 * with Trigger::free) the trace simply ends at the newest sample.
 */
inline int findTrigger(const float* x, int count, int length, float level, Trigger trigger)
{
    const int latest = count - length;
    if (latest <= 0)
        return 0;
    if (trigger == Trigger::free)
        return latest;
    const float sign = trigger == Trigger::rising ? 1.0f : -1.0f;
    // Search back at most a window (and 2048 samples), so a trace never lags far behind the sound.
    const int earliest = std::max(1, latest - std::max(length, 2048));
    // "Clearly past": a twentieth of the signal's own swing, so quiet sounds trigger as well as loud ones.
    float peak = 0.0f;
    for (int n = earliest - 1; n < count; ++n)
        peak = std::max(peak, std::abs(x[n] - level));
    const float hysteresis = 0.05f * peak;
    for (int n = latest; n >= earliest; --n)
    {
        if (! (sign * (x[n - 1] - level) < 0.0f && sign * (x[n] - level) >= 0.0f))
            continue;
        for (int m = n; m < count; ++m)
        {
            const float past = sign * (x[m] - level);
            if (past > hysteresis)
                return n;
            if (past < 0.0f)
                break;
        }
    }
    return latest;
}

/**
 * The pitch of a signal, by YIN (de Cheveigné and Kawahara) on a copy decimated to about 12 kHz: 30 Hz to 1.5 kHz.
 * Returns 0 when the signal is quiet or has no clear period. For the editor's readout; allocates only in `prepare`.
 */
class PitchEstimator
{
public:
    void prepare(double sampleRate)
    {
        factor = std::max(1, (int) std::lround(sampleRate / 12000.0));
        rate = sampleRate / factor;
        tauMin = (int) (rate / 1500.0);
        tauMax = (int) std::ceil(rate / 30.0);
        window = tauMax;
        fullRate = sampleRate;
        fullTauMax = (int) std::ceil(sampleRate / 30.0) + 32;
        decimated.assign((std::size_t) (window + tauMax + 2), 0.0f);
        difference.assign((std::size_t) tauMax + 2, 0.0f);
    }

    /** How many input samples `estimate` and `refine` read. */
    int needed() const { return std::max((window + tauMax + 2) * factor, 3 * fullTauMax + 16); }

    /** The pitch of the last `needed()` samples of `x` in Hz, or 0. */
    float estimate(const float* x, int count)
    {
        if (count < needed())
            return 0.0f;
        const int length = (int) decimated.size();
        const float* from = x + count - length * factor;
        float energy = 0.0f;
        for (int n = 0; n < length; ++n)
        {
            // The mean of each group of `factor` samples: a crude low pass, enough below 1.5 kHz.
            float sum = 0.0f;
            for (int k = 0; k < factor; ++k)
                sum += from[n * factor + k];
            decimated[(std::size_t) n] = sum / (float) factor;
            energy += decimated[(std::size_t) n] * decimated[(std::size_t) n];
        }
        if (energy / (float) length < 1.0e-7f)
            return 0.0f;

        difference[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int n = 0; n < window; ++n)
            {
                const float delta = decimated[(std::size_t) n] - decimated[(std::size_t) (n + tau)];
                d += delta * delta;
            }
            running += d;
            difference[(std::size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }
        // The first dip under the threshold, followed to its bottom; parabolic interpolation for the fraction.
        constexpr float threshold = 0.15f;
        for (int tau = std::max(2, tauMin); tau < tauMax; ++tau)
        {
            if (difference[(std::size_t) tau] >= threshold)
                continue;
            while (tau + 1 < tauMax && difference[(std::size_t) tau + 1] < difference[(std::size_t) tau])
                ++tau;
            const float a = difference[(std::size_t) tau - 1], b = difference[(std::size_t) tau],
                        c = difference[(std::size_t) tau + 1];
            const float bend = a - 2.0f * b + c;
            const float shift = bend > 0.0f ? 0.5f * (a - c) / bend : 0.0f;
            return (float) (rate / ((double) tau + std::clamp(shift, -0.5f, 0.5f)));
        }
        return 0.0f;
    }

    /**
     * `coarse` (from `estimate`) to a fraction of a cent, for the tuner: at 12 kHz a period of 440 Hz is 27 samples
     * and the parabola's fraction is worth several cents. So the difference function is searched again at the full
     * rate, 1.5 % either way, over two periods (at least 1024 samples), on the mean of neighbouring samples: the
     * highest harmonics bend the minimum away from a parabola, and that mild low pass halves their error. Within
     * a cent from 31 Hz to 1.3 kHz on a sawtooth.
     */
    float refine(const float* x, int count, float coarse) const
    {
        if (coarse <= 0.0f || count < needed())
            return coarse;
        const double period = fullRate / coarse;
        const int centre = (int) std::lround(period);
        const int reach = std::max(6, (int) std::ceil(0.015 * period));
        const int length = std::min(std::max(1024, 2 * centre), count - centre - reach - 2);
        const int start = count - length - centre - reach - 1;
        if (start < 1 || centre - reach < 2)
            return coarse;
        auto smooth = [x](int n) { return 0.5f * (x[n] + x[n - 1]); };
        std::array<float, 256> d {}; // 1.5 % either way of 30 Hz at 192 kHz: 193 lags
        const int taus = std::min(2 * reach + 1, (int) d.size());
        for (int i = 0; i < taus; ++i)
        {
            const int tau = centre - reach + i;
            float sum = 0.0f;
            for (int n = start; n < start + length; ++n)
            {
                const float delta = smooth(n) - smooth(n + tau);
                sum += delta * delta;
            }
            d[(std::size_t) i] = sum;
        }
        int at = 1;
        for (int i = 2; i < taus - 1; ++i)
            if (d[(std::size_t) i] < d[(std::size_t) at])
                at = i;
        const float before = d[(std::size_t) at - 1], best = d[(std::size_t) at], after = d[(std::size_t) at + 1];
        const float bend = before - 2.0f * best + after;
        const float shift = bend > 0.0f ? 0.5f * (before - after) / bend : 0.0f;
        return (float) (fullRate / ((double) (centre - reach + at) + std::clamp(shift, -0.5f, 0.5f)));
    }

private:
    int factor = 4, tauMin = 8, tauMax = 400, window = 400, fullTauMax = 1632;
    double rate = 12000.0, fullRate = 48000.0;
    std::vector<float> decimated, difference;
};

/** A note's name the way Logic writes it (C3 is MIDI note 60), e.g. "A3" for 440 Hz, with how many cents it is off. */
inline const char* noteName(int note)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[(std::size_t) (((note % 12) + 12) % 12)];
}
inline int noteOctave(int note) { return (int) std::floor(note / 12.0) - 2; }
inline double noteOf(double hz, double reference = 440.0) { return 69.0 + 12.0 * std::log2(hz / reference); }

/** What the tuner shows for a pitch: the nearest note and how many cents it is off, -50 to +50. */
struct Tuning
{
    int note = 69;
    float cents = 0.0f;
};
inline Tuning tuningOf(double hz, double reference)
{
    const double note = noteOf(hz, reference);
    const auto nearest = (int) std::lround(note);
    return { nearest, (float) ((note - nearest) * 100.0) };
}

/** The tuner's verdict: in tune within 3 cents (a trained ear hears about 5), close within 15. */
inline constexpr float kInTuneCents = 3.0f, kCloseCents = 15.0f;
} // namespace tonwerkscope
