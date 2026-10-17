#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

/**
 * Tonwerk Multiband's sound, plain C++: three bands split by Linkwitz-Riley crossovers, each with its own compressor
 * that pulls loud passages down and, if asked, lifts quiet ones up (upward compression, the "OTT" sound of dubstep and
 * EDM). Borrows the state variable filter from Tonwerk Wobble (`WobbleEngine.h`).
 */
namespace tonwerkmultiband
{
using tonwerkwobble::SvfStage;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr int kBands = 3;
/** Butterworth's damping: two such stages in a row make a Linkwitz-Riley crossover of 24 dB per octave. */
inline constexpr float kButterworth = 1.41421356f;
/** The downward and upward curves' soft knee, in dB. */
inline constexpr float kKneeDb = 6.0f;
/** Upward compression lifts by at most this much, so a fading tail does not climb out of the noise. */
inline constexpr float kMaxBoostDb = 18.0f;
/** Below this level the lift fades out over 12 dB, so silence and hiss stay where they are. */
inline constexpr float kBoostFloorDb = -60.0f;
/**
 * Each band's detector averages the power over this time, in seconds: long enough that a band's own cycles (25 ms at
 * 40 Hz) do not ripple through its gain, short enough that the highs follow their hits.
 */
inline constexpr std::array<float, kBands> kDetectorTime { 0.02f, 0.005f, 0.002f };

struct Band
{
    float thresholdDb = -20.0f;
    float ratio = 3.0f;
    /** 0 leaves what lies under the threshold alone, 1 lifts it all the way up to it (at most kMaxBoostDb). */
    float upward = 0.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    float gainDb = 0.0f;
    bool on = true;
    bool solo = false;
};

/** The knobs in the parameters' units. */
struct Settings
{
    float inputDb = 0.0f;
    float lowSplit = 200.0f;   // Hz, between low and mid
    float highSplit = 2500.0f; // Hz, between mid and high
    /** Scales every band's lift and reduction together: 0 is the bands as they are, 1 the full curves. */
    float depth = 1.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
    std::array<Band, kBands> bands { Band { -20.0f, 3.0f, 0.0f, 20.0f, 150.0f },
                                     Band { -20.0f, 3.0f, 0.0f, 10.0f, 120.0f },
                                     Band { -20.0f, 3.0f, 0.0f, 5.0f, 80.0f } };
};

/** A soft-kneed static curve: how many dB `over` (dB past the threshold) is moved by `slope` (0 to 1). */
inline float kneeCurve(float over, float slope, float kneeDb = kKneeDb)
{
    if (kneeDb > 0.0f && std::abs(over) <= 0.5f * kneeDb)
    {
        const float into = over + 0.5f * kneeDb;
        return slope * into * into / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

/** The gain change in dB a band asks for at `levelDb`: negative above the threshold, positive (a lift) below. */
inline float changeFor(float levelDb, const Band& band)
{
    const float reduction = kneeCurve(levelDb - band.thresholdDb, 1.0f - 1.0f / std::max(1.0f, band.ratio));
    float boost = std::min(kMaxBoostDb, kneeCurve(band.thresholdDb - levelDb, std::clamp(band.upward, 0.0f, 1.0f)));
    boost *= std::clamp((levelDb - kBoostFloorDb + 12.0f) / 12.0f, 0.0f, 1.0f);
    return boost - reduction;
}

/**
 * Three bands, each compressed on its own, summed back.
 *
 * The crossovers are Linkwitz-Riley (two Butterworth stages in a row), whose low and high parts add up to an allpass:
 * the low band takes the second crossover's allpass too, so all three sum to a flat response, and the dry side of the
 * mix runs through the same two allpasses, so dry and wet line up at any mix and never comb. Each band's detector
 * averages the louder side's power (one gain for both sides), read 3 dB up so a sine's level is its peak; the gain change
 * is smoothed in dB, falling with the attack and rising with the release.
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
        for (auto& side : sides)
            side = {};
        for (auto& b : state)
            b = {};
    }

    /** In place, stereo. */
    void process(float* left, float* right, int count, const Settings& s)
    {
        const float lowHz = std::clamp(s.lowSplit, 20.0f, 0.4f * (float) rate);
        const float highHz = std::clamp(s.highSplit, lowHz, 0.45f * (float) rate);
        const float gLow = tuning(lowHz), gHigh = tuning(highHz);
        const float input = std::pow(10.0f, s.inputDb / 20.0f);
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f), dry = 1.0f - wet;
        const float depth = std::clamp(s.depth, 0.0f, 1.0f);
        bool anySolo = false;
        for (const auto& band : s.bands)
            anySolo = anySolo || band.solo;

        std::array<float, kBands> attack {}, release {}, detector {}, makeup {};
        std::array<bool, kBands> heard {};
        for (std::size_t b = 0; b < kBands; ++b)
        {
            const auto& band = s.bands[b];
            attack[b] = coefficient(band.attackMs * 0.001f);
            release[b] = coefficient(band.releaseMs * 0.001f);
            detector[b] = coefficient(kDetectorTime[b]);
            makeup[b] = band.on ? band.gainDb : 0.0f;
            heard[b] = ! anySolo || band.solo;
            state[b].shownLevel = -100.0f;
            state[b].shownChange = 0.0f;
        }

        for (int i = 0; i < count; ++i)
        {
            std::array<std::array<float, kBands>, 2> split {};
            std::array<float, 2> clean {};
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto& f = sides[c];
                const float x = c == 0 ? left[i] : right[i];
                // The dry side through both crossovers' allpasses, so it lines up with the bands.
                clean[c] = allpass(f.dryLow, allpass(f.dryHigh, x, gHigh), gLow);
                const float in = input * x;
                const auto first = f.splitLow.process(in, gLow, kButterworth);
                const float low = f.lowLow.process(first.low, gLow, kButterworth).low;
                const float rest = f.lowHigh.process(first.high, gLow, kButterworth).high;
                const auto second = f.splitHigh.process(rest, gHigh, kButterworth);
                split[c][0] = allpass(f.lowAllpass, low, gHigh);
                split[c][1] = f.highLow.process(second.low, gHigh, kButterworth).low;
                split[c][2] = f.highHigh.process(second.high, gHigh, kButterworth).high;
            }

            std::array<float, 2> sum {};
            for (std::size_t b = 0; b < kBands; ++b)
            {
                auto& st = state[b];
                const auto& band = s.bands[b];
                const float power = std::max(split[0][b] * split[0][b], split[1][b] * split[1][b]);
                st.power = power + (st.power - power) * detector[b];
                const float levelDb = 10.0f * std::log10(2.0f * st.power + 1.0e-18f);
                const float target = band.on ? depth * changeFor(levelDb, band) : 0.0f;
                st.change = target + (st.change - target) * (target < st.change ? attack[b] : release[b]);
                if (! heard[b])
                    continue;
                const float gain = std::pow(10.0f, (st.change + makeup[b]) / 20.0f);
                sum[0] += gain * split[0][b];
                sum[1] += gain * split[1][b];
                st.shownLevel = std::max(st.shownLevel, levelDb);
                if (std::abs(st.change) > std::abs(st.shownChange))
                    st.shownChange = st.change;
            }
            left[i] = output * (dry * clean[0] + wet * sum[0]);
            right[i] = output * (dry * clean[1] + wet * sum[1]);
        }
    }

    /** For the meter: a band's detector peak in the last block (dBFS) and its largest gain change there (dB). */
    float levelNow(int band) const { return state[(std::size_t) band].shownLevel; }
    float changeNow(int band) const { return state[(std::size_t) band].shownChange; }

private:
    struct Side
    {
        SvfStage splitLow, lowLow, lowHigh, splitHigh, highLow, highHigh, lowAllpass, dryLow, dryHigh;
    };
    struct BandState
    {
        float power = 0.0f, change = 0.0f;
        float shownLevel = -100.0f, shownChange = 0.0f;
    };

    /** Two Butterworth stages' low and high parts add up to this second-order allpass at the same frequency. */
    static float allpass(SvfStage& stage, float x, float g)
    {
        return x - 2.0f * kButterworth * stage.process(x, g, kButterworth).band;
    }

    float tuning(float hz) const { return (float) std::tan(kPi * hz / rate); }
    float coefficient(float seconds) const { return (float) std::exp(-1.0 / (std::max(1.0e-5f, seconds) * rate)); }

    double rate = 48000.0;
    std::array<Side, 2> sides {};
    std::array<BandState, kBands> state {};
};
} // namespace tonwerkmultiband
