#pragma once

#include "WaveDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tonwerkadd
{
using tonwerkwave::Adsr;
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;

inline constexpr double kPi = 3.141592653589793;
inline constexpr int kLfos = 2;
/** Sine partials per voice at most. */
inline constexpr int kPartials = 64;
/** The vowels the formant knob moves through, in its order. */
inline constexpr int kVowels = 5;

// The order of every enum below is the order of a parameter's menu, saved in Logic projects: append, never reorder.

/** The two spectra the morph knob blends: the strength of each harmonic, before the shaping below. */
enum class Spectrum { sine, saw, square, triangle, pulse, organ, hollow, bright, octaves, glass, primes, random, count };
/** The filter works on the partials' levels, so it needs no stable state and costs nothing per sample. */
enum class FilterMode { low, high, band, notch };
enum class VoiceMode { poly, mono };
/** Where the partials start: all from zero, as a sawtooth's do (a sharp attack), or each somewhere else (softer). */
enum class PhaseMode { aligned, random };
enum class Target { none, pitch, morph, tilt, oddEven, stretch, partials, comb, cutoff, resonance, vowel, amp, count };

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
    float rate = 0.5f;
    int division = 6;
    Route route;
};

/** Everything a note plays from, read from the parameters each block. Times in seconds, shares 0 to 1. */
struct Settings
{
    Spectrum spectrumA = Spectrum::saw;
    Spectrum spectrumB = Spectrum::square;
    /** 0 is spectrum A, 1 spectrum B, between a blend of their partials' strengths. */
    float morph = 0.0f;
    /** How many partials sound, 1 to 64; between two whole numbers the last one is partly there. */
    float partials = 64.0f;
    /** dB per octave added to the spectrum: brighter above 0, darker below. */
    float tilt = 0.0f;
    /** -1 only the even partials (and the fundamental), 0 both, 1 only the odd ones. */
    float oddEven = 0.0f;
    /** How far the partials move off whole multiples: up (a stiff string, a bell) above 0, down below. */
    float stretch = 0.0f;
    /** Cents each partial is detuned by at most, each its own way, so they beat against each other. */
    float beating = 0.0f;
    PhaseMode phases = PhaseMode::random;
    /** How wide the partials spread, odd ones to one side and even ones to the other. */
    float width = 0.5f;

    /** A comb across the spectrum: notches every `combSpacing` partials, as deep as `combDepth`, moved by `combShift`. */
    float combDepth = 0.0f;
    float combSpacing = 4.0f;
    float combShift = 0.0f;

    /** A vowel's formants laid over the partials, fixed in hertz however high the note: A, E, I, O, U from 0 to 4. */
    float vowel = 0.0f;
    float formantDepth = 0.0f;
    /** Semitones the formants move, for a smaller or larger throat. */
    float formantShift = 0.0f;

    bool filterOn = false;
    FilterMode filterMode = FilterMode::low;
    float cutoff = 20000.0f;
    float resonance = 0.0f;
    float keytrack = 0.0f;

    /** Each partial's own envelope: the high ones come in later and die sooner, as on a struck or blown instrument. */
    bool spectralOn = false;
    /** How long the highest partials take to come in; the fundamental comes at once. */
    float bloom = 0.0f;
    /** How long the fundamental takes to fall 60 dB towards `spectralSustain`. */
    float spectralDecay = 2.0f;
    float spectralSustain = 0.0f;
    /** How much sooner the high partials fall: 0 all alike, 1 the k-th partial k times as fast. */
    float spectralDamping = 0.5f;

    EnvSettings env1 { 0.005f, 0.3f, 1.0f, 0.3f };
    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;

    EnvSettings env2 { 0.01f, 0.5f, 0.0f, 0.3f };
    Route env2Route;
    std::array<LfoSettings, kLfos> lfo {};
    Route wheel;
    /** Velocity counts from 0 at a full strike down to -1 at the softest, so a positive amount makes soft notes less. */
    Route velocity { Target::tilt, 0.25f };
    /** How much quieter soft notes are: 0 all alike, 1 about 20 dB from the hardest to a soft one. */
    float dynamics = 0.5f;

    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f;
    int bendRange = 2;
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
 * The spectra as the strengths of harmonics 1 to 64, each scaled to the same RMS (a sine of 0.35), so switching or
 * morphing keeps the level. Computed once from their formulas.
 */
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
        std::uint32_t seed = 0x6d2b79f5u;
        for (int k = 1; k <= kPartials; ++k)
        {
            const auto i = (std::size_t) k - 1;
            const bool odd = k % 2 == 1;
            const float kf = (float) k;
            t[(std::size_t) Spectrum::sine][i] = k == 1 ? 1.0f : 0.0f;
            t[(std::size_t) Spectrum::saw][i] = 1.0f / kf;
            t[(std::size_t) Spectrum::square][i] = odd ? 1.0f / kf : 0.0f;
            t[(std::size_t) Spectrum::triangle][i] = odd ? 1.0f / (kf * kf) : 0.0f;
            // A pulse a quarter wide: every fourth harmonic missing.
            t[(std::size_t) Spectrum::pulse][i] = std::abs(std::sin((float) kPi * kf * 0.25f)) / kf;
            // A drawbar organ's footages over the 8': 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'.
            constexpr std::array<float, 8> drawbars { 1.0f, 0.8f, 0.6f, 0.6f, 0.3f, 0.3f, 0.0f, 0.4f };
            t[(std::size_t) Spectrum::organ][i] = k <= 8 ? drawbars[i] : 0.0f;
            // A clarinet's: strong odd partials, faint even ones.
            t[(std::size_t) Spectrum::hollow][i] = odd ? std::pow(kf, -0.8f) : 0.04f / kf;
            t[(std::size_t) Spectrum::bright][i] = 1.0f / std::sqrt(kf);
            const bool octave = (k & (k - 1)) == 0;
            t[(std::size_t) Spectrum::octaves][i] = octave ? 1.0f / std::sqrt(1.0f + std::log2(kf)) : 0.0f;
            const int root = (int) std::lround(std::sqrt(kf));
            t[(std::size_t) Spectrum::glass][i] = root * root == k ? 1.0f / std::sqrt(kf) : 0.0f;
            t[(std::size_t) Spectrum::primes][i] = k == 1 || isPrime(k) ? std::pow(kf, -0.7f) : 0.0f;
            seed = seed * 1664525u + 1013904223u;
            const float dice = (float) (seed >> 8) / 16777216.0f;
            t[(std::size_t) Spectrum::random][i] = (k == 1 ? 1.0f : dice * dice) * std::pow(kf, -0.6f);
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

/** Whether a knob moved since the values that depend on it were worked out. */
inline bool differs(float a, float b) { return std::abs(a - b) > 1.0e-7f; }

/** A fixed detune for each partial, -1 to 1, the fundamental's 0 so the note stays in tune. */
inline float beatOffset(int k)
{
    if (k <= 1)
        return 0.0f;
    std::uint32_t h = (std::uint32_t) k * 0x9e3779b1u;
    h ^= h >> 15;
    h *= 0x85ebca77u;
    h ^= h >> 13;
    return (float) (h & 0xffff) / 32767.5f - 1.0f;
}

/** A vowel's three formants: centres and widths in hertz, strengths. A singer's, between bass and tenor. */
struct Formants
{
    std::array<float, 3> hz, width, gain;
};

inline Formants formantsFor(float vowel, float shiftSemitones)
{
    static constexpr std::array<std::array<float, 3>, kVowels> centres { {
        { 800.0f, 1150.0f, 2800.0f },  // A
        { 400.0f, 1700.0f, 2600.0f },  // E
        { 290.0f, 2250.0f, 2900.0f },  // I
        { 450.0f, 800.0f, 2830.0f },   // O
        { 325.0f, 700.0f, 2530.0f },   // U
    } };
    const float at = std::clamp(vowel, 0.0f, (float) (kVowels - 1));
    const int from = std::min((int) at, kVowels - 2);
    const float t = at - (float) from;
    const float scale = std::exp2(shiftSemitones / 12.0f);
    Formants f;
    for (std::size_t j = 0; j < 3; ++j)
        f.hz[j] = scale * (centres[(std::size_t) from][j] + t * (centres[(std::size_t) from + 1][j] - centres[(std::size_t) from][j]));
    f.width = { 160.0f * scale, 200.0f * scale, 260.0f * scale };
    f.gain = { 1.0f, 0.6f, 0.35f };
    return f;
}

/** The spectrum's knobs after modulation, as one control step of a voice uses them. */
struct Shape
{
    Spectrum a = Spectrum::saw, b = Spectrum::square;
    float morph = 0.0f, partials = 64.0f, tilt = 0.0f, oddEven = 0.0f, stretch = 0.0f, beating = 0.0f;
    float combDepth = 0.0f, combSpacing = 4.0f, combShift = 0.0f;
    float vowel = 0.0f, formantDepth = 0.0f, formantShift = 0.0f;
    bool filterOn = false;
    FilterMode filterMode = FilterMode::low;
    float cutoff = 20000.0f, resonance = 0.0f;
};

/** The shape as the knobs set it, unmodulated: for the display while nothing sounds. */
inline Shape shapeOf(const Settings& s)
{
    Shape p;
    p.a = s.spectrumA;
    p.b = s.spectrumB;
    p.morph = s.morph;
    p.partials = s.partials;
    p.tilt = s.tilt;
    p.oddEven = s.oddEven;
    p.stretch = s.stretch;
    p.beating = s.beating;
    p.combDepth = s.combDepth;
    p.combSpacing = s.combSpacing;
    p.combShift = s.combShift;
    p.vowel = s.vowel;
    p.formantDepth = s.formantDepth;
    p.formantShift = s.formantShift;
    p.filterOn = s.filterOn;
    p.filterMode = s.filterMode;
    p.cutoff = s.cutoff;
    p.resonance = s.resonance;
    return p;
}

/** The k-th partial's frequency as a multiple of the note's, k from 1. */
inline float ratioOf(int k, float stretch, float beating)
{
    // At full stretch the 64th partial lies about seven semitones off its harmonic place.
    float ratio = k == 1 ? 1.0f : (float) k * std::exp2(0.08f * stretch * std::log2((float) k));
    if (beating > 0.0f)
        ratio *= 1.0f + beating * beatOffset(k) * (float) (std::log(2.0) / 1200.0);
    return ratio;
}

/**
 * Every partial's level for a note of `f0` hertz: the morphed spectrum, its tilt, the odd and even balance, the count,
 * the comb, the formants and the filter, and nothing above the band the sample rate allows. `ratio` holds the
 * partials' frequencies over f0. All of it a gain per partial, so the shaping costs nothing per sample.
 */
inline void levelsOf(const Shape& p, double f0, double sampleRate, const float* ratio, float* level)
{
    const auto& tableA = spectra()[(std::size_t) p.a];
    const auto& tableB = spectra()[(std::size_t) p.b];
    const float morph = std::clamp(p.morph, 0.0f, 1.0f);
    // dB per octave as a power of the partial number: k^(tilt / 6.02).
    const float tiltPower = p.tilt / 6.0206f;
    const Formants formants = formantsFor(p.vowel, p.formantShift);
    const float formantDepth = std::clamp(p.formantDepth, 0.0f, 1.0f);
    const float combDepth = std::clamp(p.combDepth, 0.0f, 1.0f);
    const float spacing = std::max(1.0f, p.combSpacing);
    const float resonance = std::clamp(p.resonance, 0.0f, 1.0f);
    const float cutoff = std::max(10.0f, p.cutoff);
    const float bandQ = 0.7f + 9.0f * resonance;
    const float nyquistFrom = 0.40f * (float) sampleRate, nyquistTo = 0.47f * (float) sampleRate;
    // The spectrum, its count and its odd and even balance, then the tilt, which keeps the energy: it moves the
    // weight between low and high partials without making the note louder (+12 dB per octave would otherwise lift
    // the 64th partial by 72 dB).
    float before = 0.0f, after = 0.0f;
    for (int k = 1; k <= kPartials; ++k)
    {
        const auto i = (std::size_t) k - 1;
        float a = tableA[i] + morph * (tableB[i] - tableA[i]);
        const float count = std::clamp(p.partials - (float) k + 1.0f, 0.0f, 1.0f);
        if (a <= 0.0f || count <= 0.0f || (float) f0 * ratio[i] >= nyquistTo)
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
            a *= std::exp2(tiltPower * std::log2((float) k));
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
        const float hz = (float) f0 * ratio[i];
        if (combDepth > 0.0f)
            a *= 1.0f - combDepth * 0.5f * (1.0f - std::cos(2.0f * (float) kPi * ((float) (k - 1) / spacing + p.combShift)));
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
            // 24 dB per octave, the resonance a peak at the cutoff up to about 15 dB.
            const float d = (x - 1.0f) * 10.0f;
            const float peak = 1.0f + 6.0f * resonance / (1.0f + d * d);
            const float q = bandQ * (x - 1.0f / x);
            switch (p.filterMode)
            {
                case FilterMode::low: a *= peak / std::sqrt(1.0f + x8); break;
                case FilterMode::high: a *= peak * x4 / std::sqrt(1.0f + x8); break;
                case FilterMode::band: a *= 1.0f / (1.0f + q * q); break;
                case FilterMode::notch: a *= 1.0f - 1.0f / (1.0f + q * q); break;
            }
        }
        if (hz > nyquistFrom)
            a *= (nyquistTo - hz) / (nyquistTo - nyquistFrom);
        level[i] = a;
    }
}

/**
 * One note: up to 64 sine oscillators, each a complex phasor turned by a fixed rotation each sample (four multiplies,
 * no table, no sine call), with its level ramped across the control step. Pitch, the partials' levels and their own
 * envelopes are set every `kControl` samples; the rotations change only there, so phase never jumps. Only partials
 * that sound are computed, eight at a time so the compiler can run them side by side.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    Voice() : random(nextSeed()) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        amp.prepare(rate);
        mod.prepare(rate);
        reset();
    }

    void reset()
    {
        amp.reset();
        mod.reset();
        re.fill(0.0f);
        im.fill(0.0f);
        gainL.fill(0.0f);
        gainR.fill(0.0f);
        shown.fill(0.0f);
        active = false;
        held = false;
        sustained = false;
        fading = 0;
        pendingNote = -1;
        computed = 0;
        peak = 0.0f;
    }

    /** Starts `note`; a voice still sounding another note fades out first, unless it glides there. */
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
        amp.noteOff();
        mod.noteOff();
    }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    bool isReleasing() const { return active && ! held; }
    int currentNote() const { return fading > 0 ? pendingNote : note; }

    bool sustained = false;
    std::uint64_t age = 0;

    /** What the display draws of this voice: each partial's level as it sounds now, with the note's frequency. */
    struct Picture
    {
        float frequency = 0.0f;
        std::array<float, kPartials> level {};
        std::array<float, kPartials> ratio {};
    };

    void draw(Picture& p) const
    {
        p.frequency = (float) frequency;
        const float envelope = amp.current();
        for (std::size_t i = 0; i < (std::size_t) kPartials; ++i)
        {
            p.level[i] = shown[i] * envelope;
            p.ratio[i] = ratios[i];
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

        // The partials eight at a time, each eight in their own lanes across the whole step, so the compiler runs
        // them side by side; the lanes are summed per sample at the end.
        const int n = computed;
        const float step = 1.0f / (float) count;
        alignas(32) std::array<std::array<float, 8>, kControl> laneL {}, laneR {};
        for (int k0 = 0; k0 < n; k0 += 8)
        {
            alignas(32) std::array<float, 8> c {}, sn {}, tr {}, ti {}, gl {}, gr {}, dl {}, dr {};
            for (std::size_t j = 0; j < 8; ++j)
            {
                const auto k = (std::size_t) k0 + j;
                c[j] = re[k];
                sn[j] = im[k];
                tr[j] = turnRe[k];
                ti[j] = turnIm[k];
                gl[j] = gainL[k];
                gr[j] = gainR[k];
                dl[j] = (targetL[k] - gainL[k]) * step;
                dr[j] = (targetR[k] - gainR[k]) * step;
            }
            for (int i = 0; i < count; ++i)
            {
                auto& outL = laneL[(std::size_t) i];
                auto& outR = laneR[(std::size_t) i];
                for (std::size_t j = 0; j < 8; ++j)
                {
                    const float turned = c[j] * tr[j] - sn[j] * ti[j];
                    sn[j] = c[j] * ti[j] + sn[j] * tr[j];
                    c[j] = turned;
                    gl[j] += dl[j];
                    gr[j] += dr[j];
                    outL[j] += gl[j] * sn[j];
                    outR[j] += gr[j] * sn[j];
                }
            }
            for (std::size_t j = 0; j < 8; ++j)
            {
                const auto k = (std::size_t) k0 + j;
                re[k] = c[j];
                im[k] = sn[j];
                gainL[k] = targetL[k];
                gainR[k] = targetR[k];
            }
        }
        const float fadeFrom = fading > 0 ? (float) fading / (float) kFadeSteps : 1.0f;
        const float fadeTo = fading > 0 ? (float) (fading - 1) / (float) kFadeSteps : 1.0f;
        float loudest = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (std::size_t j = 0; j < 8; ++j)
            {
                l += laneL[(std::size_t) i][j];
                r += laneR[(std::size_t) i][j];
            }
            const float t = (float) (i + 1) * step;
            const float g = amp.next() * gain * (fadeFrom + (fadeTo - fadeFrom) * t);
            l *= g;
            r *= g;
            loudest = std::max(loudest, std::max(std::abs(l), std::abs(r)));
            left[i] += l;
            right[i] += r;
        }
        // Rounding makes the phasors grow or shrink by a hair each turn; pulled back to 1 once a step.
        for (int k = 0; k < n; ++k)
        {
            const float m = re[(std::size_t) k] * re[(std::size_t) k] + im[(std::size_t) k] * im[(std::size_t) k];
            const float back = 1.5f - 0.5f * m;
            re[(std::size_t) k] *= back;
            im[(std::size_t) k] *= back;
        }
        for (int i = 0; i < count; ++i)
            mod.next();
        if (fading > 0)
            --fading;
        first = false;

        peak = std::max(loudest, peak * 0.9f);
        if (! amp.isActive() && fading == 0 && pendingNote < 0)
            reset();
    }

private:
    static constexpr int kFadeSteps = 3;

    void begin(int newNote, float newVelocity, const Settings& s, bool glide)
    {
        const bool wasActive = active;
        note = newNote;
        velocity = newVelocity;
        held = true;
        sustained = false;
        if (! glide || ! wasActive)
            pitch = note;
        amp.set(s.env1);
        mod.set(s.env2);
        amp.noteOn();
        mod.noteOn();
        if (! wasActive)
        {
            first = true;
            for (std::size_t k = 0; k < (std::size_t) kPartials; ++k)
            {
                // A sine from zero, as in a sawtooth's sum; or anywhere, so a chord's partials do not all line up.
                const double phase = s.phases == PhaseMode::random ? kPi * random.next() : 0.0;
                re[k] = (float) std::cos(phase);
                im[k] = (float) std::sin(phase);
                gainL[k] = gainR[k] = 0.0f;
            }
            for (std::size_t i = 0; i < lfoPhase.size(); ++i)
            {
                lfoPhase[i] = 0.0;
                lfoHeld[i] = (float) random.next();
                lfoSmooth[i] = 0.0f;
            }
        }
        // Each partial's own envelope starts over with every note.
        rise.fill(s.spectralOn && s.bloom > 0.0f ? 0.0f : 1.0f);
        fall.fill(1.0f);
        active = true;
    }

    void route(std::array<float, (std::size_t) Target::count>& m, const Route& r, float value) const
    {
        if (r.target != Target::none)
            m[(std::size_t) r.target] += r.amount * value;
    }

    void control(const Settings& s, const Shared& shared, int count)
    {
        amp.set(s.env1);
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
        const double played = pitch + shared.bend + 12.0 * s.octave + s.semitones + s.fine / 100.0 + 12.0 * at(Target::pitch);
        frequency = std::clamp(440.0 * std::exp2((played - 69.0) / 12.0), 8.0, 0.45 * sampleRate);

        Shape p = shapeOf(s);
        p.morph = std::clamp(s.morph + at(Target::morph), 0.0f, 1.0f);
        p.tilt = std::clamp(s.tilt + 12.0f * at(Target::tilt), -24.0f, 24.0f);
        p.oddEven = std::clamp(s.oddEven + 2.0f * at(Target::oddEven), -1.0f, 1.0f);
        p.stretch = std::clamp(s.stretch + at(Target::stretch), -1.0f, 1.0f);
        p.partials = std::clamp(s.partials + 64.0f * at(Target::partials), 1.0f, (float) kPartials);
        p.combShift = s.combShift + at(Target::comb);
        p.vowel = std::clamp(s.vowel + 4.0f * at(Target::vowel), 0.0f, (float) (kVowels - 1));
        p.resonance = std::clamp(s.resonance + at(Target::resonance), 0.0f, 1.0f);
        // The cutoff in semitones, ten octaves at 100 % of a route, as on the other Tonwerk synths.
        const double cutoffNote = 12.0 * std::log2(std::max(20.0f, s.cutoff) / 440.0) + 69.0
                                  + s.keytrack * (pitch - 60.0) + 120.0 * at(Target::cutoff);
        p.cutoff = (float) std::clamp(440.0 * std::exp2((cutoffNote - 69.0) / 12.0), 20.0, 20000.0);

        // The partials' places and rotations, only worked out again when the stretch, the beating or the pitch move.
        if (differs(p.stretch, lastStretch) || differs(p.beating, lastBeating))
        {
            lastStretch = p.stretch;
            lastBeating = p.beating;
            for (int k = 1; k <= kPartials; ++k)
                ratios[(std::size_t) k - 1] = ratioOf(k, p.stretch, p.beating);
            lastFrequency = -1.0;
        }
        levelsOf(p, frequency, sampleRate, ratios.data(), levels.data());

        int highest = 0;
        for (int k = 0; k < kPartials; ++k)
            if (levels[(std::size_t) k] > 0.0f)
                highest = k + 1;

        // Soft notes quieter by up to 20 dB at full dynamics, on a curve so the middle of the keyboard's range matters.
        const float dynamic = std::pow(std::clamp(velocity, 0.01f, 1.0f), 1.6f * s.dynamics);
        gain = dynamic * std::clamp(1.0f + at(Target::amp), 0.0f, 2.0f);

        if (s.spectralOn)
            spectralEnvelope(s, seconds, highest);
        if (differs(s.width, lastWidth))
        {
            lastWidth = s.width;
            for (int k = 1; k <= kPartials; ++k)
            {
                // The fundamental in the middle; the others alternate sides, further out the higher they are.
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
            shown[i] = level;
            targetL[i] = level * panL[i];
            targetR[i] = level * panR[i];
        }

        // Still fading out partials that the shaping just switched off are computed until they are silent.
        int needed = highest;
        for (int k = highest; k < computed; ++k)
            if (std::abs(gainL[(std::size_t) k]) > 0.0f || std::abs(gainR[(std::size_t) k]) > 0.0f)
                needed = k + 1;
        const int previously = computed;
        computed = std::min(kPartials, (needed + 7) / 8 * 8);
        // Partials coming back after a pause start from silence, as their gains already are.
        for (int k = previously; k < computed; ++k)
            gainL[(std::size_t) k] = gainR[(std::size_t) k] = 0.0f;

        if (std::abs(frequency - lastFrequency) > 1.0e-9 || previously < computed)
        {
            lastFrequency = frequency;
            const float w = (float) (2.0 * kPi * frequency / sampleRate);
            for (int k = 0; k < computed; ++k)
            {
                const float angle = std::min(w * ratios[(std::size_t) k], (float) kPi);
                turnRe[(std::size_t) k] = std::cos(angle);
                turnIm[(std::size_t) k] = std::sin(angle);
            }
        }
    }

    /** The partials' own envelopes: the high ones rise later and fall sooner, towards the sustain. */
    void spectralEnvelope(const Settings& s, double seconds, int highest)
    {
        if (differs(s.spectralDamping, lastDamping))
        {
            lastDamping = s.spectralDamping;
            for (int k = 1; k <= kPartials; ++k)
                dampingPower[(std::size_t) k - 1] = std::pow((float) k, s.spectralDamping);
        }
        // The top partial is in about `bloom` after the note starts, the others in proportion to their octave.
        const double bloom = std::max(1.0e-4, (double) s.bloom);
        const double fallRate = 6.9 * seconds / std::max(0.005, (double) s.spectralDecay);
        const float sustain = std::clamp(s.spectralSustain, 0.0f, 1.0f);
        for (int k = 1; k <= highest; ++k)
        {
            const auto i = (std::size_t) k - 1;
            if (rise[i] < 1.0f)
            {
                const double tau = bloom * std::log2((double) k + 1.0) / 6.0 / 3.0;
                rise[i] = k == 1 ? 1.0f : 1.0f - (1.0f - rise[i]) * (float) std::exp(-seconds / tau);
                if (rise[i] > 0.9995f)
                    rise[i] = 1.0f;
            }
            fall[i] = sustain + (fall[i] - sustain) * (float) std::exp(-fallRate * dampingPower[i]);
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
    float peak = 0.0f;
    float gain = 1.0f;
    int computed = 0;

    Adsr amp, mod;
    alignas(32) std::array<float, kPartials> re {}, im {}, turnRe {}, turnIm {};
    alignas(32) std::array<float, kPartials> gainL {}, gainR {}, targetL {}, targetR {};
    std::array<float, kPartials> ratios {}, levels {}, shown {}, rise {}, fall {}, panL {}, panR {}, dampingPower {};
    float lastStretch = -2.0f, lastBeating = -1.0f, lastWidth = -1.0f, lastDamping = -1.0f;
    double lastFrequency = -1.0;
    std::array<double, kLfos> lfoPhase {};
    std::array<float, kLfos> lfoHeld {};
    std::array<float, kLfos> lfoSmooth {};
    tonwerk::Noise random;
};

/** The instrument without a host: voices, poly or mono with glide, the sustain pedal and the wheels. */
class Engine
{
public:
    static constexpr int kVoices = 16;

    void prepare(double rate)
    {
        shared.sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
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
                    // The same key again restarts its own voice rather than stacking a second one on it.
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

    /** Adds `count` samples of all voices to `left` and `right`. */
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
    std::vector<int> held;
    Shared shared;
    bool sustainPedal = false;
    float lastVelocity = 1.0f;
    std::uint64_t clock = 0;
};
} // namespace tonwerkadd
