#pragma once

#include "WaveDsp.h"
#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tonwerkbass
{
using tonwerkwave::LfoShape;

inline constexpr double kPi = 3.141592653589793;
/** Notes held at once that a released note can slide back to, newest last. */
inline constexpr int kHeld = 16;

enum class Wave { saw, square };
enum class LfoTarget { cutoff, pitch, volume };

/**
 * One sound of Tonwerk Bass, in the parameters' units except times, which are seconds: the TB-303's six knobs (tune,
 * cutoff, resonance, env mod, decay, accent) and its wave switch, then what a 303 lacks (sub, keytracking, accent
 * threshold and sweep, slide time, amp decay, drive, an LFO).
 */
struct Settings
{
    Wave wave = Wave::saw;
    int tune = 0;          // semitones
    float fine = 0.0f;     // cents
    float sub = 0.0f;      // a square an octave down, 0 to 1

    float cutoff = 500.0f; // Hz, before the envelope opens it
    float resonance = 0.6f;
    float envMod = 0.5f;   // 1 sweeps the filter 5 octaves up
    float decay = 0.6f;    // the filter envelope's fall, s
    float keytrack = 0.0f;

    float accent = 0.5f;
    int accentThreshold = 110; // velocities from here on are accented
    float sweep = 0.5f;        // how much accents in a row pile up

    float slide = 0.06f;    // s
    float ampDecay = 3.0f;  // s, the volume's fall while the key is held
    float release = 0.01f;  // s
    float drive = 0.0f;

    LfoShape lfoShape = LfoShape::triangle;
    bool lfoSync = false;
    float lfoRate = 2.0f;
    int lfoDivision = tonwerkwave::kEighth;
    LfoTarget lfoTarget = LfoTarget::cutoff;
    float lfoAmount = 0.0f;

    int bendRange = 2;
    float masterDb = -3.0f;
};

struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/**
 * Zavalishin's zero-delay-feedback ladder, four one-pole stages with the resonance fed back around them: solved
 * linearly for the feedback, then the stages' input bent by tanh, so a high resonance squeals and settles instead of
 * blowing up. The 303's own filter is a diode ladder with a softer slope; the squelch it is known for comes from the
 * resonance peak riding the envelope, which this one has too. The bass the resonance takes away is partly given back,
 * though less than all of it: a 303 thins out as it squeals.
 */
class Ladder
{
public:
    void reset() { s = {}; }

    float process(float x, float g, float k)
    {
        const float G = g / (1.0f + g);
        const float b = 1.0f / (1.0f + g);
        const float G2 = G * G;
        const float S = G2 * G * b * s[0] + G2 * b * s[1] + G * b * s[2] + b * s[3];
        const float y4 = (G2 * G2 * x + S) / (1.0f + k * G2 * G2);
        float u = std::tanh(x - k * y4);
        for (auto& state : s)
        {
            const float v = (u - state) * G;
            const float y = v + state;
            state = y + v;
            u = y;
        }
        return u * (1.0f + 0.5f * k);
    }

private:
    std::array<float, 4> s {};
};

/**
 * The bass: one voice, as on the 303. A key pressed while another is held slides to its pitch without starting the
 * envelopes again; a velocity at or over the threshold is an accent, louder, with a short, harder filter sweep that
 * piles up over accents in a row (the "wow" of a 303 line). Everything mono; the processor adds delay and hall.
 */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        attackStep = (float) (1.0 / (0.003 * rate));
        dcCoefficient = (float) std::exp(-2.0 * kPi * 15.0 / rate);
        reset();
    }

    void reset()
    {
        heldCount = 0;
        gate = false;
        amp = filterEnv = accentCharge = 0.0f;
        attacking = false;
        ladder.reset();
        dcIn = dcOut = 0.0f;
        level = 1.0f;
        lfoPhase = 0.0;
        lfoHeld = 0.5f;
    }

    void noteOn(int note, float velocity, const Settings& s)
    {
        const bool slid = heldCount > 0 && gate;
        remove(note);
        if (heldCount < kHeld)
            held[(std::size_t) heldCount++] = note;
        accented = rounded(velocity * 127.0f) >= s.accentThreshold;
        target = note;
        if (! slid)
        {
            // Fresh: from the note's own pitch, both envelopes from the top.
            pitch = note;
            filterEnv = 1.0f;
            attacking = true;
            gate = true;
        }
        sliding = slid;
    }

    void noteOff(int note)
    {
        remove(note);
        if (heldCount > 0)
        {
            // Back to the newest key still held, sliding as a 303 slides into a tied note.
            target = held[(std::size_t) heldCount - 1];
            sliding = true;
        }
        else
            gate = false;
    }

    void allNotesOff()
    {
        heldCount = 0;
        gate = false;
    }

    void setBend(double semitones) { bend = semitones; }

    /** Adds the bass to `out` (mono). */
    void render(float* out, int count, const Settings& s, const Transport& transport)
    {
        const float slideCoefficient = coefficient(std::max(0.001f, s.slide) / 3.0f);
        const float decayCoefficient = coefficient((accented ? 0.2f : std::max(0.03f, s.decay)) / 2.3f);
        const float ampCoefficient = coefficient(std::max(0.05f, s.ampDecay) / 6.9f);
        const float releaseCoefficient = coefficient(std::max(0.002f, s.release) / 4.6f);
        // Accents charge the sweep fast and let go slowly; with more sweep, slower both ways, so a run of them
        // climbs higher with each one instead of starting over.
        const float chargeCoefficient = coefficient(0.002f + 0.03f * s.sweep);
        const float dischargeCoefficient = coefficient(0.03f + 0.5f * s.sweep);
        const float levelCoefficient = coefficient(0.002f);
        const float k = 4.0f * std::pow(std::clamp(s.resonance, 0.0f, 1.0f), 0.7f) * 0.98f;
        const float accentLevel = accented ? 1.0f + 0.7f * s.accent : 1.0f;
        const float tuning = (float) s.tune + s.fine / 100.0f;
        const float drive = std::pow(10.0f, 1.5f * s.drive);
        const float driveMakeup = 1.0f / (1.0f + 2.0f * s.drive);
        const double nyquistGuard = 0.45 * sampleRate;

        // The LFO, synced to the bar while the host plays.
        const double beats = tonwerkwave::divisionBeats(s.lfoDivision);
        const double lfoStep = (s.lfoSync ? transport.bpm / 60.0 / beats : (double) s.lfoRate) / sampleRate;
        if (s.lfoSync && transport.playing && transport.hasPosition)
            lfoPhase = transport.ppq / beats - std::floor(transport.ppq / beats);

        for (int i = 0; i < count; ++i)
        {
            const double before = lfoPhase;
            lfoPhase += lfoStep;
            if (lfoPhase >= 1.0)
                lfoPhase -= std::floor(lfoPhase);
            if (lfoPhase < before)
                lfoHeld = random.nextFloat();
            const float lfo = tonwerkwave::lfoShape(s.lfoShape, lfoPhase, lfoHeld) * s.lfoAmount;

            if (sliding)
                pitch += (target - pitch) * (1.0 - slideCoefficient);
            else
                pitch = target;

            // The volume: 3 ms up, a long fall while held, the release once let go.
            if (! gate)
            {
                amp *= releaseCoefficient;
                attacking = false;
            }
            else if (attacking)
            {
                amp += attackStep;
                if (amp >= 1.0f)
                {
                    amp = 1.0f;
                    attacking = false;
                }
            }
            else
                amp *= ampCoefficient;
            filterEnv *= decayCoefficient;
            const float accentInput = accented ? filterEnv : 0.0f;
            accentCharge += (accentInput - accentCharge)
                            * (1.0f - (accentInput > accentCharge ? chargeCoefficient : dischargeCoefficient));
            level += (accentLevel - level) * (1.0f - levelCoefficient);

            double notePitch = pitch + tuning + bend;
            if (s.lfoTarget == LfoTarget::pitch)
                notePitch += 2.0 * (2.0 * lfo - s.lfoAmount);
            const double frequency = 440.0 * std::pow(2.0, (notePitch - 69.0) / 12.0);

            // The oscillator, band-limited, and the sub an octave down.
            const double dt = std::min(0.5, frequency / sampleRate);
            phase += dt;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                subPhase = subPhase < 0.5 ? 0.5 : 0.0;
            }
            float osc;
            if (s.wave == Wave::saw)
                osc = (float) (2.0 * phase - 1.0 - tonwerk::polyBlep(phase, dt));
            else
            {
                double fall = phase + 0.5;
                if (fall >= 1.0)
                    fall -= 1.0;
                osc = (float) ((phase < 0.5 ? 1.0 : -1.0) + tonwerk::polyBlep(phase, dt) - tonwerk::polyBlep(fall, dt));
            }
            if (s.sub > 0.0f)
            {
                const double subPosition = subPhase + 0.5 * phase;
                const double subDt = 0.5 * dt;
                double subFall = subPosition + 0.5;
                if (subFall >= 1.0)
                    subFall -= 1.0;
                const double sub = (subPosition < 0.5 ? 1.0 : -1.0) + tonwerk::polyBlep(subPosition, subDt)
                                   - tonwerk::polyBlep(subFall, subDt);
                osc += s.sub * (float) sub;
            }

            float octaves = s.envMod * 5.0f * filterEnv + s.accent * 4.0f * accentCharge
                            + s.keytrack * (float) (notePitch - 48.0) / 12.0f;
            if (s.lfoTarget == LfoTarget::cutoff)
                octaves += 4.0f * lfo;
            const double cutoff = std::clamp((double) s.cutoff * std::exp2((double) octaves), 20.0, nyquistGuard);
            const auto g = (float) std::tan(kPi * cutoff / sampleRate);
            float y = ladder.process(0.6f * osc, g, k);

            if (s.drive > 0.0f)
                y = std::tanh(drive * y) * driveMakeup;
            float gain = amp * level;
            if (s.lfoTarget == LfoTarget::volume)
                gain *= 1.0f - lfo;
            y *= gain;

            // No DC into the effects: the square's sub and the tanh both lean.
            const float blocked = y - dcIn + dcCoefficient * dcOut;
            dcIn = y;
            dcOut = blocked;
            out[i] += blocked;
        }
        if (! gate && amp < 1.0e-5f)
            amp = 0.0f;
    }

    /** What the display shows: the note sounding (-1 none), whether it is accented or sliding. */
    int sounding() const { return gate || amp > 1.0e-3f ? rounded((float) target) : -1; }
    bool isAccented() const { return accented; }
    bool isSliding() const { return sliding && std::abs(target - pitch) > 0.05; }

private:
    static int rounded(float value) { return (int) std::lround(value); }

    float coefficient(float seconds) const { return (float) std::exp(-1.0 / (seconds * sampleRate)); }

    void remove(int note)
    {
        const auto end = held.begin() + heldCount;
        const auto at = std::remove(held.begin(), end, note);
        heldCount = (int) std::distance(held.begin(), at);
    }

    struct Random
    {
        std::uint32_t state = 0x2545f491u;
        float nextFloat()
        {
            state = state * 1664525u + 1013904223u;
            return (float) (state >> 8) / 16777216.0f;
        }
    };

    double sampleRate = 48000.0;
    std::array<int, kHeld> held {};
    int heldCount = 0;
    bool gate = false, attacking = false, accented = false, sliding = false;
    double target = 36.0, pitch = 36.0, bend = 0.0;
    double phase = 0.0, subPhase = 0.0, lfoPhase = 0.0;
    float lfoHeld = 0.5f;
    float amp = 0.0f, filterEnv = 0.0f, accentCharge = 0.0f, level = 1.0f;
    float attackStep = 0.007f, dcCoefficient = 0.998f, dcIn = 0.0f, dcOut = 0.0f;
    Ladder ladder;
    Random random;
};
} // namespace tonwerkbass
