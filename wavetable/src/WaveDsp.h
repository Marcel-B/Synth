#pragma once

#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tonwerkwave
{
inline constexpr double kPi = 3.141592653589793;
inline constexpr int kModSlots = 8;
inline constexpr int kMacros = 4;
inline constexpr int kLfos = 2;
inline constexpr int kEnvelopes = 3;
inline constexpr int kMaxUnison = 8;

// The order of every enum below is the order of a parameter's menu, saved in Logic projects: append, never reorder.

/** How the oscillator's phase is bent before it reads the table, as Serum's warp modes do. */
enum class Warp { off, sync, bend, pwm, fm };
enum class FilterMode { low12, low24, high12, band12, notch, comb };
enum class LfoShape { sine, triangle, rampDown, rampUp, square, random };
enum class VoiceMode { poly, mono, legato };
enum class DistortionMode { soft, hard, fold, tube };
enum class ModSource
{
    none, lfo1, lfo2, env1, env2, env3, velocity, modWheel, aftertouch, note, macro1, macro2, macro3, macro4,
};
enum class ModTarget
{
    none, aPosition, aWarp, aPitch, aLevel, aDetune, bPosition, bWarp, bPitch, bLevel, bDetune, subLevel, noise,
    cutoff, resonance, filterDrive, amp, count
};

struct OscSettings
{
    bool on = true;
    int table = 0;
    /** 0 to 1 across the table's frames. */
    float position = 0.0f;
    Warp warp = Warp::off;
    float warpAmount = 0.0f;
    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;
    int unison = 1;
    /** Cents between the outermost unison voices. */
    float detune = 20.0f;
    /** 0: only the middle voices sound, 1: all equally loud. */
    float blend = 0.75f;
    /** How far the unison voices spread across the stereo field. */
    float width = 0.8f;
    float level = 0.75f;
};

struct SubSettings
{
    bool on = false;
    tonwerk::Wave shape = tonwerk::Wave::sine;
    int octave = -1;
    float level = 0.6f;
    /** Past the filter, so the low end stays clean while the filter moves. */
    bool direct = true;
};

struct FilterSettings
{
    bool on = true;
    FilterMode mode = FilterMode::low24;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float drive = 0.0f;
    /** 1: the cutoff follows the note one to one. */
    float keytrack = 0.0f;
};

/** Seconds; sustain as a share of the peak. */
struct EnvSettings
{
    float attack = 0.002f;
    float decay = 0.4f;
    float sustain = 1.0f;
    float release = 0.2f;
};

struct LfoSettings
{
    LfoShape shape = LfoShape::sine;
    bool sync = true;
    float rate = 2.0f;
    int division = 10;
    /** Start the LFO with each note; off, it runs on, locked to the bar while the host plays. */
    bool retrigger = true;
};

struct ModSlot
{
    ModSource source = ModSource::none;
    ModTarget target = ModTarget::none;
    /** -1 to 1 of the target's range. */
    float amount = 0.0f;
    /** The source from -1 to 1 instead of 0 to 1, as for vibrato. */
    bool bipolar = false;
};

struct DistortionSettings
{
    bool on = false;
    DistortionMode mode = DistortionMode::soft;
    float drive = 0.5f;
    float mix = 1.0f;
};

/** Everything a note plays from, read from the parameters each block. */
struct Settings
{
    OscSettings a;
    OscSettings b { false };
    SubSettings sub;
    float noise = 0.0f;
    FilterSettings filter;
    std::array<EnvSettings, kEnvelopes> env {};
    std::array<LfoSettings, kLfos> lfo {};
    std::array<ModSlot, kModSlots> mods {};
    std::array<float, kMacros> macros {};
    VoiceMode voiceMode = VoiceMode::poly;
    /** Seconds the pitch takes to slide to a new note in mono and legato. */
    float glide = 0.0f;
    int bendRange = 2;
    DistortionSettings distortion;
    float masterDb = 0.0f;
};

/** Note values for synced LFOs, in beats; the order of the division menu. */
struct Division
{
    const char* name;
    double beats;
};
inline constexpr std::array<Division, 14> kDivisions { {
    { "4/1", 16.0 }, { "2/1", 8.0 },        { "1/1", 4.0 },   { "1/2", 2.0 },   { "1/2T", 4.0 / 3 },
    { "1/4.", 1.5 }, { "1/4", 1.0 },        { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 }, { "1/8", 0.5 },
    { "1/8T", 1.0 / 3 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6 }, { "1/32", 0.125 },
} };
inline constexpr int kEighth = 9;

inline double divisionBeats(int division)
{
    return kDivisions[(std::size_t) std::clamp(division, 0, (int) kDivisions.size() - 1)].beats;
}

/** tanh's shape at a fraction of its cost: exact at 0, ±1 from ±3 on. */
inline float softClip(float x)
{
    if (x <= -3.0f)
        return -1.0f;
    if (x >= 3.0f)
        return 1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/**
 * An ADSR with times in seconds: a linear attack from wherever the level is (so a stolen or retriggered voice does
 * not click), then exponential falls to the sustain level and, on release, to silence, each about 99 % of the way
 * after its time. At least half a millisecond of attack, so a zero attack still does not click.
 */
class Adsr
{
public:
    void prepare(double rate) { sampleRate = rate; }

    void set(const EnvSettings& s)
    {
        attackStep = 1.0f / (float) (std::max(0.0005, (double) s.attack) * sampleRate);
        decayCoefficient = (float) std::exp(-4.6 / (std::max(0.001, (double) s.decay) * sampleRate));
        releaseCoefficient = (float) std::exp(-4.6 / (std::max(0.001, (double) s.release) * sampleRate));
        sustain = s.sustain;
    }

    void noteOn() { stage = Stage::attack; }
    void noteOff()
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }
    void reset()
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    float next()
    {
        switch (stage)
        {
            case Stage::idle:
                break;
            case Stage::attack:
                level += attackStep;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                level = sustain + (level - sustain) * decayCoefficient;
                break;
            case Stage::release:
                level *= releaseCoefficient;
                if (level < 1.0e-4f)
                    reset();
                break;
        }
        return level;
    }

    bool isActive() const { return stage != Stage::idle; }
    bool isReleasing() const { return stage == Stage::release; }
    float current() const { return level; }

private:
    enum class Stage { idle, attack, decay, release };
    double sampleRate = 48000.0;
    Stage stage = Stage::idle;
    float level = 0.0f;
    float attackStep = 0.001f;
    float decayCoefficient = 0.999f;
    float releaseCoefficient = 0.999f;
    float sustain = 1.0f;
};

/**
 * An LFO's value from 0 to 1 at `phase`. Every shape starts at 0 (the sine as an upturned cosine), so a wobble on
 * the cutoff opens from closed with each note, the way dubstep basses do.
 */
inline float lfoShape(LfoShape shape, double phase, float held)
{
    switch (shape)
    {
        case LfoShape::sine:
            return (float) (0.5 - 0.5 * std::cos(2.0 * kPi * phase));
        case LfoShape::triangle:
            return (float) (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
        case LfoShape::rampDown:
            return (float) (1.0 - phase);
        case LfoShape::rampUp:
            return (float) phase;
        case LfoShape::square:
            return phase < 0.5 ? 1.0f : 0.0f;
        case LfoShape::random:
            return held;
    }
    return 0.0f;
}

/**
 * Zavalishin's topology-preserving state variable filter, one channel: stable however fast the cutoff moves, which a
 * wobble needs. `g` is tan(pi fc / fs), `k` the damping (2 none, towards 0 self-oscillating).
 */
struct SvfStage
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float low = 0.0f, band = 0.0f, high = 0.0f;

    void process(float x, float g, float k, float a1)
    {
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = low = band = high = 0.0f; }
};

/** A feedback comb tuned to the cutoff: peaks at it and its harmonics, the hollow, metallic edge of a growl. */
class Comb
{
public:
    void prepare(double sampleRate)
    {
        // The longest delay is a cycle of 20 Hz.
        std::size_t n = 1;
        while ((double) n < sampleRate / 20.0 + 4.0)
            n <<= 1;
        buffer.assign(n, 0.0f);
        mask = n - 1;
        write = 0;
    }
    void reset() { std::fill(buffer.begin(), buffer.end(), 0.0f); }

    float process(float x, float delay, float feedback)
    {
        const float read = (float) write - delay;
        const float floorRead = std::floor(read);
        const float t = read - floorRead;
        const auto i = (std::size_t) ((long long) floorRead) & mask;
        const float a = buffer[i];
        const float b = buffer[(i + 1) & mask];
        const float y = x + feedback * (a + (b - a) * t);
        buffer[write] = softClip(y);
        write = (write + 1) & mask;
        return y;
    }

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t write = 0;
};

/** The global distortion's curve for one sample already driven by its gain. */
inline float shape(DistortionMode mode, float x)
{
    switch (mode)
    {
        case DistortionMode::soft:
            return softClip(x);
        case DistortionMode::hard:
            return std::clamp(x, -1.0f, 1.0f);
        case DistortionMode::fold:
            return (float) tonwerk::Wavefolder::fold(x);
        case DistortionMode::tube:
            // Lopsided: the negative half clips later and softer, which adds even harmonics.
            return x >= 0.0f ? softClip(x) : 0.8f * softClip(0.6f * x);
    }
    return x;
}
} // namespace tonwerkwave
