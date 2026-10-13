#pragma once

#include "WaveDsp.h"
#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Tonwerk PD: phase distortion the way Casio's CZ synths made it. A cosine is read through a bent phase: where the
 * phase runs fast the wave falls steeply, where it stands still the wave holds, and so a plain sine turns into a saw, a
 * square, a pulse or a resonant sweep as the bend (the DCW, digitally controlled wave) grows. No filter is needed: the
 * DCW envelope opens and closes the sound the way a filter envelope would. Two lines (each its own wave, DCW and DCA,
 * the digitally controlled amplifier), ring or noise modulation of the second by the first, and a chorus after them.
 * Plain C++; borrows `Adsr` and `lfoShape` from Tonwerk Wavetable's `WaveDsp.h`.
 */
namespace tonwerkpd
{
using tonwerkwave::Adsr;
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;

inline constexpr double kPi = 3.141592653589793;
inline constexpr int kLfos = 2;
inline constexpr int kLines = 2;

// The order of every enum below is the order of a parameter's menu, saved in Logic projects: append, never reorder.

/**
 * The CZ's eight waves, each a way of bending the phase: saw, square, pulse, double sine, saw-pulse and three
 * resonances (a cosine running faster than the note, under a falling, a triangular or a trapezoidal window).
 */
enum class Wave { saw, square, pulse, doubleSine, sawPulse, resonanceSaw, resonanceTriangle, resonanceTrapezoid, count };
/** Which lines sound: line 1, line 2, line 1 twice (the copy with line 2's detune, the CZ's 1+1') or both. */
enum class LineSelect { line1, line2, line1Doubled, both };
/** What line 1 does to line 2: nothing, ring modulation (the two multiplied), or line 2 read at a wandering phase. */
enum class Modulation { off, ring, noise };
enum class VoiceMode { poly, mono, legato };
enum class Target { none, pitch, dcw, dcw1, dcw2, pitch2, amp, balance, count };

inline bool isResonance(Wave w)
{
    return w == Wave::resonanceSaw || w == Wave::resonanceTriangle || w == Wave::resonanceTrapezoid;
}

/** A modulation source's fixed route: where it goes and how far, -1 to 1 of the target's range. */
struct Route
{
    Target target = Target::none;
    float amount = 0.0f;
};

struct LfoSettings
{
    LfoShape shape = LfoShape::triangle;
    bool sync = false;
    float rate = 5.0f;
    int division = 9;
    /** Seconds the LFO takes to fade in after a note starts, as the CZ's vibrato delay. */
    float fade = 0.0f;
    Route route;
};

/** One line: its wave (and a second it alternates with, cycle by cycle, as on the CZ), its DCW and its DCA. */
struct LineSettings
{
    Wave first = Wave::saw;
    /** Played every other cycle; off, the first plays every cycle. */
    bool alternate = false;
    Wave second = Wave::saw;
    /** How far the DCW bends the wave at its envelope's peak: 0 a plain sine, 1 the full wave. */
    float depth = 0.6f;
    /** How much less bend high notes get, as the CZ's key follow: at 1, two octaves above C3 have a quarter. */
    float keyFollow = 0.2f;
    float level = 0.8f;
    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;
    EnvSettings dcw { 0.005f, 0.6f, 0.35f, 0.3f };
    EnvSettings amp { 0.003f, 1.0f, 0.85f, 0.3f };
};

/** Everything a note plays from, read from the parameters each block. Times in seconds, shares 0 to 1. */
struct Settings
{
    LineSelect lines = LineSelect::line1;
    Modulation modulation = Modulation::off;
    std::array<LineSettings, kLines> line { LineSettings {},
                                            LineSettings { Wave::square, false, Wave::saw, 0.5f, 0.2f, 0.8f, 0, 0,
                                                           7.0f } };
    /** Semitones the pitch starts away from the note, and the seconds it takes to arrive (the CZ's DCO envelope). */
    float pitchEnvAmount = 0.0f;
    float pitchEnvTime = 0.1f;

    std::array<LfoSettings, kLfos> lfo { LfoSettings { LfoShape::triangle, false, 5.5f, 9, 0.4f, { Target::none, 0.0f } },
                                         LfoSettings { LfoShape::sine, true, 2.0f, 9, 0.0f, { Target::none, 0.0f } } };
    /** The wheel opens the sound, as a filter would. */
    Route wheel { Target::dcw, 0.5f };
    /** Velocity counts from 0 at a full strike down to -1 at the softest, so a positive amount makes soft notes less. */
    Route velocity { Target::dcw, 0.4f };
    /** How much quieter soft notes are: 0 all alike, 1 about 20 dB from the hardest to a soft one. */
    float dynamics = 0.5f;

    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f;
    int bendRange = 2;
    /** How far apart the two lines sit, line 1 left and line 2 right. */
    float width = 0.5f;

    float chorusMix = 0.0f;
    float chorusRate = 0.6f;
    float chorusDepth = 0.5f;
    float masterDb = 0.0f;
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

/** A cosine from a table: cos(2 pi `phase`), `phase` any real number. */
inline float cosine(double phase)
{
    static constexpr int kSize = 4096;
    static const std::array<float, kSize + 1> table = [] {
        std::array<float, kSize + 1> t {};
        for (int i = 0; i <= kSize; ++i)
            t[(std::size_t) i] = (float) std::cos(2.0 * kPi * i / kSize);
        return t;
    }();
    const double wrapped = phase - std::floor(phase);
    const double at = wrapped * kSize;
    const int i = std::min((int) at, kSize - 1);
    const float frac = (float) (at - i);
    return table[(std::size_t) i] + (table[(std::size_t) i + 1] - table[(std::size_t) i]) * frac;
}

/**
 * One sample of `wave` at `phase` (0 to 1) with the bend `d` (0 to 1). Every wave starts a cycle at 1 and is a plain
 * cosine at `d` 0 (a resonance is then one cycle of it under its window). `steepest` is the shortest stretch of the
 * cycle a fall may take (a phase share, about two samples): the full bend of a high note would otherwise fall within
 * a single sample and alias. `highest` caps a resonance's ratio below the Nyquist frequency.
 */
inline float pdWave(Wave wave, double phase, float d, double steepest, double highest = 64.0)
{
    const double p = phase;
    const double bend = std::clamp((double) d, 0.0, 1.0);
    switch (wave)
    {
        case Wave::saw:
        {
            // The first half of the cosine squeezed into the knee, the second half stretched over the rest.
            const double knee = std::max(steepest, 0.5 - 0.5 * bend);
            const double bent = p < knee ? 0.5 * p / knee : 0.5 + 0.5 * (p - knee) / (1.0 - knee);
            return cosine(bent);
        }
        case Wave::square:
        {
            // Each half falls (or rises) in its knee and then holds.
            const double half = p < 0.5 ? 2.0 * p : 2.0 * p - 1.0;
            const double knee = std::max(2.0 * steepest, 1.0 - bend);
            const double bent = std::min(1.0, half / knee);
            const float y = cosine(0.5 * bent);
            return p < 0.5 ? y : -y;
        }
        case Wave::pulse:
        {
            // The whole cycle run through in the knee, then held at the top: a narrowing pulse.
            const double knee = std::max(steepest, 1.0 - 0.95 * bend);
            return cosine(std::min(1.0, p / knee));
        }
        case Wave::doubleSine:
        {
            // Each half runs up to a whole cycle, the second turned over: the halves meet at a step the voice smooths.
            if (p < 0.5)
                return cosine(p + bend * p);
            const double q = p - 0.5;
            return -cosine(q + bend * q);
        }
        case Wave::sawPulse:
        {
            // A quick fall, a hold at the bottom, then the slow rise of the plain cosine's second half.
            const double knee = std::max(steepest, 0.5 - 0.5 * bend);
            const double bent = p < knee ? 0.5 * p / knee : p < 0.5 ? 0.5 : p;
            return cosine(bent);
        }
        case Wave::resonanceSaw:
        case Wave::resonanceTriangle:
        case Wave::resonanceTrapezoid:
        {
            // A cosine running 1 to 16 times the note, restarted each cycle under a window that falls to 0 at the
            // cycle's end, so the restart does not step: the sweep of a resonant filter, without one.
            const double ratio = std::min(highest, 1.0 + 15.0 * bend);
            double window = 1.0 - p;
            if (wave == Wave::resonanceTriangle)
                window = 1.0 - std::abs(2.0 * p - 1.0);
            else if (wave == Wave::resonanceTrapezoid)
                window = p < 0.5 ? 1.0 : 2.0 - 2.0 * p;
            return (float) (1.0 - window * (1.0 - cosine(ratio * p)));
        }
        case Wave::count:
            break;
    }
    return 0.0f;
}

/** Where the double sine ends its cycle; every other wave ends where it starts, at 1. */
inline float endOf(Wave wave, float d)
{
    return wave == Wave::doubleSine ? (float) std::cos(kPi * std::clamp(d, 0.0f, 1.0f)) : 1.0f;
}

/** Valimäki's polynomial step correction, around a step at phase 0 for an oscillator moving `dt` a sample. */
inline float polyBlep(double t, double dt)
{
    if (t < dt)
    {
        const double x = t / dt;
        return (float) (x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return (float) (x * x + x + x + 1.0);
    }
    return 0.0f;
}

class Voice
{
public:
    static constexpr int kControl = 32;

    Voice() : random(nextSeed()) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& o : oscs)
        {
            o.dcw.prepare(rate);
            o.amp.prepare(rate);
        }
        dcCoefficient = (float) (1.0 - 2.0 * kPi * 10.0 / rate);
        reset();
    }

    void reset()
    {
        for (auto& o : oscs)
        {
            o.dcw.reset();
            o.amp.reset();
            o.phase = 0.0;
            o.odd = false;
            o.playing = Wave::saw;
            o.previous = Wave::saw;
            o.previousEnd = 1.0f;
        }
        for (auto& d : dc)
            d = {};
        active = false;
        held = false;
        sustained = false;
        fading = 0;
        pendingNote = -1;
        quiet = 0;
        peak = 0.0f;
    }

    /** Starts `note`; a voice still sounding another note fades out first, unless it glides there. */
    void start(int newNote, float newVelocity, const Settings& s, bool glide, bool retrigger = true)
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
        begin(newNote, newVelocity, s, glide, retrigger);
    }

    void release()
    {
        held = false;
        if (fading > 0)
            pendingReleased = true;
        for (auto& o : oscs)
        {
            o.dcw.noteOff();
            o.amp.noteOff();
        }
    }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    bool isReleasing() const { return active && ! held; }
    int currentNote() const { return fading > 0 ? pendingNote : note; }

    bool sustained = false;
    std::uint64_t age = 0;

    /** What the display draws of this voice: each line's waves and how far they are bent right now. */
    struct Picture
    {
        std::array<bool, kLines> on {};
        std::array<Wave, kLines> first {};
        std::array<bool, kLines> alternate {};
        std::array<Wave, kLines> second {};
        std::array<float, kLines> bend {};
        std::array<float, kLines> level {};
        float frequency = 0.0f;
    };

    void draw(Picture& p) const
    {
        for (std::size_t i = 0; i < kLines; ++i)
        {
            const auto& o = oscs[i];
            p.on[i] = o.on;
            p.first[i] = o.first;
            p.alternate[i] = o.alternate;
            p.second[i] = o.second;
            p.bend[i] = o.shownBend;
            p.level[i] = o.amp.current();
        }
        p.frequency = (float) frequency;
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
            begin(n, pendingVelocity, s, false, true);
            if (pendingReleased)
                release();
            pendingReleased = false;
        }
        control(s, shared, count);

        const float step = 1.0f / (float) count;
        const float fadeFrom = fading > 0 ? (float) fading / (float) kFadeSteps : 1.0f;
        const float fadeTo = fading > 0 ? (float) (fading - 1) / (float) kFadeSteps : 1.0f;
        const bool ring = s.modulation == Modulation::ring && oscs[0].on && oscs[1].on;
        const bool noisy = s.modulation == Modulation::noise && oscs[1].on;
        float loudest = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            const float fade = fadeFrom + (fadeTo - fadeFrom) * t;
            std::array<float, kLines> out {};
            float raw0 = 0.0f;
            for (std::size_t k = 0; k < kLines; ++k)
            {
                auto& o = oscs[k];
                if (! o.on)
                    continue;
                const float env = o.dcw.next();
                const float level = o.amp.next();
                const float bend = std::clamp(o.bendBase.at(t) * env + o.bendOffset.at(t), 0.0f, 1.0f) * o.keyScale;
                double jitter = 0.0;
                if (k == 1 && noisy)
                {
                    // Line 2 read at a phase that wanders at random: its pitch the centre of a band of noise, its
                    // bend the band's brightness, as the CZ's noise.
                    noiseState += 0.4f * ((float) random.next() - noiseState);
                    jitter = 0.35 * noiseState;
                }
                float y = oscillate(o, bend, jitter);
                if (k == 0)
                    raw0 = y;
                if (k == 1 && ring)
                    y *= raw0;
                out[k] = y * level * o.gain.at(t);
            }
            // The pulse and a bent double sine carry a steady offset that is not sound.
            for (std::size_t k = 0; k < kLines; ++k)
            {
                const float x = out[k];
                const float y = x - dc[k].in + dcCoefficient * dc[k].out;
                dc[k].in = x;
                dc[k].out = y;
                out[k] = y * fade;
            }
            const float l = out[0] * oscs[0].panLeft + out[1] * oscs[1].panLeft;
            const float r = out[0] * oscs[0].panRight + out[1] * oscs[1].panRight;
            loudest = std::max({ loudest, std::abs(l), std::abs(r) });
            left[i] += l;
            right[i] += r;
        }
        if (fading > 0)
            --fading;
        first = false;

        // A voice ends once its envelopes have closed, or a released one has gone quiet for a while.
        peak = std::max(loudest, peak * 0.9f);
        bool sounding = false;
        for (const auto& o : oscs)
            sounding = sounding || (o.on && o.amp.isActive());
        if (! sounding && fading == 0 && pendingNote < 0)
            reset();
        else if (! held && loudest < 1.0e-5f && fading == 0 && pendingNote < 0)
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

    /** One sounding line of a voice. */
    struct Osc
    {
        bool on = false;
        Wave first = Wave::saw;
        bool alternate = false;
        Wave second = Wave::saw;
        Adsr dcw, amp;
        double phase = 0.0;
        double increment = 0.0;
        /** The cycle being played is the second wave's. */
        bool odd = false;
        Wave playing = Wave::saw;
        Wave previous = Wave::saw;
        float previousEnd = 1.0f;
        Ramp bendBase, bendOffset, gain;
        float keyScale = 1.0f;
        float shownBend = 0.0f;
        float lastBend = 0.0f;
        float panLeft = 1.0f, panRight = 1.0f;
    };

    struct DcBlocker
    {
        float in = 0.0f, out = 0.0f;
    };

    /** The next sample of `o`, read `jitter` of a cycle away from its phase (the noise modulation). */
    float oscillate(Osc& o, float bend, double jitter)
    {
        const double dt = o.increment;
        const double steepest = std::min(0.25, 2.0 * dt);
        const double highest = std::max(1.0, 0.45 / std::max(1.0e-9, dt));
        double read = o.phase + jitter;
        read -= std::floor(read);
        float y = pdWave(o.playing, read, bend, steepest, highest);
        // The double sine's steps, at the middle and the end of the cycle, rounded so they do not alias. The step
        // at the start belongs to the cycle just ended.
        if (o.playing == Wave::doubleSine)
        {
            const float jump = 1.0f - endOf(Wave::doubleSine, bend);
            double mid = o.phase + 0.5;
            mid -= std::floor(mid);
            y -= 0.5f * jump * polyBlep(mid, dt);
            if (o.phase > 1.0 - dt)
                y += 0.5f * jump * polyBlep(o.phase, dt);
        }
        if (o.phase < dt && o.previousEnd < 1.0f)
            y += 0.5f * (1.0f - o.previousEnd) * polyBlep(o.phase, dt);
        o.lastBend = bend;

        o.phase += dt;
        if (o.phase >= 1.0)
        {
            o.phase -= std::floor(o.phase);
            o.previous = o.playing;
            o.previousEnd = endOf(o.playing, bend);
            o.odd = o.alternate && ! o.odd;
            o.playing = o.odd ? o.second : o.first;
        }
        return y;
    }

    void begin(int newNote, float newVelocity, const Settings& s, bool glide, bool retrigger)
    {
        const bool wasActive = active;
        note = newNote;
        velocity = newVelocity;
        held = true;
        sustained = false;
        if (! glide || ! wasActive)
            pitch = note;
        if (retrigger || ! wasActive)
        {
            assign(s);
            for (auto& o : oscs)
            {
                o.dcw.noteOn();
                o.amp.noteOn();
            }
            pitchEnvLevel = 1.0f;
        }
        if (! wasActive)
        {
            first = true;
            lfoFade = 0.0f;
            for (std::size_t i = 0; i < lfoPhase.size(); ++i)
            {
                lfoPhase[i] = 0.0;
                lfoHeld[i] = (float) random.next();
                lfoSmooth[i] = 0.0f;
            }
            for (auto& o : oscs)
            {
                // Every line starts at the top of its cosine, as the CZ's do, so a note begins the same each time.
                o.phase = 0.0;
                o.odd = false;
                o.playing = o.first;
                o.previousEnd = 1.0f;
            }
        }
        active = true;
        quiet = 0;
    }

    /** Which lines play and from which settings: line 2's waves and envelopes, or line 1's again for 1+1'. */
    const LineSettings& sourceOf(std::size_t k, const Settings& s) const
    {
        return k == 1 && s.lines == LineSelect::line1Doubled ? s.line[0] : s.line[k];
    }

    void assign(const Settings& s)
    {
        oscs[0].on = s.lines != LineSelect::line2;
        oscs[1].on = s.lines != LineSelect::line1;
        for (std::size_t k = 0; k < kLines; ++k)
        {
            const auto& line = sourceOf(k, s);
            auto& o = oscs[k];
            o.dcw.set(line.dcw);
            o.amp.set(line.amp);
            o.first = line.first;
            o.alternate = line.alternate;
            o.second = line.second;
        }
    }

    void route(std::array<float, (std::size_t) Target::count>& m, const Route& r, float value) const
    {
        if (r.target != Target::none)
            m[(std::size_t) r.target] += r.amount * value;
    }

    void control(const Settings& s, const Shared& shared, int count)
    {
        const double seconds = count / sampleRate;
        const auto& transport = shared.transport;

        std::array<float, (std::size_t) Target::count> m {};
        const float smoothing = first ? 0.0f : (float) std::exp(-seconds / 0.002);
        // Both LFOs fade in together, over the first's time: the CZ's vibrato delay.
        lfoFade = s.lfo[0].fade <= 0.0f ? 1.0f : std::min(1.0f, lfoFade + (float) (seconds / s.lfo[0].fade));
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
            route(m, lfo.route, lfoSmooth[i] * (i == 0 ? lfoFade : 1.0f));
        }
        route(m, s.wheel, shared.modWheel);
        route(m, s.velocity, velocity - 1.0f);
        auto at = [&m](Target t) { return m[(std::size_t) t]; };

        if (s.voiceMode != VoiceMode::poly && s.glide > 0.0f && ! first)
            pitch += (note - pitch) * (1.0 - std::exp(-seconds / (s.glide / 4.0)));
        else
            pitch = note;
        // The pitch envelope: from its amount to the note, most of the way in its time.
        pitchEnvLevel *= (float) std::exp(-4.6 * seconds / std::max(0.001, (double) s.pitchEnvTime));

        const double played = pitch + shared.bend + 12.0 * at(Target::pitch) + s.pitchEnvAmount * pitchEnvLevel;
        frequency = 440.0 * std::exp2((played - 69.0) / 12.0);

        // Soft notes quieter by up to 20 dB at full dynamics, on a curve so the middle of the keyboard's range matters.
        const float dynamic = std::pow(std::clamp(velocity, 0.01f, 1.0f), 1.6f * s.dynamics);
        const float amp = std::clamp(1.0f + at(Target::amp), 0.0f, 2.0f);
        const float balance = std::clamp(at(Target::balance), -1.0f, 1.0f);
        const bool both = oscs[0].on && oscs[1].on;

        for (std::size_t k = 0; k < kLines; ++k)
        {
            auto& o = oscs[k];
            const auto& line = sourceOf(k, s);
            // Line 2's tuning applies to the copy in 1+1' too: that is what sets it apart.
            const auto& tuning = s.line[k];
            o.dcw.set(line.dcw);
            o.amp.set(line.amp);
            o.first = line.first;
            o.alternate = line.alternate;
            o.second = line.second;
            if (! o.alternate)
                o.odd = false;

            double notePitch = played + 12.0 * tuning.octave;
            if (k == 1)
                notePitch += tuning.semitones + tuning.fine / 100.0 + 12.0 * at(Target::pitch2);
            const double hz = std::clamp(440.0 * std::exp2((notePitch - 69.0) / 12.0), 8.0, sampleRate * 0.45);
            o.increment = hz / sampleRate;

            const float targetBend = std::clamp(line.depth, 0.0f, 1.0f);
            const float offset = at(Target::dcw) + at(k == 0 ? Target::dcw1 : Target::dcw2);
            o.bendBase.set(targetBend, first);
            o.bendOffset.set(offset, first);
            o.keyScale = std::min(1.0f, std::exp2(-2.0f * line.keyFollow * (float) std::max(0.0, notePitch - 48.0) / 24.0f));
            o.shownBend = o.lastBend;

            // Each line a quarter of full scale: a chord of both lines stays under it at 0 dB master.
            const float share = k == 0 ? 1.0f - std::max(0.0f, balance) : 1.0f + std::min(0.0f, balance);
            o.gain.set(0.25f * line.level * dynamic * amp * share, first);
            const float pan = both ? (k == 0 ? -s.width : s.width) : 0.0f;
            o.panLeft = std::sqrt(1.0f - pan);
            o.panRight = std::sqrt(1.0f + pan);
        }
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
    int fading = 0;
    int pendingNote = -1;
    float pendingVelocity = 1.0f;
    bool pendingReleased = false;
    int quiet = 0;
    float peak = 0.0f;
    float pitchEnvLevel = 0.0f;
    float lfoFade = 0.0f;
    float noiseState = 0.0f;

    std::array<Osc, kLines> oscs {};
    std::array<DcBlocker, kLines> dc {};
    float dcCoefficient = 0.999f;
    std::array<double, kLfos> lfoPhase {};
    std::array<float, kLfos> lfoHeld {};
    std::array<float, kLfos> lfoSmooth {};
    tonwerk::Noise random;
};

/**
 * A stereo chorus like the one the CZ sounds were always heard through: each side a short delay swept by a slow sine,
 * the right a quarter turn behind the left, added to the dry signal.
 */
class Chorus
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        std::size_t n = 1;
        while ((double) n < rate * 0.05)
            n <<= 1;
        for (auto& b : buffers)
            b.assign(n, 0.0f);
        mask = n - 1;
        write = 0;
        phase = 0.0;
    }

    void process(float* left, float* right, int count, float mix, float rate, float depth)
    {
        if (mix <= 0.0f)
        {
            // Kept filled, so turning it up does not start from a gap.
            for (int i = 0; i < count; ++i)
            {
                buffers[0][write] = left[i];
                buffers[1][write] = right[i];
                write = (write + 1) & mask;
            }
            return;
        }
        const double base = 0.012 * sampleRate;
        const double sweep = 0.006 * sampleRate * std::clamp(depth, 0.0f, 1.0f);
        const double increment = std::max(0.01f, rate) / sampleRate;
        const float wet = mix;
        const float scale = 1.0f / (1.0f + 0.5f * mix);
        for (int i = 0; i < count; ++i)
        {
            buffers[0][write] = left[i];
            buffers[1][write] = right[i];
            const double delays[2] = { base + sweep * std::sin(2.0 * kPi * phase),
                                       base + sweep * std::sin(2.0 * kPi * (phase + 0.25)) };
            float* sides[2] = { left, right };
            for (std::size_t c = 0; c < 2; ++c)
            {
                const double back = (double) write - delays[c];
                const double floored = std::floor(back);
                const float frac = (float) (back - floored);
                const auto a = (std::size_t) ((long long) floored & (long long) mask);
                const auto b = (a + 1) & mask;
                const float delayed = buffers[c][a] + (buffers[c][b] - buffers[c][a]) * frac;
                sides[c][i] = (sides[c][i] + wet * delayed) * scale;
            }
            write = (write + 1) & mask;
            phase += increment;
            phase -= std::floor(phase);
        }
    }

private:
    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> buffers;
    std::size_t mask = 0, write = 0;
    double phase = 0.0;
};

/** The instrument without a host: voices, poly, mono or legato with glide, the sustain pedal and the wheels. */
class Engine
{
public:
    static constexpr int kVoices = 16;

    void prepare(double rate)
    {
        shared.sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
        chorus.prepare(rate);
        held.clear();
        held.reserve(128);
        sustainPedal = false;
    }

    void noteOn(int note, float velocity, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            auto& v = freeVoice(note);
            v.start(note, velocity, s, false);
            v.age = ++clock;
            return;
        }
        const bool legato = s.voiceMode == VoiceMode::legato && ! held.empty();
        lastVelocity = velocity;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        held.push_back(note);
        for (std::size_t i = 1; i < voices.size(); ++i)
            if (voices[i].isActive() && ! voices[i].isReleasing())
                voices[i].release();
        voices[0].start(note, velocity, s, true, ! legato);
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
            voices[0].start(held.back(), lastVelocity, s, true, s.voiceMode == VoiceMode::mono);
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

    /** Adds `count` samples of all voices, through the chorus, to `left` and `right`. */
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
        chorus.process(left, right, count, s.chorusMix, s.chorusRate, s.chorusDepth);
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
    /** A voice for `note`: one already releasing it (a repeated key), a free one, the oldest releasing, the oldest. */
    Voice& freeVoice(int note)
    {
        for (auto& v : voices)
            if (v.isReleasing() && v.currentNote() == note)
                return v;
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
    Chorus chorus;
    std::vector<int> held;
    Shared shared;
    bool sustainPedal = false;
    float lastVelocity = 1.0f;
    std::uint64_t clock = 0;
};
} // namespace tonwerkpd
