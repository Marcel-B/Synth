#pragma once

#include "PhysicalVoice.h" // Noise
#include "Voice.h"         // Adsr

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Tonwerk Additiv (additive/src/AdditiveEngine.h of the Synth repository) for the Eurorack module: the same 64 sine
// partials turned as complex phasors, shaped by a gain per partial every control step (two spectra morphed, count,
// odd/even, tilt, stretch, beating, comb, vowel formants, the filter, each partial's own envelope), ported to float.
// The plugin's LFOs, second envelope, modulation routes and velocity are left out: a modular system patches those.
namespace twm::add
{
inline constexpr float kPi = 3.14159265f;
inline constexpr int kPartials = 64;
inline constexpr int kVowels = 5;

// Menu orders as in Tonwerk Additiv, so a sound reads the same in both.
enum class Spectrum : uint8_t { sine, saw, square, triangle, pulse, organ, hollow, bright, octaves, glass, primes, random, count };
enum class FilterMode : uint8_t { low, high, band, notch, count };
enum class PhaseMode : uint8_t { aligned, random };

struct Envelope
{
    float attack, decay, sustain, release;
};

/** Everything a note plays from, as the plugin's Settings without its modulation; the module's defaults. */
struct Settings
{
    Spectrum spectrumA = Spectrum::saw;
    Spectrum spectrumB = Spectrum::square;
    float morph = 0.0f;
    float partials = 64.0f;
    float tilt = 0.0f;
    float oddEven = 0.0f;
    float stretch = 0.0f;
    float beating = 0.0f;
    PhaseMode phases = PhaseMode::random;
    float width = 0.5f;
    float combDepth = 0.0f;
    float combSpacing = 4.0f;
    float combShift = 0.0f;
    float vowel = 0.0f;
    float formantDepth = 0.0f;
    float formantShift = 0.0f;
    bool filterOn = false;
    FilterMode filterMode = FilterMode::low;
    float cutoff = 20000.0f;
    float resonance = 0.0f;
    float keytrack = 0.0f;
    bool spectralOn = false;
    float bloom = 0.0f;
    float spectralDecay = 2.0f;
    float spectralSustain = 0.0f;
    float spectralDamping = 0.5f;
    Envelope env { 0.005f, 0.3f, 1.0f, 0.3f };
};

/** log2(k) for k = 1 to 64, the base of tilt, stretch and damping. */
inline const std::array<float, kPartials>& logTable()
{
    static const auto table = [] {
        std::array<float, kPartials> t {};
        for (int k = 1; k <= kPartials; ++k)
            t[(std::size_t) k - 1] = std::log2((float) k);
        return t;
    }();
    return table;
}

/** The plugin's spectra, from the same formulas, each scaled to the RMS of a sine of 0.35. */
inline const std::array<std::array<float, kPartials>, (std::size_t) Spectrum::count>& spectra()
{
    static const auto tables = [] {
        std::array<std::array<float, kPartials>, (std::size_t) Spectrum::count> t {};
        auto isPrime = [](int n) {
            if (n < 2)
                return false;
            for (int d = 2; d * d <= n; ++d)
                if (n % d == 0)
                    return false;
            return true;
        };
        auto at = [&t](Spectrum s, std::size_t i) -> float& { return t[(std::size_t) s][i]; };
        uint32_t seed = 0x6d2b79f5u;
        for (int k = 1; k <= kPartials; ++k)
        {
            const auto i = (std::size_t) k - 1;
            const bool odd = k % 2 == 1;
            const float kf = (float) k;
            at(Spectrum::sine, i) = k == 1 ? 1.0f : 0.0f;
            at(Spectrum::saw, i) = 1.0f / kf;
            at(Spectrum::square, i) = odd ? 1.0f / kf : 0.0f;
            at(Spectrum::triangle, i) = odd ? 1.0f / (kf * kf) : 0.0f;
            at(Spectrum::pulse, i) = std::abs(std::sin(kPi * kf * 0.25f)) / kf;
            constexpr std::array<float, 8> drawbars { 1.0f, 0.8f, 0.6f, 0.6f, 0.3f, 0.3f, 0.0f, 0.4f };
            at(Spectrum::organ, i) = k <= 8 ? drawbars[i] : 0.0f;
            at(Spectrum::hollow, i) = odd ? std::pow(kf, -0.8f) : 0.04f / kf;
            at(Spectrum::bright, i) = 1.0f / std::sqrt(kf);
            const bool octave = (k & (k - 1)) == 0;
            at(Spectrum::octaves, i) = octave ? 1.0f / std::sqrt(1.0f + std::log2(kf)) : 0.0f;
            const int root = (int) std::lround(std::sqrt(kf));
            at(Spectrum::glass, i) = root * root == k ? 1.0f / std::sqrt(kf) : 0.0f;
            at(Spectrum::primes, i) = k == 1 || isPrime(k) ? std::pow(kf, -0.7f) : 0.0f;
            seed = seed * 1664525u + 1013904223u;
            const float dice = (float) (seed >> 8) / 16777216.0f;
            at(Spectrum::random, i) = (k == 1 ? 1.0f : dice * dice) * std::pow(kf, -0.6f);
        }
        for (auto& table : t)
        {
            float energy = 0.0f;
            for (const float a : table)
                energy += a * a;
            const float scale = 0.35f / std::sqrt(std::max(1.0e-9f, energy));
            for (auto& a : table)
                a *= scale;
        }
        return t;
    }();
    return tables;
}

/** The plugin's fixed detune per partial, -1 to 1, the fundamental's 0. */
inline float beatOffset(int k)
{
    if (k <= 1)
        return 0.0f;
    uint32_t h = (uint32_t) k * 0x9e3779b1u;
    h ^= h >> 15;
    h *= 0x85ebca77u;
    h ^= h >> 13;
    return (float) (h & 0xffff) / 32767.5f - 1.0f;
}

struct Formants
{
    std::array<float, 3> hz, width, gain;
};

/** A vowel's three formants (A, E, I, O, U from 0 to 4), as the plugin's. */
inline Formants formantsFor(float vowel, float shiftSemitones)
{
    static constexpr float centres[kVowels][3] = {
        { 800.0f, 1150.0f, 2800.0f }, { 400.0f, 1700.0f, 2600.0f }, { 290.0f, 2250.0f, 2900.0f },
        { 450.0f, 800.0f, 2830.0f },  { 325.0f, 700.0f, 2530.0f },
    };
    const float at = std::clamp(vowel, 0.0f, (float) (kVowels - 1));
    const int from = std::min((int) at, kVowels - 2);
    const float t = at - (float) from;
    const float scale = std::exp2(shiftSemitones / 12.0f);
    Formants f;
    for (std::size_t j = 0; j < 3; ++j)
        f.hz[j] = scale * (centres[from][j] + t * (centres[from + 1][j] - centres[from][j]));
    f.width = { 160.0f * scale, 200.0f * scale, 260.0f * scale };
    f.gain = { 1.0f, 0.6f, 0.35f };
    return f;
}

/** The k-th partial's frequency over the note's, k from 1, as the plugin's `ratioOf`. */
inline float ratioOf(int k, float stretch, float beating)
{
    float ratio = k == 1 ? 1.0f : (float) k * std::exp2(0.08f * stretch * logTable()[(std::size_t) k - 1]);
    if (beating > 0.0f)
        ratio *= 1.0f + beating * beatOffset(k) * 5.7762265e-4f; // ln 2 / 1200
    return ratio;
}

/** Every partial's level for a note of `f0` hertz, as the plugin's `levelsOf`. */
inline void levelsOf(const Settings& p, float f0, float sampleRate, const float* ratio, float* level)
{
    const auto& tableA = spectra()[(std::size_t) p.spectrumA];
    const auto& tableB = spectra()[(std::size_t) p.spectrumB];
    const auto& logs = logTable();
    const float morph = std::clamp(p.morph, 0.0f, 1.0f);
    const float tiltPower = p.tilt / 6.0206f;
    const float formantDepth = std::clamp(p.formantDepth, 0.0f, 1.0f);
    const Formants formants = formantsFor(p.vowel, p.formantShift);
    const float combDepth = std::clamp(p.combDepth, 0.0f, 1.0f);
    const float spacing = std::max(1.0f, p.combSpacing);
    const float resonance = std::clamp(p.resonance, 0.0f, 1.0f);
    const float cutoff = std::max(10.0f, p.cutoff);
    const float bandQ = 0.7f + 9.0f * resonance;
    const float nyquistFrom = 0.40f * sampleRate, nyquistTo = 0.47f * sampleRate;
    float before = 0.0f, after = 0.0f;
    for (int k = 1; k <= kPartials; ++k)
    {
        const auto i = (std::size_t) k - 1;
        float a = tableA[i] + morph * (tableB[i] - tableA[i]);
        const float count = std::clamp(p.partials - (float) k + 1.0f, 0.0f, 1.0f);
        if (a <= 0.0f || count <= 0.0f || f0 * ratio[i] >= nyquistTo)
        {
            level[i] = 0.0f;
            continue;
        }
        a *= count;
        if (k > 1)
        {
            if (k % 2 == 0)
                a *= 1.0f - std::max(0.0f, p.oddEven);
            else
                a *= 1.0f + std::min(0.0f, p.oddEven);
        }
        before += a * a;
        if (std::abs(tiltPower) > 1.0e-6f && k > 1)
            a *= std::exp2(tiltPower * logs[i]);
        after += a * a;
        level[i] = a;
    }
    const float keep = after > 1.0e-12f ? std::sqrt(before / after) : 1.0f;

    for (int k = 1; k <= kPartials; ++k)
    {
        const auto i = (std::size_t) k - 1;
        float a = level[i] * keep;
        if (a <= 0.0f)
            continue;
        const float hz = f0 * ratio[i];
        if (combDepth > 0.0f)
            a *= 1.0f - combDepth * 0.5f * (1.0f - std::cos(2.0f * kPi * ((float) (k - 1) / spacing + p.combShift)));
        if (formantDepth > 0.0f)
        {
            float g = 0.02f;
            for (std::size_t j = 0; j < 3; ++j)
            {
                const float d = (hz - formants.hz[j]) / (0.5f * formants.width[j]);
                g += formants.gain[j] / (1.0f + d * d);
            }
            a *= 1.0f - formantDepth + formantDepth * 1.5f * g;
        }
        if (p.filterOn)
        {
            const float x = hz / cutoff;
            const float x2 = x * x, x4 = x2 * x2, x8 = x4 * x4;
            const float d = (x - 1.0f) * 10.0f;
            const float peak = 1.0f + 6.0f * resonance / (1.0f + d * d);
            const float q = bandQ * (x - 1.0f / x);
            switch (p.filterMode)
            {
                case FilterMode::low: a *= peak / std::sqrt(1.0f + x8); break;
                case FilterMode::high: a *= peak * x4 / std::sqrt(1.0f + x8); break;
                case FilterMode::band: a *= 1.0f / (1.0f + q * q); break;
                case FilterMode::notch: a *= 1.0f - 1.0f / (1.0f + q * q); break;
                case FilterMode::count: break;
            }
        }
        if (hz > nyquistFrom)
            a *= (nyquistTo - hz) / (nyquistTo - nyquistFrom);
        level[i] = a;
    }
}

/**
 * The plugin's voice for one gate: `start` begins a note (again: the envelopes restart from where they are and the
 * partials keep their phase, as the plugin's mono mode), `setNote` moves its pitch while it sounds. Renders in steps
 * of at most kControl samples, the partials' levels and rotations set once a step and their gains ramped across it.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    explicit Voice(uint32_t seed = 0x2545f491u * 3u) : random(seed) {}

    void prepare(float rate)
    {
        sampleRate = rate;
        amp.prepare(rate);
        reset();
    }

    void reset()
    {
        amp.reset();
        re.fill(0.0f);
        im.fill(0.0f);
        gainL.fill(0.0f);
        gainR.fill(0.0f);
        active = false;
        computed = 0;
    }

    void start(float newNote, const Settings& s)
    {
        const bool wasActive = active;
        note = newNote;
        setEnvelope(s.env);
        amp.noteOn();
        if (! wasActive)
        {
            first = true;
            for (std::size_t k = 0; k < (std::size_t) kPartials; ++k)
            {
                const float phase = s.phases == PhaseMode::random ? kPi * random.next() : 0.0f;
                re[k] = std::cos(phase);
                im[k] = std::sin(phase);
                gainL[k] = gainR[k] = 0.0f;
            }
            // The plugin draws its two LFOs' first random values here; drawn too, so the phases of later notes match.
            random.next();
            random.next();
        }
        rise.fill(s.spectralOn && s.bloom > 0.0f ? 0.0f : 1.0f);
        fall.fill(1.0f);
        active = true;
    }

    void release() { amp.noteOff(); }
    void setNote(float value) { note = value; }
    bool isActive() const { return active; }
    float level() const { return amp.current(); }
    /** How many partials the last step computed, for the CPU test. */
    int partialsComputed() const { return computed; }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s)
    {
        if (! active)
            return;
        control(s, count);

        // One partial at a time across the step, its phasor, rotation and gains in registers: the ESP32-S3's FPU has
        // no vector lanes, and sixteen float registers hold one partial's state but not eight.
        float sumL[kControl], sumR[kControl];
        std::fill(sumL, sumL + count, 0.0f);
        std::fill(sumR, sumR + count, 0.0f);
        const float step = 1.0f / (float) count;
        for (int k = 0; k < computed; ++k)
        {
            const auto i = (std::size_t) k;
            float c = re[i], sn = im[i];
            const float tr = turnRe[i], ti = turnIm[i];
            float gl = gainL[i], gr = gainR[i];
            const float dl = (targetL[i] - gl) * step, dr = (targetR[i] - gr) * step;
            for (int n = 0; n < count; ++n)
            {
                const float turned = c * tr - sn * ti;
                sn = c * ti + sn * tr;
                c = turned;
                gl += dl;
                gr += dr;
                sumL[n] += gl * sn;
                sumR[n] += gr * sn;
            }
            // Rounding makes the phasor grow or shrink by a hair each turn; pulled back to 1 once a step.
            const float m = c * c + sn * sn;
            const float back = 1.5f - 0.5f * m;
            re[i] = c * back;
            im[i] = sn * back;
            gainL[i] = targetL[i];
            gainR[i] = targetR[i];
        }
        for (int n = 0; n < count; ++n)
        {
            const float g = amp.next();
            left[n] += sumL[n] * g;
            right[n] += sumR[n] * g;
        }
        first = false;
        if (! amp.isActive())
            reset();
    }

private:
    void setEnvelope(const Envelope& e)
    {
        if (e.attack != envelope.attack || e.decay != envelope.decay || e.sustain != envelope.sustain
            || e.release != envelope.release)
        {
            envelope = e;
            amp.set(e.attack, e.decay, e.sustain, e.release);
        }
    }

    void control(const Settings& s, int count)
    {
        setEnvelope(s.env);
        const float seconds = (float) count / sampleRate;
        frequency = std::clamp(440.0f * std::exp2((note - 69.0f) / 12.0f), 8.0f, 0.45f * sampleRate);

        Settings p = s;
        p.morph = std::clamp(s.morph, 0.0f, 1.0f);
        p.tilt = std::clamp(s.tilt, -24.0f, 24.0f);
        p.oddEven = std::clamp(s.oddEven, -1.0f, 1.0f);
        p.stretch = std::clamp(s.stretch, -1.0f, 1.0f);
        p.partials = std::clamp(s.partials, 1.0f, (float) kPartials);
        p.vowel = std::clamp(s.vowel, 0.0f, (float) (kVowels - 1));
        p.resonance = std::clamp(s.resonance, 0.0f, 1.0f);
        const float cutoffNote = 12.0f * std::log2(std::max(20.0f, s.cutoff) / 440.0f) + 69.0f + s.keytrack * (note - 60.0f);
        p.cutoff = std::clamp(440.0f * std::exp2((cutoffNote - 69.0f) / 12.0f), 20.0f, 20000.0f);

        if (p.stretch != lastStretch || p.beating != lastBeating)
        {
            lastStretch = p.stretch;
            lastBeating = p.beating;
            for (int k = 1; k <= kPartials; ++k)
                ratios[(std::size_t) k - 1] = ratioOf(k, p.stretch, p.beating);
            lastFrequency = -1.0f;
        }
        levelsOf(p, frequency, sampleRate, ratios.data(), levels.data());

        int highest = 0;
        for (int k = 0; k < kPartials; ++k)
            if (levels[(std::size_t) k] > 0.0f)
                highest = k + 1;

        if (s.spectralOn)
            spectralEnvelope(s, seconds, highest);
        if (s.width != lastWidth)
        {
            lastWidth = s.width;
            for (int k = 1; k <= kPartials; ++k)
            {
                const float side = k % 2 == 0 ? 1.0f : -1.0f;
                const float pan = k == 1 ? 0.0f : s.width * side * std::min(1.0f, (float) (k - 1) / 4.0f);
                panL[(std::size_t) k - 1] = std::sqrt(1.0f - pan);
                panR[(std::size_t) k - 1] = std::sqrt(1.0f + pan);
            }
        }
        for (int k = 0; k < kPartials; ++k)
        {
            const auto i = (std::size_t) k;
            const float level = s.spectralOn ? levels[i] * rise[i] * fall[i] : levels[i];
            targetL[i] = level * panL[i];
            targetR[i] = level * panR[i];
        }

        // Partials the shaping just switched off are computed until they have faded out.
        int needed = highest;
        for (int k = highest; k < computed; ++k)
            if (gainL[(std::size_t) k] != 0.0f || gainR[(std::size_t) k] != 0.0f)
                needed = k + 1;
        const int previously = computed;
        computed = std::min(kPartials, (needed + 7) / 8 * 8);
        for (int k = previously; k < computed; ++k)
            gainL[(std::size_t) k] = gainR[(std::size_t) k] = 0.0f;

        if (frequency != lastFrequency || previously < computed)
        {
            lastFrequency = frequency;
            const float w = 2.0f * kPi * frequency / sampleRate;
            for (int k = 0; k < computed; ++k)
            {
                const float angle = std::min(w * ratios[(std::size_t) k], kPi);
                turnRe[(std::size_t) k] = std::cos(angle);
                turnIm[(std::size_t) k] = std::sin(angle);
            }
        }
    }

    /** The partials' own envelopes, as the plugin's; their factors are only worked out again when a knob moves. */
    void spectralEnvelope(const Settings& s, float seconds, int highest)
    {
        if (s.spectralDamping != lastDamping || s.spectralDecay != lastDecay || s.bloom != lastBloom
            || seconds != lastSeconds)
        {
            lastDamping = s.spectralDamping;
            lastDecay = s.spectralDecay;
            lastBloom = s.bloom;
            lastSeconds = seconds;
            const float bloom = std::max(1.0e-4f, s.bloom);
            const float fallRate = 6.9f * seconds / std::max(0.005f, s.spectralDecay);
            for (int k = 1; k <= kPartials; ++k)
            {
                const auto i = (std::size_t) k - 1;
                const float tau = bloom * std::log2((float) k + 1.0f) / 6.0f / 3.0f;
                riseFactor[i] = std::exp(-seconds / tau);
                fallFactor[i] = std::exp(-fallRate * std::exp2(s.spectralDamping * logTable()[i]));
            }
        }
        const float sustain = std::clamp(s.spectralSustain, 0.0f, 1.0f);
        for (int k = 1; k <= highest; ++k)
        {
            const auto i = (std::size_t) k - 1;
            if (rise[i] < 1.0f)
            {
                rise[i] = k == 1 ? 1.0f : 1.0f - (1.0f - rise[i]) * riseFactor[i];
                if (rise[i] > 0.9995f)
                    rise[i] = 1.0f;
            }
            fall[i] = sustain + (fall[i] - sustain) * fallFactor[i];
        }
    }

    float sampleRate = 48000.0f;
    float note = 36.0f;
    float frequency = 65.4f;
    bool active = false;
    bool first = true;
    int computed = 0;
    Adsr amp;
    Envelope envelope { -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, kPartials> re {}, im {}, turnRe {}, turnIm {};
    std::array<float, kPartials> gainL {}, gainR {}, targetL {}, targetR {};
    std::array<float, kPartials> ratios {}, levels {}, rise {}, fall {}, panL {}, panR {};
    std::array<float, kPartials> riseFactor {}, fallFactor {};
    float lastStretch = -2.0f, lastBeating = -1.0f, lastWidth = -1.0f;
    float lastDamping = -1.0f, lastDecay = -1.0f, lastBloom = -1.0f, lastSeconds = -1.0f;
    float lastFrequency = -1.0f;
    phys::Noise random;
};

/** What the module plays from: the sound, the pitch (MIDI note numbers, fractional) and the gate. */
struct PlayParams
{
    Settings settings;
    float note = 36.0f;
    bool gate = false;
};

/**
 * The module's instrument: one voice. A rising gate starts a note; while the gate is on (or no cable is in), a jump
 * at V/OCT of half a semitone within 4 ms starts one too, so a sequencer without a gate plays; the voice follows
 * V/OCT all the time. Writes left and right; the partials sit apart by `width`.
 */
class Player
{
public:
    static constexpr int kControl = Voice::kControl;
    static constexpr int kJumpBlocks = 6;
    static constexpr float kJumpSemitones = 0.5f;
    static constexpr float kHoldOffSeconds = 0.015f;
    /** The plugin's level at its master 0 dB (its default is -6 dB): a spectrum's RMS is that of a sine of 0.35. */
    static constexpr float kOutput = 1.0f;

    void prepare(float rate)
    {
        sampleRate = rate;
        voice.prepare(rate);
        recent.fill(36.0f);
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
    }

    float envelope() const { return voice.isActive() ? std::min(1.0f, voice.level()) : 0.0f; }
    int strikes() const { return struck; }
    int partials() const { return voice.partialsComputed(); }

private:
    void strike(const PlayParams& p)
    {
        voice.start(p.note, p.settings);
        ++struck;
        sinceStrike = 0.0f;
    }

    float sampleRate = 48000.0f;
    Voice voice;
    bool gateWas = false;
    float sinceStrike = 1.0f;
    std::array<float, kJumpBlocks> recent {};
    int position = 0;
    int struck = 0;
};
} // namespace twm::add
