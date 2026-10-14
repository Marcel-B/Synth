#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tonwerkshimmer
{
inline constexpr double kPi = 3.141592653589793;

struct Interval
{
    const char* name;
    float semitones;
};
/** The shimmer's intervals, in the menu's order (saved in projects: append only). */
inline constexpr std::array<Interval, 6> kIntervals { {
    { "Oktave", 12.0f },
    { "Quinte", 7.0f },
    { "Quarte", 5.0f },
    { "Oktave + Quinte", 19.0f },
    { "Zwei Oktaven", 24.0f },
    { "Oktave tiefer", -12.0f },
} };

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    float size = 0.7f;
    float decay = 5.0f;          // RT60 in seconds
    float predelay = 0.02f;
    float damping = 7000.0f;     // Hz: the loop's high cut, so the highs die sooner (off at 20 kHz)
    float lowCut = 80.0f;        // Hz: the loop's low cut, so the tail does not boom (off at 20 Hz)
    float shimmer = 0.5f;        // the share of the loop that passes the pitch shifter
    int interval = 0;
    float modulation = 0.3f;     // the lines' slow wander, which keeps long tails from ringing metallic
    bool hold = false;           // the tail circulates without end, nothing new goes in
    float width = 1.0f;
    float mix = 0.3f;
    float outputDb = 0.0f;
};

/** A first-order TPT filter, low and high pass from one state. */
struct OnePole
{
    float g = 1.0f, state = 0.0f;
    void set(double frequency, double rate)
    {
        const double t = std::tan(kPi * std::min(frequency, 0.45 * rate) / rate);
        g = (float) (t / (1.0 + t));
    }
    float low(float x)
    {
        const float v = (x - state) * g;
        const float y = v + state;
        state = y + v;
        return y;
    }
    float high(float x) { return x - low(x); }
};

/** A ring buffer of a power of two, read whole or between samples (linear). */
struct Line
{
    std::vector<float> data;
    int mask = 0, at = 0;

    void prepare(int atLeast)
    {
        int length = 1;
        while (length < atLeast + 4)
            length <<= 1;
        data.assign((std::size_t) length, 0.0f);
        mask = length - 1;
        at = 0;
    }
    void clear() { std::fill(data.begin(), data.end(), 0.0f); }
    void write(float x)
    {
        data[(std::size_t) at] = x;
        at = (at + 1) & mask;
    }
    /** `delay` samples behind the last written (1 is the newest). */
    float tap(int delay) const { return data[(std::size_t) ((at - delay) & mask)]; }
    float tap(double delay) const
    {
        const double whole = std::floor(delay);
        const auto t = (float) (delay - whole);
        const int d = (int) whole;
        return tap(d) + t * (tap(d + 1) - tap(d));
    }
};

/** A Schroeder allpass: smears a click into a dense burst without colouring it. */
struct Allpass
{
    Line line;
    int delay = 1;
    float gain = 0.6f;
    float process(float x)
    {
        const float delayed = line.tap(delay);
        const float v = x + gain * delayed;
        line.write(v);
        return delayed - gain * v;
    }
};

/**
 * A pitch shifter of two taps sliding through a 60 ms window, each faded in and out by a triangle so the two always
 * sum to one: cheap, a little grainy, and never louder than its input, so it can sit inside a feedback loop.
 */
struct PitchShifter
{
    Line line;
    double window = 2880.0, phase = 0.0;

    void prepare(double rate)
    {
        window = 0.06 * rate;
        line.prepare((int) window + 4);
        phase = 0.0;
    }
    float process(float x, double ratio)
    {
        line.write(x);
        // The taps' delay shrinks by (ratio - 1) per sample when shifting up, grows when shifting down.
        phase -= (ratio - 1.0) / window;
        phase -= std::floor(phase);
        const double second = phase + 0.5 - std::floor(phase + 0.5);
        const auto fadeA = (float) (1.0 - std::abs(2.0 * phase - 1.0));
        const auto fadeB = (float) (1.0 - std::abs(2.0 * second - 1.0));
        return fadeA * line.tap(1.0 + phase * window) + fadeB * line.tap(1.0 + second * window);
    }
};

/**
 * A shimmer reverb: an eight-line feedback delay network (Hadamard mixing, so every line feeds every other at equal
 * strength; each line's gain set from its length so the tail falls by 60 dB in `decay`; a low cut and a high cut in
 * each line) after a predelay and four allpasses per side. The shimmer sits in the loop: two of the eight lines pass
 * a pitch shifter by `shimmer`, so each round trip lifts a share of the tail by the interval, and the lifted part is
 * lifted again on the next, rising like a choir. Inside the loop it can never grow the tail (the shifter is at most
 * unity), unlike a shifter feeding the input. Four lines wander slowly by up to 1 ms (`modulation`). `hold` sets the
 * loop to unity, without filters or shimmer, and fades the input out over 50 ms. The wet level is scaled by the fourth
 * root of (1 - g²), half way to keeping a held chord's level whatever the decay.
 */
class Engine
{
public:
    static constexpr int kLines = 8;

    void prepare(double rate)
    {
        sampleRate = rate;
        for (int i = 0; i < kLines; ++i)
            lines[(std::size_t) i].prepare((int) (kLengths[(std::size_t) i] * kMaxScale * rate + 0.002 * rate) + 8);
        for (auto& p : predelay)
            p.prepare((int) (0.5 * rate) + 8);
        for (std::size_t c = 0; c < 2; ++c)
            for (std::size_t k = 0; k < 4; ++k)
            {
                auto& a = diffusers[c][k];
                a.delay = std::max(1, (int) (kDiffusion[k] * (c == 0 ? 1.0 : 1.07) * rate));
                a.line.prepare(a.delay + 4);
            }
        for (auto& s : shifters)
            s.prepare(rate);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            l.clear();
        for (auto& p : predelay)
            p.clear();
        for (auto& side : diffusers)
            for (auto& a : side)
                a.line.clear();
        for (auto& s : shifters)
        {
            s.line.clear();
            s.phase = 0.0;
        }
        for (auto& f : damp)
            f = {};
        for (auto& f : cut)
            f = {};
        for (std::size_t k = 0; k < 4; ++k)
            wander[k] = { std::cos(0.5 * kPi * (double) k), std::sin(0.5 * kPi * (double) k) };
        holdMix = 0.0f;
        smoothedScale = -1.0f;
        shownLevel = 0.0f;
    }

    /** In place, stereo. */
    void process(float* left, float* right, int count, const Settings& s)
    {
        const double scale = kMinScale + (kMaxScale - kMinScale) * std::clamp((double) s.size, 0.0, 1.0);
        if (smoothedScale < 0.0f)
            smoothedScale = (float) scale;
        std::array<float, kLines> gain {};
        double meanSquare = 0.0;
        for (int i = 0; i < kLines; ++i)
        {
            const double seconds = kLengths[(std::size_t) i] * scale;
            const double g = s.hold ? 1.0 : std::pow(10.0, -3.0 * seconds / std::max(0.1, (double) s.decay));
            gain[(std::size_t) i] = (float) g;
            meanSquare += g * g / kLines;
        }
        const auto wetScale = (float) std::pow(std::max(1.0e-4, 1.0 - meanSquare), 0.25);
        // At their ends the filters are off: even a one-pole at 20 kHz takes a little off each pass, and a long tail
        // makes hundreds of passes.
        const bool damped = ! s.hold && s.damping < 19990.0f, cutting = ! s.hold && s.lowCut > 20.5f;
        for (auto& f : damp)
            f.set(s.damping, sampleRate);
        for (auto& f : cut)
            f.set(s.lowCut, sampleRate);
        const double ratio = std::exp2(kIntervals[(std::size_t) std::clamp(s.interval, 0, (int) kIntervals.size() - 1)]
                                           .semitones / 12.0);
        const float shimmer = s.hold ? 0.0f : std::clamp(s.shimmer, 0.0f, 1.0f);
        const int predelaySamples = std::clamp((int) std::lround(s.predelay * sampleRate), 0,
                                               predelay[0].mask - 4);
        const double modDepth = std::clamp((double) s.modulation, 0.0, 1.0) * 0.001 * sampleRate;
        // The four wanders turn as phasors (a rotation per sample, renormalised each block): no sine calls.
        for (std::size_t k = 0; k < 4; ++k)
        {
            const double w = 2.0 * kPi * 0.43 * kModRates[k] / sampleRate;
            rotation[k] = { std::cos(w), std::sin(w) };
            const double norm = std::hypot(wander[k][0], wander[k][1]);
            wander[k] = { wander[k][0] / norm, wander[k][1] / norm };
        }
        const float holdStep = (float) (1.0 / (0.05 * sampleRate));
        const float sizeGlide = (float) (1.0 - std::exp(-1.0 / (0.1 * sampleRate)));
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float pan = std::clamp(s.width, 0.0f, 1.0f);
        const float dryGain = std::min(1.0f, 2.0f * (1.0f - s.mix));
        const float wetGain = std::min(1.0f, 2.0f * s.mix) * wetScale * kWetLevel;
        float energy = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const float inL = left[i], inR = right[i];
            holdMix = s.hold ? std::min(1.0f, holdMix + holdStep) : std::max(0.0f, holdMix - holdStep);
            const float send = 1.0f - holdMix;
            predelay[0].write(inL * send);
            predelay[1].write(inR * send);
            float feedL = predelaySamples > 0 ? predelay[0].tap(predelaySamples) : inL * send;
            float feedR = predelaySamples > 0 ? predelay[1].tap(predelaySamples) : inR * send;
            for (auto& a : diffusers[0])
                feedL = a.process(feedL);
            for (auto& a : diffusers[1])
                feedR = a.process(feedR);

            // A size change glides, so the lines' taps move smoothly (a pitch bend) rather than click.
            smoothedScale += ((float) scale - smoothedScale) * sizeGlide;
            std::array<float, kLines> out {};
            for (int k = 0; k < kLines; ++k)
            {
                const double length = kLengths[(std::size_t) k] * smoothedScale * sampleRate;
                if (k >= 4 && modDepth > 0.0)
                {
                    // Four lines wander, each at its own speed and phase.
                    auto& p = wander[(std::size_t) (k - 4)];
                    const auto& r = rotation[(std::size_t) (k - 4)];
                    p = { p[0] * r[0] - p[1] * r[1], p[0] * r[1] + p[1] * r[0] };
                    out[(std::size_t) k] = lines[(std::size_t) k].tap(length + modDepth * (1.0 + p[1]));
                }
                else
                    out[(std::size_t) k] = lines[(std::size_t) k].tap((int) length);
            }

            // Out: the even lines to the left, the odd to the right, with alternating signs for a wide image.
            const float wetL = out[0] - out[2] + out[4] - out[6];
            const float wetR = out[1] - out[3] + out[5] - out[7];
            energy += wetL * wetL + wetR * wetR;

            // Damping and the low cut, then the shimmer on two lines, then the gains, then the Hadamard mix.
            for (int k = 0; k < kLines; ++k)
            {
                float x = out[(std::size_t) k];
                if (damped)
                    x = damp[(std::size_t) k].low(x);
                if (cutting)
                    x = cut[(std::size_t) k].high(x);
                if (k < 2 && shimmer > 0.0f)
                    x += shimmer * (shifters[(std::size_t) k].process(x, ratio) - x);
                out[(std::size_t) k] = x * gain[(std::size_t) k];
            }
            hadamard(out);
            for (int k = 0; k < kLines; ++k)
                lines[(std::size_t) k].write(out[(std::size_t) k] + (k % 2 == 0 ? feedL : feedR) * kInputGain);

            const float mid = 0.5f * (wetL + wetR);
            const float l = mid + pan * (wetL - mid);
            const float r = mid + pan * (wetR - mid);
            left[i] = output * (dryGain * inL + wetGain * l);
            right[i] = output * (dryGain * inR + wetGain * r);
        }
        shownLevel = std::sqrt(energy / (float) std::max(1, 2 * count)) * wetScale * kWetLevel;
    }

    /** The tail's RMS in the last block, for the display. */
    float levelNow() const { return shownLevel; }

private:
    /** The lines' lengths in seconds at size 1: mutually prime in samples at 48 kHz, spread over 30 to 75 ms. */
    static constexpr std::array<double, kLines> kLengths { 0.02971, 0.03713, 0.04111, 0.04373,
                                                           0.05331, 0.05993, 0.06771, 0.07309 };
    static constexpr std::array<double, 4> kModRates { 1.0, 1.31, 0.73, 1.57 };
    static constexpr std::array<double, 4> kDiffusion { 0.0048, 0.0036, 0.0127, 0.0093 };
    static constexpr double kMinScale = 0.35, kMaxScale = 1.6;
    /** Each side feeds four lines; a half keeps a full-scale input from crowding the loop. */
    static constexpr float kInputGain = 0.5f;
    /** Four lines summed per side; this brings a short tail to about the dry's loudness at mix 50 %. */
    static constexpr float kWetLevel = 0.6f;

    /** The 8 by 8 Hadamard matrix, scaled to keep the energy: three rounds of sums and differences. */
    static void hadamard(std::array<float, kLines>& x)
    {
        for (int width = 1; width < kLines; width <<= 1)
            for (int i = 0; i < kLines; i += 2 * width)
                for (int j = i; j < i + width; ++j)
                {
                    const float a = x[(std::size_t) j], b = x[(std::size_t) (j + width)];
                    x[(std::size_t) j] = a + b;
                    x[(std::size_t) (j + width)] = a - b;
                }
        for (auto& v : x)
            v *= 0.35355339f;
    }

    double sampleRate = 48000.0;
    std::array<Line, kLines> lines;
    std::array<Line, 2> predelay;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<PitchShifter, 2> shifters;
    std::array<OnePole, kLines> damp {}, cut {};
    std::array<std::array<double, 2>, 4> wander {}, rotation {};
    float holdMix = 0.0f, smoothedScale = -1.0f, shownLevel = 0.0f;
};
} // namespace tonwerkshimmer
