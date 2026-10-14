#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace tonwerkglue
{
inline constexpr double kPi = 3.141592653589793;

/** The ratios, attacks and releases the switches offer, in their menus' order (saved in projects: append only). */
inline constexpr std::array<float, 5> kRatios { 1.5f, 2.0f, 4.0f, 10.0f, 20.0f };
inline constexpr std::array<float, 6> kAttacks { 0.0001f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f };
/** The fixed releases in seconds; the menu's last entry is Auto. */
inline constexpr std::array<float, 4> kReleases { 0.1f, 0.3f, 0.6f, 1.2f };
inline constexpr int kAutoRelease = (int) kReleases.size();

struct Division
{
    const char* name;
    double beats;
};
/** The pump's note values, in the menu's order (saved in projects: append only). */
inline constexpr std::array<Division, 8> kDivisions { {
    { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4.", 1.5 }, { "1/4", 1.0 }, { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 },
    { "1/8", 0.5 }, { "1/16", 0.25 },
} };
inline constexpr int kQuarter = 3;
inline constexpr int kEighth = 6;

/** Where the detector listens: the signal itself, or the side chain (silence when none is connected). */
enum class Source { internal, sidechain };

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    float thresholdDb = -10.0f;
    int ratio = 2;             // index into kRatios: 4:1
    int attack = 4;            // index into kAttacks: 10 ms
    int release = kAutoRelease;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
    Source source = Source::internal;
    float sidechainHighpass = 20.0f; // Hz; the detector ignores what lies below (20 is all but off)
    bool pump = false;
    int pumpDivision = kQuarter;
    float pumpDepthDb = 12.0f;
    float pumpLength = 0.6f;   // the share of a step the gain takes to come back
    float pumpCurve = 0.5f;    // 0 comes back in a straight line (in dB), 1 holds low and comes back late
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** The reduction the static curve asks for at `levelDb`, in dB (0 or more), with a soft knee `kneeDb` wide. */
inline float reductionFor(float levelDb, float thresholdDb, float ratio, float kneeDb)
{
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f - 1.0f / ratio;
    if (kneeDb > 0.0f && std::abs(over) <= 0.5f * kneeDb)
    {
        const float into = over + 0.5f * kneeDb;
        return slope * into * into / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

/**
 * The pump's reduction in dB at `position` (0 to 1 through a step): down at once (a 2 ms slope, so it does not click),
 * then back over `length` of the step along the curve.
 */
inline float pumpAt(double position, double stepSeconds, const Settings& s)
{
    const double fall = std::min(0.002 / std::max(stepSeconds, 1.0e-3), 0.5);
    if (position < fall)
        return s.pumpDepthDb * (float) (position / fall);
    const double length = std::clamp((double) s.pumpLength, 0.05, 1.0);
    const double t = (position - fall) / std::max(1.0e-6, length - fall);
    if (t >= 1.0)
        return 0.0f;
    // The curve bends the way back: an exponent from 1 (straight) to 4 (holding low, then rising late).
    const double exponent = 1.0 + 3.0 * std::clamp((double) s.pumpCurve, 0.0, 1.0);
    return s.pumpDepthDb * (float) (1.0 - std::pow(t, exponent));
}

/** A first-order TPT high pass. */
struct OnePole
{
    float g = 1.0f, state = 0.0f;
    void set(double frequency, double rate)
    {
        const double t = std::tan(kPi * std::min(frequency, 0.45 * rate) / rate);
        g = (float) (t / (1.0 + t));
    }
    float high(float x)
    {
        const float v = (x - state) * g;
        const float low = v + state;
        state = low + v;
        return x - low;
    }
};

/**
 * A bus compressor in the manner of the console ones, plus a pump in the host's tempo.
 *
 * The detector takes the louder side (both sides get one gain, so the image stays put) of the input or the side
 * chain, after a high pass (two poles), as a peak level in dB; the static curve has a soft knee. The reduction is
 * smoothed in dB, rising with the attack and falling with the release, each a one-pole time constant. Auto release
 * is two stages, as on the consoles: a fast one (100 ms) for short peaks, and under it a slow one (1.5 s) that only
 * takes on what lasts (it rises over 300 ms), so a single hit lets go at once and a dense passage breathes slowly.
 * The pump works without any key: on each step of the tempo it pulls the gain down by its depth and lets it back,
 * placed by the song position while the host plays. Both reductions add in dB; the mix sets dry beside wet.
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
        for (auto& f : highpass)
            f = {};
        reduction = slowReduction = 0.0f;
        pumpPhase = 0.0;
        shownReduction = shownPump = 0.0f;
        shownLevel = -100.0f;
    }

    /**
     * In place, stereo; `keyLeft`/`keyRight` are the side chain (null when there is none). `bpm` sets the pump's
     * step; `beat` is the song position in quarter notes at the block's start while the host plays, negative otherwise.
     */
    void process(float* left, float* right, const float* keyLeft, const float* keyRight, int count, const Settings& s,
                 double bpm, double beat = -1.0)
    {
        const auto& division = kDivisions[(std::size_t) std::clamp(s.pumpDivision, 0, (int) kDivisions.size() - 1)];
        const double stepSeconds = division.beats * 60.0 / std::max(1.0, bpm);
        if (beat >= 0.0)
        {
            pumpPhase = beat / division.beats;
            pumpPhase -= std::floor(pumpPhase);
        }
        const double pumpStep = 1.0 / (stepSeconds * sampleRate);

        for (auto& f : highpass)
            f.set(std::max(1.0f, s.sidechainHighpass), sampleRate);
        const float ratio = kRatios[(std::size_t) std::clamp(s.ratio, 0, (int) kRatios.size() - 1)];
        const float attackSeconds = kAttacks[(std::size_t) std::clamp(s.attack, 0, (int) kAttacks.size() - 1)];
        const bool autoRelease = s.release >= kAutoRelease;
        const float releaseSeconds = autoRelease ? 0.1f : kReleases[(std::size_t) std::max(0, s.release)];
        const float attack = coefficient(attackSeconds);
        const float release = coefficient(releaseSeconds);
        const float slowAttack = coefficient(0.3f), slowRelease = coefficient(1.5f);
        const float makeupDb = s.makeupDb;
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f), dry = 1.0f - wet;
        const bool fromKey = s.source == Source::sidechain;
        float levelPeak = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            float keyL = 0.0f, keyR = 0.0f;
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
            keyL = highpass[0].high(highpass[2].high(keyL));
            keyR = highpass[1].high(highpass[3].high(keyR));
            const float level = std::max(std::abs(keyL), std::abs(keyR));
            levelPeak = std::max(levelPeak, level);
            const float levelDb = 20.0f * std::log10(level + 1.0e-9f);
            const float target = reductionFor(levelDb, s.thresholdDb, ratio, s.kneeDb);

            reduction = target + (reduction - target) * (target > reduction ? attack : release);
            float applied = reduction;
            if (autoRelease)
            {
                slowReduction = reduction + (slowReduction - reduction) * (reduction > slowReduction ? slowAttack : slowRelease);
                applied = std::max(reduction, slowReduction);
            }

            float pumpDb = 0.0f;
            if (s.pump)
            {
                pumpDb = pumpAt(pumpPhase, stepSeconds, s);
                pumpPhase += pumpStep;
                if (pumpPhase >= 1.0)
                    pumpPhase -= 1.0;
            }

            const float gain = std::pow(10.0f, (makeupDb - applied - pumpDb) / 20.0f);
            left[i] = output * (dry * left[i] + wet * gain * left[i]);
            right[i] = output * (dry * right[i] + wet * gain * right[i]);
            shownReduction = applied;
            shownPump = pumpDb;
        }
        shownLevel = 20.0f * std::log10(levelPeak + 1.0e-9f);
        if (! s.pump)
            pumpPhase = 0.0;
    }

    /** For the meter: the compressor's reduction and the pump's, in dB, and the detector's peak in the last block. */
    float reductionNow() const { return shownReduction; }
    float pumpNow() const { return shownPump; }
    float levelNow() const { return shownLevel; }

private:
    float coefficient(float seconds) const { return (float) std::exp(-1.0 / (std::max(1.0e-5f, seconds) * sampleRate)); }

    double sampleRate = 48000.0;
    std::array<OnePole, 4> highpass {};
    float reduction = 0.0f, slowReduction = 0.0f;
    double pumpPhase = 0.0;
    float shownReduction = 0.0f, shownPump = 0.0f, shownLevel = -100.0f;
};
} // namespace tonwerkglue
