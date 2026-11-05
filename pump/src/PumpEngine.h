#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace tonwerkpump
{
inline constexpr double kPi = 3.141592653589793;

struct Division
{
    const char* name;
    double beats;
};
/** The tempo mode's note values, in the menu's order (saved in projects: append only). */
inline constexpr std::array<Division, 9> kDivisions { {
    { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4.", 1.5 }, { "1/4", 1.0 }, { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 },
    { "1/8", 0.5 }, { "1/8T", 1.0 / 3 }, { "1/16", 0.25 },
} };
inline constexpr int kQuarter = 3;

/**
 * What starts a duck, in the menu's order (saved in projects: append only). `trigger`: each hit on the key above the
 * threshold plays the drawn curve; `compressor`: a sidechain compressor, the reduction following the key's level;
 * `tempo`: the curve on every step of the host's tempo, no key needed.
 */
enum class Mode { trigger, compressor, tempo };
/** Where the detector listens (menu order, append only): the side chain bus, or the signal itself. */
enum class Source { sidechain, input };
/** Which part of the signal ducks (menu order, append only): all of it, only below `split`, only above it. */
enum class Band { full, low, high };

/** The fastest a duck may fall, in seconds, so even attack 0 does not click. */
inline constexpr float kShortestFall = 0.0003f;
/** A hit re-arms once the key has fallen this far under the threshold, and no sooner than `kRearmSeconds`. */
inline constexpr float kHysteresisDb = 4.0f;
inline constexpr float kRearmSeconds = 0.03f;

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    Mode mode = Mode::trigger;
    Source source = Source::sidechain;
    float thresholdDb = -30.0f;
    float ratio = 8.0f;
    float depthDb = 18.0f;    // the deepest the duck goes, in every mode
    float attack = 0.001f;
    float hold = 0.0f;
    float release = 0.2f;
    float shape = 0.3f;       // -1 comes back early and lingers, 0 a straight line in dB, 1 holds low and rises late
    int division = kQuarter;
    float dynamics = 0.0f;    // trigger: how much a softer hit ducks less (0 every hit the full depth)
    float keyLowpass = 20000.0f; // Hz; the detector hears only what lies below (20 kHz is off)
    Band band = Band::full;
    float split = 120.0f;     // Hz, between the low and the high band
    float mix = 1.0f;
    float outputDb = 0.0f;
    bool listen = false;      // play the filtered key instead, to set the filter by ear
};

/** The curve's exponent for the way back: 4^shape, from a quarter (back early) to four (holds low, rises late). */
inline double exponentFor(float shape) { return std::pow(4.0, (double) std::clamp(shape, -1.0f, 1.0f)); }

/**
 * The duck's reduction in dB `t` seconds after it started: down from `fromDb` (where an earlier duck still was) to the
 * depth within the attack (a straight line in dB), held, then back to 0 over `release` along the shape.
 */
inline float duckAt(double t, float fromDb, float depthDb, float attack, float hold, float release, float shape)
{
    const double fall = std::max((double) attack, (double) kShortestFall);
    if (t < 0.0)
        return fromDb;
    if (t < fall)
        return fromDb + (depthDb - fromDb) * (float) (t / fall);
    t -= fall;
    if (t < hold)
        return depthDb;
    t -= hold;
    if (release <= 0.0f || t >= release)
        return 0.0f;
    return depthDb * (float) (1.0 - std::pow(t / release, exponentFor(shape)));
}

/** The reduction a compressor of `ratio` asks for at `levelDb`, with a 6 dB soft knee, at most `depthDb`. */
inline float reductionFor(float levelDb, float thresholdDb, float ratio, float depthDb)
{
    constexpr float knee = 6.0f;
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    float reduction = 0.0f;
    if (std::abs(over) <= 0.5f * knee)
    {
        const float into = over + 0.5f * knee;
        reduction = slope * into * into / (2.0f * knee);
    }
    else if (over > 0.0f)
        reduction = slope * over;
    return std::min(reduction, depthDb);
}

/** A TPT state variable filter (Cytomic's form), its low pass and high pass from the same state. */
struct Split
{
    float g = 0.0f, k = 1.41421356f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    float lastHigh = 0.0f;
    void set(double frequency, double rate, float q = 0.70710678f)
    {
        g = (float) std::tan(kPi * std::clamp(frequency, 10.0, 0.45 * rate) / rate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    /** The low pass of `x`; the high pass of the same sample is left in `lastHigh`. */
    float low(float x)
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        lastHigh = x - k * v1 - v2;
        return v2;
    }
    float high(float x)
    {
        low(x);
        return lastHigh;
    }
};

/**
 * A Linkwitz-Riley crossover (fourth order: two Butterworth stages each way). The bands sum to an allpass of the
 * input, so the level stays flat and either band can duck without the other leaking through as a plain complement
 * would (`x - low` keeps a band pass that is barely 6 dB down an octave and a half under the split).
 */
struct Crossover
{
    Split first, low2, high2;
    void set(double frequency, double rate)
    {
        first.set(frequency, rate);
        low2.set(frequency, rate);
        high2.set(frequency, rate);
    }
    void split(float x, float& low, float& high)
    {
        const float l1 = first.low(x);
        const float h1 = first.lastHigh;
        low = low2.low(l1);
        high = high2.high(h1);
    }
};

/**
 * A ducker made for pumping. The key (the side chain, or the signal itself) goes through a low pass, so a kick on a
 * drum bus speaks and the hats do not, and a peak follower (instant up, 10 ms down) reads its level on the louder side.
 * Trigger mode starts the curve whenever that level rises over the threshold and the detector is armed (the key fell
 * `kHysteresisDb` under the threshold since, and `kRearmSeconds` passed), so each kick plays the same shape however
 * long it rings; a new hit starts falling from where the last duck still is, so nothing jumps. `dynamics` lets a soft
 * hit duck less: its depth scales with how far the loudest point of its attack lay over the threshold (full at 12 dB).
 * Compressor mode is a sidechain compressor: the reduction follows the key's level by the ratio, at most the depth,
 * smoothed in dB with the attack and release, held at its deepest for the hold time. Tempo mode plays the curve on
 * each step, placed by the song position while the host plays and running on at the tempo otherwise; its way back is
 * cut short where a step would not fit it, so the gain is home before the next one.
 * One gain for both sides, so the image stays put; `band` ducks only the lows (the bass under the kick) or the highs
 * of a Linkwitz-Riley crossover, whose bands sum to an allpass (flat, so the level stays where it was).
 */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        reset();
    }

    void reset()
    {
        for (auto& f : keyFilter)
            f = {};
        for (auto& f : bands)
            f = {};
        follower = 0.0f;
        armed = true;
        sinceTrigger = 1.0e9;
        duckFrom = 0.0f;
        hitScale = 1.0f;
        attacking = false;
        reduction = 0.0f;
        held = 0.0;
        tempoPhase = 0.0;
        previousPhase = 1.0;
        current = 0.0f;
        shownReduction = 0.0f;
        shownLevel = -100.0f;
        hits = 0;
    }

    /**
     * In place, stereo; `keyLeft`/`keyRight` are the side chain (null when there is none). `bpm` sets the tempo mode's
     * step; `beat` is the song position in quarter notes at the block's start while the host plays, negative otherwise.
     */
    void process(float* left, float* right, const float* keyLeft, const float* keyRight, int count, const Settings& s,
                 double bpm, double beat = -1.0)
    {
        const auto& division = kDivisions[(std::size_t) std::clamp(s.division, 0, (int) kDivisions.size() - 1)];
        const double stepSeconds = division.beats * 60.0 / std::max(1.0, bpm);
        if (s.mode == Mode::tempo && beat >= 0.0)
        {
            const double phase = beat / division.beats;
            tempoPhase = phase - std::floor(phase);
        }
        const double tempoStep = 1.0 / (stepSeconds * sampleRate);
        // The tempo mode's way back ends before the next step, a millisecond to spare.
        const float tempoRelease = (float) std::max(0.0, std::min((double) s.release,
                                                                  stepSeconds - std::max(s.attack, kShortestFall) - s.hold - 0.001));

        const bool filtered = s.keyLowpass < 19000.0f;
        if (filtered)
            // A fourth-order Butterworth per side: two stages, Q 0.54 and 1.31.
            for (std::size_t k = 0; k < keyFilter.size(); ++k)
                keyFilter[k].set(s.keyLowpass, sampleRate, k < 2 ? 0.5412f : 1.3066f);
        if (s.band != Band::full)
            for (auto& f : bands)
                f.set(s.split, sampleRate);

        const float fall = (float) std::exp(-1.0 / (0.01 * sampleRate));
        const float attackCoefficient = coefficient(std::max(s.attack, kShortestFall));
        const float releaseCoefficient = coefficient(std::max(s.release, 0.005f));
        const double dt = 1.0 / sampleRate;
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f);
        const bool fromKey = s.source == Source::sidechain;
        const float threshold = std::pow(10.0f, s.thresholdDb / 20.0f);
        const float rearm = std::pow(10.0f, (s.thresholdDb - kHysteresisDb) / 20.0f);
        float levelPeak = 0.0f, deepest = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            float keyL, keyR;
            if (fromKey)
            {
                keyL = keyLeft != nullptr ? keyLeft[i] : 0.0f;
                keyR = keyRight != nullptr ? keyRight[i] : keyL;
            }
            else
            {
                keyL = left[i];
                keyR = right[i];
            }
            if (filtered)
            {
                keyL = keyFilter[2].low(keyFilter[0].low(keyL));
                keyR = keyFilter[3].low(keyFilter[1].low(keyR));
            }
            const float level = std::max(std::abs(keyL), std::abs(keyR));
            follower = std::max(level, follower * fall);
            levelPeak = std::max(levelPeak, follower);

            float target = 0.0f;
            switch (s.mode)
            {
                case Mode::trigger:
                {
                    sinceTrigger += dt;
                    if (! armed && follower < rearm && sinceTrigger >= kRearmSeconds)
                        armed = true;
                    if (armed && follower >= threshold)
                    {
                        armed = false;
                        duckFrom = current;
                        sinceTrigger = 0.0;
                        attacking = true;
                        hitScale = 0.0f;
                        ++hits;
                    }
                    // The hit's strength is read while it falls: the loudest point of its attack decides.
                    if (attacking)
                    {
                        const float over = 20.0f * std::log10(follower / threshold + 1.0e-9f);
                        hitScale = std::max(hitScale, std::clamp(over / 12.0f, 0.0f, 1.0f));
                        if (sinceTrigger >= std::max(s.attack, kShortestFall))
                            attacking = false;
                    }
                    const float scale = 1.0f - std::clamp(s.dynamics, 0.0f, 1.0f) * (1.0f - hitScale);
                    target = duckAt(sinceTrigger, duckFrom, s.depthDb * scale, s.attack, s.hold, s.release, s.shape);
                    break;
                }
                case Mode::compressor:
                {
                    const float wanted = reductionFor(20.0f * std::log10(follower + 1.0e-9f), s.thresholdDb, s.ratio,
                                                      s.depthDb);
                    if (wanted >= reduction)
                    {
                        reduction = wanted + (reduction - wanted) * attackCoefficient;
                        held = 0.0;
                    }
                    else if ((held += dt) > s.hold)
                        reduction = wanted + (reduction - wanted) * releaseCoefficient;
                    target = reduction;
                    break;
                }
                case Mode::tempo:
                {
                    // A new step (the phase wrapped, or the song jumped back): the duck starts from where it is.
                    if (tempoPhase < previousPhase)
                        duckFrom = current;
                    previousPhase = tempoPhase;
                    const double t = tempoPhase * stepSeconds;
                    target = duckAt(t, duckFrom, s.depthDb, s.attack, s.hold, tempoRelease, s.shape);
                    tempoPhase += tempoStep;
                    if (tempoPhase >= 1.0)
                        tempoPhase -= 1.0;
                    break;
                }
            }
            current = target;
            deepest = std::max(deepest, current);
            const float gain = std::pow(10.0f, -current / 20.0f);
            const float duck = 1.0f + wet * (gain - 1.0f);

            if (s.listen)
            {
                left[i] = output * keyL;
                right[i] = output * keyR;
                continue;
            }
            float l = left[i], r = right[i];
            if (s.band == Band::full)
            {
                l *= duck;
                r *= duck;
            }
            else
            {
                float lowL, highL, lowR, highR;
                bands[0].split(l, lowL, highL);
                bands[1].split(r, lowR, highR);
                l = s.band == Band::low ? duck * lowL + highL : lowL + duck * highL;
                r = s.band == Band::low ? duck * lowR + highR : lowR + duck * highR;
            }
            left[i] = output * l;
            right[i] = output * r;
        }
        shownLevel = 20.0f * std::log10(levelPeak + 1.0e-9f);
        shownReduction = deepest;
        if (s.mode != Mode::tempo)
        {
            tempoPhase = 0.0;
            previousPhase = 1.0;
        }
    }

    /** For the meter: the deepest reduction in dB and the key's peak level in dBFS in the last call. */
    float reductionNow() const { return shownReduction; }
    float levelNow() const { return shownLevel; }
    /** How many hits the trigger mode has heard since the last reset. */
    int hitsHeard() const { return hits; }

private:
    float coefficient(float seconds) const { return (float) std::exp(-1.0 / (std::max(1.0e-5f, seconds) * sampleRate)); }

    double sampleRate = 48000.0;
    std::array<Split, 4> keyFilter {};
    std::array<Crossover, 2> bands {};
    float follower = 0.0f;
    bool armed = true, attacking = false;
    double sinceTrigger = 1.0e9;
    float duckFrom = 0.0f, hitScale = 1.0f;
    float reduction = 0.0f;
    double held = 0.0;
    double tempoPhase = 0.0, previousPhase = 1.0;
    float current = 0.0f;
    float shownReduction = 0.0f, shownLevel = -100.0f;
    int hits = 0;
};
} // namespace tonwerkpump
