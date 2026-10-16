#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Tonwerk Filter's sound, plain C++: one multimode filter per channel whose cutoff an envelope follower (the input's
 * own level, as in an auto-wah) and an LFO (free in Hz or on the host's tempo) move in octaves. Borrows the tempo
 * divisions and the state variable filter from Tonwerk Wobble (`WobbleEngine.h`), whose drawn curves this does not
 * have: here the filter's types and the follower are the point.
 */
namespace tonwerkfilter
{
using tonwerkwobble::divisionBeats;
using tonwerkwobble::kDivisions;
using tonwerkwobble::SvfStage;

inline constexpr double kPi = 3.14159265358979323846;

/** The filter types; the order of the menu, saved in projects: append only. */
enum class Type
{
    lowpass12,
    lowpass24,
    ladder,
    highpass12,
    highpass24,
    bandpass,
    notch,
    comb,
};
inline constexpr std::array<const char*, 8> kTypeNames { "Tiefpass 12", "Tiefpass 24", "Ladder 24", "Hochpass 12",
                                                         "Hochpass 24", "Bandpass",    "Notch",     "Kamm" };

/** The LFO's shapes; the order of the menu, saved in projects: append only. */
enum class Shape
{
    sine,
    triangle,
    sawUp,
    sawDown,
    square,
    random,
};
inline constexpr std::array<const char*, 6> kShapeNames { "Sinus", "Dreieck", "Säge auf", "Säge ab", "Rechteck",
                                                          "Zufall" };

/** How far the envelope and the LFO move the cutoff at 100 %, in octaves. */
inline constexpr float kEnvelopeOctaves = 6.0f;
inline constexpr float kLfoOctaves = 4.0f;

struct Settings
{
    Type type = Type::lowpass24;
    /** Hz, before modulation. */
    float cutoff = 1000.0f;
    /** 0 to 1; the ladder and the comb sing near the top, the ladder self-oscillates there. */
    float resonance = 0.3f;
    /** Saturation before the filter, in dB of gain; 0 leaves the signal alone. */
    float driveDb = 0.0f;

    /** -1 to 1: how far the follower opens (or, negative, closes) the filter, as a share of `kEnvelopeOctaves`. */
    float envelopeAmount = 0.0f;
    /** Gain on the input before the follower reads it, in dB: quiet tracks need more to reach the top. */
    float sensitivityDb = 0.0f;
    float attackMs = 10.0f;
    float releaseMs = 200.0f;

    Shape shape = Shape::sine;
    bool sync = false;
    /** Cycles per second when free. */
    float rate = 1.0f;
    /** A `kDivisions` index when synced. */
    int division = tonwerkwobble::kQuarter;
    /** 0 to 1, a share of `kLfoOctaves` either way. */
    float lfoAmount = 0.0f;
    /** The right channel's LFO runs this many degrees ahead of the left's. */
    float spreadDegrees = 0.0f;

    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** One cycle of the LFO at `phase` (0 to 1), from -1 to 1; `random` is the held value of this cycle. */
inline float lfoValue(Shape shape, double phase, float random)
{
    const auto p = (float) (phase - std::floor(phase));
    switch (shape)
    {
        case Shape::triangle: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
        case Shape::sawUp: return 2.0f * p - 1.0f;
        case Shape::sawDown: return 1.0f - 2.0f * p;
        case Shape::square: return p < 0.5f ? 1.0f : -1.0f;
        case Shape::random: return random;
        case Shape::sine:
        default: return (float) std::sin(2.0 * kPi * p);
    }
}

/** A fixed random value for each whole cycle, so both channels and every block agree on it. */
inline float randomFor(std::int64_t cycle)
{
    auto x = (std::uint64_t) cycle * 0x9E3779B97F4A7C15ull;
    x ^= x >> 31;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    return (float) (x >> 40) / (float) (1ull << 23) - 1.0f;
}

/** tanh within 2.5 %, without the library call; at ±3 it reaches ±1 and stays there. */
inline float softClip(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Where the LFO is at the start of a block, in cycles, and how far it goes per sample. */
struct Clock
{
    double phase = 0.0;
    double increment = 0.0;
};

/**
 * A peak follower with separate attack and release, on the louder of the two channels so the filter moves alike on
 * both sides. Its value is the level, 1.0 at full scale.
 */
class Follower
{
public:
    void prepare(float sampleRate) { rate = sampleRate; }
    void reset() { level = 0.0f; }
    void set(float attackMs, float releaseMs)
    {
        attack = 1.0f - std::exp(-1000.0f / (std::max(attackMs, 0.1f) * rate));
        release = 1.0f - std::exp(-1000.0f / (std::max(releaseMs, 1.0f) * rate));
    }
    float process(float x)
    {
        const float size = std::abs(x);
        level += (size > level ? attack : release) * (size - level);
        return level;
    }
    float value() const { return level; }

private:
    float rate = 48000.0f;
    float attack = 1.0f, release = 1.0f, level = 0.0f;
};

/**
 * The filter for one channel. The cutoff and its coefficients follow every `kControl` samples (6000 times a second at
 * 48 kHz, far above the fastest LFO), and the cutoff is smoothed in octaves over 3 ms, so a jump of the square or
 * the random LFO does not click.
 */
class Channel
{
public:
    static constexpr int kControl = 8;

    void prepare(float sampleRate)
    {
        rate = sampleRate;
        // 3 ms: a square's step glides instead of clicking, a 20 Hz LFO still moves the whole way.
        smoothing = 1.0f - std::exp(-1.0f / (0.003f * sampleRate));
        // A comb as low as 20 Hz, with room for the interpolation.
        std::size_t size = 1;
        while (size < (std::size_t) (sampleRate / kCombLowest) + 4)
            size <<= 1;
        comb.assign(size, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& stage : stages)
            stage.reset();
        ladder.fill(0.0f);
        std::fill(comb.begin(), comb.end(), 0.0f);
        write = 0;
        countdown = 0;
        primed = false;
    }

    /** `octaves` moves the cutoff from `s.cutoff`; returns the filtered sample, before the mix. */
    float process(float x, float octaves, const Settings& s, float driveGain, float driveMakeup)
    {
        if (! primed)
        {
            smoothed = octaves;
            primed = true;
        }
        smoothed += smoothing * (octaves - smoothed);
        if (countdown-- <= 0)
        {
            countdown = kControl - 1;
            update(s);
        }
        const float y = driveGain > 1.0f ? softClip(driveGain * x) * driveMakeup : x;
        return filter(y, s.type);
    }

    /** The cutoff the filter stands at now, in Hz, for the display. */
    float cutoffNow() const { return hz; }

private:
    static constexpr float kButterworth = 1.41421356f;
    static constexpr float kCombLowest = 20.0f;

    float tuning(float f) const { return (float) std::tan(kPi * f / rate); }

    void update(const Settings& s)
    {
        const float top = std::min(20000.0f, 0.45f * rate);
        hz = std::clamp(s.cutoff * std::exp2(smoothed), 20.0f, top);
        g = tuning(hz);
        const float r = std::clamp(s.resonance, 0.0f, 1.0f);
        // Q from 0.7 (no peak) to about 16: the top sings without running away.
        k = kButterworth - (kButterworth - 0.06f) * r;
        // The ladder self-oscillates from 4; the top of the knob goes a little past, so it sings on by itself.
        ladderK = 4.2f * r;
        ladderG = g / (1.0f + g);
        // The comb's feedback, its sign kept positive so the teeth sit on the cutoff's harmonics.
        combFeedback = 0.97f * r;
        combDelay = std::clamp(rate / hz, 2.0f, (float) comb.size() - 3.0f);
        // Noise through the comb gains 1 / (1 - fb^2) in power; this keeps it near its dry level.
        combGain = std::sqrt(1.0f - combFeedback * combFeedback);
    }

    float filter(float x, Type type)
    {
        switch (type)
        {
            case Type::lowpass12: return stages[0].process(x, g, k).low;
            case Type::lowpass24: return stages[1].process(stages[0].process(x, g, kButterworth).low, g, k).low;
            case Type::highpass12: return stages[0].process(x, g, k).high;
            case Type::highpass24: return stages[1].process(stages[0].process(x, g, kButterworth).high, g, k).high;
            // k times the band output peaks at 0 dB however narrow it is.
            case Type::bandpass: return k * stages[0].process(x, g, k).band;
            case Type::notch:
            {
                const auto out = stages[0].process(x, g, k);
                return out.low + out.high;
            }
            case Type::ladder: return processLadder(x);
            case Type::comb: return processComb(x);
        }
        return x;
    }

    /**
     * Four one-poles with the fourth's output fed back, solved without a sample of delay (Zavalishin) so it keeps its
     * tune at high cutoffs, as Tonwerk Bass's ladder, with a soft clip on the stages' input instead of tanh: loud notes
     * push the resonance down instead of up, the way the transistors do. The passband's loss with resonance
     * (1 / (1 + k)) is half made up, as the classic ladders do it.
     */
    float processLadder(float x)
    {
        const float G = ladderG, b = 1.0f / (1.0f + g), G2 = G * G;
        const float S = G2 * G * b * ladder[0] + G2 * b * ladder[1] + G * b * ladder[2] + b * ladder[3];
        const float y4 = (G2 * G2 * x + S) / (1.0f + ladderK * G2 * G2);
        float u = softClip(x - ladderK * y4);
        for (auto& state : ladder)
        {
            const float v = (u - state) * G;
            const float y = v + state;
            state = y + v;
            u = y;
        }
        return u * (1.0f + 0.5f * ladderK);
    }

    /** A feedback comb on the cutoff's period: a row of peaks at its harmonics, sharper with the resonance. */
    float processComb(float x)
    {
        const float read = (float) write - combDelay + (float) comb.size();
        const auto mask = comb.size() - 1;
        const auto i = (std::size_t) read;
        const float frac = read - (float) i;
        const float a = comb[i & mask], b = comb[(i + 1) & mask];
        const float delayed = a + frac * (b - a);
        const float y = x + combFeedback * delayed;
        comb[write] = y;
        write = (write + 1) & mask;
        return combGain * y;
    }

    float rate = 48000.0f;
    std::array<SvfStage, 2> stages;
    std::array<float, 4> ladder {};
    std::vector<float> comb;
    std::size_t write = 0;
    float g = 0.1f, k = kButterworth, hz = 1000.0f;
    float ladderK = 0.0f, ladderG = 0.1f;
    float combFeedback = 0.0f, combDelay = 48.0f, combGain = 1.0f;
    float smoothed = 0.0f, smoothing = 0.01f;
    int countdown = 0;
    bool primed = false;
};

/** Both channels, the follower, the LFO and the dry/wet mix. */
class Engine
{
public:
    void prepare(float sampleRate)
    {
        rate = sampleRate;
        follower.prepare(sampleRate);
        for (auto& channel : channels)
            channel.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        follower.reset();
        for (auto& channel : channels)
            channel.reset();
        mixSmoothed = -1.0f;
    }

    /**
     * Processes `count` samples of up to two channels in place; `clock` says where the LFO is at the first sample, the
     * right channel reads it `spreadDegrees` further on.
     */
    void process(float* const* audio, int channelCount, int count, const Settings& s, const Clock& clock)
    {
        channelCount = std::min(channelCount, 2);
        if (channelCount <= 0)
            return;
        follower.set(s.attackMs, s.releaseMs);
        const float driveGain = std::pow(10.0f, std::max(0.0f, s.driveDb) / 20.0f);
        // A sample at -6 dBFS comes out where it went in, so the drive thickens rather than just louder.
        const float driveMakeup = 0.5f / softClip(0.5f * driveGain);
        const float sensitivity = std::pow(10.0f, s.sensitivityDb / 20.0f);
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float mixTarget = std::clamp(s.mix, 0.0f, 1.0f);
        if (mixSmoothed < 0.0f)
            mixSmoothed = mixTarget;
        const double spread = s.spreadDegrees / 360.0;
        const float envelopeOctaves = s.envelopeAmount * kEnvelopeOctaves;
        const float lfoOctaves = std::clamp(s.lfoAmount, 0.0f, 1.0f) * kLfoOctaves;
        const float mixStep = 1.0f - std::exp(-1.0f / (0.02f * rate));

        for (int n = 0; n < count; ++n)
        {
            float loudest = 0.0f;
            for (int ch = 0; ch < channelCount; ++ch)
                loudest = std::max(loudest, std::abs(audio[ch][n]));
            const float envelope = std::min(1.0f, follower.process(loudest * sensitivity));
            mixSmoothed += mixStep * (mixTarget - mixSmoothed);
            const double at = clock.phase + (double) n * clock.increment;
            for (int ch = 0; ch < channelCount; ++ch)
            {
                const double phase = at + (ch == 1 ? spread : 0.0);
                const float lfo = lfoOctaves > 0.0f ? lfoOctaves * lfoValue(s.shape, phase, randomFor((std::int64_t) std::floor(phase))) : 0.0f;
                const float x = audio[ch][n];
                const float y = channels[(std::size_t) ch].process(x, envelopeOctaves * envelope + lfo, s, driveGain,
                                                                     driveMakeup);
                audio[ch][n] = output * (x + mixSmoothed * (y - x));
            }
        }
    }

    const Channel& channel(int index) const { return channels[(std::size_t) std::clamp(index, 0, 1)]; }
    float envelope() const { return follower.value(); }

private:
    float rate = 48000.0f;
    Follower follower;
    std::array<Channel, 2> channels;
    float mixSmoothed = -1.0f;
};
} // namespace tonwerkfilter
