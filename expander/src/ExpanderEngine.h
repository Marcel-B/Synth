#pragma once

#include "WaveDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Tonwerk Expander's sound, plain C++: a polyphonic synth in the manner of the classic two-oscillator expander
 * modules of the seventies (nothing copied from any of them). Per voice two oscillators with saw and pulse, the second
 * hard synced to the first if wanted and swept by the filter envelope, into a 12 dB state variable filter whose mode
 * fades from lowpass through notch to highpass, with a bandpass switch. Eight voices (poly, mono or all eight in
 * unison), each a little out of tune and out of cutoff with the others by `drift`, as separate analog modules are.
 * Borrows `Adsr`, `lfoShape`, `softClip` and the divisions from Tonwerk Wavetable's `WaveDsp.h`.
 */
namespace tonwerkexpander
{
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;

inline constexpr double kPi = 3.141592653589793;
inline constexpr int kVoices = 8;
inline constexpr int kHeld = 16;

/** The oscillator's waves; the order of the menu, saved in projects: append only. */
enum class Wave
{
    saw,
    pulse,
    both,
};
inline constexpr std::array<const char*, 3> kWaveNames { "Säge", "Puls", "Säge + Puls" };

/** The voice modes; the order of the menu, saved in projects: append only. */
enum class VoiceMode
{
    poly,
    mono,
    unison,
};
inline constexpr std::array<const char*, 3> kModeNames { "Poly", "Mono", "Unison" };

struct OscSettings
{
    Wave wave = Wave::saw;
    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;         // cents
    float width = 0.5f;        // the pulse's duty cycle, 0.05 to 0.95
    float level = 0.8f;
};

struct Settings
{
    OscSettings osc1 {};
    OscSettings osc2 { Wave::saw, 0, 0, 7.0f, 0.5f, 0.6f };
    bool sync = false;         // oscillator 2 restarts with each cycle of oscillator 1
    float envPitch = 0.0f;     // semitones the filter envelope moves oscillator 2

    float cutoff = 1200.0f;    // Hz
    float resonance = 0.2f;
    float mode = 0.0f;         // 0 lowpass, 0.5 notch, 1 highpass
    bool bandpass = false;     // overrides the mode
    float envCutoff = 0.4f;    // -1 to 1, times 6 octaves
    float keytrack = 0.5f;     // 1: the cutoff follows the key one to one
    float lfoCutoff = 0.0f;    // 0 to 1, times 3 octaves either way

    EnvSettings filterEnv { 0.005f, 0.4f, 0.3f, 0.4f };
    EnvSettings ampEnv { 0.005f, 0.3f, 0.8f, 0.3f };

    LfoShape lfoShape = LfoShape::triangle;
    bool lfoSync = false;
    float lfoRate = 5.0f;      // Hz
    int lfoDivision = 9;       // 1/8
    float lfoDelay = 0.0f;     // s until the LFO has faded in after a key
    float lfoPitch = 0.0f;     // cents either way, both oscillators
    float lfoPwm = 0.0f;       // 0 to 1 of the pulse width's room

    VoiceMode voiceMode = VoiceMode::poly;
    float unisonDetune = 15.0f; // cents between the outermost unison voices
    float drift = 0.3f;        // how far the voices differ from each other
    float spread = 0.5f;       // how far the voices sit apart left and right
    float glide = 0.0f;        // s
    float velocity = 0.5f;     // how much the key's velocity moves level and cutoff
    int bendRange = 2;
    float masterDb = -6.0f;
};

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
    /** The LFO, -1 to 1. */
    float lfo = 0.0f;
};

/**
 * Two of a voice's state variable filter's states in one: Zavalishin's topology-preserving SVF, whose band state is
 * held by a soft clip, so full resonance on a loud chord warms and stays bounded instead of growing, as the analog
 * filter's transistors do.
 */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float low = 0.0f, band = 0.0f, high = 0.0f;

    void process(float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = kLimit * tonwerkwave::softClip((2.0f * v1 - ic1) / kLimit);
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = low = band = high = 0.0f; }

    static constexpr float kLimit = 1.5f;
};

/** The filter's damping at `resonance` 0 to 1: from a flat 12 dB slope to a peak of about 20 dB, short of ringing. */
inline float dampingFor(float resonance) { return 2.0f - 1.9f * std::clamp(resonance, 0.0f, 1.0f); }

/** Lowpass and highpass weighted by the mode, scaled so the louder of them is whole: notch in the middle. */
inline std::array<float, 2> modeWeights(float mode)
{
    const float m = std::clamp(mode, 0.0f, 1.0f);
    const float low = 1.0f - m, high = m;
    const float scale = 1.0f / std::max(low, high);
    return { low * scale, high * scale };
}

/**
 * An oscillator of saw and pulse whose steps are smoothed by a polyBLEP placed at the step's exact time, its output a
 * sample late so a step can still correct the sample before it. That lets a hard sync's reset, which the master's
 * phase announces only as it happens, be smoothed as cleanly as the oscillator's own wrap.
 */
class Oscillator
{
public:
    void reset(double startPhase)
    {
        phase = startPhase;
        held = 0.0f;
    }

    /**
     * Moves on by `dt` (cycles per sample) and returns the previous sample. `syncAt` >= 0: the master wrapped that
     * share of the sample after the last one, and this oscillator restarts there.
     */
    float next(double dt, float width, float sawMix, float pulseMix, double syncAt = -1.0)
    {
        float now = 0.0f;
        double t = 0.0;
        if (syncAt >= 0.0)
        {
            advance(t, syncAt, dt, width, sawMix, pulseMix, now);
            // Both waves jump back to the start of their cycle: the saw from where it was to -1, the pulse to 1.
            const float pulseBefore = phase < width ? 1.0f : -1.0f;
            step(sawMix * 2.0f * (float) phase + pulseMix * (pulseBefore - 1.0f), syncAt, now);
            phase = 0.0;
        }
        advance(t, 1.0, dt, width, sawMix, pulseMix, now);

        // The pulse's own mean taken out, so a narrow pulse carries no offset into the filter.
        const float saw = 2.0f * (float) phase - 1.0f;
        const float pulse = (phase < width ? 1.0f : -1.0f) - (2.0f * width - 1.0f);
        now += sawMix * saw + pulseMix * pulse;
        const float out = held;
        held = now;
        return out;
    }

    /** Where the wrap fell in the last `next`: >= 0 the share of the sample after the previous one, -1 none. */
    static double wrapOf(double phaseBefore, double dt)
    {
        const double p = phaseBefore + dt;
        return p >= 1.0 ? 1.0 - (p - 1.0) / dt : -1.0;
    }

    double phase = 0.0;

private:
    /** Runs the phase on from `t` to `stop` (shares of the sample), smoothing the pulse's fall and the wrap on the way. */
    void advance(double& t, double stop, double dt, float width, float sawMix, float pulseMix, float& now)
    {
        for (;;)
        {
            const double target = phase < width ? (double) width : 1.0;
            const double at = t + (target - phase) / dt;
            if (at > stop)
                break;
            t = at;
            if (target < 1.0)
            {
                step(2.0f * pulseMix, at, now);
                phase = target;
            }
            else
            {
                step(2.0f * sawMix - 2.0f * pulseMix, at, now);
                phase = 0.0;
            }
        }
        phase += (stop - t) * dt;
        t = stop;
    }

    /** A step down by `height` at `at` (0 to 1, the share of the sample after the previous one). */
    void step(float height, double at, float& now)
    {
        const auto t = (float) std::clamp(at, 0.0, 1.0);
        const float half = 0.5f * height;
        held -= half * (1.0f - t) * (1.0f - t);
        now += half * t * t;
    }

    float held = 0.0f;
};

class Voice
{
public:
    static constexpr int kControl = 16;

    void prepare(double rate, int index)
    {
        sampleRate = rate;
        filterEnv.prepare(rate);
        ampEnv.prepare(rate);
        // Each voice its own fixed deviations, the same every time: the modules of a polyphonic expander.
        std::uint32_t seed = 0x9e3779b9u * (std::uint32_t) (index + 1);
        auto random = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return (float) (seed >> 8) / 8388608.0f - 1.0f;
        };
        offsetPitch1 = random();
        offsetPitch2 = random();
        offsetCutoff = random();
        wanderRate = 0.07 + 0.05 * (1.0 + random());
        wanderPhase = 0.5 * (1.0 + random());
        reset();
    }

    void reset()
    {
        filterEnv.reset();
        ampEnv.reset();
        svf.reset();
        osc1.reset(0.0);
        osc2.reset(0.31);
        active = held = sustained = false;
        first = true;
    }

    /**
     * Starts `newNote`, gliding from `from` (a note, or < 0 for none). `retrigger` false keeps the envelopes running
     * (a mono key pressed while another is held). `detune` in cents and `pan` -1 to 1 come from the voice's place.
     */
    void start(int newNote, float newVelocity, double from, bool retrigger, double detune, float pan, float gain)
    {
        note = newNote;
        velocity = newVelocity;
        pitch = from >= 0.0 && active ? pitch : (from >= 0.0 ? from : (double) newNote);
        unison = detune;
        panPosition = pan;
        voiceGain = gain;
        if (retrigger || ! active)
        {
            filterEnv.noteOn();
            ampEnv.noteOn();
            lfoAge = 0.0;
        }
        active = held = true;
        sustained = false;
    }

    void release()
    {
        held = false;
        filterEnv.noteOff();
        ampEnv.noteOff();
    }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    bool isReleasing() const { return active && ! held; }
    int currentNote() const { return note; }
    float cutoffNow() const { return cutoffHz; }
    float level() const { return ampEnv.current(); }

    bool sustained = false;
    std::uint64_t age = 0;

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Shared& shared)
    {
        if (! active)
            return;
        control(s, shared, count);

        const float step = 1.0f / (float) count;
        const auto saw1 = (float) (s.osc1.wave != Wave::pulse), pulse1 = s.osc1.wave == Wave::saw ? 0.0f : 0.7f;
        const auto saw2 = (float) (s.osc2.wave != Wave::pulse), pulse2 = s.osc2.wave == Wave::saw ? 0.0f : 0.7f;
        const auto weights = modeWeights(s.mode);
        const float level1 = 0.5f * s.osc1.level, level2 = 0.5f * s.osc2.level;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            const double dt1 = dt1From + (dt1To - dt1From) * t;
            const double dt2 = dt2From + (dt2To - dt2From) * t;
            const float width1 = width1From + (width1To - width1From) * t;
            const float width2 = width2From + (width2To - width2From) * t;
            const double wrap = s.sync ? Oscillator::wrapOf(osc1.phase, dt1) : -1.0;
            const float a = osc1.next(dt1, width1, saw1, pulse1);
            const float b = osc2.next(dt2, width2, saw2, pulse2, wrap);
            const float x = tonwerkwave::softClip(level1 * a + level2 * b);

            svf.process(x, gFrom + (gTo - gFrom) * t, k);
            // The band doubled, so the bandpass passes its centre whole as the lowpass passes the bass.
            const float y = s.bandpass ? 2.0f * svf.band : weights[0] * svf.low + weights[1] * svf.high;

            filterEnv.next();
            const float out = y * ampEnv.next() * gainNow;
            left[i] += out * panLeft;
            right[i] += out * panRight;
        }
        first = false;
        if (! ampEnv.isActive())
            reset();
    }

private:
    void control(const Settings& s, const Shared& shared, int count)
    {
        const double rate = sampleRate;
        const double seconds = (double) count / rate;
        filterEnv.set(s.filterEnv);
        ampEnv.set(s.ampEnv);

        const float glideCoefficient = s.glide <= 0.0005f ? 0.0f : (float) std::exp(-3.0 * seconds / s.glide);
        pitch = note + (pitch - note) * glideCoefficient;

        // The LFO fades in over `lfoDelay` after a key.
        lfoAge += seconds;
        const float fade = s.lfoDelay <= 0.001f ? 1.0f : (float) std::min(1.0, lfoAge / s.lfoDelay);
        const float lfo = shared.lfo * fade;

        wanderPhase += wanderRate * seconds;
        wanderPhase -= std::floor(wanderPhase);
        const auto wander = (float) std::sin(2.0 * kPi * wanderPhase);

        const float env = filterEnv.current();
        const double vibrato = (s.lfoPitch + 50.0f * shared.modWheel) * lfo;
        const double common = pitch + shared.bend + (unison + vibrato + 3.0 * s.drift * wander) / 100.0;
        const double note1 = common + 12.0 * s.osc1.octave + s.osc1.semitones + (s.osc1.fine + 6.0f * s.drift * offsetPitch1) / 100.0;
        const double note2 = common + 12.0 * s.osc2.octave + s.osc2.semitones + (s.osc2.fine + 6.0f * s.drift * offsetPitch2) / 100.0
                             + s.envPitch * env;
        auto dtOf = [rate](double n) { return std::min(0.45, 440.0 * std::exp2((n - 69.0) / 12.0) / rate); };
        dt1From = first ? dtOf(note1) : dt1To;
        dt2From = first ? dtOf(note2) : dt2To;
        dt1To = dtOf(note1);
        dt2To = dtOf(note2);

        const float pwm = 0.4f * s.lfoPwm * lfo;
        width1From = first ? std::clamp(s.osc1.width + pwm, 0.05f, 0.95f) : width1To;
        width2From = first ? std::clamp(s.osc2.width + pwm, 0.05f, 0.95f) : width2To;
        width1To = std::clamp(s.osc1.width + pwm, 0.05f, 0.95f);
        width2To = std::clamp(s.osc2.width + pwm, 0.05f, 0.95f);

        // Velocity darkens and softens a soft key; at 0 every key plays alike.
        const float soft = s.velocity * (1.0f - velocity);
        const double octaves = 6.0 * s.envCutoff * env + 3.0 * s.lfoCutoff * lfo + s.keytrack * (pitch - 60.0) / 12.0
                               + 0.25 * s.drift * offsetCutoff - 3.0 * soft;
        const double highest = std::min(18000.0, 0.45 * rate);
        cutoffHz = (float) std::clamp((double) s.cutoff * std::exp2(octaves), 20.0, highest);
        gFrom = first ? (float) std::tan(kPi * cutoffHz / rate) : gTo;
        gTo = (float) std::tan(kPi * cutoffHz / rate);
        k = dampingFor(s.resonance);

        gainNow = voiceGain * (1.0f - soft);
        // Equal power across the field, whole in the middle.
        const float angle = (float) (0.25 * kPi * (1.0 + std::clamp(panPosition * s.spread, -1.0f, 1.0f)));
        panLeft = std::cos(angle) * 1.41421356f;
        panRight = std::sin(angle) * 1.41421356f;
    }

    double sampleRate = 48000.0;
    tonwerkwave::Adsr filterEnv, ampEnv;
    Oscillator osc1, osc2;
    Svf svf;
    bool active = false, held = false, first = true;
    int note = 60;
    float velocity = 1.0f;
    double pitch = 60.0, unison = 0.0, lfoAge = 0.0;
    float panPosition = 0.0f, voiceGain = 1.0f, gainNow = 1.0f, panLeft = 1.0f, panRight = 1.0f;
    float offsetPitch1 = 0.0f, offsetPitch2 = 0.0f, offsetCutoff = 0.0f;
    double wanderRate = 0.1, wanderPhase = 0.0;
    double dt1From = 0.0, dt1To = 0.0, dt2From = 0.0, dt2To = 0.0;
    float width1From = 0.5f, width1To = 0.5f, width2From = 0.5f, width2To = 0.5f;
    float gFrom = 0.1f, gTo = 0.1f, k = 1.6f, cutoffHz = 1000.0f;
};

/** The instrument without a host: eight voices, poly, mono or unison, glide, the sustain pedal and the wheels. */
class Engine
{
public:
    void prepare(double rate)
    {
        shared.sampleRate = rate;
        for (int i = 0; i < kVoices; ++i)
            voices[(std::size_t) i].prepare(rate, i);
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v.reset();
        heldCount = 0;
        sustainPedal = false;
        lastNote = -1.0;
        lfoPhase = 0.0;
    }

    void noteOn(int note, float velocity, const Settings& s)
    {
        const double from = s.glide > 0.0005f ? lastNote : -1.0;
        lastNote = note;
        if (s.voiceMode == VoiceMode::poly)
        {
            auto& v = freeVoice(note);
            v.start(note, velocity, from, true, 0.0, panOf(index(v)), 1.0f);
            v.age = ++clock;
            return;
        }
        const bool legato = heldCount > 0;
        remove(note);
        if (heldCount < kHeld)
            notes[(std::size_t) heldCount++] = note;
        lastVelocity = velocity;
        play(note, velocity, from, ! legato, s);
    }

    void noteOff(int note, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isHeld() && v.currentNote() == note)
                {
                    if (sustainPedal)
                        v.sustained = true;
                    else
                        v.release();
                }
            return;
        }
        const bool wasSounding = heldCount > 0 && notes[(std::size_t) heldCount - 1] == note;
        remove(note);
        if (! wasSounding)
            return;
        if (heldCount > 0)
        {
            const int back = notes[(std::size_t) heldCount - 1];
            play(back, lastVelocity, lastNote, false, s);
            lastNote = back;
        }
        else
            for (auto& v : voices)
            {
                if (sustainPedal)
                    v.sustained = v.isHeld();
                else
                    v.release();
            }
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
        heldCount = 0;
        sustainPedal = false;
        for (auto& v : voices)
        {
            v.sustained = false;
            v.release();
        }
    }

    void setBend(double semitones) { shared.bend = semitones; }
    void setModWheel(float value) { shared.modWheel = value; }

    /** Adds `count` samples of all voices to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Transport& transport)
    {
        const double beats = tonwerkwave::divisionBeats(s.lfoDivision);
        const double rate = s.lfoSync ? transport.bpm / 60.0 / beats : (double) s.lfoRate;
        // A synced LFO sits on the song's bars while it plays.
        if (s.lfoSync && transport.playing && transport.hasPosition)
            lfoPhase = transport.ppq / beats - std::floor(transport.ppq / beats);
        int done = 0;
        while (done < count)
        {
            const int n = std::min(Voice::kControl, count - done);
            const double before = lfoPhase;
            lfoPhase += rate * n / shared.sampleRate;
            lfoPhase -= std::floor(lfoPhase);
            if (lfoPhase < before)
                lfoHeld = random();
            shared.lfo = 2.0f * tonwerkwave::lfoShape(s.lfoShape, lfoPhase, lfoHeld) - 1.0f;
            for (auto& v : voices)
                v.render(left + done, right + done, n, s, shared);
            done += n;
        }
    }

    /** The newest voice still sounding, for the display; nullptr when none. */
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
    /** Mono plays the first voice, unison all eight around the note. */
    void play(int note, float velocity, double from, bool retrigger, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::mono)
        {
            for (std::size_t i = 1; i < voices.size(); ++i)
                if (voices[i].isHeld())
                    voices[i].release();
            voices[0].start(note, velocity, from, retrigger, 0.0, 0.0f, 1.0f);
            voices[0].age = ++clock;
            return;
        }
        // Eight voices of uncorrelated phase add up by their power: each at 1/sqrt(8) keeps a key's level.
        const float gain = 1.0f / std::sqrt((float) kVoices);
        for (int i = 0; i < kVoices; ++i)
        {
            const double place = 2.0 * i / (kVoices - 1) - 1.0;
            auto& v = voices[(std::size_t) i];
            v.start(note, velocity, from, retrigger, 0.5 * s.unisonDetune * place, (float) place, gain);
            v.age = ++clock;
        }
    }

    /** A voice for `note`: one already releasing it (a repeated key), a free one, the oldest releasing, the oldest. */
    Voice& freeVoice(int note)
    {
        for (auto& v : voices)
            if (v.isReleasing() && v.currentNote() == note)
                return v;
        // The free voice after the one used last, so each key takes the next module and its place in the field.
        for (int n = 1; n <= kVoices; ++n)
        {
            auto& v = voices[(std::size_t) ((lastVoice + n) % kVoices)];
            if (! v.isActive())
            {
                lastVoice = index(v);
                return v;
            }
        }
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isReleasing() && (best == nullptr || v.age < best->age))
                best = &v;
        if (best == nullptr)
            for (auto& v : voices)
                if (best == nullptr || v.age < best->age)
                    best = &v;
        lastVoice = index(*best);
        return *best;
    }

    int index(const Voice& v) const { return (int) (&v - voices.data()); }

    /** The voices' places, spread out in the order they are taken: left, right, nearer the middle, ... */
    static float panOf(int voice)
    {
        static constexpr std::array<float, kVoices> places { -1.0f, 1.0f, -0.43f, 0.43f, -0.71f, 0.71f, -0.14f, 0.14f };
        return places[(std::size_t) voice];
    }

    void remove(int note)
    {
        const auto end = notes.begin() + heldCount;
        const auto at = std::remove(notes.begin(), end, note);
        heldCount = (int) std::distance(notes.begin(), at);
    }

    float random()
    {
        randomState = randomState * 1664525u + 1013904223u;
        return (float) (randomState >> 8) / 16777216.0f;
    }

    std::array<Voice, kVoices> voices;
    Shared shared;
    std::array<int, kHeld> notes {};
    int heldCount = 0;
    float lastVelocity = 1.0f;
    double lastNote = -1.0;
    bool sustainPedal = false;
    std::uint64_t clock = 0;
    int lastVoice = kVoices - 1;
    double lfoPhase = 0.0;
    float lfoHeld = 0.5f;
    std::uint32_t randomState = 0x2545f491u;
};
} // namespace tonwerkexpander
