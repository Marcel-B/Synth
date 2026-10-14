#pragma once

#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tonwerkvocoder
{
inline constexpr double kPi = 3.141592653589793;
inline constexpr int kMaxBands = 32;
inline constexpr int kVoices = 8;
/** The band counts the menu offers, in its order. */
inline constexpr std::array<int, 5> kBandCounts { 8, 12, 16, 24, 32 };
/** The filter bank's lowest and highest centre, Hz. */
inline constexpr double kLowest = 100.0, kHighest = 8000.0;

enum class CarrierWave { saw, square, pulse, superSaw, count };

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    int bands = 16;
    float formant = 0.0f;   // semitones the carrier's bands sit above the voice's
    float sharpness = 0.4f; // 0 wide, overlapping bands; 1 narrow ones
    float attack = 0.005f;
    float release = 0.06f;
    float sibilance = 0.5f;
    bool freeze = false;

    CarrierWave wave = CarrierWave::saw;
    float detune = 15.0f; // cents, the super saw's outer saws
    int octave = 0;
    float carrierAttack = 0.01f;
    float carrierRelease = 0.25f;
    float noise = 0.05f;
    bool latch = false;

    float width = 0.6f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** RBJ's biquad, transposed direct form II, floats. */
struct Biquad
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    /** A bandpass with 0 dB at `frequency`. */
    void bandpass(double frequency, double q, double rate)
    {
        const double w = 2.0 * kPi * std::min(frequency, 0.45 * rate) / rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) (alpha / a0);
        b1 = 0.0f;
        b2 = (float) (-alpha / a0);
        a1 = (float) (-2.0 * std::cos(w) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void highpass(double frequency, double q, double rate)
    {
        const double w = 2.0 * kPi * frequency / rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double c = std::cos(w);
        b0 = (float) ((1.0 + c) / 2.0 / a0);
        b1 = (float) (-(1.0 + c) / a0);
        b2 = b0;
        a1 = (float) (-2.0 * c / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }
};

/**
 * The carrier: an eight-voice synth the MIDI notes play, saws, squares, pulses or a super saw of three. Its level hardly
 * matters, since the bank evens the carrier out band by band; what matters is that it has something in every band.
 */
class Carrier
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& v : voices)
            v = {};
        heldCount = 0;
    }

    void noteOn(int note, const Settings& s)
    {
        if (s.latch && heldCount == 0)
            // A new chord after the hands came off replaces the latched one.
            for (auto& v : voices)
                if (v.note >= 0 && ! v.keyDown)
                    v.gate = false;
        ++heldCount;
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.note == note)
                chosen = &v;
        if (chosen == nullptr)
            for (auto& v : voices)
                if (v.note < 0 && (chosen == nullptr || v.level < chosen->level))
                    chosen = &v;
        if (chosen == nullptr)
        {
            // All busy: take the quietest, the one fading out longest.
            chosen = &voices[0];
            for (auto& v : voices)
                if (v.level < chosen->level)
                    chosen = &v;
        }
        chosen->note = note;
        chosen->gate = chosen->keyDown = true;
        newest = note;
    }

    void noteOff(int note, const Settings& s)
    {
        heldCount = std::max(0, heldCount - 1);
        for (auto& v : voices)
            if (v.note == note && v.keyDown)
            {
                v.keyDown = false;
                if (! s.latch)
                    v.gate = false;
            }
    }

    void allNotesOff()
    {
        heldCount = 0;
        for (auto& v : voices)
            v.gate = v.keyDown = false;
    }

    /** Latch switched off: what only the latch held lets go. */
    void unlatch()
    {
        for (auto& v : voices)
            if (! v.keyDown)
                v.gate = false;
    }

    void setBend(double semitones) { bend = semitones; }

    /**
     * Writes the carrier (without its noise) to `out` and how much of it sounds to `gate`: the voices' levels summed,
     * at most 1, so the bank can even out the carrier's spectrum and still follow its envelopes.
     */
    void render(float* out, float* gate, int count, const Settings& s)
    {
        std::fill(out, out + count, 0.0f);
        std::fill(gate, gate + count, 0.0f);
        const float attackStep = (float) (1.0 / (std::max(0.001, (double) s.carrierAttack) * sampleRate));
        const float releaseCoefficient
            = (float) std::exp(-4.6 / (std::max(0.005, (double) s.carrierRelease) * sampleRate));
        const double spread = std::exp2(s.detune / 1200.0);
        for (auto& v : voices)
        {
            if (v.note < 0)
                continue;
            const double frequency = 440.0 * std::exp2((v.note + 12.0 * s.octave + bend - 69.0) / 12.0);
            const double dt = std::min(0.45, frequency / sampleRate);
            for (int i = 0; i < count; ++i)
            {
                if (v.gate)
                    v.level = std::min(1.0f, v.level + attackStep);
                else
                    v.level *= releaseCoefficient;
                float value = 0.0f;
                switch (s.wave)
                {
                    case CarrierWave::saw:
                        value = saw(v.phase[0], dt);
                        break;
                    case CarrierWave::square:
                        value = pulse(v.phase[0], dt, 0.5);
                        break;
                    case CarrierWave::pulse:
                        value = pulse(v.phase[0], dt, 0.2);
                        break;
                    case CarrierWave::superSaw:
                    case CarrierWave::count:
                        value = 0.6f
                                * (saw(v.phase[0], dt) + saw(v.phase[1], std::min(0.45, dt * spread))
                                   + saw(v.phase[2], dt / spread));
                        break;
                }
                out[i] += v.level * value;
                gate[i] += v.level;
            }
            if (! v.gate && v.level < 1.0e-4f)
                v = {};
        }
        for (int i = 0; i < count; ++i)
            gate[i] = std::min(1.0f, gate[i]);
    }

    /** The newest note played, for the display (-1 before any). */
    int newestNote() const { return newest; }
    /** Each voice's note while it sounds, -1 otherwise. */
    template <typename Visit>
    void forEachNote(Visit visit) const
    {
        for (const auto& v : voices)
            if (v.note >= 0 && v.gate)
                visit(v.note);
    }

private:
    struct Voice
    {
        int note = -1;
        bool gate = false, keyDown = false;
        float level = 0.0f;
        std::array<double, 3> phase { 0.0, 0.31, 0.67 };
    };

    static float saw(double& phase, double dt)
    {
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        return (float) (2.0 * phase - 1.0 - tonwerk::polyBlep(phase, dt));
    }

    static float pulse(double& phase, double dt, double width)
    {
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        double fall = phase - width;
        if (fall < 0.0)
            fall += 1.0;
        // Centred, so a narrow pulse carries no DC into the bank.
        return (float) ((phase < width ? 1.0 : 0.0) - width + 0.5 * tonwerk::polyBlep(phase, dt)
                        - 0.5 * tonwerk::polyBlep(fall, dt))
               * 2.0f;
    }

    double sampleRate = 48000.0;
    std::array<Voice, kVoices> voices {};
    int heldCount = 0;
    int newest = -1;
    double bend = 0.0;
};

/**
 * A channel vocoder as the analog ones were built: the voice split into bands by bandpasses (two in a row for steeper
 * sides), each band's level followed with its own attack and release, and the carrier split the same way, each of its
 * bands set to the voice's level there. The carrier's bands are first evened out by their own slow level, so a saw's
 * weak top or a single note's empty low bands do not decide the sound: what comes out has the voice's spectrum and
 * about its loudness, whether one note or a chord plays. A floor keeps a band the carrier leaves empty from being
 * blown up into noise. The voice's hiss above 5 kHz goes past the bank by `sibilance`, as on the classic units, so
 * consonants stay understandable. Bands alternate left and right by `width`.
 */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        carrier.prepare(rate);
        configured = -1;
        shownFormant = 1.0e9f;
        shownSharpness = -1.0f;
        for (auto& band : bank)
            band = {};
        for (auto& h : hiss)
            h.reset();
        for (auto& h : hiss)
            h.highpass(5000.0, 0.7071, rate);
        carrierBuffer.fill(0.0f);
    }

    Carrier carrier;

    /** Voice in, vocoder out; `voice` is mono, the result stereo. */
    void process(const float* voice, float* left, float* right, int count, const Settings& s)
    {
        configure(s);
        const float attack = (float) std::exp(-1.0 / (std::max(0.0005, (double) s.attack) * sampleRate));
        const float release = (float) std::exp(-1.0 / (std::max(0.002, (double) s.release) * sampleRate));
        // The carrier's own level is followed slowly, so evening it out does not flatten its attack.
        const float carrierFollow = (float) std::exp(-1.0 / (0.03 * sampleRate));
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const int bands = activeBands;

        for (int done = 0; done < count;)
        {
            const int n = std::min(count - done, kChunk);
            carrier.render(carrierBuffer.data(), gateBuffer.data(), n, s);
            for (int i = 0; i < n; ++i)
            {
                const float in = voice[done + i];
                const float gate = gateBuffer[(std::size_t) i];
                // The noise sounds with the notes, so without a key the vocoder is silent.
                const float c = carrierBuffer[(std::size_t) i] + s.noise * gate * (float) noise.next();
                float l = 0.0f, r = 0.0f;
                for (int b = 0; b < bands; ++b)
                {
                    auto& band = bank[(std::size_t) b];
                    if (! s.freeze)
                    {
                        const float m = std::abs(band.modulator[1].process(band.modulator[0].process(in)));
                        band.level = m + (band.level - m) * (m > band.level ? attack : release);
                    }
                    const float x = band.carrier[1].process(band.carrier[0].process(c));
                    band.carrierLevel = std::abs(x) + (band.carrierLevel - std::abs(x)) * carrierFollow;
                    const float y = x * band.level / (band.carrierLevel + kFloor);
                    l += y * band.left;
                    r += y * band.right;
                }
                // Evened out, the carrier would sound at full level to the end of its release: the gate gives the
                // notes' envelopes back. The hiss, too, needs a key.
                const float wet = kLevel * gate;
                const float sibilant = s.sibilance * hiss[1].process(hiss[0].process(in));
                const float dry = (1.0f - s.mix) * in;
                left[done + i] = output * (s.mix * wet * (l + sibilant) + dry);
                right[done + i] = output * (s.mix * wet * (r + sibilant) + dry);
            }
            done += n;
        }
        for (int b = 0; b < kMaxBands; ++b)
            shownLevels[(std::size_t) b] = b < bands ? bank[(std::size_t) b].level : 0.0f;
    }

    int bands() const { return activeBands; }
    /** Each band's voice level as last followed, for the display. */
    const std::array<float, kMaxBands>& levels() const { return shownLevels; }
    /** A band's centre on the voice's side, Hz. */
    static double centre(int band, int bands)
    {
        return kLowest * std::pow(kHighest / kLowest, bands > 1 ? (double) band / (bands - 1) : 0.0);
    }

private:
    static constexpr int kChunk = 64;
    /** About -46 dBFS: below this a band of the carrier is left as it is rather than raised. */
    static constexpr float kFloor = 0.005f;
    /** The overlapping bands add up to about 2 dB over the voice; this brings the sum back to its loudness. */
    static constexpr float kLevel = 0.8f;

    struct Band
    {
        std::array<Biquad, 2> modulator, carrier;
        float level = 0.0f, carrierLevel = 0.0f;
        float left = 1.0f, right = 1.0f;
    };

    static bool same(float a, float b) { return ! (a < b) && ! (b < a); }

    void configure(const Settings& s)
    {
        const int bands = std::clamp(s.bands, 1, kMaxBands);
        if (bands == configured && same(s.formant, shownFormant) && same(s.sharpness, shownSharpness)
            && same(s.width, shownWidth))
            return;
        const bool countChanged = bands != configured;
        configured = activeBands = bands;
        shownFormant = s.formant;
        shownSharpness = s.sharpness;
        shownWidth = s.width;
        // Neighbours meet at the geometric middle; a sharper bank narrows each band below that spacing.
        const double ratio = std::pow(kHighest / kLowest, 1.0 / std::max(1, bands - 1));
        const double edge = std::sqrt(ratio);
        const double q = (0.6 + 2.4 * s.sharpness) / (edge - 1.0 / edge);
        const double shift = std::exp2(s.formant / 12.0);
        for (int b = 0; b < bands; ++b)
        {
            auto& band = bank[(std::size_t) b];
            const double f = centre(b, bands);
            for (auto& filter : band.modulator)
                filter.bandpass(f, q, sampleRate);
            for (auto& filter : band.carrier)
                filter.bandpass(std::min(f * shift, 0.45 * sampleRate), q, sampleRate);
            // Odd bands lean left, even ones right; at 0 width both sides get each band, at the same total power.
            const double pan = (b % 2 == 0 ? -1.0 : 1.0) * s.width;
            const double angle = (pan + 1.0) * kPi / 4.0;
            band.left = (float) (std::cos(angle) * std::sqrt(2.0));
            band.right = (float) (std::sin(angle) * std::sqrt(2.0));
            if (countChanged)
            {
                for (auto& filter : band.modulator)
                    filter.reset();
                for (auto& filter : band.carrier)
                    filter.reset();
                band.level = band.carrierLevel = 0.0f;
            }
        }
    }

    double sampleRate = 48000.0;
    std::array<Band, kMaxBands> bank {};
    std::array<Biquad, 2> hiss {};
    std::array<float, kChunk> carrierBuffer {}, gateBuffer {};
    std::array<float, kMaxBands> shownLevels {};
    tonwerk::Noise noise { 0x1234567u };
    int configured = -1, activeBands = 16;
    float shownFormant = 0.0f, shownSharpness = 0.0f, shownWidth = -1.0f;
};
} // namespace tonwerkvocoder
