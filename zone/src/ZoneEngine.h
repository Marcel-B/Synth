#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

/**
 * Tonwerk Zone's sound, plain C++: a compressor that listens to and works on one range of the spectrum, a bell around a
 * frequency or everything below or above it (a dynamic EQ: a de-esser, a tamer of harsh mids or a boomy bass, or room
 * for the kick in the bass's lows with the kick on the side chain). Borrows the state variable filter from Tonwerk
 * Wobble (`WobbleEngine.h`).
 */
namespace tonwerkzone
{
using tonwerkwobble::SvfStage;

inline constexpr double kPi = 3.14159265358979323846;
/** Butterworth's damping, for the shelves. */
inline constexpr float kButterworth = 1.41421356f;
inline constexpr float kKneeDb = 6.0f;

/** The range's shape; the order of the menu, saved in projects: append only. */
enum class Shape
{
    bell,
    low,
    high
};

/** What is turned down: only the range, or everything (the range only tells when). Menu order: append only. */
enum class Mode
{
    band,
    wide
};

enum class Source
{
    internal,
    sidechain
};

/** The knobs in the parameters' units. */
struct Settings
{
    Shape shape = Shape::bell;
    float frequency = 1000.0f;
    /** The bell's width in semitones between its -3 dB points. */
    float widthSemitones = 12.0f;
    Mode mode = Mode::band;
    float thresholdDb = -24.0f;
    float ratio = 4.0f;
    float attackMs = 5.0f;
    float releaseMs = 100.0f;
    /** The most the range is turned down, in dB. */
    float rangeDb = 24.0f;
    Source source = Source::internal;
    /** Plays what the detector hears instead of the result, to find the range. */
    bool listen = false;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** The bell's damping (1/Q) for a width in semitones between its half-power points. */
inline float dampingFor(float semitones)
{
    const double n = std::exp2(std::clamp((double) semitones, 1.0, 72.0) / 12.0);
    return (float) ((n - 1.0) / std::sqrt(n));
}

/** The reduction in dB the curve asks for at `levelDb`: a soft knee, then the ratio, at most the range. */
inline float reductionFor(float levelDb, const Settings& s)
{
    const float over = levelDb - s.thresholdDb;
    const float slope = 1.0f - 1.0f / std::max(1.0f, s.ratio);
    float reduction = 0.0f;
    if (std::abs(over) <= 0.5f * kKneeDb)
    {
        const float into = over + 0.5f * kKneeDb;
        reduction = slope * into * into / (2.0f * kKneeDb);
    }
    else if (over > 0.0f)
        reduction = slope * over;
    return std::min(reduction, std::max(0.0f, s.rangeDb));
}

/**
 * The response in dB at `hz` while the range is turned down to gain `g` (the analog prototype; the digital filter
 * matches it closely below a quarter of the sample rate), for the editor's curve.
 */
inline float responseDb(double hz, const Settings& s, float g)
{
    if (s.mode == Mode::wide)
        return 20.0f * std::log10(std::max(g, 1.0e-6f));
    const std::complex<double> j(0.0, 1.0);
    const auto x = j * (hz / std::max(1.0, (double) s.frequency));
    std::complex<double> h;
    if (s.shape == Shape::bell)
    {
        const double k = dampingFor(s.widthSemitones);
        h = (x * x + k * g * x + 1.0) / (x * x + k * x + 1.0);
    }
    else
    {
        const double k = kButterworth, root = std::sqrt((double) g);
        h = s.shape == Shape::low ? (x * x + k * root * x + (double) g) / (x * x + k * x + 1.0)
                                  : ((double) g * x * x + k * root * x + 1.0) / (x * x + k * x + 1.0);
    }
    return (float) (20.0 * std::log10(std::max(std::abs(h), 1.0e-6)));
}

/**
 * The compressor on its range.
 *
 * One state variable filter per side splits the signal into its low, band and high parts, which add back up to the
 * signal; the range is the band (scaled to peak at 1) for the bell, the low or high part for the shelves. Turning it
 * down by g is a matter of how the parts are summed (the bell x - (1 - g)·band, the low shelf hp + k√g·bp + g·lp, the
 * high shelf mirrored), so at g = 1 the output is the input to the bit, the gain can move every sample without
 * recomputing the filter, and the mix never combs. The detector averages the louder side's power of the range (of the
 * input, or of the side chain through its own filter) over about two of its cycles, read 3 dB up so a sine's level is
 * its peak; the reduction is smoothed in dB, rising with the attack, falling with the release.
 */
class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& f : main)
            f.reset();
        for (auto& f : key)
            f.reset();
        power = 0.0f;
        reduction = 0.0f;
        shownReduction = 0.0f;
        shownLevel = -100.0f;
    }

    /** In place, stereo; `keyLeft`/`keyRight` are the side chain (null when there is none). */
    void process(float* left, float* right, const float* keyLeft, const float* keyRight, int count, const Settings& s)
    {
        const float hz = std::clamp(s.frequency, 10.0f, 0.45f * (float) rate);
        const float g = (float) std::tan(kPi * hz / rate);
        const float k = s.shape == Shape::bell ? dampingFor(s.widthSemitones) : kButterworth;
        const float attack = coefficient(s.attackMs * 0.001f);
        const float release = coefficient(s.releaseMs * 0.001f);
        // About two cycles of the range's frequency, so a low range's own wave does not ripple through the gain.
        const float detector = coefficient(std::clamp(2.0f / hz, 0.001f, 0.025f));
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f), dry = 1.0f - wet;
        const bool fromKey = s.source == Source::sidechain;
        shownLevel = -100.0f;
        shownReduction = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const float in[2] = { left[i], right[i] };
            std::array<SvfStage::Out, 2> parts {};
            std::array<float, 2> heard {};
            for (std::size_t c = 0; c < 2; ++c)
            {
                parts[c] = main[c].process(in[c], g, k);
                if (fromKey)
                {
                    const float* source = c == 0 ? keyLeft : (keyRight != nullptr ? keyRight : keyLeft);
                    heard[c] = range(key[c].process(source != nullptr ? source[i] : 0.0f, g, k), k, s.shape);
                }
                else
                    heard[c] = range(parts[c], k, s.shape);
            }
            const float p = std::max(heard[0] * heard[0], heard[1] * heard[1]);
            power = p + (power - p) * detector;
            const float levelDb = 10.0f * std::log10(2.0f * power + 1.0e-18f);
            const float target = reductionFor(levelDb, s);
            reduction = target + (reduction - target) * (target > reduction ? attack : release);
            const float gain = std::pow(10.0f, -reduction / 20.0f);
            shownLevel = std::max(shownLevel, levelDb);
            shownReduction = std::max(shownReduction, reduction);

            for (std::size_t c = 0; c < 2; ++c)
            {
                float y;
                if (s.listen)
                    y = heard[c];
                else if (s.mode == Mode::wide)
                    y = gain * in[c];
                else
                    y = turnedDown(in[c], parts[c], k, gain, s.shape);
                const float out = output * (dry * in[c] + wet * y);
                (c == 0 ? left : right)[i] = out;
            }
        }
    }

    /** For the meter: the detector's peak level in the last block (dBFS) and the deepest reduction there (dB). */
    float levelNow() const { return shownLevel; }
    float reductionNow() const { return shownReduction; }

private:
    /** The range's part of the signal: the bell's band scaled to peak at 1, or the low or high part. */
    static float range(const SvfStage::Out& part, float k, Shape shape)
    {
        switch (shape)
        {
            case Shape::low: return part.low;
            case Shape::high: return part.high;
            case Shape::bell: break;
        }
        return k * part.band;
    }

    /** The input with its range turned down to `gain`; the input itself when `gain` is 1. */
    static float turnedDown(float x, const SvfStage::Out& part, float k, float gain, Shape shape)
    {
        if (shape == Shape::bell)
            return x - (1.0f - gain) * k * part.band;
        const float shelf = k * (1.0f - std::sqrt(gain)) * part.band;
        if (shape == Shape::low)
            return x - shelf - (1.0f - gain) * part.low;
        return x - shelf - (1.0f - gain) * part.high;
    }

    float coefficient(float seconds) const { return (float) std::exp(-1.0 / (std::max(1.0e-5f, seconds) * rate)); }

    double rate = 48000.0;
    std::array<SvfStage, 2> main {}, key {};
    float power = 0.0f, reduction = 0.0f;
    float shownReduction = 0.0f, shownLevel = -100.0f;
};
} // namespace tonwerkzone
