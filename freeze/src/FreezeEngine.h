#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

/**
 * Tonwerk Freeze: a moment of the incoming sound held as an endless pad. Plain C++, no JUCE, so the tests can run it.
 *
 * The input is analysed all the time (a short-time Fourier transform, frame about 85 ms, hop a quarter): each bin's
 * power is averaged over `captureMs`, and its true frequency, from the phase difference between frames, weighted by
 * that power. Freezing takes that average. Averaged rather than one frame, so a pad does not carry the click of the
 * frame it was taken from, and a melody played into it becomes a chord.
 *
 * The frozen spectrum is played back as a phase vocoder that never moves on: every hop each spectral peak advances
 * its phase by its own frequency, and the bins around it keep the phase relation they had in the analysis (Laroche and
 * Dolson's identity phase locking). Without that a held partial spreads over four bins with unrelated phases and
 * beats; with it a frozen chord sounds like the chord. `blur` adds random phase per frame on top, up to fully random
 * (Paulstretch's wash), and smooths the magnitudes across bins. Pitch shifting moves each peak with the bins around
 * it, so a partial keeps its shape; the shimmer is a second copy an octave above the shift.
 *
 * `motion` lets the pad live: 32 bands across the spectrum each drift in level (up to ±12 dB) and a little in pitch
 * (up to ±15 cents), on random paths that take about `driftSeconds` each, so no two bands move together.
 *
 * A new freeze while the old one still sounds morphs to it over `fadeMs` (or, with `stack`, layers the two); the
 * frozen layer fades in and out over the same time. The dry signal is not delayed: the frozen layer is a sound of its
 * own and has no latency to make up.
 */
namespace tonwerkfreeze
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;
/** Bands of the editor's spectrum, log spaced from 30 Hz to 18 kHz. */
inline constexpr int kDisplayBands = 96;
inline constexpr float kDisplayLowHz = 30.0f;
inline constexpr float kDisplayHighHz = 18000.0f;

struct Settings
{
    /** How long the input is averaged before a freeze, as a time constant. */
    float captureMs = 300.0f;
    /** Fade in and out, and the morph from one freeze to the next. */
    float fadeMs = 800.0f;
    /** 0: a new freeze replaces the old; 1: it is layered on top. */
    float stack = 0.0f;
    float pitch = 0.0f;
    float shimmer = 0.0f;
    float motion = 0.3f;
    float driftSeconds = 6.0f;
    float blur = 0.1f;
    float width = 0.6f;
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    /** 0.5 keeps the dry signal and the frozen layer both at full level; above that the dry gives way. */
    float mix = 0.5f;
    /** The frozen layer's level. */
    float levelDb = 0.0f;
};

/**
 * An in-place radix-2 complex FFT with its tables made once. Each stage's twiddles lie side by side and the butterflies
 * work on the floats directly: seven times faster than the same loops on std::complex, whose operator* checks for
 * infinities, and the FFT is most of what this effect costs.
 */
class Fft
{
public:
    void prepare(int size)
    {
        n = size;
        real.clear();
        imag.clear();
        for (int half = 1; half < n; half *= 2)
            for (int k = 0; k < half; ++k)
            {
                real.push_back((float) std::cos(-kPi * k / half));
                imag.push_back((float) std::sin(-kPi * k / half));
            }
        reversed.resize((std::size_t) n);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            reversed[(std::size_t) i] = r;
        }
    }

    /** Forward; the inverse is the forward of the conjugate, conjugated and divided by the size. */
    void forward(std::complex<float>* data) const
    {
        for (int i = 0; i < n; ++i)
            if (i < reversed[(std::size_t) i])
                std::swap(data[i], data[reversed[(std::size_t) i]]);
        // The standard lets a std::complex<float> array be read as pairs of floats.
        auto* d = reinterpret_cast<float*>(data);
        std::size_t table = 0;
        for (int half = 1; half < n; half *= 2)
        {
            const float* wr = real.data() + table;
            const float* wi = imag.data() + table;
            for (int start = 0; start < n; start += 2 * half)
            {
                float* a = d + 2 * start;
                float* b = d + 2 * (start + half);
                for (int k = 0; k < half; ++k)
                {
                    const float xr = b[2 * k], xi = b[2 * k + 1];
                    const float tr = wr[k] * xr - wi[k] * xi, ti = wr[k] * xi + wi[k] * xr;
                    const float ar = a[2 * k], ai = a[2 * k + 1];
                    b[2 * k] = ar - tr;
                    b[2 * k + 1] = ai - ti;
                    a[2 * k] = ar + tr;
                    a[2 * k + 1] = ai + ti;
                }
            }
            table += (std::size_t) half;
        }
    }

private:
    int n = 0;
    std::vector<float> real, imag;
    std::vector<int> reversed;
};

/** xorshift32: the same dice on every run, so a bounce sounds the same and the tests can count on it. */
class Dice
{
public:
    explicit Dice(std::uint32_t seed = 0x2545f491u) : state(seed) {}
    /** -1 to 1. */
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 2147483648.0 - 1.0);
    }

private:
    std::uint32_t state;
};

class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        // About 70 ms or more: 4096 at 44.1 and 48 kHz, fine enough to part the partials of a low note.
        size = 1024;
        while (size < (int) (rate * 0.07))
            size *= 2;
        hop = size / 4;
        half = size / 2;
        const auto n = (std::size_t) size, bins = (std::size_t) half + 1;
        fft.prepare(size);
        window.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            window[i] = (float) (0.5 - 0.5 * std::cos(kTwoPi * (double) i / (double) size));
        buffer.assign(n, {});
        for (auto& c : channels)
        {
            c.input.assign(n, 0.0f);
            c.ola.assign(n, 0.0f);
            c.power.assign(bins, 0.0f);
            c.weightedFreq.assign(bins, 0.0f);
            c.last.assign(bins, {});
            c.now.assign(bins, {});
            c.fromMag.assign(bins, 0.0f);
            c.toMag.assign(bins, 0.0f);
            c.freq.resize(bins);
            for (std::size_t k = 0; k < bins; ++k)
                c.freq[k] = (float) k;
            c.analysisPhasor.assign(bins, { 1.0f, 0.0f });
            c.out.assign(bins, {});
            for (auto& layer : c.synthPhase)
                layer.assign(bins, 0.0f);
        }
        // The same random start for both sides, so a mono input stays mono until `width` parts them; random rather
        // than zero, or the first frame would start every partial in phase, a click.
        Dice start(0x9e3779b9u);
        for (std::size_t k = 0; k < bins; ++k)
        {
            const float p = (float) kPi * start.next();
            for (auto& c : channels)
                for (auto& layer : c.synthPhase)
                    layer[k] = p;
        }
        for (int i = 0; i < kJitterSteps; ++i)
            jitterTable[(std::size_t) i] = std::polar(1.0f, (float) (kPi * (2.0 * i / (kJitterSteps - 1) - 1.0)));
        stereoOffset.resize(bins);
        // A quarter turn either way, at random per partial: the sides decorrelate fully at width 100 %, yet in mono
        // every partial still sums to 3 dB up, never cancels as random turns up to half a turn would.
        for (auto& o : stereoOffset)
            o = (float) (0.5 * kPi) * (start.next() < 0.0f ? -1.0f : 1.0f);
        mag.assign(bins, 0.0f);
        liveBin.assign(bins, 0.0f);
        heldBin.assign(bins, 0.0f);
        smoothed.assign(bins, 0.0f);
        gains.assign(bins, 1.0f);
        filter.assign(bins, 1.0f);
        peaks.reserve(bins);
        bounds.reserve(bins);

        // Each bin's place among the motion bands and the display bands, by its frequency on a log scale.
        bandAt.resize(bins);
        displayBand.resize(bins);
        for (std::size_t k = 0; k < bins; ++k)
        {
            const double hz = std::max(1.0, (double) k * rate / size);
            const double motionPos = std::log(hz / 40.0) / std::log(16000.0 / 40.0) * (kMotionBands - 1);
            bandAt[k] = (float) std::clamp(motionPos, 0.0, (double) kMotionBands - 1.0);
            const double displayPos = std::log(hz / kDisplayLowHz) / std::log(kDisplayHighHz / kDisplayLowHz) * kDisplayBands;
            displayBand[k] = hz < kDisplayLowHz || displayPos >= kDisplayBands ? -1 : (int) displayPos;
        }
        // Low bands narrower than a bin hold none; they show the bin nearest their middle.
        for (int b = 0; b < kDisplayBands; ++b)
        {
            const double middle = kDisplayLowHz * std::pow(kDisplayHighHz / kDisplayLowHz, (b + 0.5) / kDisplayBands);
            nearestBin[(std::size_t) b] = std::clamp((int) std::lround(middle * size / rate), 1, half);
            hasBins[(std::size_t) b] = false;
        }
        for (const int b : displayBand)
            if (b >= 0)
                hasBins[(std::size_t) b] = true;
        expectedStep.resize(bins);
        for (std::size_t k = 0; k < bins; ++k)
            expectedStep[k] = wrap((float) std::fmod(kTwoPi * hop / size * (double) k, kTwoPi));
        Dice bandDice(0x85ebca6bu);
        for (auto& b : bands)
        {
            b = {};
            b.from = bandDice.next();
            b.to = bandDice.next();
            b.pitchFrom = bandDice.next();
            b.pitchTo = bandDice.next();
            b.t = 0.5f + 0.5f * bandDice.next();
            b.speed = 1.0f + 0.3f * bandDice.next();
        }
        dice = Dice();
        cutoffs = { -1.0f, -1.0f };
        position = 0;
        counter = 0;
        taken = 0;
        level = 0.0f;
        morph = 1.0f;
        on = false;
        captured = false;
        captureWanted = false;
        dryGain = 1.0f;
        wetGain = 0.0f;
        live.fill(-120.0f);
        held.fill(-120.0f);
        update(settings);
    }

    void update(const Settings& s)
    {
        settings = s;
        if (std::abs(cutoffs[0] - s.lowCutHz) > 0.01f || std::abs(cutoffs[1] - s.highCutHz) > 0.01f)
        {
            cutoffs = { s.lowCutHz, s.highCutHz };
            // Fourth-order Butterworth slopes (24 dB an octave) on the magnitude; at their ends of the range they are off.
            for (std::size_t k = 0; k < filter.size(); ++k)
            {
                const double hz = (double) k * rate / size;
                double g = 1.0;
                if (s.lowCutHz > 20.5f)
                {
                    const double r = std::pow(hz / s.lowCutHz, 8.0);
                    g *= std::sqrt(r / (1.0 + r));
                }
                if (s.highCutHz < 19999.0f)
                    g *= std::sqrt(1.0 / (1.0 + std::pow(hz / s.highCutHz, 8.0)));
                filter[k] = (float) g;
            }
        }
    }

    /** On: takes what was heard and fades it in; off: fades it out. */
    void setFrozen(bool frozen)
    {
        if (frozen && ! on)
            captureWanted = true;
        on = frozen;
    }

    /** Takes a new freeze at the next hop, morphing from (or stacking on) the one that sounds. */
    void capture()
    {
        if (on)
            captureWanted = true;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int count)
    {
        const float mixDry = std::min(1.0f, 2.0f * (1.0f - settings.mix));
        const float mixWet = std::min(1.0f, 2.0f * settings.mix) * std::pow(10.0f, settings.levelDb / 20.0f);
        // About 20 ms, so knobs and the fade do not click.
        const float smooth = 1.0f - std::exp(-1.0f / (0.02f * (float) rate));
        for (int i = 0; i < count; ++i)
        {
            auto& left = channels[0];
            auto& right = channels[1];
            const float l = inL[i], r = inR[i];
            left.input[(std::size_t) position] = l;
            right.input[(std::size_t) position] = r;
            const float wl = left.ola[(std::size_t) position], wr = right.ola[(std::size_t) position];
            left.ola[(std::size_t) position] = 0.0f;
            right.ola[(std::size_t) position] = 0.0f;
            // The dry gives way only as far as the frozen layer is there, so a high mix does not mute the track.
            dryGain += smooth * ((1.0f - (1.0f - mixDry) * level) - dryGain);
            wetGain += smooth * (mixWet - wetGain);
            outL[i] = l * dryGain + wl * wetGain;
            outR[i] = r * dryGain + wr * wetGain;
            position = (position + 1) % size;
            if (++counter == hop)
            {
                counter = 0;
                frame();
            }
        }
    }

    int frameSize() const { return size; }
    int hopSize() const { return hop; }
    /** How far the frozen layer is faded in, 0 to 1. */
    float frozenLevel() const { return level; }
    bool isFrozen() const { return on; }
    /** How many freezes were taken since `prepare`. */
    int freezesTaken() const { return taken; }
    /** The input's and the frozen layer's spectrum in `kDisplayBands` bands, dB against a full-scale sine. */
    const std::array<float, kDisplayBands>& liveSpectrum() const { return live; }
    const std::array<float, kDisplayBands>& frozenSpectrum() const { return held; }

private:
    static constexpr int kMotionBands = 32;
    static constexpr int kLayers = 2;
    /** The random phase turns of the blur, from a table: a sine per bin would cost more than the FFT. */
    static constexpr int kJitterSteps = 1024;

    /** The gain that keeps a pad's level as the blur grows, from the loss measured at every tenth of it. */
    static float blurMakeUp(float blur)
    {
        static constexpr std::array<float, 11> lossDb { 0.0f, 0.1f, 0.45f, 1.46f, 2.72f, 3.72f, 4.3f, 4.66f, 4.87f, 4.99f, 5.1f };
        const float at = std::clamp(blur, 0.0f, 1.0f) * 10.0f;
        const int i = std::min((int) at, 9);
        const float gainDb = lossDb[(std::size_t) i] + (lossDb[(std::size_t) i + 1] - lossDb[(std::size_t) i]) * (at - (float) i);
        return std::pow(10.0f, gainDb / 20.0f);
    }

    struct Channel
    {
        std::vector<float> input, ola;
        // The analysis: power and power-weighted true frequency (in bins), averaged; the last frame's phase.
        std::vector<float> power, weightedFreq;
        std::vector<std::complex<float>> last, now;
        // The freeze: magnitudes morphing from one to the other, each peak's frequency (in bins), the phases the
        // analysis had (as unit phasors, so playing them back needs no sine), which the bins around a peak keep
        // against it.
        std::vector<float> fromMag, toMag, freq;
        std::vector<std::complex<float>> analysisPhasor;
        std::array<std::vector<float>, kLayers> synthPhase;
        std::vector<std::complex<float>> out;
    };

    struct Band
    {
        float from = 0, to = 0, pitchFrom = 0, pitchTo = 0, t = 0, speed = 1;
        float value() const { return from + (to - from) * (0.5f - 0.5f * std::cos((float) kPi * t)); }
        float pitch() const { return pitchFrom + (pitchTo - pitchFrom) * (0.5f - 0.5f * std::cos((float) kPi * t)); }
    };

    void frame()
    {
        analyse();
        if (captureWanted)
        {
            captureWanted = false;
            takeFreeze();
        }
        const float step = (float) hop / (std::max(settings.fadeMs, 1.0f) * 0.001f * (float) rate);
        level = on && captured ? std::min(1.0f, level + step) : std::max(0.0f, level - step);
        morph = std::min(1.0f, morph + step);
        if (level <= 0.0f)
        {
            held.fill(-120.0f);
            return;
        }
        moveBands();
        synthesise();
    }

    void analyse()
    {
        const auto n = (std::size_t) size;
        // Oldest sample first: the ring's write position is where the oldest one sits.
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto at = ((std::size_t) position + i) % n;
            buffer[i] = { window[i] * channels[0].input[at], window[i] * channels[1].input[at] };
        }
        fft.forward(buffer.data());
        const float a = 1.0f - std::exp(-(float) hop / (std::max(settings.captureMs, 1.0f) * 0.001f * (float) rate));
        const float toBins = (float) size / (float) (kTwoPi * hop);
        live.fill(0.0f);
        // One complex FFT of left + i·right: the two spectra part by the symmetry of real signals.
        for (int k = 0; k <= half; ++k)
        {
            const auto z = buffer[(std::size_t) k];
            const auto zc = std::conj(buffer[(std::size_t) ((size - k) % size)]);
            const std::array<std::complex<float>, 2> x { 0.5f * (z + zc), std::complex<float>(0.0f, -0.5f) * (z - zc) };
            float shown = 0.0f;
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels[c];
                const auto xr = x[c].real(), xi = x[c].imag();
                const float m2 = xr * xr + xi * xi;
                // The phase step since the last frame, as the angle of this frame times the last one's conjugate.
                const auto before = ch.last[(std::size_t) k];
                const float step = std::atan2(xi * before.real() - xr * before.imag(), xr * before.real() + xi * before.imag());
                const float delta = wrap(step - expectedStep[(std::size_t) k]);
                ch.last[(std::size_t) k] = x[c];
                const float trueBin = (float) k + delta * toBins;
                auto& p = ch.power[(std::size_t) k];
                auto& f = ch.weightedFreq[(std::size_t) k];
                p += a * (m2 - p);
                f += a * (m2 * trueBin - f);
                if (p < 1.0e-24f)
                    p = f = 0.0f;
                shown += 0.5f * m2;
            }
            if (const int b = displayBand[(std::size_t) k]; b >= 0)
                live[(std::size_t) b] = std::max(live[(std::size_t) b], shown);
            liveBin[(std::size_t) k] = shown;
        }
        fillEmpty(live, liveBin);
    }

    void takeFreeze()
    {
        const bool morphing = captured && level > 0.001f;
        for (auto& ch : channels)
        {
            double oldEnergy = 0.0, newEnergy = 0.0, combined = 0.0;
            for (int k = 0; k <= half; ++k)
            {
                const auto i = (std::size_t) k;
                const float now = ch.fromMag[i] + (ch.toMag[i] - ch.fromMag[i]) * morph;
                ch.fromMag[i] = now;
                const float fresh = std::sqrt(ch.power[i]);
                const float old2 = morphing ? settings.stack * now * now : 0.0f;
                const float fresh2 = fresh * fresh;
                oldEnergy += (double) now * now;
                newEnergy += fresh2;
                combined += old2 + fresh2;
                ch.toMag[i] = std::sqrt(old2 + fresh2);
                // Frequency and phase relation from whichever freeze the bin owes more to.
                if (fresh2 >= old2)
                {
                    const float f = ch.power[i] > 0.0f ? ch.weightedFreq[i] / ch.power[i] : (float) k;
                    ch.freq[i] = std::clamp(f, (float) k - 2.0f, (float) k + 2.0f);
                    const float m = std::abs(ch.last[i]);
                    ch.analysisPhasor[i] = m > 0.0f ? ch.last[i] / m : std::complex<float>(1.0f, 0.0f);
                }
            }
            // A layered freeze is as loud as the louder of the two, not their sum; build-ups stay in level.
            if (morphing && settings.stack > 0.0f && combined > 1.0e-20)
            {
                const double wanted = (1.0 - settings.stack) * newEnergy + settings.stack * std::max(oldEnergy, newEnergy);
                const auto scale = (float) std::sqrt(wanted / combined);
                for (auto& m : ch.toMag)
                    m *= scale;
            }
            if (! morphing)
                ch.fromMag = ch.toMag;
        }
        morph = morphing ? 0.0f : 1.0f;
        captured = true;
        ++taken;
    }

    void moveBands()
    {
        const float step = (float) hop / (std::max(settings.driftSeconds, 0.1f) * (float) rate);
        for (auto& b : bands)
        {
            b.t += step * b.speed;
            if (b.t >= 1.0f)
            {
                b.t -= 1.0f;
                b.from = b.to;
                b.to = dice.next();
                b.pitchFrom = b.pitchTo;
                b.pitchTo = dice.next();
            }
        }
        // Each band's gain once, the bins between two bands in between: far cheaper than a power per bin.
        const float depthDb = 12.0f * settings.motion;
        std::array<float, kMotionBands> bandGain {};
        for (std::size_t b = 0; b < bands.size(); ++b)
            bandGain[b] = std::pow(10.0f, depthDb * bands[b].value() / 20.0f);
        for (std::size_t k = 0; k < gains.size(); ++k)
        {
            const float pos = bandAt[k];
            const auto b = (std::size_t) std::min((int) pos, kMotionBands - 2);
            gains[k] = filter[k] * (bandGain[b] + (bandGain[b + 1] - bandGain[b]) * (pos - (float) b));
        }
    }

    float bandValue(std::size_t k, bool pitch) const
    {
        const float pos = bandAt[k];
        const int b = std::min((int) pos, kMotionBands - 2);
        const float frac = pos - (float) b;
        const auto& lo = bands[(std::size_t) b];
        const auto& hi = bands[(std::size_t) b + 1];
        return pitch ? lo.pitch() + (hi.pitch() - lo.pitch()) * frac : lo.value() + (hi.value() - lo.value()) * frac;
    }

    void synthesise()
    {
        const auto bins = (std::size_t) half + 1;
        const float ratio = std::pow(2.0f, settings.pitch / 12.0f);
        const std::array<float, kLayers> ratios { ratio, 2.0f * ratio };
        const std::array<float, kLayers> layerGain { 1.0f, settings.shimmer };
        // Up to about eight bins (90 Hz at 48 kHz) at full blur.
        const float spread = settings.blur > 0.0f ? std::exp(-1.0f / (8.0f * settings.blur * settings.blur)) : 0.0f;
        const float jitter = (float) kPi * settings.blur;
        const float advance = (float) (kTwoPi * hop / size);
        const float driftCents = 15.0f * settings.motion;
        // Hann analysis times Hann synthesis overlapping by four adds up to 1.5. Random phases make the overlapping
        // frames add in power rather than in amplitude, and wider lobes make shorter frames: the blur's loss, measured
        // on a pad (tests/FreezeTests.cpp), is made up for.
        const float makeUp = blurMakeUp(settings.blur);
        const float scale = makeUp * level / 1.5f / (float) size;

        for (std::size_t c = 0; c < 2; ++c)
        {
            auto& ch = channels[c];
            for (std::size_t k = 0; k < bins; ++k)
                mag[k] = ch.fromMag[k] + (ch.toMag[k] - ch.fromMag[k]) * morph;
            if (spread > 0.0f)
            {
                // Smoothed in power, forwards and back so it stays centred; the width grows smoothly with the blur.
                for (std::size_t k = 0; k < bins; ++k)
                    smoothed[k] = mag[k] * mag[k];
                for (std::size_t k = 1; k < bins; ++k)
                    smoothed[k] += spread * (smoothed[k - 1] - smoothed[k]);
                for (std::size_t k = bins - 1; k-- > 0;)
                    smoothed[k] += spread * (smoothed[k + 1] - smoothed[k]);
                for (std::size_t k = 0; k < bins; ++k)
                    mag[k] = std::sqrt(std::max(0.0f, smoothed[k]));
            }

            // Peaks, and where each one's bins end: at the lowest point between it and the next.
            peaks.clear();
            float loudest = 0.0f;
            for (const float m : mag)
                loudest = std::max(loudest, m);
            const float floor = std::max(loudest * 1.0e-5f, 1.0e-9f);
            for (std::size_t k = 1; k + 1 < bins; ++k)
                if (mag[k] > floor && mag[k] > mag[k - 1] && mag[k] >= mag[k + 1])
                    peaks.push_back((int) k);
            bounds.clear();
            bounds.push_back(0);
            for (std::size_t p = 0; p + 1 < peaks.size(); ++p)
            {
                int lowest = peaks[p];
                for (int k = peaks[p]; k <= peaks[p + 1]; ++k)
                    if (mag[(std::size_t) k] < mag[(std::size_t) lowest])
                        lowest = k;
                bounds.push_back(lowest + 1);
            }
            bounds.push_back(half + 1);

            std::fill(ch.out.begin(), ch.out.end(), std::complex<float>());
            for (std::size_t layer = 0; layer < kLayers; ++layer)
            {
                if (layerGain[layer] <= 0.0f)
                    continue;
                auto& synth = ch.synthPhase[layer];
                for (std::size_t p = 0; p < peaks.size(); ++p)
                {
                    const auto peak = (std::size_t) peaks[p];
                    const float drift = std::pow(2.0f, driftCents * bandValue(peak, true) / 1200.0f);
                    const float f = ch.freq[peak] * ratios[layer] * drift;
                    const int shift = (int) std::lround(ch.freq[peak] * ratios[layer] - ch.freq[peak]);
                    const int target = (int) peak + shift;
                    if (target <= 0 || target >= half)
                        continue;
                    const float psi = wrap(synth[(std::size_t) target] + advance * f);
                    synth[(std::size_t) target] = psi;
                    // The peak's phase, less its own analysis phase, turned for the right side: every bin of its
                    // region then needs one complex product, not a sine.
                    const float angle = psi + (c == 1 ? settings.width * stereoOffset[peak] : 0.0f);
                    const auto up = ch.analysisPhasor[peak];
                    const float cr = std::cos(angle), ci = std::sin(angle);
                    const float br = cr * up.real() + ci * up.imag(), bi = ci * up.real() - cr * up.imag();
                    for (int k = bounds[p]; k < bounds[p + 1]; ++k)
                    {
                        const int j = k + shift;
                        if (j < 0 || j > half)
                            continue;
                        const auto u = ch.analysisPhasor[(std::size_t) k];
                        float vr = br * u.real() - bi * u.imag(), vi = br * u.imag() + bi * u.real();
                        if (jitter > 0.0f)
                        {
                            const auto turn = jitterTable[(std::size_t) ((0.5f + 0.5f * jitter / (float) kPi * dice.next()) * (kJitterSteps - 1))];
                            const float tr = vr * turn.real() - vi * turn.imag();
                            vi = vr * turn.imag() + vi * turn.real();
                            vr = tr;
                        }
                        const float m = layerGain[layer] * mag[(std::size_t) k];
                        ch.out[(std::size_t) j] += std::complex<float>(m * vr, m * vi);
                    }
                }
            }
            for (std::size_t k = 0; k < bins; ++k)
                ch.out[k] *= gains[k];
        }

        // Both sides through one inverse FFT: left + i·right, each mirrored as a real signal's spectrum is.
        const auto& l = channels[0].out;
        const auto& r = channels[1].out;
        const std::complex<float> i1(0.0f, 1.0f);
        buffer[0] = { l[0].real(), r[0].real() };
        buffer[(std::size_t) half] = { l[(std::size_t) half].real(), r[(std::size_t) half].real() };
        held.fill(0.0f);
        for (int k = 1; k < half; ++k)
        {
            const auto lk = l[(std::size_t) k], rk = r[(std::size_t) k];
            buffer[(std::size_t) k] = lk + i1 * rk;
            buffer[(std::size_t) (size - k)] = std::conj(lk) + i1 * std::conj(rk);
            const float shown = 0.5f * (std::norm(lk) + std::norm(rk)) * level * level;
            if (const int b = displayBand[(std::size_t) k]; b >= 0)
                held[(std::size_t) b] = std::max(held[(std::size_t) b], shown);
            heldBin[(std::size_t) k] = shown;
        }
        fillEmpty(held, heldBin);
        for (auto& z : buffer)
            z = std::conj(z);
        fft.forward(buffer.data());
        const auto n = (std::size_t) size;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto at = ((std::size_t) position + i) % n;
            const float w = window[i] * scale;
            // conj of the result: real part is left, minus the imaginary part is right.
            channels[0].ola[at] += w * buffer[i].real();
            channels[1].ola[at] -= w * buffer[i].imag();
        }
    }

    void fillEmpty(std::array<float, kDisplayBands>& shown, const std::vector<float>& bins) const
    {
        // In power until here: a logarithm per band, not per bin.
        for (std::size_t b = 0; b < shown.size(); ++b)
            shown[b] = toDb(hasBins[b] ? shown[b] : bins[(std::size_t) nearestBin[b]]);
    }

    /** To -π ... π. */
    static float wrap(float phase)
    {
        constexpr auto twoPi = (float) kTwoPi;
        return phase - twoPi * std::nearbyint(phase / twoPi);
    }

    float toDb(float power) const
    {
        // A full-scale sine's peak bin under a Hann window has magnitude size / 4.
        const float reference = (float) size * (float) size / 16.0f;
        return 10.0f * std::log10(power / reference + 1.0e-12f);
    }

    double rate = 48000.0;
    int size = 4096, hop = 1024, half = 2048;
    Settings settings;
    Fft fft;
    std::vector<float> window;
    std::vector<std::complex<float>> buffer;
    std::array<Channel, 2> channels;
    std::vector<float> stereoOffset, mag, gains, filter, bandAt;
    std::vector<float> smoothed, liveBin, heldBin, expectedStep;
    std::array<bool, kDisplayBands> hasBins {};
    std::array<int, kDisplayBands> nearestBin {};
    std::array<std::complex<float>, kJitterSteps> jitterTable {};
    std::vector<int> peaks, bounds, displayBand;
    std::array<Band, kMotionBands> bands {};
    std::array<float, 2> cutoffs { -1.0f, -1.0f };
    Dice dice;
    int position = 0, counter = 0, taken = 0;
    float level = 0.0f, morph = 1.0f, dryGain = 1.0f, wetGain = 0.0f;
    bool on = false, captured = false, captureWanted = false;
    std::array<float, kDisplayBands> live {}, held {};
};
} // namespace tonwerkfreeze
