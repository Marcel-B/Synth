#pragma once

#include "Voice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Tonwerk Physical (physical/src/PhysicalEngine.h of the Synth repository) for the Eurorack module: the same exciters
// and resonators, ported to float like the wavetable voice, without the plugin's LFOs and modulation routes.
namespace twm::phys
{
// Menu orders as in Tonwerk Physical, so a sound reads the same in both.
enum class Exciter : uint8_t { pluck, strike, bow, blow, count };
enum class Resonator : uint8_t { string, closedTube, openTube, bar, bell, membrane, bowl, count };
enum class Body : uint8_t { off, guitar, violin, box, soundboard, count };
enum class FilterMode : uint8_t { low12, low24, high12, band12, count };

inline constexpr int kModes = 16;

inline bool isWaveguide(Resonator r) { return r == Resonator::string || r == Resonator::closedTube || r == Resonator::openTube; }
inline bool isSustained(Exciter e) { return e == Exciter::bow || e == Exciter::blow; }

/** Everything a note plays from, as in the plugin's Settings; times in seconds, shares 0 to 1. */
struct Settings
{
    Exciter exciter = Exciter::pluck;
    float hardness = 0.5f;
    float pressure = 0.5f;
    float noise = 0.2f;
    float position = 0.2f;
    float attack = 0.08f, envDecay = 0.3f, sustain = 0.8f, envRelease = 0.2f;

    Resonator resonator = Resonator::string;
    float decay = 4.0f;
    float damping = 0.4f;
    float inharmonicity = 0.0f;
    float release = 0.3f;

    Body body = Body::off;
    float bodyMix = 0.5f;

    bool filterOn = false;
    FilterMode filterMode = FilterMode::low12;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float keytrack = 0.0f;

    /** The plugin's default velocity route: a full strike is harder by this much than the softest. */
    float velocityToHardness = 0.5f;
    float dynamics = 0.5f;
};

/** The plugin's mode tables: a bar, a bell, a drum skin, a bowl. */
struct ModeTable
{
    int count = 0;
    std::array<float, kModes> ratio {};
    std::array<float, kModes> gain {};
    std::array<bool, kModes> centred {};
};

inline const ModeTable& modeTable(Resonator r)
{
    static const ModeTable bar { 8,
                                 { 1.0f, 3.99f, 9.98f, 17.4f, 26.8f, 38.1f, 51.5f, 66.9f },
                                 { 1.0f, 0.55f, 0.3f, 0.16f, 0.09f, 0.05f, 0.03f, 0.02f },
                                 {} };
    static const ModeTable bell { 12,
                                  { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.5f, 2.67f, 3.01f, 4.16f, 5.43f, 6.8f, 8.22f },
                                  { 0.7f, 0.9f, 0.75f, 0.45f, 0.8f, 0.4f, 0.35f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f },
                                  {} };
    static const ModeTable membrane {
        16,
        { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.154f, 4.231f, 4.602f,
          4.832f, 4.903f },
        { 1.0f, 0.8f, 0.7f, 0.65f, 0.55f, 0.5f, 0.45f, 0.4f, 0.4f, 0.38f, 0.33f, 0.3f, 0.3f, 0.27f, 0.25f, 0.25f },
        { true, false, false, true, false, false, false, false, true, false, false, false, false, false, false, true }
    };
    static const ModeTable bowl { 12,
                                  { 1.0f, 1.003f, 2.32f, 2.326f, 4.25f, 4.26f, 6.63f, 6.645f, 9.38f, 9.4f, 12.6f, 12.63f },
                                  { 0.7f, 0.7f, 0.45f, 0.45f, 0.3f, 0.3f, 0.18f, 0.18f, 0.1f, 0.1f, 0.06f, 0.06f },
                                  {} };
    return r == Resonator::bell ? bell : r == Resonator::membrane ? membrane : r == Resonator::bowl ? bowl : bar;
}

/** The plugin's noise (xorshift), in float. */
class Noise
{
public:
    explicit Noise(uint32_t seed) : state(seed) {}
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) state * (1.0f / 2147483648.0f) - 1.0f;
    }

private:
    uint32_t state;
};

/**
 * The plugin's delay line with third-order Lagrange reads, of a fixed 4096 samples: a loop of 16 Hz (the lowest note
 * the plugin plays) at 48 kHz fits, and nothing is allocated.
 */
class DelayLine
{
public:
    static constexpr int kSize = 4096;
    void clear() { buffer.fill(0.0f); }

    float read(float delay) const
    {
        const float whole = std::floor(delay);
        const float d = delay - whole + 1.0f;
        const auto n = (uint32_t) whole;
        const float x0 = buffer[(write - n + 1) & kMask];
        const float x1 = buffer[(write - n) & kMask];
        const float x2 = buffer[(write - n - 1) & kMask];
        const float x3 = buffer[(write - n - 2) & kMask];
        const float d1 = d - 1.0f, d2 = d - 2.0f, d3 = d - 3.0f;
        return -x0 * d1 * d2 * d3 * (1.0f / 6.0f) + x1 * d * d2 * d3 * 0.5f - x2 * d * d1 * d3 * 0.5f
               + x3 * d * d1 * d2 * (1.0f / 6.0f);
    }
    void push(float x)
    {
        buffer[write] = x;
        write = (write + 1) & kMask;
    }

private:
    static constexpr uint32_t kMask = kSize - 1;
    std::array<float, kSize> buffer {};
    uint32_t write = 0;
};

inline float lossDelay(float a, float w) { return std::atan2(a * std::sin(w), 1.0f - a * std::cos(w)) / w; }
inline float allpassDelay(float c, float w)
{
    const float phase = std::atan2(-std::sin(w), c + std::cos(w)) - std::atan2(-c * std::sin(w), 1.0f + c * std::cos(w));
    return -phase / w;
}
inline float decayGain(float seconds, float samples, float rate)
{
    return std::exp2(-9.965784f * samples / (std::max(1.0e-3f, seconds) * rate));
}

/**
 * One note of Tonwerk Physical, mono: an exciter (pluck, strike, bow, breath) driving a string, a closed or an open
 * tube (waveguides) or a bar, bell, skin or bowl (two-pole resonances). The pitch is a MIDI note with a fraction, as
 * V/Oct gives it, and may move while the note rings. Everything that the plugin works out per control step it works
 * out here too, in float, and only when something it depends on moved: the ESP32-S3 has no FPU for double, and the
 * resonances' coefficients are most of a step's cost.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    explicit Voice(uint32_t seed = 0x2545f491u * 3u) : random(seed) {}

    void prepare(float rate)
    {
        sampleRate = rate;
        exciterEnv.prepare(rate);
        dcCoefficient = 1.0f - 2.0f * kPi * 20.0f / rate;
        reset();
    }

    void reset()
    {
        neck.clear();
        bridge.clear();
        exciterEnv.reset();
        for (auto& m : modes)
            m = {};
        lossState = 0.0f;
        dispersionState = {};
        excite = {};
        dcIn = dcOut = 0.0f;
        filterStages[0].reset();
        filterStages[1].reset();
        active = false;
        held = false;
        fading = 0;
        pending = false;
        quiet = 0;
        peak = 0.0f;
        tuned = {};
    }

    /** Starts a note; `again` strikes a ringing voice where it is, otherwise a ringing one fades out first. */
    void start(float newNote, float newVelocity, const Settings& s, bool again)
    {
        if (active && ! again && peak > 1.0e-3f)
        {
            pending = true;
            pendingNote = newNote;
            pendingVelocity = newVelocity;
            pendingReleased = false;
            fading = kFadeSteps;
            held = true;
            return;
        }
        begin(newNote, newVelocity, s);
    }

    void release()
    {
        held = false;
        if (fading > 0)
            pendingReleased = true;
        exciterEnv.noteOff();
    }

    /** The pitch moves while the note rings (V/Oct): the loop and the resonances follow at the next control step. */
    void setNote(float newNote)
    {
        if (pending)
            pendingNote = newNote;
        else
            note = newNote;
    }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    float level() const { return peak; }

    /** Adds `count` (at most kControl) samples to `out`. */
    void render(float* out, int count, const Settings& s)
    {
        if (! active)
            return;
        if (fading == 0 && pending)
        {
            pending = false;
            const float n = pendingNote, v = pendingVelocity;
            const bool released = pendingReleased;
            reset();
            begin(n, v, s);
            if (released)
                release();
        }
        control(s);

        float sound[kControl] {};
        if (isWaveguide(resonator))
            renderWaveguide(sound, count);
        else
            renderModes(sound, count);

        const float step = 1.0f / (float) count;
        const float fadeFrom = fading > 0 ? (float) fading / (float) kFadeSteps : 1.0f;
        const float fadeTo = fading > 0 ? (float) (fading - 1) / (float) kFadeSteps : 1.0f;
        float loudest = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            float x = sound[i];
            const float y = x - dcIn + dcCoefficient * dcOut;
            dcIn = x;
            dcOut = y;
            x = y;
            if (s.filterOn)
                x = filter(s.filterMode, x, t);
            x *= gain.at(t) * (fadeFrom + (fadeTo - fadeFrom) * t);
            loudest = std::max(loudest, std::abs(x));
            out[i] += x;
        }
        if (fading > 0)
            --fading;
        first = false;

        peak = std::max(loudest, peak * 0.9f);
        const bool driven = excite.left > 0 || (isSustained(s.exciter) && exciterEnv.isActive());
        if (! driven && loudest < 2.0e-5f && fading == 0 && ! pending)
        {
            if (++quiet > (int) (0.05f * sampleRate / kControl))
                reset();
        }
        else
            quiet = 0;
    }

private:
    static constexpr int kFadeSteps = 3;

    struct Ramp
    {
        float from = 0.0f, to = 0.0f;
        void set(float value, bool jump)
        {
            from = jump ? value : to;
            to = value;
        }
        float at(float t) const { return from + (to - from) * t; }
    };

    struct Mode
    {
        float a1 = 0.0f, a2 = 0.0f;
        float bStrike = 0.0f, bDrive = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct Excitation
    {
        int left = 0;
        int length = 1;
        bool pluck = false;
        float smooth = 1.0f;
        float noisy = 0.0f;
        float drive = 0.0f;
        float driveTo = 0.0f;
        float lowpass = 0.0f;
        float coefficient = 0.5f;
    };

    /** What the resonator's tuning was last worked out from; the same values again need no new coefficients. */
    struct Tuned
    {
        Resonator resonator = Resonator::count;
        float frequency = 0.0f, decay = 0.0f, damping = 0.0f, inharmonicity = 0.0f, position = 0.0f;
        bool operator==(const Tuned&) const = default;
    };

    void begin(float newNote, float newVelocity, const Settings& s)
    {
        const bool wasActive = active;
        note = newNote;
        velocity = newVelocity;
        held = true;
        exciterEnv.set(s.attack, s.envDecay, s.sustain, s.envRelease);
        exciterEnv.noteOn();
        strikePending = ! isSustained(s.exciter);
        if (! wasActive)
        {
            first = true;
            // The plugin draws its two LFOs' first random values here. Drawn too (and dropped), so the port's noise
            // is the plugin's to the sample and the host test can compare the two.
            random.next();
            random.next();
        }
        active = true;
        quiet = 0;
    }

    void control(const Settings& s)
    {
        exciterEnv.set(s.attack, s.envDecay, s.sustain, s.envRelease);

        resonator = s.resonator;
        const float highest = isWaveguide(resonator) ? sampleRate / 8.0f : sampleRate * 0.45f;
        frequency = std::clamp(noteToHz(note), 16.0f, highest);

        const float hardness = std::clamp(s.hardness + s.velocityToHardness * (velocity - 1.0f), 0.0f, 1.0f);
        const float pressure = std::clamp(s.pressure, 0.0f, 1.0f);
        position = std::clamp(s.position, 0.02f, 0.5f);
        float decay = std::clamp(s.decay, 0.01f, 60.0f);
        if (! held)
            decay = std::min(decay, std::max(0.005f, s.release));
        const float damping = std::clamp(s.damping, 0.0f, 1.0f);

        const float dynamic = std::pow(std::clamp(velocity, 0.01f, 1.0f), 1.6f * s.dynamics);

        const float cutoffHz = std::min(150.0f * std::exp2(7.2f * hardness), 0.45f * sampleRate);
        excite.coefficient = 1.0f - std::exp(-2.0f * kPi * cutoffHz / sampleRate);

        bowing = resonator == Resonator::string && s.exciter == Exciter::bow;
        reed = resonator == Resonator::closedTube && s.exciter == Exciter::blow;
        const Tuned now { resonator, frequency, decay, damping, s.inharmonicity, position };
        if (! (now == tuned))
        {
            if (isWaveguide(resonator))
                tuneWaveguide(s, decay, damping);
            else
                tuneModes(s, decay, damping);
            tuned = now;
        }
        else if (isWaveguide(resonator))
        {
            // Unchanged: the loop's lengths stay where the last step left them.
            neckFrom = neckTo;
            bridgeFrom = bridgeTo;
        }

        bowSlope = 5.0f - 4.0f * pressure;
        driveScale = 0.03f + 0.2f * pressure;
        reedSlope = -0.18f - 0.3f * hardness;
        breathScale = (0.2f + 0.11f * pressure) / -reedSlope;
        noiseShare = s.noise;
        excite.driveTo = isSustained(s.exciter) && ! bowing && ! reed ? pressure * driveNormal : 0.0f;
        const float reedLevel = reed ? -reedSlope / 0.33f : 1.0f;
        gain.set(dynamic * reedLevel * levelOf(resonator, s.exciter), first);

        if (strikePending)
        {
            strikePending = false;
            startBurst(s.exciter, hardness, s.noise);
        }

        const float cutoffNote = 12.0f * std::log2(std::max(20.0f, s.cutoff) / 440.0f) + 69.0f
                                 + s.keytrack * (note - 60.0f);
        const float cutoff = std::clamp(noteToHz(cutoffNote), 20.0f, std::min(20000.0f, 0.45f * sampleRate));
        const float resonance = std::clamp(s.resonance, 0.0f, 1.0f);
        filterG.set(std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
    }

    static float levelOf(Resonator r, Exciter e)
    {
        const bool sustained = isSustained(e);
        switch (r)
        {
            case Resonator::string: return e == Exciter::bow ? 0.8f : sustained ? 0.6f : 0.7f;
            case Resonator::closedTube: return e == Exciter::blow ? 0.18f : sustained ? 0.6f : 0.5f;
            case Resonator::openTube: return 0.85f;
            case Resonator::bar: return 0.7f;
            case Resonator::bell: return 0.3f;
            case Resonator::membrane: return 0.5f;
            case Resonator::bowl: return 0.4f;
            case Resonator::count: break;
        }
        return 0.5f;
    }

    void startBurst(Exciter exciter, float hardness, float noise)
    {
        const float period = sampleRate / frequency;
        float length = 0.0f;
        if (exciter == Exciter::pluck && isWaveguide(resonator))
            length = std::clamp(period, 0.0005f * sampleRate, 0.03f * sampleRate);
        else if (exciter == Exciter::pluck)
            length = std::min((0.0002f + 0.002f * (1.0f - hardness)) * sampleRate, 0.6f * period);
        else
            length = std::min((0.00025f + 0.006f * (1.0f - hardness) * (1.0f - hardness)) * sampleRate, 0.6f * period);
        excite.length = std::max(2, (int) length);
        excite.left = excite.length;
        excite.pluck = exciter == Exciter::pluck;
        if (isWaveguide(resonator))
        {
            excite.smooth = 1.0f - noise;
            excite.noisy = noise;
        }
        else
        {
            excite.smooth = (1.0f - noise) * (kPi / 2.0f / (float) excite.length);
            excite.noisy = noise / std::sqrt((float) excite.length);
        }
    }

    float nextExcitation(float env)
    {
        float v = 0.0f;
        if (excite.left > 0)
        {
            const float phase = 1.0f - (float) excite.left / (float) excite.length;
            const float shape = std::sin(kPi * phase);
            const float edge = std::min(1.0f, std::min(phase, 1.0f - phase) * (float) excite.length / 16.0f);
            const float envelope = excite.pluck ? edge : shape;
            v = excite.smooth * shape + excite.noisy * envelope * random.next();
            --excite.left;
        }
        if (excite.drive > 0.0f)
            v += excite.drive * env * random.next();
        excite.lowpass += (v - excite.lowpass) * excite.coefficient;
        if (std::abs(excite.lowpass) < 1.0e-15f)
            excite.lowpass = 0.0f;
        return excite.lowpass;
    }

    void tuneWaveguide(const Settings& s, float decay, float damping)
    {
        const float period = sampleRate / frequency;
        const float w = 2.0f * kPi * frequency / sampleRate;
        const bool closed = resonator == Resonator::closedTube;
        const float pass = closed ? 0.5f * period : period;
        const float dc = decayGain(decay, pass, sampleRate);
        const float high = decayGain(decay * std::exp(-5.0f * damping), pass, sampleRate);
        const float ratio = high / dc;
        lossA = (1.0f - ratio) / (1.0f + ratio);
        lossB = dc * (1.0f - lossA);
        float length = pass - lossDelay(lossA, w);

        float c = 0.0f, stretch = 0.0f;
        if (resonator == Resonator::string && s.inharmonicity > 0.005f)
        {
            float bend = 3.0f * std::exp2(4.0f * (1.0f - s.inharmonicity));
            for (;;)
            {
                c = std::max(-0.995f, -1.0f + bend * w);
                if (c > -0.005f)
                    break;
                stretch = 4.0f * allpassDelay(c, w);
                if (stretch <= 0.5f * length)
                    break;
                bend *= 1.5f;
            }
        }
        stiff = c < -0.005f;
        if (! stiff)
        {
            c = 0.0f;
            stretch = 0.0f;
        }
        dispersion = c;
        length = std::max(4.0f, length - stretch);

        float n = 0.0f, b = 0.0f;
        if (resonator == Resonator::string)
        {
            n = std::max(2.0f, position * length);
            b = std::max(2.0f, length - n);
        }
        else
            n = std::max(2.0f, length);
        neckFrom = first ? n : neckTo;
        bridgeFrom = first ? b : bridgeTo;
        neckTo = n;
        bridgeTo = b;
        driveNormal = 4.0f * std::sqrt(std::max(1.0e-6f, 1.0f - dc * dc));
    }

    void tuneModes(const Settings& s, float decay, float damping)
    {
        const auto& table = modeTable(resonator);
        const float stretch = 1.0f + 0.5f * s.inharmonicity;
        modeCount = 0;
        for (int k = 0; k < table.count; ++k)
        {
            const float ratio = std::pow(table.ratio[(std::size_t) k], stretch);
            const float f = frequency * ratio;
            if (f >= 0.45f * sampleRate)
                continue;
            float weight = table.gain[(std::size_t) k];
            if (resonator == Resonator::bar)
                weight *= 0.2f + 0.8f * std::abs(std::sin(kPi * (float) (k + 1) * position));
            else if (resonator == Resonator::membrane)
            {
                const float edge = 2.0f * position;
                weight *= table.centred[(std::size_t) k] ? 1.0f - 0.5f * edge : edge;
            }
            else
                weight *= 0.6f + 0.4f * std::abs(std::sin(kPi * (float) (k + 1) * position));
            const float t60 = std::clamp(decay * std::pow(ratio, -1.5f * damping), 0.005f, 120.0f);
            const float r = std::exp(-6.907755f / (t60 * sampleRate));
            const float theta = 2.0f * kPi * f / sampleRate;
            auto& m = modes[(std::size_t) modeCount];
            m.a1 = 2.0f * r * std::cos(theta);
            m.a2 = r * r;
            const float sinTheta = std::sin(theta);
            m.bStrike = weight * sinTheta;
            m.bDrive = weight * sinTheta * std::sqrt(2.0f * (1.0f - r * r));
            ++modeCount;
        }
        for (int k = modeCount; k < kModes; ++k)
            modes[(std::size_t) k].y1 = modes[(std::size_t) k].y2 = 0.0f;
        driveNormal = 1.5f;
    }

    void renderWaveguide(float* out, int count)
    {
        const float step = 1.0f / (float) count;
        const float driveFrom = excite.drive;
        const float driveTo = excite.driveTo;
        const float a = lossA, b0 = lossB, c = dispersion;
        const bool string = resonator == Resonator::string;
        const bool closed = resonator == Resonator::closedTube;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            excite.drive = driveFrom + (driveTo - driveFrom) * t;
            const float env = exciterEnv.next();
            if (string)
            {
                const float nut = -neck.read(neckFrom + (neckTo - neckFrom) * t);
                const float arriving = bridge.read(bridgeFrom + (bridgeTo - bridgeFrom) * t);
                float x = arriving;
                if (stiff)
                    for (auto& state : dispersionState)
                    {
                        const float v = c * x + state;
                        state = x - c * v;
                        x = v;
                    }
                lossState = b0 * x + a * lossState;
                const float back = -lossState;
                float push = 0.0f;
                if (bowing)
                {
                    const float speed = env * driveScale * (1.0f + noiseShare * 0.3f * random.next());
                    const float dv = speed - (nut + back);
                    float grip = std::abs(dv * bowSlope) + 0.75f;
                    grip = 1.0f / (grip * grip * grip * grip);
                    push = dv * std::clamp(grip, 0.01f, 1.0f);
                }
                else
                    push = nextExcitation(env);
                neck.push(back + push);
                bridge.push(nut + push);
                out[i] = arriving;
            }
            else
            {
                const float arriving = neck.read(neckFrom + (neckTo - neckFrom) * t);
                lossState = b0 * arriving + a * lossState;
                const float back = closed ? -lossState : lossState;
                if (reed)
                {
                    const float breath = env * breathScale * (1.0f + noiseShare * 0.25f * random.next());
                    const float across = back - breath;
                    const float opening = std::clamp(0.7f + reedSlope * across, -1.0f, 1.0f);
                    neck.push(breath + across * opening);
                }
                else
                    neck.push(back + nextExcitation(env));
                out[i] = arriving;
            }
        }
        excite.drive = driveTo;
        neckFrom = neckTo;
        bridgeFrom = bridgeTo;
    }

    void renderModes(float* out, int count)
    {
        const float step = 1.0f / (float) count;
        const float driveFrom = excite.drive;
        const float driveTo = excite.driveTo;
        const bool driving = excite.driveTo > 0.0f || driveFrom > 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            excite.drive = driveFrom + (driveTo - driveFrom) * t;
            const float e = nextExcitation(exciterEnv.next());
            float sum = 0.0f;
            for (int k = 0; k < modeCount; ++k)
            {
                auto& m = modes[(std::size_t) k];
                const float y = (driving ? m.bDrive : m.bStrike) * e + m.a1 * m.y1 - m.a2 * m.y2;
                m.y2 = m.y1;
                m.y1 = y;
                sum += y;
            }
            out[i] = sum;
        }
        excite.drive = driveTo;
    }

    float filter(FilterMode mode, float x, float t)
    {
        const float g = filterG.at(t);
        const float k = filterK.at(t);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        if (mode == FilterMode::low24)
        {
            constexpr float k1 = 1.414f;
            filterStages[0].process(x, g, k1, 1.0f / (1.0f + g * (g + k1)));
            filterStages[1].process(filterStages[0].low, g, k, a1);
            return filterStages[1].low;
        }
        filterStages[0].process(x, g, k, a1);
        switch (mode)
        {
            case FilterMode::low12: return filterStages[0].low;
            case FilterMode::high12: return filterStages[0].high;
            case FilterMode::band12: return k * filterStages[0].band;
            default: break;
        }
        return x;
    }

    float sampleRate = 48000.0f;
    float note = 60.0f;
    float frequency = 261.6f;
    float velocity = 1.0f;
    bool held = false;
    bool active = false;
    bool first = true;
    bool strikePending = false;
    int fading = 0;
    bool pending = false;
    float pendingNote = 60.0f;
    float pendingVelocity = 1.0f;
    bool pendingReleased = false;
    int quiet = 0;
    float peak = 0.0f;
    Tuned tuned;

    Resonator resonator = Resonator::string;
    float position = 0.2f;
    Adsr exciterEnv;
    Excitation excite;
    bool bowing = false, reed = false;
    float bowSlope = 3.0f, driveScale = 0.1f, breathScale = 0.7f, reedSlope = -0.3f, noiseShare = 0.0f;
    float driveNormal = 1.0f;

    DelayLine neck, bridge;
    float neckFrom = 2.0f, neckTo = 2.0f, bridgeFrom = 2.0f, bridgeTo = 2.0f;
    float lossA = 0.0f, lossB = 1.0f, lossState = 0.0f;
    float dispersion = 0.0f;
    bool stiff = false;
    std::array<float, 4> dispersionState {};

    std::array<Mode, kModes> modes {};
    int modeCount = 0;

    float dcIn = 0.0f, dcOut = 0.0f, dcCoefficient = 0.997f;
    Ramp gain;
    Noise random;
    SvfStage filterStages[2] {};
    Ramp filterG, filterK;
};

/** The plugin's body after the voices, mono: four resonances of a guitar, a violin, a wooden box or a soundboard. */
class BodyFilter
{
public:
    void prepare(float rate)
    {
        sampleRate = rate;
        for (auto& s : stages)
            s.reset();
        lastBody = Body::count;
    }

    void process(float* x, int count, Body body, float mix)
    {
        if (body == Body::off || body == Body::count || mix <= 0.0f)
            return;
        if (body != lastBody || mix != lastMix)
            tune(body, mix);
        for (int n = 0; n < count; ++n)
        {
            float add = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                stages[i].process(x[n], g[i], k[i], a[i]);
                add += weight[i] * stages[i].band;
            }
            x[n] += add;
        }
    }

private:
    void tune(Body body, float mix)
    {
        struct Peak
        {
            float hz, q, gain;
        };
        static constexpr Peak peaks[4][4] {
            { { 98.0f, 6.0f, 1.5f }, { 204.0f, 8.0f, 1.2f }, { 390.0f, 5.0f, 0.6f }, { 2400.0f, 1.5f, 0.3f } },
            { { 275.0f, 9.0f, 1.0f }, { 460.0f, 7.0f, 1.2f }, { 1000.0f, 3.0f, 0.4f }, { 2700.0f, 1.8f, 0.8f } },
            { { 120.0f, 5.0f, 1.4f }, { 420.0f, 4.0f, 0.8f }, { 1500.0f, 3.0f, 0.4f }, { 3500.0f, 2.0f, 0.2f } },
            { { 80.0f, 3.0f, 0.8f }, { 250.0f, 2.5f, 0.6f }, { 900.0f, 2.0f, 0.4f }, { 2500.0f, 1.5f, 0.3f } },
        };
        const auto& set = peaks[(int) body - 1];
        for (int i = 0; i < 4; ++i)
        {
            g[i] = std::tan(kPi * std::min(set[i].hz, 0.45f * sampleRate) / sampleRate);
            k[i] = 1.0f / set[i].q;
            a[i] = 1.0f / (1.0f + g[i] * (g[i] + k[i]));
            weight[i] = mix * set[i].gain * k[i];
        }
        lastBody = body;
        lastMix = mix;
    }

    float sampleRate = 48000.0f;
    SvfStage stages[4] {};
    float g[4] {}, k[4] {}, a[4] {}, weight[4] {};
    Body lastBody = Body::count;
    float lastMix = -1.0f;
};

/** What the control task hands the audio task: the sound, the pitch, the gate and the strength of the next strike. */
struct PlayParams
{
    Settings settings;
    /** MIDI semitones (60 = C4), V/Oct, tune and fine added. */
    float note = 48.0f;
    /** How hard the next pluck or strike is, 0 to 1; a bow or breath always starts at full strength. */
    float velocity = 0.7f;
    /** How many notes may ring at once, 1 to kMaxVoices: each new strike takes the next voice. */
    int voices = 2;
    bool gate = false;
};

/**
 * The module's player: up to four voices, the newest following V/Oct while the older ones ring on at the pitch they
 * were struck at. A rising gate starts a note and a falling one lets the newest go (bow and breath stop, the damper
 * comes down by `release`). With the gate high, which is how the module sees an empty gate jack, a jump of V/Oct
 * plucks or strikes again, so a sequence of V/Oct alone plays notes; a glide does not, and bow and breath only follow
 * the pitch. Writes `dry`, the voices alone, and `out`, through the body.
 */
class Player
{
public:
    static constexpr int kControl = Voice::kControl;
    static constexpr int kMaxVoices = 4;
    /** A jump: at least half a semitone within 4 ms. A glide of a whole octave in a tenth of a second is none. */
    static constexpr float kJumpSemitones = 0.5f;
    static constexpr int kJumpBlocks = 6;
    /** No second strike this soon after one: a sequencer's V/Oct may settle just after its gate. */
    static constexpr float kHoldOffSeconds = 0.015f;
    /**
     * The voices' level at the jacks (full scale is ±5 V): at its defaults and full strength the loudest pairing, a
     * plucked skin, peaks at full scale, the plucked string at 2.3 V, all of them between -17 and -10 dBFS RMS.
     */
    static constexpr float kOutput = 1.2f;

    Player()
        : voices { Voice(0x2545f491u * 3u), Voice(0x2545f491u * 5u), Voice(0x2545f491u * 7u),
                   Voice(0x2545f491u * 9u) }
    {
    }

    void prepare(float rate)
    {
        sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
        body.prepare(rate);
        newest = -1;
        struck = 0;
        gateWas = false;
        sinceStrike = 1.0f;
    }

    void render(float* dry, float* out, int count, const PlayParams& p)
    {
        const auto& s = p.settings;
        const float before = recent[(position + 1) % kJumpBlocks];
        if (p.gate && ! gateWas)
            strike(p);
        else if (! p.gate && gateWas && newest >= 0)
            voices[newest].release();
        else if (p.gate && ! isSustained(s.exciter) && sinceStrike >= kHoldOffSeconds
                 && std::abs(p.note - before) >= kJumpSemitones)
            strike(p);
        gateWas = p.gate;
        sinceStrike += (float) count / sampleRate;
        position = (position + 1) % kJumpBlocks;
        recent[position] = p.note;
        if (newest >= 0)
            voices[newest].setNote(p.note);

        std::fill(dry, dry + count, 0.0f);
        for (auto& v : voices)
            v.render(dry, count, s);
        for (int i = 0; i < count; ++i)
            dry[i] *= kOutput;
        std::copy(dry, dry + count, out);
        body.process(out, count, s.body, s.bodyMix);
    }

    /** How loud the newest note is, for the LEDs' glow. */
    float envelope() const { return newest >= 0 ? std::min(1.0f, 2.0f * voices[newest].level()) : 0.0f; }

    /** How many notes were struck since `prepare`, for the host test. */
    int strikes() const { return struck; }

    int activeVoices() const
    {
        return (int) std::count_if(std::begin(voices), std::end(voices), [](const Voice& v) { return v.isActive(); });
    }

private:
    void strike(const PlayParams& p)
    {
        const int count = std::clamp(p.voices, 1, kMaxVoices);
        const auto& s = p.settings;
        const float velocity = isSustained(s.exciter) ? 1.0f : std::clamp(p.velocity, 0.05f, 1.0f);
        // Voices past the count let go; the next one in turn takes the note (one voice strikes itself again).
        for (int i = count; i < kMaxVoices; ++i)
            if (voices[i].isActive())
                voices[i].release();
        if (newest >= 0 && newest < count && voices[newest].isHeld())
            voices[newest].release();
        const int next = count == 1 ? 0 : (newest + 1) % count;
        voices[next].start(p.note, velocity, s, count == 1);
        newest = next;
        ++struck;
        sinceStrike = 0.0f;
    }

    float sampleRate = 48000.0f;
    Voice voices[kMaxVoices];
    BodyFilter body;
    int newest = -1;
    int struck = 0;
    bool gateWas = false;
    /** V/Oct over the last kJumpBlocks blocks, for the jumps. */
    float recent[kJumpBlocks] {};
    int position = 0;
    float sinceStrike = 1.0f;
};
} // namespace twm::phys
