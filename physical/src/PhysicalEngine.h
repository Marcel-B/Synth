#pragma once

#include "WaveDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tonwerkphys
{
using tonwerkwave::Adsr;
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;
using tonwerkwave::SvfStage;

inline constexpr double kPi = 3.141592653589793;
inline constexpr int kLfos = 2;
/** Resonances of a struck body; the bell and the drum skin use them all. */
inline constexpr int kModes = 16;

// The order of every enum below is the order of a parameter's menu, saved in Logic projects: append, never reorder.

/** What sets the body ringing: a pluck and a strike start it and let it ring, a bow and breath keep it going. */
enum class Exciter { pluck, strike, bow, blow };
/**
 * What rings. A string and two tubes are waveguides (delay lines closed into a loop); the bar, the bell, the drum skin
 * and the bowl are banks of resonances at their own ratios.
 */
enum class Resonator { string, closedTube, openTube, bar, bell, membrane, bowl, count };
/** The instrument's body after the voices: resonances of a guitar, a violin, a wooden box or a piano's soundboard. */
enum class Body { off, guitar, violin, box, soundboard, count };
enum class FilterMode { low12, low24, high12, band12 };
enum class VoiceMode { poly, mono };
enum class Target { none, pitch, pressure, hardness, position, decay, damping, cutoff, resonance, amp, count };

inline bool isWaveguide(Resonator r) { return r == Resonator::string || r == Resonator::closedTube || r == Resonator::openTube; }
inline bool isSustained(Exciter e) { return e == Exciter::bow || e == Exciter::blow; }

/** A modulation source's fixed route: where it goes and how far, -1 to 1 of the target's range. */
struct Route
{
    Target target = Target::none;
    float amount = 0.0f;
};

struct LfoSettings
{
    LfoShape shape = LfoShape::sine;
    bool sync = false;
    float rate = 5.0f;
    int division = 6;
    Route route;
};

/** Everything a note plays from, read from the parameters each block. Times in seconds, shares 0 to 1. */
struct Settings
{
    Exciter exciter = Exciter::pluck;
    /** How hard the plectrum or hammer is: brighter and shorter towards 1. For a bow and breath, the noise's colour. */
    float hardness = 0.5f;
    /** A bow's force on the string or the breath on a reed; how much a bow or breath drives any other body. */
    float pressure = 0.5f;
    /** The share of noise in a pluck or strike; the scrape of a bow, the air in a breath. */
    float noise = 0.2f;
    /** Where on the string the pluck, hammer or bow sits, as a share of its length (0.5 the middle); on a bar or skin, where it is struck. */
    float position = 0.2f;
    /** How a bow's speed or the breath rises and falls while a key is held. */
    EnvSettings env1 { 0.08f, 0.3f, 0.8f, 0.2f };

    Resonator resonator = Resonator::string;
    /** Seconds the lowest resonance takes to fall 60 dB while the key is held. */
    float decay = 4.0f;
    /** How much faster the high resonances die than the low ones. */
    float damping = 0.4f;
    /** How far the overtones stretch away from whole multiples: a stiff piano string, a bent bell. */
    float inharmonicity = 0.0f;
    /** The 60 dB fall once the key is let go (never longer than `decay`): the damper. */
    float release = 0.3f;

    Body body = Body::off;
    float bodyMix = 0.5f;

    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;

    bool filterOn = false;
    FilterMode filterMode = FilterMode::low12;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float keytrack = 0.0f;

    EnvSettings env2 { 0.01f, 0.5f, 0.0f, 0.3f };
    Route env2Route;
    std::array<LfoSettings, kLfos> lfo {};
    Route wheel;
    /** Velocity counts from 0 at a full strike down to -1 at the softest, so a positive amount makes soft notes less. */
    Route velocity { Target::hardness, 0.5f };
    /** How much quieter soft notes are: 0 all alike, 1 about 20 dB from the hardest to a soft one. */
    float dynamics = 0.5f;

    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f;
    int bendRange = 2;
    /** How far the notes spread across the stereo field, low ones left, high ones right, as on a piano. */
    float width = 0.4f;
    float masterDb = -6.0f;
};

/** What the host says about time, at the start of the stretch being rendered. */
struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/** What all voices share during one control step. */
struct Shared
{
    double sampleRate = 48000.0;
    double bend = 0.0;
    float modWheel = 0.0f;
    Transport transport;
};

/**
 * A struck body's resonances, as ratios to the note and their strengths when struck at the edge. The bar's first three
 * are tuned to 1, 4 and 10 the way a marimba's bars are cut; the skin's are a circular membrane's (the zeros of
 * Bessel's functions), `centred` marking the ones a strike in the middle still sets ringing; the bell's are a church
 * bell's hum, prime, minor third, fifth and nominal and what lies above; the bowl's come in close pairs, so they beat.
 */
struct ModeTable
{
    int count = 0;
    std::array<float, kModes> ratio {};
    std::array<float, kModes> gain {};
    std::array<bool, kModes> centred {};
};

inline const ModeTable& modeTable(Resonator r)
{
    static const std::array<ModeTable, 4> tables = [] {
        std::array<ModeTable, 4> t {};
        t[0] = { 8,
                 { 1.0f, 3.99f, 9.98f, 17.4f, 26.8f, 38.1f, 51.5f, 66.9f },
                 { 1.0f, 0.55f, 0.3f, 0.16f, 0.09f, 0.05f, 0.03f, 0.02f },
                 {} };
        t[1] = { 12,
                 { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.5f, 2.67f, 3.01f, 4.16f, 5.43f, 6.8f, 8.22f },
                 { 0.7f, 0.9f, 0.75f, 0.45f, 0.8f, 0.4f, 0.35f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f },
                 {} };
        t[2] = { 16,
                 { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.154f, 4.231f,
                   4.602f, 4.832f, 4.903f },
                 { 1.0f, 0.8f, 0.7f, 0.65f, 0.55f, 0.5f, 0.45f, 0.4f, 0.4f, 0.38f, 0.33f, 0.3f, 0.3f, 0.27f, 0.25f,
                   0.25f },
                 { true, false, false, true, false, false, false, false, true, false, false, false, false, false,
                   false, true } };
        t[3] = { 12,
                 { 1.0f, 1.003f, 2.32f, 2.326f, 4.25f, 4.26f, 6.63f, 6.645f, 9.38f, 9.4f, 12.6f, 12.63f },
                 { 0.7f, 0.7f, 0.45f, 0.45f, 0.3f, 0.3f, 0.18f, 0.18f, 0.1f, 0.1f, 0.06f, 0.06f },
                 {} };
        return t;
    }();
    const std::size_t index = r == Resonator::bell ? 1 : r == Resonator::membrane ? 2 : r == Resonator::bowl ? 3 : 0;
    return tables[index];
}

/**
 * A delay line read anywhere between its samples by third-order Lagrange interpolation: flat enough that a string
 * does not lose its top, and a length that may glide without clicks. `read` before `write` in each sample.
 */
class DelayLine
{
public:
    void prepare(int longest)
    {
        std::size_t n = 1;
        while ((int) n < longest + 8)
            n <<= 1;
        buffer.assign(n, 0.0f);
        mask = n - 1;
        write = 0;
    }
    void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }

    /** The sample written `delay` samples ago, at least 2. */
    float read(float delay) const
    {
        const float whole = std::floor(delay);
        const float d = delay - whole + 1.0f;
        const auto n = (std::size_t) whole;
        const float x0 = buffer[(write - n + 1) & mask];
        const float x1 = buffer[(write - n) & mask];
        const float x2 = buffer[(write - n - 1) & mask];
        const float x3 = buffer[(write - n - 2) & mask];
        const float d1 = d - 1.0f, d2 = d - 2.0f, d3 = d - 3.0f;
        return -x0 * d1 * d2 * d3 * (1.0f / 6.0f) + x1 * d * d2 * d3 * 0.5f - x2 * d * d1 * d3 * 0.5f
               + x3 * d * d1 * d2 * (1.0f / 6.0f);
    }

    void push(float x)
    {
        buffer[write] = x;
        write = (write + 1) & mask;
    }

    /** The line's content `ago` samples back, without interpolation, for the display. */
    float at(int ago) const { return buffer[(write - (std::size_t) std::max(1, ago)) & mask]; }

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t write = 0;
};

/** Phase delay in samples of the loss filter g(1-a) / (1 - a z^-1) at `w` radians per sample. */
inline double lossDelay(double a, double w) { return std::atan2(a * std::sin(w), 1.0 - a * std::cos(w)) / w; }

/** Phase delay in samples of the first-order allpass (c + z^-1) / (1 + c z^-1) at `w`. */
inline double allpassDelay(double c, double w)
{
    const double phase = std::atan2(-std::sin(w), c + std::cos(w)) - std::atan2(-c * std::sin(w), 1.0 + c * std::cos(w));
    return -phase / w;
}

/** 0.001 (60 dB down) after `seconds`, as a gain per `samples` at `rate`. */
inline double decayGain(double seconds, double samples, double rate)
{
    return std::pow(10.0, -3.0 * samples / (std::max(1.0e-3, seconds) * rate));
}

/**
 * One note: an exciter (a pluck or strike that is over in a moment, or a bow or breath that lasts while the key is
 * held) driving a resonator. The string is two delay lines meeting where it is plucked, struck or bowed (the bow's
 * stick and slip from the classic friction curve), with a loss filter and a stretch of allpasses at the bridge; the
 * closed tube has a reed at its mouth whose opening follows the pressure across it, so blown it plays by itself; the
 * struck bodies are banks of two-pole resonances. Pitch and the loop's lengths are set every `kControl` samples and
 * glide between, so vibrato and glide stay smooth.
 */
class Voice
{
public:
    static constexpr int kControl = 32;
    /** Display points along a waveguide. */
    static constexpr int kShape = 96;

    Voice() : random(nextSeed()) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        // The longest loop: a little below the lowest C a piano has, at this rate.
        const int longest = (int) std::ceil(rate / 15.0);
        neck.prepare(longest);
        bridge.prepare(longest);
        exciterEnv.prepare(rate);
        mod.prepare(rate);
        dcCoefficient = (float) (1.0 - 2.0 * kPi * 20.0 / rate);
        reset();
    }

    void reset()
    {
        neck.clear();
        bridge.clear();
        exciterEnv.reset();
        mod.reset();
        modes = {};
        lossState = 0.0f;
        dispersionState = {};
        excite = {};
        dcIn = dcOut = 0.0f;
        filterStages[0].reset();
        filterStages[1].reset();
        active = false;
        held = false;
        sustained = false;
        fading = 0;
        quiet = 0;
        peak = 0.0f;
    }

    /** Starts `note`; a voice still ringing another note fades out first, unless it glides there. */
    void start(int newNote, float newVelocity, const Settings& s, bool glide)
    {
        if (active && ! glide && newNote != note && peak > 1.0e-3f)
        {
            pendingNote = newNote;
            pendingVelocity = newVelocity;
            fading = kFadeSteps;
            held = true;
            sustained = false;
            return;
        }
        begin(newNote, newVelocity, s, glide);
    }

    void release()
    {
        held = false;
        if (fading > 0)
            pendingReleased = true;
        exciterEnv.noteOff();
        mod.noteOff();
    }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    bool isReleasing() const { return active && ! held; }
    int currentNote() const { return fading > 0 ? pendingNote : note; }

    bool sustained = false;
    std::uint64_t age = 0;

    /** What the display draws of this voice. */
    struct Picture
    {
        Resonator resonator = Resonator::string;
        float frequency = 0.0f;
        float position = 0.2f;
        int points = 0;
        std::array<float, kShape> shape {};
        int modeCount = 0;
        std::array<float, kModes> modeRatio {};
        std::array<float, kModes> modeLevel {};
    };

    void draw(Picture& p) const
    {
        p.resonator = resonator;
        p.frequency = (float) frequency;
        p.position = position;
        p.points = 0;
        p.modeCount = 0;
        if (isWaveguide(resonator))
        {
            // The loop laid out from one end to the other: what travels towards the far end and what comes back.
            const float neckLength = neckTo, bridgeLength = resonator == Resonator::string ? bridgeTo : 0.0f;
            const float total = std::max(2.0f, neckLength + bridgeLength);
            p.points = kShape;
            for (int i = 0; i < kShape; ++i)
            {
                const float along = total * (float) i / (float) (kShape - 1);
                p.shape[(std::size_t) i] = along < neckLength ? neck.at((int) (neckLength - along) + 1)
                                                              : bridge.at((int) (along - neckLength) + 1);
            }
            return;
        }
        p.modeCount = modeCount;
        for (int k = 0; k < modeCount; ++k)
        {
            const auto& m = modes[(std::size_t) k];
            const float s = std::max(1.0e-4f, m.sinTheta);
            const float level = std::sqrt(std::max(0.0f, m.y1 * m.y1 + m.y2 * m.y2 - m.a1 / std::max(1.0e-6f, m.r) * m.y1 * m.y2)) / s;
            p.modeRatio[(std::size_t) k] = m.ratio;
            p.modeLevel[(std::size_t) k] = level;
        }
    }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Shared& shared)
    {
        if (! active)
            return;
        if (fading == 0 && pendingNote >= 0)
        {
            const int n = pendingNote;
            pendingNote = -1;
            reset();
            begin(n, pendingVelocity, s, false);
            if (pendingReleased)
                release();
            pendingReleased = false;
        }
        control(s, shared, count);

        std::array<float, kControl> out {};
        if (isWaveguide(resonator))
            renderWaveguide(out.data(), count);
        else
            renderModes(out.data(), count);

        const float step = 1.0f / (float) count;
        const float fadeFrom = fading > 0 ? (float) fading / (float) kFadeSteps : 1.0f;
        const float fadeTo = fading > 0 ? (float) (fading - 1) / (float) kFadeSteps : 1.0f;
        float loudest = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            // A slow-moving bow or a breath carries a steady push that is not sound; the walls take it out.
            float x = out[(std::size_t) i];
            const float y = x - dcIn + dcCoefficient * dcOut;
            dcIn = x;
            dcOut = y;
            x = y;
            if (s.filterOn)
                x = filter(s.filterMode, x, t);
            x *= gain.at(t) * (fadeFrom + (fadeTo - fadeFrom) * t);
            loudest = std::max(loudest, std::abs(x));
            left[i] += x * panLeft;
            right[i] += x * panRight;
        }
        for (int i = 0; i < count; ++i)
            mod.next();
        if (fading > 0)
            --fading;
        first = false;

        // A voice ends once nothing drives it and it has rung down to silence for a while.
        peak = std::max(loudest, peak * 0.9f);
        const bool driven = excite.left > 0 || (isSustained(s.exciter) && exciterEnv.isActive());
        if (! driven && loudest < 2.0e-5f && fading == 0 && pendingNote < 0)
        {
            if (++quiet > (int) (0.05 * sampleRate / kControl))
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
        float a1 = 0.0f, r = 0.0f, a2 = 0.0f;
        float bStrike = 0.0f, bDrive = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
        float sinTheta = 0.0f;
        float ratio = 1.0f;
    };

    /** A pluck or strike in progress, and the steady push of a bow or breath driving a body it does not play by itself. */
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

    void begin(int newNote, float newVelocity, const Settings& s, bool glide)
    {
        const bool wasActive = active;
        note = newNote;
        velocity = newVelocity;
        held = true;
        sustained = false;
        if (! glide || ! wasActive)
            pitch = note;
        exciterEnv.set(s.env1);
        mod.set(s.env2);
        exciterEnv.noteOn();
        mod.noteOn();
        // Only a pluck or strike sets off a burst; a bow or breath keeps playing from where it is.
        strikePending = ! isSustained(s.exciter);
        if (! wasActive)
        {
            first = true;
            for (std::size_t i = 0; i < lfoPhase.size(); ++i)
            {
                lfoPhase[i] = 0.0;
                lfoHeld[i] = (float) random.next();
                lfoSmooth[i] = 0.0f;
            }
        }
        active = true;
        quiet = 0;
    }

    void route(std::array<float, (std::size_t) Target::count>& m, const Route& r, float value) const
    {
        if (r.target != Target::none)
            m[(std::size_t) r.target] += r.amount * value;
    }

    void control(const Settings& s, const Shared& shared, int count)
    {
        exciterEnv.set(s.env1);
        mod.set(s.env2);
        const double seconds = count / sampleRate;
        const auto& transport = shared.transport;

        std::array<float, (std::size_t) Target::count> m {};
        const float smoothing = first ? 0.0f : (float) std::exp(-seconds / 0.002);
        for (std::size_t i = 0; i < kLfos; ++i)
        {
            const auto& lfo = s.lfo[i];
            const double beats = tonwerkwave::divisionBeats(lfo.division);
            double now = lfoPhase[i];
            if (lfo.sync && transport.playing && transport.hasPosition)
            {
                now = transport.ppq / beats;
                now -= std::floor(now);
            }
            else
            {
                const double rate = lfo.sync ? transport.bpm / 60.0 / beats : (double) lfo.rate;
                now += rate * seconds;
                now -= std::floor(now);
            }
            if (now < lfoPhase[i])
                lfoHeld[i] = (float) random.next();
            lfoPhase[i] = now;
            const float value = lfo.shape == LfoShape::random ? lfoHeld[i]
                                                              : 2.0f * tonwerkwave::lfoShape(lfo.shape, now, 0.0f) - 1.0f;
            lfoSmooth[i] = value + (lfoSmooth[i] - value) * smoothing;
            route(m, lfo.route, lfoSmooth[i]);
        }
        route(m, s.env2Route, mod.current());
        route(m, s.wheel, shared.modWheel);
        route(m, s.velocity, velocity - 1.0f);
        auto at = [&m](Target t) { return m[(std::size_t) t]; };

        if (s.voiceMode == VoiceMode::mono && s.glide > 0.0f && ! first)
            pitch += (note - pitch) * (1.0 - std::exp(-seconds / (s.glide / 4.0)));
        else
            pitch = note;

        resonator = s.resonator;
        const double played = pitch + shared.bend + 12.0 * s.octave + s.semitones + s.fine / 100.0 + 12.0 * at(Target::pitch);
        const double highest = isWaveguide(resonator) ? sampleRate / 8.0 : sampleRate * 0.45;
        frequency = std::clamp(440.0 * std::exp2((played - 69.0) / 12.0), 16.0, highest);

        const float hardness = std::clamp(s.hardness + at(Target::hardness), 0.0f, 1.0f);
        const float pressure = std::clamp(s.pressure + at(Target::pressure), 0.0f, 1.0f);
        position = std::clamp(s.position + 0.5f * at(Target::position), 0.02f, 0.5f);
        double decay = std::clamp((double) s.decay * std::exp2(4.0 * at(Target::decay)), 0.01, 60.0);
        if (! held)
            decay = std::min(decay, (double) std::max(0.005f, s.release));
        const float damping = std::clamp(s.damping + at(Target::damping), 0.0f, 1.0f);

        // Soft notes quieter by up to 20 dB at full dynamics, on a curve so the middle of the keyboard's range matters.
        const float dynamic = std::pow(std::clamp(velocity, 0.01f, 1.0f), 1.6f * s.dynamics);
        const float amp = std::clamp(1.0f + at(Target::amp), 0.0f, 2.0f);

        const float pan = std::clamp((float) (note - 60) / 30.0f, -1.0f, 1.0f) * s.width;
        panLeft = std::sqrt(1.0f - pan);
        panRight = std::sqrt(1.0f + pan);

        // The noise's colour: a soft felt or plectrum lets through a few hundred hertz, a hard one everything.
        const double cutoffHz = std::min(150.0 * std::exp2(7.2 * hardness), 0.45 * sampleRate);
        excite.coefficient = (float) (1.0 - std::exp(-2.0 * kPi * cutoffHz / sampleRate));

        bowing = resonator == Resonator::string && s.exciter == Exciter::bow;
        reed = resonator == Resonator::closedTube && s.exciter == Exciter::blow;
        if (isWaveguide(resonator))
            tuneWaveguide(s, decay, damping);
        else
            tuneModes(s, decay, damping);

        // The bow's speed and force, or the breath on the reed; elsewhere the steady push of noise a bow or breath gives.
        bowSlope = 5.0f - 4.0f * pressure;
        driveScale = 0.03f + 0.2f * pressure;
        // The reed sounds only while breath times stiffness lies in a narrow window: below it the reed barely moves,
        // above it the breath presses it shut. So hardness sets the stiffness (a harder reed is brighter) and the
        // pressure where in that window the breath sits.
        reedSlope = -0.18f - 0.3f * hardness;
        breathScale = (0.2f + 0.11f * pressure) / -reedSlope;
        noiseShare = s.noise;
        excite.driveTo = isSustained(s.exciter) && ! bowing && ! reed ? pressure * driveNormal : 0.0f;
        // A stiffer reed swings less far; made up for, so hardness changes the colour and not the level.
        const float reedLevel = reed ? -reedSlope / 0.33f : 1.0f;
        gain.set(dynamic * amp * reedLevel * levelOf(resonator, s.exciter), first);

        if (strikePending)
        {
            strikePending = false;
            startBurst(s.exciter, hardness, s.noise);
        }

        // The filter, as on Tonwerk Granular: the cutoff in semitones, ten octaves at 100 % of a route.
        const double cutoffNote = 12.0 * std::log2(std::max(20.0f, s.cutoff) / 440.0) + 69.0
                                  + s.keytrack * (pitch - 60.0) + 120.0 * at(Target::cutoff);
        const double cutoff = std::clamp(440.0 * std::exp2((cutoffNote - 69.0) / 12.0), 20.0,
                                         std::min(20000.0, 0.45 * sampleRate));
        const float resonance = std::clamp(s.resonance + at(Target::resonance), 0.0f, 1.0f);
        filterG.set((float) std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
    }

    /**
     * The levels that make every body about as loud as the plucked string at the same strength: measured, for each
     * resonator and whether it is struck, bowed on the string, blown on the reed or driven by noise.
     */
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
        // A pluck lasts one period on a string or tube (Karplus and Strong's burst), a strike as long as the felt or
        // hammer touches: a quarter millisecond hard, six soft, but never so long that it smothers the note itself.
        const double period = sampleRate / frequency;
        double length = 0.0;
        if (exciter == Exciter::pluck && isWaveguide(resonator))
            length = std::clamp(period, 0.0005 * sampleRate, 0.03 * sampleRate);
        else if (exciter == Exciter::pluck)
            length = std::min((0.0002 + 0.002 * (1.0 - hardness)) * sampleRate, 0.6 * period);
        else
            length = std::min((0.00025 + 0.006 * (1.0 - hardness) * (1.0 - hardness)) * sampleRate, 0.6 * period);
        excite.length = std::max(2, (int) length);
        excite.left = excite.length;
        excite.pluck = exciter == Exciter::pluck;
        // A struck body answers a pulse by its area, a string by its height: so the pulse is scaled to one or the other.
        if (isWaveguide(resonator))
        {
            excite.smooth = 1.0f - noise;
            excite.noisy = noise;
        }
        else
        {
            excite.smooth = (1.0f - noise) * (float) (kPi / 2.0 / excite.length);
            excite.noisy = noise / std::sqrt((float) excite.length);
        }
    }

    /** The next sample of a pluck or strike, plus any noise a bow or breath pushes in, through the hardness lowpass. */
    float nextExcitation(float env)
    {
        float v = 0.0f;
        if (excite.left > 0)
        {
            const float phase = 1.0f - (float) excite.left / (float) excite.length;
            const float shape = std::sin((float) kPi * phase);
            // A pluck's noise is a flat burst with short edges; a strike's follows the felt's push.
            const float edge = std::min(1.0f, std::min(phase, 1.0f - phase) * (float) excite.length / 16.0f);
            const float envelope = excite.pluck ? edge : shape;
            v = excite.smooth * shape + excite.noisy * envelope * (float) random.next();
            --excite.left;
        }
        if (excite.drive > 0.0f)
            v += excite.drive * env * (float) random.next();
        excite.lowpass += (v - excite.lowpass) * excite.coefficient;
        // Its tail would sink into denormals, which every resonance would then carry.
        if (std::abs(excite.lowpass) < 1.0e-15f)
            excite.lowpass = 0.0f;
        return excite.lowpass;
    }

    void tuneWaveguide(const Settings& s, double decay, float damping)
    {
        const double period = sampleRate / frequency;
        const double w = 2.0 * kPi * frequency / sampleRate;
        const bool closed = resonator == Resonator::closedTube;
        // One trip round the loop: the whole period, or half of it in the closed tube, whose far end turns the wave over.
        const double pass = closed ? 0.5 * period : period;
        const double dc = decayGain(decay, pass, sampleRate);
        const double high = decayGain(decay * std::exp(-5.0 * damping), pass, sampleRate);
        const double ratio = high / dc;
        lossA = (float) ((1.0 - ratio) / (1.0 + ratio));
        lossB = (float) (dc * (1.0 - lossA));
        double length = pass - lossDelay(lossA, w);

        // The stiff string's stretch: four allpasses whose delay falls from the low partials to the high ones, so the
        // high ones come round sooner and sound sharp. Their pole sits at `bend` times the note's frequency: overtones
        // below it stay in tune, those above stretch, the more the closer it sits to the note (a few percent at the 8th
        // partial halfway up the knob). Eased off where the note is too short to hold them.
        double c = 0.0, stretch = 0.0;
        if (resonator == Resonator::string && s.inharmonicity > 0.005f)
        {
            double bend = 3.0 * std::exp2(4.0 * (1.0 - (double) s.inharmonicity));
            for (;;)
            {
                c = std::max(-0.995, -1.0 + bend * w);
                if (c > -0.005)
                    break;
                stretch = 4.0 * allpassDelay(c, w);
                if (stretch <= 0.5 * length)
                    break;
                bend *= 1.5;
            }
        }
        stiff = c < -0.005;
        if (! stiff)
        {
            c = 0.0;
            stretch = 0.0;
        }
        dispersion = (float) c;
        length = std::max(4.0, length - stretch);

        float n = 0.0f, b = 0.0f;
        if (resonator == Resonator::string)
        {
            n = std::max(2.0f, (float) (position * length));
            b = std::max(2.0f, (float) (length - n));
        }
        else
            n = std::max(2.0f, (float) length);
        neckFrom = first ? n : neckTo;
        bridgeFrom = first ? b : bridgeTo;
        neckTo = n;
        bridgeTo = b;
        // Noise into a loop that rings long builds up; scaled by what the loop lets through, it is as loud as a pluck.
        driveNormal = 4.0f * (float) std::sqrt(std::max(1.0e-6, 1.0 - dc * dc));
    }

    void tuneModes(const Settings& s, double decay, float damping)
    {
        const auto& table = modeTable(resonator);
        const float stretch = 1.0f + 0.5f * s.inharmonicity;
        modeCount = 0;
        for (int k = 0; k < table.count; ++k)
        {
            const float ratio = std::pow(table.ratio[(std::size_t) k], stretch);
            const double f = frequency * ratio;
            if (f >= 0.45 * sampleRate)
                continue;
            // Where it is struck: a bar in its middle loses the modes with a node there, a skin in its middle all but
            // the round ones; the bell and the bowl hardly mind.
            float weight = table.gain[(std::size_t) k];
            if (resonator == Resonator::bar)
                weight *= 0.2f + 0.8f * std::abs(std::sin((float) kPi * (float) (k + 1) * position));
            else if (resonator == Resonator::membrane)
            {
                const float edge = 2.0f * position;
                weight *= table.centred[(std::size_t) k] ? 1.0f - 0.5f * edge : edge;
            }
            else
                weight *= 0.6f + 0.4f * std::abs(std::sin((float) kPi * (float) (k + 1) * position));
            const double t60 = std::clamp(decay * std::pow((double) ratio, -1.5 * damping), 0.005, 120.0);
            const double r = std::exp(-6.907755 / (t60 * sampleRate));
            const double theta = 2.0 * kPi * f / sampleRate;
            auto& m = modes[(std::size_t) modeCount];
            m.ratio = ratio;
            m.r = (float) r;
            m.a1 = (float) (2.0 * r * std::cos(theta));
            m.a2 = (float) (r * r);
            m.sinTheta = (float) std::sin(theta);
            // A unit impulse rings at the mode's weight; steady noise at unit strength sustains it there too.
            m.bStrike = weight * m.sinTheta;
            m.bDrive = (float) (weight * m.sinTheta * std::sqrt(2.0 * (1.0 - r * r)));
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
                    // The bow sticks while the string moves with it and slips once it pulls away: the friction curve
                    // grips less the faster they part, and harder pressure makes it grip longer.
                    const float speed = env * driveScale * (1.0f + noiseShare * 0.3f * (float) random.next());
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
                    // The reed closes as the pressure across it rises: a valve between the mouth and the tube.
                    const float breath = env * breathScale * (1.0f + noiseShare * 0.25f * (float) random.next());
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
        // A strike is an impulse into each mode; steady noise from a bow or breath feeds them by their own measure.
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
            case FilterMode::low24: break;
        }
        return x;
    }

    static std::uint32_t nextSeed()
    {
        static std::uint32_t count = 0;
        return 0x2545f491u * (++count * 2u + 1u);
    }

    double sampleRate = 48000.0;
    int note = 60;
    double pitch = 60.0;
    double frequency = 261.6;
    float velocity = 1.0f;
    bool held = false;
    bool active = false;
    bool first = true;
    bool strikePending = false;
    int fading = 0;
    int pendingNote = -1;
    float pendingVelocity = 1.0f;
    bool pendingReleased = false;
    int quiet = 0;
    float peak = 0.0f;

    Resonator resonator = Resonator::string;
    float position = 0.2f;
    Adsr exciterEnv, mod;
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
    float panLeft = 1.0f, panRight = 1.0f;
    std::array<double, kLfos> lfoPhase {};
    std::array<float, kLfos> lfoHeld {};
    std::array<float, kLfos> lfoSmooth {};
    tonwerk::Noise random;
    std::array<SvfStage, 2> filterStages {};
    Ramp filterG, filterK;
};

/**
 * The body after the voices: four resonances of a guitar's air and top, a violin's two main modes and its bridge, a
 * wooden box, or a piano's soundboard, added to the voices' sum on each side.
 */
class BodyFilter
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& side : stages)
            for (auto& s : side)
                s.reset();
    }

    void process(float* left, float* right, int count, Body body, float mix)
    {
        if (body == Body::off || body == Body::count || mix <= 0.0f)
            return;
        struct Peak
        {
            float hz, q, gain;
        };
        static constexpr std::array<std::array<Peak, 4>, 4> peaks { {
            { { { 98.0f, 6.0f, 1.5f }, { 204.0f, 8.0f, 1.2f }, { 390.0f, 5.0f, 0.6f }, { 2400.0f, 1.5f, 0.3f } } },
            { { { 275.0f, 9.0f, 1.0f }, { 460.0f, 7.0f, 1.2f }, { 1000.0f, 3.0f, 0.4f }, { 2700.0f, 1.8f, 0.8f } } },
            { { { 120.0f, 5.0f, 1.4f }, { 420.0f, 4.0f, 0.8f }, { 1500.0f, 3.0f, 0.4f }, { 3500.0f, 2.0f, 0.2f } } },
            { { { 80.0f, 3.0f, 0.8f }, { 250.0f, 2.5f, 0.6f }, { 900.0f, 2.0f, 0.4f }, { 2500.0f, 1.5f, 0.3f } } },
        } };
        const auto& set = peaks[(std::size_t) body - 1];
        std::array<float, 4> g {}, k {}, a {}, weight {};
        for (std::size_t i = 0; i < 4; ++i)
        {
            g[i] = (float) std::tan(kPi * std::min((double) set[i].hz, 0.45 * sampleRate) / sampleRate);
            k[i] = 1.0f / set[i].q;
            a[i] = 1.0f / (1.0f + g[i] * (g[i] + k[i]));
            weight[i] = mix * set[i].gain * k[i];
        }
        float* sides[2] = { left, right };
        for (std::size_t c = 0; c < 2; ++c)
        {
            float* x = sides[c];
            if (c == 1 && right == left)
                break;
            for (int n = 0; n < count; ++n)
            {
                float add = 0.0f;
                for (std::size_t i = 0; i < 4; ++i)
                {
                    stages[c][i].process(x[n], g[i], k[i], a[i]);
                    add += weight[i] * stages[c][i].band;
                }
                x[n] += add;
            }
        }
    }

private:
    double sampleRate = 48000.0;
    std::array<std::array<SvfStage, 4>, 2> stages {};
};

/** The instrument without a host: voices, poly or mono with glide, the sustain pedal and the wheels. */
class Engine
{
public:
    static constexpr int kVoices = 12;

    void prepare(double rate)
    {
        shared.sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
        body.prepare(rate);
        held.clear();
        held.reserve(128);
        sustainPedal = false;
    }

    void noteOn(int note, float velocity, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && v.currentNote() == note)
                {
                    // The same key again sets the ringing string going once more, as on a real instrument.
                    v.start(note, velocity, s, true);
                    v.age = ++clock;
                    return;
                }
            auto& v = freeVoice();
            v.start(note, velocity, s, false);
            v.age = ++clock;
            return;
        }
        lastVelocity = velocity;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        held.push_back(note);
        for (std::size_t i = 1; i < voices.size(); ++i)
            if (voices[i].isActive() && ! voices[i].isReleasing())
                voices[i].release();
        voices[0].start(note, velocity, s, true);
        voices[0].age = ++clock;
    }

    void noteOff(int note, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && v.isHeld() && v.currentNote() == note)
                {
                    if (sustainPedal)
                        v.sustained = true;
                    else
                        v.release();
                }
            return;
        }
        const bool wasSounding = ! held.empty() && held.back() == note;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        if (! wasSounding)
            return;
        if (! held.empty())
            voices[0].start(held.back(), lastVelocity, s, true);
        else
            voices[0].release();
    }

    void setSustain(bool down)
    {
        sustainPedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained)
                {
                    v.sustained = false;
                    v.release();
                }
    }

    void allNotesOff()
    {
        held.clear();
        sustainPedal = false;
        for (auto& v : voices)
        {
            v.sustained = false;
            v.release();
        }
    }

    void setBend(double semitones) { shared.bend = semitones; }
    void setModWheel(float value) { shared.modWheel = value; }

    /** Adds `count` samples of all voices and the body to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Transport& transport)
    {
        int done = 0;
        while (done < count)
        {
            const int n = std::min(Voice::kControl, count - done);
            shared.transport = transport;
            shared.transport.ppq += (double) done / shared.sampleRate * transport.bpm / 60.0;
            for (auto& v : voices)
                v.render(left + done, right + done, n, s, shared);
            done += n;
        }
        body.process(left, right, count, s.body, s.bodyMix);
    }

    const Voice* newestVoice() const
    {
        const Voice* newest = nullptr;
        for (const auto& v : voices)
            if (v.isActive() && (newest == nullptr || v.age > newest->age))
                newest = &v;
        return newest;
    }

    int activeVoices() const
    {
        return (int) std::count_if(voices.begin(), voices.end(), [](const Voice& v) { return v.isActive(); });
    }

private:
    Voice& freeVoice()
    {
        for (auto& v : voices)
            if (! v.isActive())
                return v;
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isReleasing() && (best == nullptr || v.age < best->age))
                best = &v;
        if (best != nullptr)
            return *best;
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;
        return *best;
    }

    std::array<Voice, kVoices> voices;
    BodyFilter body;
    std::vector<int> held;
    Shared shared;
    bool sustainPedal = false;
    float lastVelocity = 1.0f;
    std::uint64_t clock = 0;
};
} // namespace tonwerkphys
