#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tonwerkreverb
{
inline constexpr double kPi = 3.141592653589793;
/** The longest predelay, so a synced quarter at 60 BPM still fits. */
inline constexpr double kMaxPredelay = 1.0;
/** Where the two-band decay splits the loop: below it `bassTime` stretches the decay. */
inline constexpr double kCrossover = 250.0;

/**
 * The spaces, in the menu's order (saved in projects: append only). All share one network; a space sets how long its
 * lines are against the hall's (`scale`), how strongly the loop's allpasses smear each pass (`loopDiffusion`), how far
 * its early reflections reach (`earlySpan`, seconds at size 1) and how loud they are against the knob (`earlyLevel`).
 */
struct Space
{
    const char* name;
    double scale;
    double loopDiffusion;
    double inputDiffusion;
    double earlySpan;
    float earlyLevel;
};
inline constexpr std::array<Space, 4> kSpaces { {
    // A small room: short lines and close, strong reflections.
    { "Raum", 0.32, 0.65, 0.7, 0.030, 1.0f },
    // The concert hall: the reflections spread over 80 ms, then a smooth tail.
    { "Saal", 1.0, 0.5, 0.7, 0.080, 0.8f },
    // A plate has no walls: no reflections, a dense, bright onset from strong diffusion everywhere.
    { "Platte", 0.45, 1.0, 1.0, 0.0, 0.0f },
    // Stone and height: long lines, sparse and late reflections.
    { "Kathedrale", 2.0, 0.4, 0.6, 0.140, 0.6f },
} };

struct Division
{
    const char* name;
    double beats;
};
/** The note values the predelay's sync offers, in the menu's order (saved in projects: append only). */
inline constexpr std::array<Division, 9> kDivisions { {
    { "1/4", 1.0 }, { "1/8.", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3 }, { "1/16.", 0.375 }, { "1/16", 0.25 },
    { "1/16T", 1.0 / 6 }, { "1/32", 0.125 }, { "1/64", 0.0625 },
} };
inline constexpr int kSixteenth = 5;

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    int space = 1;
    float size = 0.6f;
    float decay = 2.5f;          // RT60 in seconds, of the middle and highs
    float predelay = 0.02f;
    bool sync = false;           // the predelay as a note value of the tempo
    int division = kSixteenth;
    float bassTime = 1.0f;       // the decay below kCrossover against `decay`
    float damping = 9000.0f;     // Hz: the loop's high cut, so the highs die sooner (off at 20 kHz)
    float diffusion = 0.8f;      // how quickly the echoes thicken into a smooth tail
    float early = 0.5f;          // the early reflections' level
    float modulation = 0.3f;     // the lines' slow wander, against metallic ringing
    float lowCut = 80.0f;        // Hz, on the wet (off at 20 Hz)
    float highCut = 16000.0f;    // Hz, on the wet (off at 20 kHz)
    float duck = 0.0f;           // how far the wet drops while the input plays
    float duckRelease = 0.3f;
    bool freeze = false;         // the tail circulates without end, nothing new goes in
    float width = 1.0f;
    float mix = 0.3f;
    float outputDb = 0.0f;
};

inline double predelaySeconds(const Settings& s, double bpm)
{
    if (! s.sync)
        return std::clamp((double) s.predelay, 0.0, kMaxPredelay);
    const auto& d = kDivisions[(std::size_t) std::clamp(s.division, 0, (int) kDivisions.size() - 1)];
    return std::clamp(d.beats * 60.0 / std::max(1.0, bpm), 0.0, kMaxPredelay);
}

inline const Space& spaceOf(int index) { return kSpaces[(std::size_t) std::clamp(index, 0, (int) kSpaces.size() - 1)]; }

/** The lines' scale at a size: 0.4 to 1.6 of the space's. */
inline double sizeScale(float size) { return 0.4 + 1.2 * std::clamp((double) size, 0.0, 1.0); }

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

/** A Schroeder allpass of a delay that may change per block: smears without colouring, never adds or takes energy. */
struct Allpass
{
    Line line;
    int delay = 1;
    float process(float x, float gain)
    {
        const float delayed = line.tap(delay);
        const float v = x + gain * delayed;
        line.write(v);
        return delayed - gain * v;
    }
};

/**
 * An algorithmic reverb: predelay, early reflections (a tapped line per side) and a late tail from an eight-line
 * feedback delay network (fast Hadamard mixing; an allpass inside each line, so the echo density grows on every
 * pass, as in a plate). Each line's decay is two-band: a one-pole split at kCrossover gives the lows their own gain
 * from `decay` × `bassTime`, so a hall can ring longer in the bass than in the middle, and `damping` takes the highs
 * sooner. Four lines wander by up to 1 ms (`modulation`). The wet passes a 12 dB low cut and high cut, then ducking
 * (Tonwerk Delay's follower: full depth from -12 dBFS, none under -42). `freeze` sets the loop to unity, without
 * damping or two bands (the allpasses stay, being lossless), and fades the input out over 50 ms. A change of space fades the wet out over 10 ms, clears the lines and
 * fades it back, since all the lines' lengths jump. Mix law as Tonwerk Delay's: the dry whole up to 50 %, the wet
 * whole from 50 %. The tail's level is scaled by the fourth root of (1 - g²), half way to keeping a held chord's
 * loudness whatever the decay.
 */
class Engine
{
public:
    static constexpr int kLines = 8;
    static constexpr int kTaps = 10;

    void prepare(double rate)
    {
        sampleRate = rate;
        double longest = 0.0;
        for (const auto& space : kSpaces)
            longest = std::max(longest, space.scale);
        const double most = longest * sizeScale(1.0f);
        for (int i = 0; i < kLines; ++i)
        {
            lines[(std::size_t) i].prepare((int) (kLengths[(std::size_t) i] * most * rate + 0.002 * rate) + 8);
            loopAllpass[(std::size_t) i].line.prepare((int) (kLengths[(std::size_t) i] * kLoopShare * most * rate) + 8);
        }
        for (auto& p : predelay)
            p.prepare((int) (kMaxPredelay * rate) + 8);
        double furthest = 0.0;
        for (const auto& space : kSpaces)
            furthest = std::max(furthest, space.earlySpan);
        for (auto& e : earlyLine)
            e.prepare((int) (furthest * sizeScale(1.0f) * rate) + 8);
        for (std::size_t c = 0; c < 2; ++c)
            for (std::size_t k = 0; k < 4; ++k)
                diffusers[c][k].line.prepare((int) (kDiffusion[k] * 1.07 * std::sqrt(longest) * rate) + 8);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            l.clear();
        for (auto& a : loopAllpass)
            a.line.clear();
        for (auto& p : predelay)
            p.clear();
        for (auto& e : earlyLine)
            e.clear();
        for (auto& side : diffusers)
            for (auto& a : side)
                a.line.clear();
        for (auto& f : split)
            f = {};
        for (auto& f : damp)
            f = {};
        for (auto& f : wetFilters)
            f.reset();
        for (std::size_t k = 0; k < 4; ++k)
            wander[k] = { std::cos(0.5 * kPi * (double) k), std::sin(0.5 * kPi * (double) k) };
        freezeMix = 0.0f;
        smoothedScale = -1.0f;
        playing = -1;
        spaceFade = 1.0f;
        follower = 0.0f;
        duckGain = 1.0f;
        shownLevel = 0.0f;
        shownDuck = 1.0f;
    }

    /** In place, stereo; `bpm` sets a synced predelay. */
    void process(float* left, float* right, int count, const Settings& s, double bpm = 120.0)
    {
        if (playing < 0)
            setSpace(std::clamp(s.space, 0, (int) kSpaces.size() - 1));
        const int wanted = std::clamp(s.space, 0, (int) kSpaces.size() - 1);
        const Space& space = kSpaces[(std::size_t) playing];
        const double scale = space.scale * sizeScale(s.size);
        if (smoothedScale < 0.0f)
            smoothedScale = (float) scale;

        // The decay's gains per line, for the lows and for the rest, from the loop's time (line plus its allpass).
        std::array<float, kLines> high {}, low {};
        double meanSquare = 0.0;
        for (int i = 0; i < kLines; ++i)
        {
            const double seconds = kLengths[(std::size_t) i] * (1.0 + kLoopShare) * scale;
            const double decay = std::max(0.1, (double) s.decay);
            const double h = s.freeze ? 1.0 : std::pow(10.0, -3.0 * seconds / decay);
            const double l = s.freeze ? 1.0
                                      : std::pow(10.0, -3.0 * seconds / (decay * std::clamp((double) s.bassTime, 0.25, 4.0)));
            high[(std::size_t) i] = (float) h;
            low[(std::size_t) i] = (float) std::min(l, 0.9995);
            if (s.freeze)
                low[(std::size_t) i] = 1.0f;
            meanSquare += h * h / kLines;
        }
        const auto tailScale = (float) std::pow(std::max(1.0e-4, 1.0 - meanSquare), 0.25);
        // At its end the damping is off: even a one-pole at 20 kHz takes a little off each pass, and a long tail
        // makes hundreds of passes.
        const bool damped = ! s.freeze && s.damping < 19990.0f;
        const bool twoBands = ! s.freeze && std::abs(s.bassTime - 1.0f) > 1.0e-3f;
        for (auto& f : damp)
            f.set(s.damping, sampleRate);
        for (auto& f : split)
            f.set(kCrossover, sampleRate);
        const bool lowCutting = s.lowCut > 20.5f, highCutting = s.highCut < 19990.0f;
        const float lowG = (float) std::tan(kPi * std::min((double) s.lowCut, 0.45 * sampleRate) / sampleRate);
        const float highG = (float) std::tan(kPi * std::min((double) s.highCut, 0.45 * sampleRate) / sampleRate);
        constexpr float kButterworth = 1.41421356f;

        const float diffusion = std::clamp(s.diffusion, 0.0f, 1.0f);
        const auto inputGain = (float) (0.75 * diffusion * space.inputDiffusion);
        const auto loopGain = (float) (0.6 * diffusion * space.loopDiffusion);
        const double diffuserScale = std::sqrt(space.scale * sizeScale(s.size));
        for (std::size_t c = 0; c < 2; ++c)
            for (std::size_t k = 0; k < 4; ++k)
                diffusers[c][k].delay = std::clamp((int) (kDiffusion[k] * (c == 0 ? 1.0 : 1.07) * diffuserScale * sampleRate),
                                                   1, diffusers[c][k].line.mask - 4);

        // The early reflections: taps spread over the space's span, falling and alternating in sign, the right side
        // on its own times. Normalised so each side's taps sum to one in power.
        const double span = space.earlySpan * sizeScale(s.size) * sampleRate;
        const float early = std::clamp(s.early, 0.0f, 1.0f) * space.earlyLevel * kEarlyLevel;
        std::array<std::array<int, kTaps>, 2> tapAt {};
        std::array<float, kTaps> tapGain {};
        double power = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double fall = std::pow(1.0 - (double) k / kTaps, 1.5);
            tapGain[(std::size_t) k] = (float) ((k % 3 == 1 ? -1.0 : 1.0) * fall);
            power += fall * fall;
            for (std::size_t c = 0; c < 2; ++c)
                tapAt[c][(std::size_t) k] = std::clamp((int) (span * kEarlyTimes[c][(std::size_t) k]), 1,
                                                       earlyLine[c].mask - 4);
        }
        for (auto& g : tapGain)
            g *= early / (float) std::sqrt(power);
        const bool withEarly = early > 0.0f && span >= 1.0;

        const int predelaySamples = std::clamp((int) std::lround(predelaySeconds(s, bpm) * sampleRate), 0,
                                               predelay[0].mask - 4);
        const double modDepth = std::clamp((double) s.modulation, 0.0, 1.0) * 0.001 * sampleRate;
        // The four wanders turn as phasors (a rotation per sample, renormalised each block): no sine calls.
        for (std::size_t k = 0; k < 4; ++k)
        {
            const double w = 2.0 * kPi * 0.5 * kModRates[k] / sampleRate;
            rotation[k] = { std::cos(w), std::sin(w) };
            const double norm = std::hypot(wander[k][0], wander[k][1]);
            wander[k] = { wander[k][0] / norm, wander[k][1] / norm };
        }
        const float freezeStep = (float) (1.0 / (0.05 * sampleRate));
        const float spaceStep = (float) (1.0 / (0.01 * sampleRate));
        const float sizeGlide = (float) (1.0 - std::exp(-1.0 / (0.1 * sampleRate)));
        const float attack = (float) std::exp(-1.0 / (0.005 * sampleRate));
        const float release = (float) std::exp(-1.0 / (std::max(0.01, (double) s.duckRelease) * sampleRate));
        const float duckSmooth = (float) std::exp(-1.0 / (0.01 * sampleRate));
        const float duck = std::clamp(s.duck, 0.0f, 1.0f);
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float pan = std::clamp(s.width, 0.0f, 1.0f);
        const float dryGain = std::min(1.0f, 2.0f * (1.0f - s.mix));
        const float wetGain = std::min(1.0f, 2.0f * s.mix);
        const float tailGain = tailScale * kTailLevel;
        float energy = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const float inL = left[i], inR = right[i];
            freezeMix = s.freeze ? std::min(1.0f, freezeMix + freezeStep) : std::max(0.0f, freezeMix - freezeStep);
            const float send = 1.0f - freezeMix;
            predelay[0].write(inL * send);
            predelay[1].write(inR * send);
            const float preL = predelaySamples > 0 ? predelay[0].tap(predelaySamples) : inL * send;
            const float preR = predelaySamples > 0 ? predelay[1].tap(predelaySamples) : inR * send;

            float earlyL = 0.0f, earlyR = 0.0f;
            if (withEarly)
            {
                earlyLine[0].write(preL);
                earlyLine[1].write(preR);
                for (int k = 0; k < kTaps; ++k)
                {
                    earlyL += tapGain[(std::size_t) k] * earlyLine[0].tap(tapAt[0][(std::size_t) k]);
                    earlyR += tapGain[(std::size_t) k] * earlyLine[1].tap(tapAt[1][(std::size_t) k]);
                }
            }

            float feedL = preL, feedR = preR;
            for (auto& a : diffusers[0])
                feedL = a.process(feedL, inputGain);
            for (auto& a : diffusers[1])
                feedR = a.process(feedR, inputGain);

            // A size change glides, so the lines' taps move smoothly (a pitch bend) rather than click.
            smoothedScale += ((float) scale - smoothedScale) * sizeGlide;
            std::array<float, kLines> out {};
            for (int k = 0; k < kLines; ++k)
            {
                const double length = kLengths[(std::size_t) k] * smoothedScale * sampleRate;
                if (k >= 4 && modDepth > 0.0)
                {
                    auto& p = wander[(std::size_t) (k - 4)];
                    const auto& r = rotation[(std::size_t) (k - 4)];
                    p = { p[0] * r[0] - p[1] * r[1], p[0] * r[1] + p[1] * r[0] };
                    out[(std::size_t) k] = lines[(std::size_t) k].tap(length + modDepth * (1.0 + p[1]));
                }
                else
                    out[(std::size_t) k] = lines[(std::size_t) k].tap((int) length);
            }

            // Out: the even lines to the left, the odd to the right, with alternating signs for a wide image.
            const float tailL = out[0] - out[2] + out[4] - out[6];
            const float tailR = out[1] - out[3] + out[5] - out[7];

            // Each line: its allpass, the two-band decay, the damping; then the Hadamard mix.
            const float loopDelayScale = smoothedScale * (float) (kLoopShare * sampleRate);
            for (int k = 0; k < kLines; ++k)
            {
                auto& a = loopAllpass[(std::size_t) k];
                a.delay = std::clamp((int) (kLengths[(std::size_t) k] * loopDelayScale), 1, a.line.mask - 4);
                float x = a.process(out[(std::size_t) k], loopGain);
                if (twoBands)
                {
                    const float lows = split[(std::size_t) k].low(x);
                    x = high[(std::size_t) k] * x + (low[(std::size_t) k] - high[(std::size_t) k]) * lows;
                }
                else
                    x *= high[(std::size_t) k];
                if (damped)
                    x = damp[(std::size_t) k].low(x);
                out[(std::size_t) k] = x;
            }
            hadamard(out);
            for (int k = 0; k < kLines; ++k)
                lines[(std::size_t) k].write(out[(std::size_t) k] + (k % 2 == 0 ? feedL : feedR) * kInputGain);

            float wetL = tailGain * tailL + earlyL, wetR = tailGain * tailR + earlyR;
            if (lowCutting)
            {
                wetL = wetFilters[0].process(wetL, lowG, kButterworth).high;
                wetR = wetFilters[1].process(wetR, lowG, kButterworth).high;
            }
            if (highCutting)
            {
                wetL = wetFilters[2].process(wetL, highG, kButterworth).low;
                wetR = wetFilters[3].process(wetR, highG, kButterworth).low;
            }

            if (duck > 0.0f)
            {
                const float level = std::max(std::abs(inL), std::abs(inR));
                follower = level + (follower - level) * (level > follower ? attack : release);
                const float db = 20.0f * std::log10(follower + 1.0e-9f);
                const float amount = std::clamp((db + 42.0f) / 30.0f, 0.0f, 1.0f);
                const float wantGain = 1.0f - duck * amount;
                duckGain = wantGain + (duckGain - wantGain) * duckSmooth;
            }
            else
                duckGain = 1.0f + (duckGain - 1.0f) * duckSmooth;

            // A new space: out over 10 ms, the lines cleared and set, back in.
            if (wanted != playing)
            {
                spaceFade -= spaceStep;
                if (spaceFade <= 0.0f)
                {
                    spaceFade = 0.0f;
                    setSpace(wanted);
                    return finish(left, right, i, count, s, bpm, energy);
                }
            }
            else
                spaceFade = std::min(1.0f, spaceFade + spaceStep);

            const float gain = duckGain * spaceFade;
            wetL *= gain;
            wetR *= gain;
            energy += wetL * wetL + wetR * wetR;
            const float mid = 0.5f * (wetL + wetR);
            const float l = mid + pan * (wetL - mid);
            const float r = mid + pan * (wetR - mid);
            left[i] = output * (dryGain * inL + wetGain * l);
            right[i] = output * (dryGain * inR + wetGain * r);
        }
        shownLevel = std::sqrt(energy / (float) std::max(1, 2 * count));
        shownDuck = duckGain;
    }

    /** The wet's RMS in the last block, for the display. */
    float levelNow() const { return shownLevel; }
    /** The wet's gain from ducking, 1 when none, for the display. */
    float duckGainNow() const { return shownDuck; }
    /** The early reflections' times as shares of the space's span, left side, for the display. */
    static const std::array<double, kTaps>& earlyTimes() { return kEarlyTimes[0]; }

private:
    /** The lines' lengths in seconds at scale 1: mutually prime in samples at 48 kHz, spread over 30 to 75 ms. */
    static constexpr std::array<double, kLines> kLengths { 0.02971, 0.03713, 0.04111, 0.04373,
                                                           0.05331, 0.05993, 0.06771, 0.07309 };
    /** Each line's allpass against the line's length. */
    static constexpr double kLoopShare = 0.15;
    static constexpr std::array<double, 4> kModRates { 1.0, 1.31, 0.73, 1.57 };
    static constexpr std::array<double, 4> kDiffusion { 0.0048, 0.0036, 0.0127, 0.0093 };
    /** Where the early reflections fall in their span, per side: uneven, so they do not buzz at one pitch. */
    static constexpr std::array<std::array<double, kTaps>, 2> kEarlyTimes { {
        { 0.043, 0.127, 0.198, 0.281, 0.352, 0.447, 0.561, 0.668, 0.802, 1.0 },
        { 0.067, 0.112, 0.231, 0.305, 0.398, 0.476, 0.593, 0.712, 0.851, 0.97 },
    } };
    /** Each side feeds four lines; a half keeps a full-scale input from crowding the loop. */
    static constexpr float kInputGain = 0.5f;
    /** Four lines summed per side; this brings a short tail to about the dry's loudness at mix 50 %. */
    static constexpr float kTailLevel = 0.6f;
    static constexpr float kEarlyLevel = 0.7f;

    void setSpace(int index)
    {
        for (auto& l : lines)
            l.clear();
        for (auto& a : loopAllpass)
            a.line.clear();
        for (auto& e : earlyLine)
            e.clear();
        for (auto& side : diffusers)
            for (auto& a : side)
                a.line.clear();
        for (auto& f : wetFilters)
            f.reset();
        playing = index;
        smoothedScale = -1.0f;
    }

    /** After a change of space: the rest of the block with the new one, fading in from silence. */
    void finish(float* left, float* right, int at, int count, const Settings& s, double bpm, float energy)
    {
        const float dryGain = std::min(1.0f, 2.0f * (1.0f - s.mix));
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        // The sample where the old space ran out plays dry, the rest goes through the new one.
        left[at] *= output * dryGain;
        right[at] *= output * dryGain;
        if (at + 1 < count)
            process(left + at + 1, right + at + 1, count - at - 1, s, bpm);
        shownLevel = std::max(shownLevel, std::sqrt(energy / (float) std::max(1, 2 * count)));
    }

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
    std::array<Allpass, kLines> loopAllpass;
    std::array<Line, 2> predelay, earlyLine;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<OnePole, kLines> split {}, damp {};
    std::array<tonwerkwobble::SvfStage, 4> wetFilters {};
    std::array<std::array<double, 2>, 4> wander {}, rotation {};
    float freezeMix = 0.0f, smoothedScale = -1.0f, spaceFade = 1.0f;
    int playing = -1;
    float follower = 0.0f, duckGain = 1.0f, shownLevel = 0.0f, shownDuck = 1.0f;
};
} // namespace tonwerkreverb
