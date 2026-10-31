#pragma once

#include "PhysicalVoice.h" // Noise
#include "Voice.h"         // Adsr

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Tonwerk PD (pd/src/PdEngine.h of the Synth repository) for the Eurorack module: the same phase distortion, two
// lines with their DCW and DCA envelopes, ring and noise modulation and the chorus, ported to float like the
// wavetable voice, without the plugin's LFOs, pitch envelope and modulation routes (a modular system patches those).
namespace twm::pd
{
inline constexpr float kPi = 3.14159265f;
inline constexpr int kLines = 2;

// Menu orders as in Tonwerk PD, so a sound reads the same in both.
enum class Wave : uint8_t { saw, square, pulse, doubleSine, sawPulse, resonanceSaw, resonanceTriangle, resonanceTrapezoid, count };
enum class LineSelect : uint8_t { line1, line2, line1Doubled, both, count };
enum class Modulation : uint8_t { off, ring, noise, count };

struct Envelope
{
    float attack, decay, sustain, release;
};

/** One line, as in the plugin's LineSettings. */
struct LineSettings
{
    Wave first = Wave::saw;
    bool alternate = false;
    Wave second = Wave::saw;
    float depth = 0.6f;
    float keyFollow = 0.2f;
    float level = 0.8f;
    int octave = 0;
    float semitones = 0.0f; // the plugin's whole semitones and cents in one
    Envelope dcw { 0.005f, 0.6f, 0.35f, 0.3f };
    Envelope amp { 0.003f, 1.0f, 0.85f, 0.3f };
};

/** Everything a note plays from, as in the plugin's Settings; `bend` is the static DCW offset its routes would add. */
struct Settings
{
    LineSelect lines = LineSelect::line1;
    Modulation modulation = Modulation::off;
    std::array<LineSettings, kLines> line { LineSettings {},
                                            LineSettings { Wave::square, false, Wave::saw, 0.5f, 0.2f, 0.8f, 0, 0.07f } };
    float bend = 0.0f;
    float balance = 0.0f;
    float width = 0.5f;
    float chorusMix = 0.0f;
    float chorusRate = 0.6f;
    float chorusDepth = 0.5f;
};

/** cos(2 pi `phase`) from a table, as the plugin's `cosine`. */
inline float cosine(float phase)
{
    static constexpr int kSize = 4096;
    static const std::array<float, kSize + 1> table = [] {
        std::array<float, kSize + 1> t {};
        for (int i = 0; i <= kSize; ++i)
            t[(std::size_t) i] = (float) std::cos(2.0 * 3.141592653589793 * i / kSize);
        return t;
    }();
    const float wrapped = phase - std::floor(phase);
    const float at = wrapped * kSize;
    const int i = std::min((int) at, kSize - 1);
    const float frac = at - (float) i;
    return table[(std::size_t) i] + (table[(std::size_t) i + 1] - table[(std::size_t) i]) * frac;
}

/** The plugin's `pdWave` in float: `wave` at `p` (0 to 1) bent by `d`. */
inline float pdWave(Wave wave, float p, float d, float steepest, float highest)
{
    const float bend = std::clamp(d, 0.0f, 1.0f);
    switch (wave)
    {
        case Wave::saw:
        {
            const float knee = std::max(steepest, 0.5f - 0.5f * bend);
            const float bent = p < knee ? 0.5f * p / knee : 0.5f + 0.5f * (p - knee) / (1.0f - knee);
            return cosine(bent);
        }
        case Wave::square:
        {
            const float half = p < 0.5f ? 2.0f * p : 2.0f * p - 1.0f;
            const float knee = std::max(2.0f * steepest, 1.0f - bend);
            const float y = cosine(0.5f * std::min(1.0f, half / knee));
            return p < 0.5f ? y : -y;
        }
        case Wave::pulse:
        {
            const float knee = std::max(steepest, 1.0f - 0.95f * bend);
            return cosine(std::min(1.0f, p / knee));
        }
        case Wave::doubleSine:
        {
            if (p < 0.5f)
                return cosine(p + bend * p);
            const float q = p - 0.5f;
            return -cosine(q + bend * q);
        }
        case Wave::sawPulse:
        {
            const float knee = std::max(steepest, 0.5f - 0.5f * bend);
            const float bent = p < knee ? 0.5f * p / knee : p < 0.5f ? 0.5f : p;
            return cosine(bent);
        }
        case Wave::resonanceSaw:
        case Wave::resonanceTriangle:
        case Wave::resonanceTrapezoid:
        {
            const float ratio = std::min(highest, 1.0f + 15.0f * bend);
            float window = 1.0f - p;
            if (wave == Wave::resonanceTriangle)
                window = 1.0f - std::abs(2.0f * p - 1.0f);
            else if (wave == Wave::resonanceTrapezoid)
                window = p < 0.5f ? 1.0f : 2.0f - 2.0f * p;
            return 1.0f - window * (1.0f - cosine(ratio * p));
        }
        case Wave::count:
            break;
    }
    return 0.0f;
}

inline float endOf(Wave wave, float d)
{
    return wave == Wave::doubleSine ? cosine(0.5f * std::clamp(d, 0.0f, 1.0f)) : 1.0f;
}

inline float polyBlep(float t, float dt)
{
    if (t < dt)
    {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

/**
 * The plugin's voice for one gate: `start` begins a note (again: the envelopes restart from where they are, the
 * lines keep their phase, as the plugin's mono mode), `setNote` moves its pitch while it sounds. Renders in steps of
 * at most kControl samples, the tuning and gains set once a step and ramped as in the plugin.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    explicit Voice(uint32_t seed = 0x2545f491u * 3u) : random(seed) {}

    void prepare(float rate)
    {
        sampleRate = rate;
        for (auto& o : oscs)
        {
            o.dcw.prepare(rate);
            o.amp.prepare(rate);
        }
        dcCoefficient = 1.0f - 2.0f * kPi * 10.0f / rate;
        reset();
    }

    void reset()
    {
        for (auto& o : oscs)
        {
            o.dcw.reset();
            o.amp.reset();
            o.phase = 0;
            o.odd = false;
            o.playing = Wave::saw;
            o.previousEnd = 1.0f;
        }
        for (auto& d : dc)
            d = {};
        active = false;
    }

    void start(float newNote, const Settings& s)
    {
        const bool wasActive = active;
        note = newNote;
        assign(s);
        for (auto& o : oscs)
        {
            o.dcw.noteOn();
            o.amp.noteOn();
        }
        if (! wasActive)
        {
            first = true;
            // The plugin draws its two LFOs' first random values here; drawn too, so the noise matches it.
            random.next();
            random.next();
            for (auto& o : oscs)
            {
                o.phase = 0;
                o.odd = false;
                o.playing = o.first;
                o.previousEnd = 1.0f;
            }
        }
        active = true;
    }

    void release()
    {
        for (auto& o : oscs)
        {
            o.dcw.noteOff();
            o.amp.noteOff();
        }
    }

    void setNote(float value) { note = value; }
    bool isActive() const { return active; }
    /** The louder line's DCA, for the LEDs. */
    float level() const { return std::max(oscs[0].on ? oscs[0].amp.current() : 0.0f, oscs[1].on ? oscs[1].amp.current() : 0.0f); }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s)
    {
        if (! active)
            return;
        control(s);
        const float step = 1.0f / (float) count;
        const bool ring = s.modulation == Modulation::ring && oscs[0].on && oscs[1].on;
        const bool noisy = s.modulation == Modulation::noise && oscs[1].on;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
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
                float jitter = 0.0f;
                if (k == 1 && noisy)
                {
                    noiseState += 0.4f * (random.next() - noiseState);
                    jitter = 0.35f * noiseState;
                }
                float y = oscillate(o, bend, jitter);
                if (k == 0)
                    raw0 = y;
                if (k == 1 && ring)
                    y *= raw0;
                out[k] = y * level * o.gain.at(t);
            }
            for (std::size_t k = 0; k < kLines; ++k)
            {
                const float x = out[k];
                const float y = x - dc[k].in + dcCoefficient * dc[k].out;
                dc[k].in = x;
                dc[k].out = y;
                out[k] = y;
            }
            left[i] += out[0] * oscs[0].panLeft + out[1] * oscs[1].panLeft;
            right[i] += out[0] * oscs[0].panRight + out[1] * oscs[1].panRight;
        }
        first = false;
        bool sounding = false;
        for (const auto& o : oscs)
            sounding = sounding || (o.on && o.amp.isActive());
        if (! sounding)
            reset();
    }

private:
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

    struct Osc
    {
        bool on = false;
        Wave first = Wave::saw;
        bool alternate = false;
        Wave second = Wave::saw;
        Adsr dcw, amp;
        // The phase in 32 bits, as the wavetable voice's: a float phase would round away up to a cent per second,
        // which a resonance sixteen times the note multiplies.
        uint32_t phase = 0;
        uint32_t step = 0;
        float increment = 0.0f;
        bool odd = false;
        Wave playing = Wave::saw;
        float previousEnd = 1.0f;
        Ramp bendBase, bendOffset, gain;
        float keyScale = 1.0f;
        float panLeft = 1.0f, panRight = 1.0f;
    };

    struct DcBlocker
    {
        float in = 0.0f, out = 0.0f;
    };

    float oscillate(Osc& o, float bend, float jitter)
    {
        const float dt = o.increment;
        const float steepest = std::min(0.25f, 2.0f * dt);
        const float highest = std::max(1.0f, 0.45f / std::max(1.0e-9f, dt));
        const float phase = (float) o.phase * kPhaseScale;
        float read = phase + jitter;
        read -= std::floor(read);
        float y = pdWave(o.playing, read, bend, steepest, highest);
        if (o.playing == Wave::doubleSine)
        {
            const float jump = 1.0f - endOf(Wave::doubleSine, bend);
            const float mid = (float) (o.phase + 0x80000000u) * kPhaseScale;
            y -= 0.5f * jump * polyBlep(mid, dt);
            if (phase > 1.0f - dt)
                y += 0.5f * jump * polyBlep(phase, dt);
        }
        if (phase < dt && o.previousEnd < 1.0f)
            y += 0.5f * (1.0f - o.previousEnd) * polyBlep(phase, dt);

        const uint32_t before = o.phase;
        o.phase += o.step;
        if (o.phase < before)
        {
            o.previousEnd = endOf(o.playing, bend);
            o.odd = o.alternate && ! o.odd;
            o.playing = o.odd ? o.second : o.first;
        }
        return y;
    }

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
            o.dcw.set(line.dcw.attack, line.dcw.decay, line.dcw.sustain, line.dcw.release);
            o.amp.set(line.amp.attack, line.amp.decay, line.amp.sustain, line.amp.release);
            o.first = line.first;
            o.alternate = line.alternate;
            o.second = line.second;
        }
    }

    void control(const Settings& s)
    {
        assign(s);
        const float balance = std::clamp(s.balance, -1.0f, 1.0f);
        const bool both = oscs[0].on && oscs[1].on;
        for (std::size_t k = 0; k < kLines; ++k)
        {
            auto& o = oscs[k];
            const auto& line = sourceOf(k, s);
            const auto& tuning = s.line[k];
            if (! o.alternate)
                o.odd = false;
            float notePitch = note + 12.0f * (float) tuning.octave;
            if (k == 1)
                notePitch += tuning.semitones;
            const float hz = std::clamp(440.0f * std::exp2((notePitch - 69.0f) / 12.0f), 8.0f, sampleRate * 0.45f);
            o.increment = hz / sampleRate;
            o.step = (uint32_t) std::llround(o.increment * 4294967296.0f);

            o.bendBase.set(std::clamp(line.depth, 0.0f, 1.0f), first);
            o.bendOffset.set(s.bend, first);
            o.keyScale = std::min(1.0f, std::exp2(-2.0f * line.keyFollow * std::max(0.0f, notePitch - 48.0f) / 24.0f));

            const float share = k == 0 ? 1.0f - std::max(0.0f, balance) : 1.0f + std::min(0.0f, balance);
            o.gain.set(0.25f * line.level * share, first);
            const float pan = both ? (k == 0 ? -s.width : s.width) : 0.0f;
            o.panLeft = std::sqrt(1.0f - pan);
            o.panRight = std::sqrt(1.0f + pan);
        }
    }

    static constexpr float kPhaseScale = 1.0f / 4294967296.0f;
    float sampleRate = 48000.0f;
    float note = 60.0f;
    bool active = false;
    bool first = true;
    float noiseState = 0.0f;
    std::array<Osc, kLines> oscs {};
    std::array<DcBlocker, kLines> dc {};
    float dcCoefficient = 0.999f;
    phys::Noise random;
};

/** The plugin's chorus: each side a delay of 12 ms swept by up to 6 ms, the right a quarter turn behind. */
class Chorus
{
public:
    static constexpr int kSize = 4096; // a power of two over 50 ms at 48 kHz
    void prepare(float rate)
    {
        sampleRate = rate;
        for (auto& b : buffers)
            b.fill(0.0f);
        write = 0;
        phase = 0;
    }

    void process(float* left, float* right, int count, float mix, float rate, float depth)
    {
        constexpr int mask = kSize - 1;
        if (mix <= 0.0f)
        {
            for (int i = 0; i < count; ++i)
            {
                buffers[0][(std::size_t) write] = left[i];
                buffers[1][(std::size_t) write] = right[i];
                write = (write + 1) & mask;
            }
            return;
        }
        const float base = 0.012f * sampleRate;
        const float sweep = 0.006f * sampleRate * std::clamp(depth, 0.0f, 1.0f);
        // The sweep's phase in 32 bits: a float phase stepping by a millionth would lose half a percent of its rate.
        const auto increment = (uint32_t) std::llround(std::max(0.01f, rate) / sampleRate * 4294967296.0f);
        const float scale = 1.0f / (1.0f + 0.5f * mix);
        float* sides[2] = { left, right };
        for (int i = 0; i < count; ++i)
        {
            buffers[0][(std::size_t) write] = left[i];
            buffers[1][(std::size_t) write] = right[i];
            // sin(2 pi x) is cos(2 pi (x - 1/4)).
            const float at = (float) phase * (1.0f / 4294967296.0f);
            const float delays[2] = { base + sweep * cosine(at - 0.25f), base + sweep * cosine(at) };
            for (std::size_t c = 0; c < 2; ++c)
            {
                const float back = (float) write - delays[c];
                const float floored = std::floor(back);
                const float frac = back - floored;
                const int a = (int) floored & mask;
                const int b = (a + 1) & mask;
                const float delayed = buffers[c][(std::size_t) a] + (buffers[c][(std::size_t) b] - buffers[c][(std::size_t) a]) * frac;
                sides[c][i] = (sides[c][i] + mix * delayed) * scale;
            }
            write = (write + 1) & mask;
            phase += increment;
        }
    }

private:
    float sampleRate = 48000.0f;
    std::array<std::array<float, kSize>, 2> buffers {};
    int write = 0;
    uint32_t phase = 0;
};

/** What the module plays from: the sound, the pitch (MIDI note numbers, fractional) and the gate. */
struct PlayParams
{
    Settings settings;
    float note = 48.0f;
    bool gate = false;
};

/**
 * The module's instrument: one voice and the chorus. A rising gate starts a note; while the gate is on (or no cable is
 * in), a jump at V/OCT of half a semitone within 4 ms starts one too, so a sequencer without a gate plays; the voice
 * follows V/OCT all the time. Writes left and right; the lines sit apart by `width`.
 */
class Player
{
public:
    static constexpr int kControl = Voice::kControl;
    static constexpr int kJumpBlocks = 6;
    static constexpr float kJumpSemitones = 0.5f;
    static constexpr float kHoldOffSeconds = 0.015f;
    /** The plugin's level (a line a quarter of full scale) times this keeps both lines fully bent with the chorus under full scale (±5 V). */
    static constexpr float kOutput = 1.35f;

    void prepare(float rate)
    {
        sampleRate = rate;
        voice.prepare(rate);
        chorus.prepare(rate);
        recent.fill(48.0f);
    }

    void render(float* left, float* right, int count, const PlayParams& p)
    {
        const float before = recent[(std::size_t) ((position + 1) % kJumpBlocks)];
        if (p.gate && ! gateWas)
            strike(p);
        else if (! p.gate && gateWas)
            voice.release();
        else if (p.gate && sinceStrike >= kHoldOffSeconds && std::abs(p.note - before) >= kJumpSemitones)
            strike(p);
        gateWas = p.gate;
        sinceStrike += (float) count / sampleRate;
        position = (position + 1) % kJumpBlocks;
        recent[(std::size_t) position] = p.note;

        voice.setNote(p.note);
        std::fill(left, left + count, 0.0f);
        std::fill(right, right + count, 0.0f);
        for (int done = 0; done < count; done += Voice::kControl)
        {
            const int n = std::min(Voice::kControl, count - done);
            voice.render(left + done, right + done, n, p.settings);
        }
        for (int i = 0; i < count; ++i)
        {
            left[i] *= kOutput;
            right[i] *= kOutput;
        }
        const auto& s = p.settings;
        chorus.process(left, right, count, s.chorusMix, s.chorusRate, s.chorusDepth);
    }

    float envelope() const { return voice.isActive() ? std::min(1.0f, voice.level()) : 0.0f; }
    int strikes() const { return struck; }

private:
    void strike(const PlayParams& p)
    {
        voice.start(p.note, p.settings);
        ++struck;
        sinceStrike = 0.0f;
    }

    float sampleRate = 48000.0f;
    Voice voice;
    Chorus chorus;
    bool gateWas = false;
    float sinceStrike = 1.0f;
    std::array<float, kJumpBlocks> recent {};
    int position = 0;
    int struck = 0;
};
} // namespace twm::pd
