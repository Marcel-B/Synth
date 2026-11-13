#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tonwerkspread
{
using tonwerkwobble::SvfStage;

/**
 * How a side is made from the middle. A menu saved in projects: append only.
 * Diffus: the middle delayed and through four allpasses, an irregular pattern of what goes left and right, smooth.
 * Kamm: the middle delayed by `time` (Lauridsen's widener), a regular comb, strong and a little tonal.
 * Bänder: alternate bands of the spectrum to the left and right, no delay, no smear.
 */
enum Mode
{
    diffuse,
    comb,
    bands
};
inline constexpr std::array<const char*, 3> kModeNames { "Diffus", "Kamm", "Bänder" };

/**
 * The knobs in their own units. The defaults are the effect as it starts: a moderate, diffuse widening with the lows in
 * the middle.
 */
struct Settings
{
    /** The side the input already has: 0 mono, 1 as it is, 2 twice as wide. */
    float width = 1.0f;
    int mode = diffuse;
    /** How much side is made from the middle, 0 to 1. */
    float amount = 0.5f;
    /** The delay behind the made side (Diffus, Kamm), seconds. */
    float time = 0.012f;
    /** A slow wander of the made side, 0 to 1. */
    float motion = 0.2f;
    /** Below this the side is taken out, so the bass stays in the middle; at `kBassOff` or under, off. */
    float bassMono = 120.0f;
    /** A lowpass on the made side only, Hz. */
    float highCut = 20000.0f;
    /** Plays the mono sum on both sides, to hear what a mono speaker hears. */
    bool monoCheck = false;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

inline constexpr float kBassOff = 20.5f;
/** At or above this the made side's high cut is out of the way. */
inline constexpr float kHighOff = 19900.0f;
inline constexpr float kMaxTime = 0.040f;
/** How far the wander moves the delay, at motion 1. */
inline constexpr float kMotionSeconds = 0.0015f;
inline constexpr double kMotionHz = 0.31;
/** The Bänder mode's bands: centres an octave apart go left, what lies between them right. Odd, see `madeBands`. */
inline constexpr int kBands = 9;
inline constexpr float kFirstBand = 70.0f;
inline constexpr float kBandQ = 1.2f;

/** A delay line read anywhere behind its newest sample, on a Catmull-Rom cubic. */
class DelayLine
{
public:
    void prepare(int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4)
            size <<= 1;
        buffer.assign((std::size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }
    void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }
    void push(float x)
    {
        buffer[(std::size_t) write] = x;
        write = (write + 1) & mask;
    }
    /** The sample `delay` behind the next one to be pushed (1 is the newest), at least two for the cubic. */
    float read(float delay) const
    {
        const float at = std::max(2.0f, delay);
        const int whole = (int) at;
        const float t = at - (float) whole;
        auto sample = [this](int back) { return buffer[(std::size_t) ((write - back) & mask)]; };
        // y0 one newer than y1, y2 and y3 older.
        const float y0 = sample(whole - 1), y1 = sample(whole), y2 = sample(whole + 1), y3 = sample(whole + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

/**
 * A Schroeder allpass, flat in level. Its length is whole samples: an interpolated read inside the loop would lowpass
 * the highs once per round and take a third of a noise's power away.
 */
class Allpass
{
public:
    void prepare(int maxSamples) { line.prepare(maxSamples); }
    void clear() { line.clear(); }
    float process(float x, int delay, float g)
    {
        const float delayed = line.read((float) delay);
        const float v = x + g * delayed;
        line.push(v);
        return delayed - g * v;
    }

private:
    DelayLine line;
};

/**
 * A stereo widener that never changes the mono sum: everything happens on the side (L - R)/2, and the middle passes
 * as it came, so a mono speaker hears the input whatever the knobs say. The side is the input's own times `width` plus
 * a side made from the middle (`Mode`): left gets middle + made, right middle - made, two complementary patterns that
 * cancel in the sum. Below `bassMono` the whole side goes through a 24 dB Linkwitz-Riley high pass, so the lows sit in
 * the middle. All changes glide; latency 0.
 */
class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        const int longest = (int) std::ceil((kMaxTime + kMotionSeconds) * rate) + 8;
        combLine.prepare(longest);
        diffuseLine.prepare(longest);
        for (auto& stage : allpasses)
            stage.prepare(longest);
        smoothing = (float) (1.0 - std::exp(-1.0 / (0.02 * rate)));
        glide = (float) (1.0 - std::exp(-1.0 / (0.05 * rate)));
        reset();
    }

    void reset()
    {
        combLine.clear();
        diffuseLine.clear();
        for (auto& stage : allpasses)
            stage.clear();
        for (auto& stage : bandFilters)
            stage.reset();
        for (auto& stage : bassStages)
            stage.reset();
        highState = 0.0f;
        highFade = highOn = 0.0f;
        phase = 0.0;
        primed = false;
        counter = 0;
    }

    void process(float* left, float* right, int count, const Settings& s)
    {
        const Targets target = targetsOf(s);
        if (! primed)
        {
            now = target;
            primed = true;
            updateBands(target, 0.0f);
            updateBass(target.bass);
        }
        const double lfoStep = kMotionHz / rate;
        for (int i = 0; i < count; ++i)
        {
            now.approach(target, smoothing, glide);
            if ((counter++ & 15) == 0)
            {
                // Coefficients every 16 samples: the wander and the cutoffs move slowly.
                lfo = (float) std::sin(6.283185307179586 * phase);
                updateBands(now, lfo);
                updateBass(now.bass);
                highG = (float) std::tan(3.141592653589793 * std::min((double) now.high, 0.45 * rate) / rate);
                highOn = now.high < kHighOff ? 1.0f : 0.0f;
            }
            phase += lfoStep;
            if (phase >= 1.0)
                phase -= 1.0;

            const float l = left[i], r = right[i];
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            float made = now.diffuse * madeDiffuse(mid) + now.comb * madeComb(mid) + now.bands * madeBands(mid);
            // A one-pole TPT lowpass on the made side only, out of the way at the top of its range.
            const float v = (made - highState) * highG / (1.0f + highG);
            const float low = v + highState;
            highState = low + v;
            highFade += smoothing * (highOn - highFade);
            made += highFade * (low - made);

            float wide = now.width * side + now.amount * made;
            // Bass mono: the side alone through a Linkwitz-Riley high pass, the middle untouched. That turns the side's
            // phase a little above the crossover (a hard-left treble leaks into the right about 17 dB down three octaves
            // up, less higher), but the sum stays the input sample for sample and a centred sound keeps its peaks;
            // turning the middle by the matching allpass instead raised a sawtooth's peaks by 5 dB. The filter runs
            // also while off, so switching on fades into a filter that has settled.
            float high = wide;
            for (auto& stage : bassStages)
                high = stage.process(high, bassG, kButterworth).high;
            wide += now.bassOn * (high - wide);
            float outL = l + now.mix * (mid + wide - l);
            float outR = r + now.mix * (mid - wide - r);
            outL *= now.gain;
            outR *= now.gain;
            const float sum = 0.5f * (outL + outR);
            left[i] = outL + now.mono * (sum - outL);
            right[i] = outR + now.mono * (sum - outR);
        }
    }

    /**
     * For the editor's curve: how much louder a centred sound comes out on the left than on the right at `hz`, in dB,
     * at these settings without the wander. Positive leans left, negative right.
     */
    static float sideGainAt(const Settings& s, double hz, double sampleRate = 48000.0)
    {
        // The made side's gain relative to the middle, as a complex number, at the settings without wander.
        const double w = 6.283185307179586 * hz;
        double re = 0.0, im = 0.0;
        const auto mode = std::clamp(s.mode, 0, 2);
        if (mode == comb)
        {
            re = std::cos(w * s.time);
            im = -std::sin(w * s.time);
        }
        else if (mode == diffuse)
        {
            // Pre-delay times four allpasses (each e^{-jwD} - g over 1 - g e^{-jwD}).
            double ar = std::cos(w * s.time * kDiffuseLead), ai = -std::sin(w * s.time * kDiffuseLead);
            for (std::size_t k = 0; k < kAllpassShares.size(); ++k)
            {
                const double d = allpassLength((float) (s.time * sampleRate), k) / sampleRate;
                const double er = std::cos(w * d), ei = -std::sin(w * d);
                const double nr = er - kAllpassGain, ni = ei;
                const double dr = 1.0 - kAllpassGain * er, di = -kAllpassGain * ei;
                const double den = dr * dr + di * di;
                const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
                const double tr = ar * hr - ai * hi, ti = ar * hi + ai * hr;
                ar = tr;
                ai = ti;
            }
            re = ar;
            im = ai;
        }
        else
        {
            // The product of the allpasses (-s^2 + s/Q - 1) / (s^2 + s/Q + 1) at s = jx.
            re = 1.0;
            im = 0.0;
            // Evaluated at the bilinear transform's warped frequency, which is the digital filters' response exactly.
            const double warped = std::tan(3.141592653589793 * std::min(hz, 0.499 * sampleRate) / sampleRate);
            for (int k = 0; k < kBands; ++k)
            {
                const double centre = std::min(kFirstBand * std::exp2(k), 0.45 * sampleRate);
                const double x = warped / std::tan(3.141592653589793 * centre / sampleRate);
                const double nr = x * x - 1.0, ni = x / kBandQ;
                const double dr = 1.0 - x * x, di = x / kBandQ;
                const double den = dr * dr + di * di;
                const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
                const double tr = re * hr - im * hi, ti = re * hi + im * hr;
                re = tr;
                im = ti;
            }
        }
        if (s.highCut < kHighOff)
        {
            // The high cut, one pole as the bilinear transform warps it.
            const double c = std::tan(3.141592653589793 * hz / sampleRate)
                             / std::tan(3.141592653589793 * std::min((double) s.highCut, 0.45 * sampleRate) / sampleRate);
            const double hr = 1.0 / (1.0 + c * c), hi = -c / (1.0 + c * c);
            const double fr = re * hr - im * hi, fi = re * hi + im * hr;
            re = fr;
            im = fi;
        }
        double amount = s.amount;
        if (s.bassMono > kBassOff)
        {
            const double x = hz / s.bassMono;
            amount *= (x * x * x * x) / (1.0 + x * x * x * x); // LR4's magnitude, squared Butterworth.
        }
        // Left over right for a centred source: |1 + a h| / |1 - a h|, in dB.
        const double lr = std::hypot(1.0 + amount * re, amount * im), rr = std::hypot(1.0 - amount * re, amount * im);
        return (float) (20.0 * std::log10(std::max(lr, 1.0e-4) / std::max(rr, 1.0e-4)));
    }

    static constexpr float kDiffuseLead = 0.5f;
    static constexpr std::array<float, 4> kAllpassShares { 0.37f, 0.23f, 0.17f, 0.11f };
    static constexpr float kAllpassGain = 0.5f;

private:
    static constexpr float kButterworth = 1.4142135f;

    struct Targets
    {
        float width = 1.0f, amount = 0.0f, time = 0.0f, motion = 0.0f, bass = 20.0f, bassOn = 0.0f, high = 20000.0f;
        float diffuse = 1.0f, comb = 0.0f, bands = 0.0f, mono = 0.0f, mix = 1.0f, gain = 1.0f;

        void approach(const Targets& to, float k, float g)
        {
            auto move = [k](float& v, float target) { v += k * (target - v); };
            move(width, to.width);
            move(amount, to.amount);
            move(motion, to.motion);
            move(bassOn, to.bassOn);
            move(diffuse, to.diffuse);
            move(comb, to.comb);
            move(bands, to.bands);
            move(mono, to.mono);
            move(mix, to.mix);
            move(gain, to.gain);
            time += g * (to.time - time);
            bass += g * (to.bass - bass);
            high += g * (to.high - high);
        }
    };

    Targets targetsOf(const Settings& s) const
    {
        Targets t;
        t.width = std::clamp(s.width, 0.0f, 2.0f);
        t.amount = std::clamp(s.amount, 0.0f, 1.0f);
        t.time = (float) (std::clamp(s.time, 0.001f, kMaxTime) * rate);
        t.motion = std::clamp(s.motion, 0.0f, 1.0f);
        t.bass = std::clamp(s.bassMono, 20.0f, 1000.0f);
        t.bassOn = s.bassMono > kBassOff ? 1.0f : 0.0f;
        t.high = std::clamp(s.highCut, 500.0f, 20000.0f);
        const int mode = std::clamp(s.mode, 0, 2);
        t.diffuse = mode == diffuse ? 1.0f : 0.0f;
        t.comb = mode == comb ? 1.0f : 0.0f;
        t.bands = mode == bands ? 1.0f : 0.0f;
        t.mono = s.monoCheck ? 1.0f : 0.0f;
        t.mix = std::clamp(s.mix, 0.0f, 1.0f);
        t.gain = std::pow(10.0f, std::clamp(s.outputDb, -60.0f, 24.0f) / 20.0f);
        return t;
    }

    float wander() const { return now.motion * lfo * (float) (kMotionSeconds * rate); }

    float madeComb(float mid)
    {
        const float out = combLine.read(now.time + wander());
        combLine.push(mid);
        return out;
    }

    static int allpassLength(float timeSamples, std::size_t k)
    {
        return std::max(2, (int) std::lround(timeSamples * kAllpassShares[k]));
    }

    float madeDiffuse(float mid)
    {
        // The lead's wander runs the other way from the comb's, so a mode change does not line them up.
        float x = diffuseLine.read(now.time * kDiffuseLead - 0.7f * wander());
        diffuseLine.push(mid);
        for (std::size_t k = 0; k < allpasses.size(); ++k)
            x = allpasses[k].process(x, allpassLength(now.time, k), kAllpassGain);
        return x;
    }

    /**
     * A cascade of second-order allpasses, one per band: each turns the phase by a whole turn around its centre, so
     * the product is +1 at every centre and -1 between them (the band count is odd), flat in level everywhere.
     */
    float madeBands(float mid)
    {
        float x = mid;
        for (int k = 0; k < kBands; ++k)
        {
            const auto out = bandFilters[(std::size_t) k].process(x, bandG[(std::size_t) k], 1.0f / kBandQ);
            x = 2.0f / kBandQ * out.band - x;
        }
        return x;
    }

    void updateBands(const Targets& t, float lfoValue)
    {
        // The wander moves the band pattern by up to a third of an octave.
        const double shift = std::exp2(t.motion * lfoValue / 3.0);
        for (int k = 0; k < kBands; ++k)
        {
            const double f = std::min(kFirstBand * std::exp2(k) * shift, 0.45 * rate);
            bandG[(std::size_t) k] = (float) std::tan(3.141592653589793 * f / rate);
        }
    }

    void updateBass(float hz) { bassG = (float) std::tan(3.141592653589793 * std::min((double) hz, 0.45 * rate) / rate); }

    double rate = 48000.0;
    float smoothing = 0.001f, glide = 0.0005f;
    Targets now;
    bool primed = false;
    unsigned counter = 0;
    double phase = 0.0;
    float lfo = 0.0f;
    DelayLine combLine, diffuseLine;
    std::array<Allpass, 4> allpasses;
    std::array<SvfStage, kBands> bandFilters;
    std::array<float, kBands> bandG {};
    std::array<SvfStage, 2> bassStages;
    float bassG = 0.0f;
    float highState = 0.0f, highG = 1.0f, highOn = 0.0f, highFade = 0.0f;
};
} // namespace tonwerkspread
