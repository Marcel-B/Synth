#pragma once

#include "Tables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
// Menu orders as in Tonwerk Wavetable, so a sound reads the same in both.
enum class Warp : uint8_t { off, sync, bend, pwm, fm, count };
enum class FilterMode : uint8_t { low12, low24, high12, band12, notch, comb, off, count };

inline constexpr int kMaxUnison = 4;
inline constexpr float kPi = 3.14159265358979f;

/** tanh's shape at a fraction of its cost: exact at 0, ±1 from ±3 on (the plugin's softClip). */
inline float softClip(float x)
{
    if (x <= -3.0f)
        return -1.0f;
    if (x >= 3.0f)
        return 1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float noteToHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

struct OscParams
{
    bool on = true;
    int table = 0;
    /** 0 to 1 across the table's frames. */
    float position = 0.0f;
    Warp warp = Warp::off;
    float warpAmount = 0.0f;
    /** Offset from the played note, in semitones (octaves, semitones and fine together). */
    float transpose = 0.0f;
    int unison = 1;
    /** Cents between the outermost unison voices. */
    float detune = 20.0f;
    /** 0: only the middle voices sound, 1: all equally loud. */
    float blend = 0.75f;
    float level = 0.75f;
};

/** Everything the voice plays from; the control task fills it from the panel and the CV inputs. */
struct VoiceParams
{
    /** The pitch in MIDI semitones (60 = C4), V/Oct, tune and fine already added. */
    float note = 36.0f;
    OscParams a;
    OscParams b { false };
    bool subOn = false;
    /** Octaves below the note. */
    int subOctave = 1;
    float subLevel = 0.6f;
    /** Square instead of sine. */
    bool subSquare = false;

    FilterMode filter = FilterMode::low24;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float drive = 0.0f;
    /** How far the envelope opens the cutoff at its peak, in octaves. */
    float envToCutoff = 0.0f;
    /** 1: the cutoff follows the note one to one. */
    float keytrack = 0.0f;

    float attack = 0.002f;
    float decay = 0.4f;
    float sustain = 1.0f;
    float release = 0.2f;
    bool gate = false;
};

/**
 * The plugin's Adsr (wavetable/src/WaveDsp.h): a linear attack from wherever the level is, exponential falls to the
 * sustain level and to silence, each about 99 % of the way after its time; at least half a millisecond of attack.
 */
class Adsr
{
public:
    void prepare(float rate) { sampleRate = rate; }
    void set(float attack, float decay, float sustainLevel, float release)
    {
        attackStep = 1.0f / (std::max(0.0005f, attack) * sampleRate);
        decayCoefficient = std::exp(-4.6f / (std::max(0.001f, decay) * sampleRate));
        releaseCoefficient = std::exp(-4.6f / (std::max(0.001f, release) * sampleRate));
        sustain = sustainLevel;
    }
    void noteOn() { stage = Stage::attack; }
    void noteOff()
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }
    float next()
    {
        switch (stage)
        {
            case Stage::idle: break;
            case Stage::attack:
                level += attackStep;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay: level = sustain + (level - sustain) * decayCoefficient; break;
            case Stage::release:
                level *= releaseCoefficient;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
        }
        return level;
    }
    float current() const { return level; }
    bool isActive() const { return stage != Stage::idle; }
    void reset()
    {
        stage = Stage::idle;
        level = 0.0f;
    }

private:
    enum class Stage : uint8_t { idle, attack, decay, release };
    float sampleRate = 48000.0f;
    Stage stage = Stage::idle;
    float level = 0.0f;
    float attackStep = 0.001f;
    float decayCoefficient = 0.999f;
    float releaseCoefficient = 0.999f;
    float sustain = 1.0f;
};

/** Zavalishin's TPT state variable filter, one stage, as in the plugin: stable however fast the cutoff moves. */
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

/** The plugin's feedback comb tuned to the cutoff; 4096 samples hold a cycle of 20 Hz at 48 kHz. */
class Comb
{
public:
    static constexpr int kSize = 4096;
    void reset() { std::fill(buffer, buffer + kSize, 0.0f); }
    float process(float x, float delay, float feedback)
    {
        const float read = (float) write - delay;
        const float floorRead = std::floor(read);
        const float t = read - floorRead;
        const int i = (int) floorRead & (kSize - 1);
        const float a = buffer[i];
        const float b = buffer[(i + 1) & (kSize - 1)];
        const float y = x + feedback * (a + (b - a) * t);
        buffer[write] = softClip(y);
        write = (write + 1) & (kSize - 1);
        return y;
    }

private:
    float buffer[kSize] {};
    int write = 0;
};

/**
 * One voice of Tonwerk Wavetable for a Eurorack module, mono: two wavetable oscillators with unison (up to 4 here,
 * 8 in the plugin), warp, a sub, the filter (SVF 12/24 dB, high, band, notch, comb, with drive) and an ADSR as VCA
 * and filter envelope. It writes two signals: `osc`, the oscillators and sub as a plain VCO, and `voice`, the same
 * through filter and VCA.
 *
 * Unlike the plugin it runs in float with 32-bit integer phases: the ESP32-S3's FPU has single precision only, and
 * double would be emulated. Everything that moves is worked out once per control step of kControl samples and ramps
 * across it, as in the plugin, so CV at the control task's rate does not step audibly.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    void prepare(float rate)
    {
        sampleRate = rate;
        env.prepare(rate);
        filterStage[0].reset();
        filterStage[1].reset();
        combFilter.reset();
        first = true;
        // One voice starts at phase 0; unison voices at spread-out phases, or they sound as one at the attack.
        for (auto* osc : { &oscA, &oscB })
            for (int u = 0; u < kMaxUnison; ++u)
                osc->phase[u] = u == 0 ? 0u : 0x9E3779B9u * (uint32_t) (u + (osc == &oscB ? 7 : 0));
    }

    /** Renders `count` (at most kControl) samples into `osc` and `voice`. */
    void render(float* osc, float* voice, int count, const VoiceParams& p)
    {
        control(p);
        const Table& tableA = Bank::table(p.a.table);
        const Table& tableB = Bank::table(p.b.table);
        const Table& subTable = Bank::table(0);
        const bool needA = p.a.on || p.b.warp == Warp::fm;
        const bool needB = p.b.on || p.a.warp == Warp::fm;
        const float step = 1.0f / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            float x = 0.0f;
            float monoB = 0.0f;
            if (needB)
            {
                float out = 0.0f;
                monoB = oscB.tick(tableB, p.b.warp, t, lastA, out);
                if (p.b.on)
                    x += out;
            }
            if (needA)
            {
                float out = 0.0f;
                lastA = oscA.tick(tableA, p.a.warp, t, monoB, out);
                if (p.a.on)
                    x += out;
            }
            float subSample = 0.0f;
            if (p.subOn)
            {
                // The sub reads the basic table: its first frame is the sine, its last the square.
                subSample = subLevel.at(t) * subTable.read(subTableLevel, p.subSquare ? (float) (kFrames - 1) : 0.0f,
                                                           (float) subPhase * kPhaseToFloat);
                subPhase += subIncrement;
            }
            osc[i] = x + subSample;

            // As in the plugin, the sub joins past the filter, so the low end stays clean while the filter moves.
            float y = x;
            if (p.filter != FilterMode::off)
                y = filter(p.filter, y, t);
            voice[i] = (y + subSample) * env.next();
        }
        first = false;
    }

    float envelope() const { return env.current(); }

private:
    static constexpr float kPhaseToFloat = 1.0f / 4294967296.0f;

    /** A value that ramps from the last step's to this step's over the step; jumps on the first step. */
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

    struct Oscillator
    {
        uint32_t phase[kMaxUnison] {};
        uint32_t increment[kMaxUnison] {};
        float gain[kMaxUnison] {};
        int voices = 1;
        int level = 0;
        Ramp frame, warp, volume;

        /** The next sample into `out` (with its level); returns the raw mono sample for FM. */
        float tick(const Table& table, Warp mode, float t, float fmSource, float& out)
        {
            const float pos = frame.at(t);
            const float w = warp.at(t);
            float sync = 1.0f, bend = 1.0f, width = 1.0f, fm = 0.0f;
            switch (mode)
            {
                case Warp::off:
                case Warp::count: break;
                case Warp::sync: sync = 1.0f + 7.0f * w; break;
                case Warp::bend: bend = std::exp2(-4.0f * w); break;
                case Warp::pwm: width = 1.0f - 0.95f * w; break;
                case Warp::fm: fm = 1.5f * w * fmSource; break;
            }
            const float inverseWidth = 1.0f / width;
            float mono = 0.0f, sum = 0.0f;
            for (int u = 0; u < voices; ++u)
            {
                float p = (float) phase[u] * kPhaseToFloat;
                phase[u] += increment[u];
                switch (mode)
                {
                    case Warp::off:
                    case Warp::count: break;
                    case Warp::sync: p *= sync; p -= std::floor(p); break;
                    // Squeezes the cycle towards its start: the wave's first part plays faster, the rest slower.
                    case Warp::bend: p = p / (p + bend * (1.0f - p)); break;
                    // The cycle plays in the first part of the period; the rest holds its start.
                    case Warp::pwm: p = p < width ? p * inverseWidth : 0.0f; break;
                    case Warp::fm: p += fm; p -= std::floor(p); break;
                }
                const float sample = table.read(level, pos, p);
                mono += sample;
                sum += sample * gain[u];
            }
            out = sum * volume.at(t);
            return mono / (float) voices;
        }
    };

    void control(const VoiceParams& p)
    {
        if (p.gate && ! gateWas)
            env.noteOn();
        else if (! p.gate && gateWas)
            env.noteOff();
        gateWas = p.gate;
        env.set(p.attack, p.decay, p.sustain, p.release);

        setOscillator(oscA, p.a, p.note);
        setOscillator(oscB, p.b, p.note);

        const float subHz = noteToHz(p.note - 12.0f * (float) p.subOctave);
        const float subCycles = std::min(0.49f, subHz / sampleRate);
        subIncrement = (uint32_t) (subCycles * 4294967296.0f);
        subTableLevel = Table::levelFor(subCycles);
        subLevel.set(std::clamp(p.subLevel, 0.0f, 1.0f), first);

        // The cutoff moves in semitones as in the plugin; the envelope opens it by envToCutoff octaves at its peak.
        const float cutoffNote = 12.0f * std::log2(std::max(20.0f, p.cutoff) / 440.0f) + 69.0f
                                 + p.keytrack * (p.note - 60.0f) + 12.0f * p.envToCutoff * env.current();
        const float cutoff = std::clamp(noteToHz(cutoffNote), 20.0f, std::min(20000.0f, 0.45f * sampleRate));
        const float resonance = std::clamp(p.resonance, 0.0f, 1.0f);
        filterG.set(std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
        combDelay.set(sampleRate / cutoff, first);
        combFeedback = 0.97f * resonance;
        const float drive = std::clamp(p.drive, 0.0f, 1.0f);
        // Up to 24 dB into the clipper.
        driveGain = std::pow(10.0f, 1.2f * drive);
        const bool wasOn = driveOn;
        driveOn = drive > 0.0f;
        // The plugin takes a fixed share of the gain back (gain^-0.6), which suits its voices at a fraction of full
        // scale; the module's oscillators reach the clipper at full swing, where that left a turned-up drive up to 12 dB
        // quieter. So the module gives back what the clipper took, measured: the power before and after it, followed
        // over about 100 ms. The VCA comes after, so the followers see the steady oscillators, not the notes.
        if (driveOn && ! wasOn)
            powerIn = powerOut = 0.0f;
        float makeUp = driveMakeUp.to;
        if (! driveOn)
            makeUp = 1.0f;
        else if (powerIn > 1.0e-8f && powerOut > 1.0e-8f)
            makeUp = std::clamp(std::sqrt(powerIn / powerOut), 1.0f / 16.0f, 4.0f);
        driveMakeUp.set(makeUp, first || ! wasOn);
        powerCoefficient = 1.0f - std::exp(-1.0f / (0.1f * sampleRate));
    }

    void setOscillator(Oscillator& osc, const OscParams& o, float note)
    {
        osc.voices = std::clamp(o.unison, 1, kMaxUnison);
        const float w = std::clamp(o.warpAmount, 0.0f, 1.0f);
        osc.frame.set(std::clamp(o.position, 0.0f, 1.0f) * (float) (kFrames - 1), first);
        osc.warp.set(w, first);
        osc.volume.set(std::clamp(o.level, 0.0f, 1.0f), first);

        const float base = note + o.transpose;
        const float detune = std::max(0.0f, o.detune) / 100.0f;
        float total = 0.0f;
        float fastest = 0.0f;
        float weight[kMaxUnison] {};
        for (int u = 0; u < osc.voices; ++u)
        {
            // -1 to 1 across the voices: their place in pitch.
            const float spread = osc.voices == 1 ? 0.0f : 2.0f * (float) u / (float) (osc.voices - 1) - 1.0f;
            const float cycles = std::min(0.49f, noteToHz(base + 0.5f * detune * spread) / sampleRate);
            osc.increment[u] = (uint32_t) (cycles * 4294967296.0f);
            fastest = std::max(fastest, cycles);
            weight[u] = 1.0f - std::abs(spread) * (1.0f - o.blend);
            total += weight[u] * weight[u];
        }
        // Voices at unrelated phases add up in power, so the sum is scaled to keep one voice's loudness.
        const float norm = 1.0f / std::sqrt(std::max(1.0e-6f, total));
        for (int u = 0; u < osc.voices; ++u)
            osc.gain[u] = weight[u] * norm;
        // Warping raises the frequencies the table is read at; the band-limited level allows for it.
        switch (o.warp)
        {
            case Warp::off:
            case Warp::count: break;
            case Warp::sync: fastest *= 1.0f + 7.0f * w; break;
            case Warp::bend: fastest *= std::exp2(2.0f * w); break;
            case Warp::pwm: fastest /= 1.0f - 0.95f * w; break;
            case Warp::fm: fastest *= 1.0f + 3.0f * w; break;
        }
        osc.level = Table::levelFor(fastest);
    }

    float filter(FilterMode mode, float x, float t)
    {
        if (driveOn)
        {
            const float y = softClip(x * driveGain);
            powerIn += powerCoefficient * (x * x - powerIn);
            powerOut += powerCoefficient * (y * y - powerOut);
            x = y * driveMakeUp.at(t);
        }
        if (mode == FilterMode::comb)
        {
            const float makeUp = std::sqrt(1.0f - combFeedback * combFeedback);
            return combFilter.process(x, combDelay.at(t), combFeedback) * makeUp;
        }
        const float g = filterG.at(t);
        const float k = filterK.at(t);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        if (mode == FilterMode::low24)
        {
            // Two stages; only the second resonates, so 24 dB keeps one clear peak instead of a doubled one.
            constexpr float k1 = 1.414f;
            filterStage[0].process(x, g, k1, 1.0f / (1.0f + g * (g + k1)));
            filterStage[1].process(filterStage[0].low, g, k, a1);
            return filterStage[1].low;
        }
        auto& s = filterStage[0];
        s.process(x, g, k, a1);
        switch (mode)
        {
            case FilterMode::low12: return s.low;
            case FilterMode::high12: return s.high;
            // Scaled by k, so the peak stays at unity however narrow the band.
            case FilterMode::band12: return k * s.band;
            case FilterMode::notch: return s.low + s.high;
            default: return x;
        }
    }

    float sampleRate = 48000.0f;
    bool first = true;
    bool gateWas = false;
    float lastA = 0.0f;

    Adsr env;
    Oscillator oscA, oscB;
    uint32_t subPhase = 0, subIncrement = 0;
    int subTableLevel = 0;
    Ramp subLevel;

    SvfStage filterStage[2];
    Comb combFilter;
    Ramp filterG, filterK, combDelay;
    float combFeedback = 0.0f;
    float driveGain = 1.0f;
    Ramp driveMakeUp { 1.0f, 1.0f };
    float powerIn = 0.0f, powerOut = 0.0f, powerCoefficient = 0.0f;
    bool driveOn = false;
};
} // namespace twm
