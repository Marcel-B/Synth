#pragma once

#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

/**
 * Tonwerk Robovox: a voice sung through a synth. The robot voices of AI songs are a vocoder whose carrier is not the
 * singer's pitch but a low, perfectly steady note (in Marcel's example D1 and G1, the bass line), stepping from note to
 * note without a glide, with the voice's formants laid over its dense comb of harmonics. A vocoder fails at this when
 * its carrier is played by hand at the voice's pitch or has too few bands; here the carrier is made inside, from the
 * voice's own pitch quantised to a scale and moved down by octaves (or from MIDI notes), and the formants are taken from
 * a short-time spectrum, finer than a filter bank. Plain C++, mono in and out.
 */
namespace tonwerkrobovox
{
/** Menu orders are saved in projects: append only. */
enum class Mode
{
    voice,
    midi
};
enum class Scale
{
    chromatic,
    major,
    minor,
    pentatonic
};
enum class CarrierWave
{
    saw,
    square,
    pulse
};

struct Settings
{
    Mode mode = Mode::voice;
    /** The scale's root, 0 for C to 11 for B. */
    int key = 2;
    Scale scale = Scale::minor;
    /** Octaves between the voice and its carrier (voice mode only). */
    int octave = -2;
    float glideMs = 0.0f;
    CarrierWave wave = CarrierWave::saw;
    /** Semitones the formants move, without the pitch. */
    float formant = 0.0f;
    /** How wide the voice's spectrum is smoothed into its envelope: narrower follows the formants more closely. */
    float bandwidthHz = 250.0f;
    /** 0 to 1: how much hiss replaces the carrier in a consonant. */
    float sibilance = 0.6f;
    /** 0 to 1: the carrier's lowest harmonics kept up under the voice, the bass foundation of the example. */
    float bass = 0.4f;
    /** 0 to 1, 1 only the robot. */
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** A frequency as a (fractional) MIDI note. */
inline double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

/** The notes of a scale as bits over the twelve semitones above its root. */
inline uint32_t scaleMask(Scale scale)
{
    switch (scale)
    {
        case Scale::major: return 0b101010110101u;
        case Scale::minor: return 0b010110101101u;
        case Scale::pentatonic: return 0b010010101001u;
        case Scale::chromatic: break;
    }
    return 0xfffu;
}

inline bool inScale(int note, int key, Scale scale)
{
    const int degree = ((note - key) % 12 + 12) % 12;
    return ((scaleMask(scale) >> degree) & 1u) != 0;
}

/** The note of the scale nearest to `midi` (fractional). */
inline int nearestInScale(double midi, int key, Scale scale)
{
    const int centre = (int) std::lround(midi);
    int best = centre;
    double distance = 1.0e9;
    for (int note = centre - 6; note <= centre + 6; ++note)
        if (inScale(note, key, scale) && std::abs(note - midi) < distance)
        {
            distance = std::abs(note - midi);
            best = note;
        }
    return best;
}

/**
 * Snaps a sung pitch to the scale with zero retune time, the hard steps of a pitch corrector turned all the way. A note
 * holds until the voice is clearly nearer another, and a new note must be heard twice in a row, so a wobble on a
 * boundary or one wrong reading does not flutter the carrier.
 */
class Quantiser
{
public:
    static constexpr double kHysteresis = 0.3;

    void reset()
    {
        current = -1;
        pending = -1;
    }

    /** Takes a pitch reading (MIDI, fractional) and returns the note to play, or -1 before the first. */
    int next(double midi, int key, Scale scale)
    {
        const int nearest = nearestInScale(midi, key, scale);
        if (current >= 0 && inScale(current, key, scale)
            && std::abs(midi - current) <= std::abs(midi - nearest) + kHysteresis)
        {
            pending = -1;
            return current;
        }
        if (current < 0 || nearest == pending)
        {
            current = nearest;
            pending = -1;
        }
        else
            pending = nearest;
        return current;
    }

    int note() const { return current; }

private:
    int current = -1;
    int pending = -1;
};

/**
 * The voice's pitch by YIN (de Cheveigné and Kawahara), on a copy lowpassed at 1 kHz and taken down to about 12 kHz:
 * plenty for a voice's fundamental from 60 Hz to 1 kHz and a fraction of the work. A reading uses the last 25 ms.
 */
class PitchTracker
{
public:
    void prepare(double sampleRate)
    {
        factor = std::max(1, (int) std::lround(sampleRate / 12000.0));
        rate = sampleRate / factor;
        window = (int) std::lround(0.025 * rate);
        tauMin = std::max(2, (int) (rate / 1000.0));
        tauMax = (int) std::ceil(rate / 60.0);
        length = window + tauMax + 1;
        history.assign((std::size_t) (2 * length), 0.0f);
        difference.assign((std::size_t) tauMax + 2, 0.0f);
        // Butterworth, fourth order, as two biquads (Q 0.54 and 1.31).
        const double w = std::tan(3.14159265358979 * 1000.0 / sampleRate);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const double q = i == 0 ? 0.5411961 : 1.3065630;
            const double norm = 1.0 / (1.0 + w / q + w * w);
            auto& s = lowpass[i];
            s = {};
            s.b0 = w * w * norm;
            s.b1 = 2.0 * s.b0;
            s.b2 = s.b0;
            s.a1 = 2.0 * (w * w - 1.0) * norm;
            s.a2 = (1.0 - w / q + w * w) * norm;
        }
        count = 0;
        at = 0;
    }

    void push(float x)
    {
        double y = x;
        for (auto& s : lowpass)
            y = s.process(y);
        if (++count < factor)
            return;
        count = 0;
        // Written twice, so the last `length` samples always lie in one piece.
        history[(std::size_t) at] = history[(std::size_t) (at + length)] = (float) y;
        at = (at + 1) % length;
    }

    /** The pitch in Hz, or 0 when the last 25 ms are too quiet or not periodic (a consonant, a breath). */
    double analyse()
    {
        const float* x = history.data() + at;
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += (double) x[j] * x[j];
        // About -55 dBFS: below that a reading would be noise.
        if (energy < window * 3.0e-6)
            return 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            const float* y = x + tau;
            for (int j = 0; j < window; ++j)
            {
                const float d = x[j] - y[j];
                sum += d * d;
            }
            difference[(std::size_t) tau] = sum;
        }
        // Cumulative mean normalised difference: dips below the threshold mark a period.
        double running = 0.0;
        int found = -1;
        double best = 1.0e9;
        int bestTau = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += difference[(std::size_t) tau];
            const float normalised = running > 0.0 ? (float) (difference[(std::size_t) tau] * tau / running) : 1.0f;
            difference[(std::size_t) tau] = normalised;
            if (tau >= tauMin && normalised < best)
            {
                best = normalised;
                bestTau = tau;
            }
        }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (difference[(std::size_t) tau] < kThreshold)
            {
                while (tau + 1 < tauMax && difference[(std::size_t) tau + 1] < difference[(std::size_t) tau])
                    ++tau;
                found = tau;
                break;
            }
        if (found < 0)
        {
            if (best > kUnvoiced)
                return 0.0;
            found = bestTau;
        }
        // A parabola through the dip and its neighbours finds the period between samples.
        double period = found;
        if (found > 1 && found < tauMax)
        {
            const double a = difference[(std::size_t) found - 1], b = difference[(std::size_t) found],
                         c = difference[(std::size_t) found + 1];
            const double denominator = a - 2.0 * b + c;
            if (denominator > 0.0)
                period += 0.5 * (a - c) / denominator;
        }
        return rate / period;
    }

private:
    static constexpr float kThreshold = 0.15f;
    static constexpr float kUnvoiced = 0.35f;

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double process(double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    std::array<Biquad, 2> lowpass;
    std::vector<float> history, difference;
    double rate = 12000.0;
    int factor = 4, window = 300, tauMin = 12, tauMax = 200, length = 501, count = 0, at = 0;
};

/** An in-place radix-2 complex FFT with its tables made once. */
class Fft
{
public:
    void prepare(int size)
    {
        n = size;
        twiddles.resize((std::size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
            twiddles[(std::size_t) k] = std::polar(1.0f, (float) (-2.0 * 3.14159265358979 * k / n));
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
        for (int half = 1; half < n; half *= 2)
        {
            const int step = n / (2 * half);
            for (int start = 0; start < n; start += 2 * half)
                for (int k = 0; k < half; ++k)
                {
                    const auto t = twiddles[(std::size_t) (k * step)] * data[start + k + half];
                    data[start + k + half] = data[start + k] - t;
                    data[start + k] += t;
                }
        }
    }

private:
    int n = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> reversed;
};

/**
 * The robot: pitch tracking, the carrier and a short-time vocoder. Every quarter frame the voice's and the carrier's
 * spectra are taken together (one complex FFT carries both), the carrier is flattened by its own envelope and given
 * the voice's, and the frames are added back with Hann windows at 75 % overlap. The voice is delayed half a frame
 * before the vocoder, so the carrier's pitch, read from the voice as it comes in, changes with the syllable it belongs
 * to. Latency: a frame and a half.
 */
class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        // About 21 ms at any rate: long enough for the formants, short enough for consonants.
        frame = 1024;
        while (frame * 48000.0 < 1024.0 * sampleRate * 0.75)
            frame *= 2;
        hop = frame / 4;
        lead = frame / 2;
        fft.prepare(frame);
        window.resize((std::size_t) frame);
        for (int i = 0; i < frame; ++i)
            window[(std::size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / frame));
        voiceRing.assign((std::size_t) frame, 0.0f);
        carrierRing.assign((std::size_t) frame, 0.0f);
        overlap.assign((std::size_t) frame, 0.0f);
        spectrum.assign((std::size_t) frame, {});
        const auto bins = (std::size_t) frame / 2 + 1;
        voicePower.assign(bins, 0.0f);
        carrierPower.assign(bins, 0.0f);
        voiceEnvelope.assign(bins, 0.0f);
        carrierEnvelope.assign(bins, 0.0f);
        scratch.assign(bins + 1, 0.0);
        leadLine.assign((std::size_t) lead, 0.0f);
        dryLine.assign((std::size_t) latency(), 0.0f);
        pitch.prepare(sampleRate);
        quantiser.reset();
        oscillator.reset();
        midiQueue.clear();
        midiQueue.reserve(256);
        heldNote = -1;
        carrierPitch = -1.0;
        noiseShare = 0.0f;
        carrierAverage.assign(bins, 0.0f);
        // The carrier's spectrum is held and released over about 50 ms: a note under 50 Hz has frames without a single
        // edge of its wave, and flattening those by themselves would blow up what little they hold.
        average = (float) (1.0 - std::exp(-hop / (0.05 * sampleRate)));
        ringAt = overlapAt = hopCount = leadAt = dryAt = 0;
        detected = -1.0;
    }

    /** Samples the output lags the input: a frame and the voice's head start. */
    int latency() const { return frame + lead; }

    void update(const Settings& s)
    {
        settings = s;
        gain = std::pow(10.0f, s.outputDb / 20.0f);
        glide = s.glideMs > 0.0f ? 1.0 - std::exp(-1.0 / (s.glideMs * 0.001 * rate)) : 1.0;
    }

    /**
     * A MIDI note for the carrier (midi mode). It takes effect when the voice it was played with reaches the vocoder,
     * and holds after its key is let go, as a vocoder's carrier would be held: the voice decides when it sounds.
     */
    void setMidiNote(int note)
    {
        if (midiQueue.size() < midiQueue.capacity())
            midiQueue.push_back({ lead, note });
    }

    void process(const float* in, float* out, int count)
    {
        for (int i = 0; i < count; ++i)
            out[i] = next(in[i]);
    }

    float next(float x)
    {
        pitch.push(x);
        advanceMidi();

        // The carrier, gliding to its note or stepping straight there.
        const int note = carrierNote();
        if (note >= 0)
        {
            if (carrierPitch < 0.0 || glide >= 1.0)
                carrierPitch = note;
            else
                carrierPitch += glide * (note - carrierPitch);
        }
        const double hz = tonwerk::noteFrequency(carrierPitch < 0.0 ? 38.0 : carrierPitch);
        double tone = 0.0;
        switch (settings.wave)
        {
            case CarrierWave::saw: tone = oscillator.next(tonwerk::Wave::sawtooth, hz, rate); break;
            case CarrierWave::square: tone = oscillator.next(tonwerk::Wave::square, hz, rate); break;
            case CarrierWave::pulse: tone = 2.0 * oscillator.next(tonwerk::Wave::square, hz, rate, 0.15); break;
        }
        const auto carrier = (float) tone;

        // The voice reaches the vocoder `lead` samples late, in step with the carrier's pitch.
        const float voice = leadLine[(std::size_t) leadAt];
        leadLine[(std::size_t) leadAt] = x;
        leadAt = (leadAt + 1) % lead;

        const float wet = overlap[(std::size_t) ((overlapAt + hopCount) % frame)];
        overlap[(std::size_t) ((overlapAt + hopCount) % frame)] = 0.0f;
        voiceRing[(std::size_t) ringAt] = voice;
        carrierRing[(std::size_t) ringAt] = carrier;
        ringAt = (ringAt + 1) % frame;
        if (++hopCount == hop)
        {
            hopCount = 0;
            overlapAt = (overlapAt + hop) % frame;
            const double reading = pitch.analyse();
            detected = reading > 0.0 ? midiOf(reading) : -1.0;
            if (detected > 0.0)
                quantiser.next(detected, settings.key, settings.scale);
            vocode();
        }

        const float dry = dryLine[(std::size_t) dryAt];
        dryLine[(std::size_t) dryAt] = x;
        dryAt = (dryAt + 1) % (int) dryLine.size();
        return settings.mix * softClip(kMakeup * gain * wet) + (1.0f - settings.mix) * gain * dry;
    }

    /**
     * Straight up to 0.6, then bending towards 1 without reaching it. A low carrier makes a train of pulses whose peaks
     * stand far over its loudness; this keeps them under full scale, as the limiter on a finished song does.
     */
    static float softClip(float x)
    {
        constexpr float knee = 0.6f;
        const float size = std::abs(x);
        if (size <= knee)
            return x;
        return std::copysign(knee + (1.0f - knee) * std::tanh((size - knee) / (1.0f - knee)), x);
    }

    /** The carrier's note now (MIDI), -1 before there is one. */
    int carrierNote() const
    {
        if (settings.mode == Mode::midi && heldNote >= 0)
            return heldNote;
        const int sung = quantiser.note();
        return sung < 0 ? -1 : sung + 12 * settings.octave;
    }

    /** The voice's last pitch reading (MIDI, fractional), -1 while it is not singing. */
    double sungPitch() const { return detected; }

private:
    /** About +4 dB: what the flattened carrier loses against the voice at the default two octaves down. */
    static constexpr float kMakeup = 1.6f;

    struct MidiChange
    {
        int delay;
        int note;
    };

    void advanceMidi()
    {
        for (auto it = midiQueue.begin(); it != midiQueue.end();)
            if (--it->delay <= 0)
            {
                heldNote = it->note;
                it = midiQueue.erase(it);
            }
            else
                ++it;
    }

    /** Moving average over 2h + 1 bins of `values` into `out`, the edges averaging what they have. */
    void boxSmooth(const std::vector<float>& values, std::vector<float>& out, int h)
    {
        const int bins = (int) values.size();
        scratch[0] = 0.0;
        for (int k = 0; k < bins; ++k)
            scratch[(std::size_t) k + 1] = scratch[(std::size_t) k] + values[(std::size_t) k];
        for (int k = 0; k < bins; ++k)
        {
            const int lo = std::max(0, k - h), hi = std::min(bins - 1, k + h);
            out[(std::size_t) k] = (float) ((scratch[(std::size_t) hi + 1] - scratch[(std::size_t) lo]) / (hi - lo + 1));
        }
    }

    /** Two box passes: a triangle `2h` bins wide at its foot, without the ripple of one box. */
    void envelope(std::vector<float>& power, std::vector<float>& out, int h)
    {
        boxSmooth(power, out, h);
        boxSmooth(out, power, h);
        for (std::size_t k = 0; k < out.size(); ++k)
            out[k] = std::sqrt(power[k]);
    }

    void vocode()
    {
        const int bins = frame / 2 + 1;
        for (int i = 0; i < frame; ++i)
        {
            const auto at = (std::size_t) ((ringAt + i) % frame);
            spectrum[(std::size_t) i] = { voiceRing[at] * window[(std::size_t) i], carrierRing[at] * window[(std::size_t) i] };
        }
        fft.forward(spectrum.data());
        // One complex FFT of voice + i·carrier: their spectra part by the symmetry of real signals.
        const float binHz = (float) (rate / frame);
        double low = 0.0, high = 0.0, body = 0.0;
        int bodyBins = 0;
        for (int k = 0; k < bins; ++k)
        {
            const auto z = spectrum[(std::size_t) k];
            const auto mirror = std::conj(spectrum[(std::size_t) ((frame - k) % frame)]);
            const auto v = 0.5f * (z + mirror);
            const auto c = std::complex<float>(0.0f, -0.5f) * (z - mirror);
            spectrum[(std::size_t) k] = c;
            voicePower[(std::size_t) k] = std::norm(v);
            carrierPower[(std::size_t) k] = std::norm(c);
            const float f = k * binHz;
            if (f > 4000.0f)
                high += voicePower[(std::size_t) k];
            else if (f > 100.0f)
                low += voicePower[(std::size_t) k];
            if (f > 100.0f && f < 4000.0f)
            {
                body += voicePower[(std::size_t) k];
                ++bodyBins;
            }
        }
        // Hiss: a frame whose energy lies mostly above 4 kHz is a consonant, and noise takes the carrier's place.
        const double share = high / (low + high + 1.0e-12);
        noiseShare = settings.sibilance * (float) std::clamp((share - 0.2) / 0.3, 0.0, 1.0);

        const int h = std::max(1, (int) std::lround(0.5f * settings.bandwidthHz / binHz));
        envelope(voicePower, voiceEnvelope, h);
        // The carrier's own envelope must not see its single harmonics, or flattening it would flatten them too.
        const double carrierHz = tonwerk::noteFrequency(carrierPitch < 0.0 ? 38.0 : carrierPitch);
        const int hc = std::max(h, (int) std::lround(carrierHz / binHz));
        envelope(carrierPower, carrierEnvelope, hc);
        float loudest = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            auto& held = carrierAverage[(std::size_t) k];
            // Up at once (the first frame, a louder note), down slowly through frames between the wave's edges.
            const float now = carrierEnvelope[(std::size_t) k] * carrierEnvelope[(std::size_t) k];
            held = std::max(now, held + average * (now - held));
            loudest = std::max(loudest, held);
        }
        // 60 dB under the carrier's strongest bin at most: nothing it hardly has is raised further.
        const float floor = std::sqrt(loudest) * 1.0e-3f + 1.0e-9f;

        const float ratio = std::pow(2.0f, settings.formant / 12.0f);
        const float level = (float) std::sqrt(body / std::max(1, bodyBins));
        for (int k = 0; k < bins; ++k)
        {
            // The voice's envelope read from where it lies before the shift.
            const float from = k / ratio;
            const int below = (int) from;
            float shaped = 0.0f;
            if (below + 1 < bins)
            {
                const float t = from - below;
                shaped = voiceEnvelope[(std::size_t) below] * (1.0f - t) + voiceEnvelope[(std::size_t) below + 1] * t;
            }
            const float f = k * binHz;
            const float foundation = f < 120.0f ? 1.0f : f < 400.0f ? (400.0f - f) / 280.0f : 0.0f;
            shaped = std::max(shaped, settings.bass * level * foundation);
            const float flat = std::max(std::sqrt(carrierAverage[(std::size_t) k]), floor);
            // White noise of the same level in place of the tone: unit bins of random phase.
            const float phase = 3.14159265f * (float) noise.next();
            const auto hiss = std::complex<float>(std::cos(phase), std::sin(phase));
            spectrum[(std::size_t) k] = k == 0 ? 0.0f
                                               : (spectrum[(std::size_t) k] / flat * (1.0f - noiseShare) + hiss * noiseShare)
                                                     * shaped;
        }
        // The real output's spectrum is its own mirror; conjugate in, conjugate out makes the forward FFT an inverse.
        for (int k = 1; k < frame / 2; ++k)
            spectrum[(std::size_t) (frame - k)] = std::conj(spectrum[(std::size_t) k]);
        spectrum[(std::size_t) frame / 2] = { spectrum[(std::size_t) frame / 2].real(), 0.0f };
        for (auto& z : spectrum)
            z = std::conj(z);
        fft.forward(spectrum.data());
        // Hann in and Hann out at 75 % overlap add up to 1.5.
        const float scale = 1.0f / (1.5f * (float) frame);
        for (int i = 0; i < frame; ++i)
            overlap[(std::size_t) ((overlapAt + i) % frame)] += spectrum[(std::size_t) i].real() * window[(std::size_t) i] * scale;
    }

    Settings settings;
    double rate = 48000.0, glide = 1.0, carrierPitch = -1.0, detected = -1.0;
    float gain = 1.0f, noiseShare = 0.0f, average = 0.1f;
    int frame = 1024, hop = 256, lead = 512;
    int ringAt = 0, overlapAt = 0, hopCount = 0, leadAt = 0, dryAt = 0, heldNote = -1;
    Fft fft;
    PitchTracker pitch;
    Quantiser quantiser;
    tonwerk::BlepOscillator oscillator;
    tonwerk::Noise noise { 0x2545f491u };
    std::vector<float> window, voiceRing, carrierRing, overlap, leadLine, dryLine;
    std::vector<float> voicePower, carrierPower, voiceEnvelope, carrierEnvelope, carrierAverage;
    std::vector<double> scratch;
    std::vector<std::complex<float>> spectrum;
    std::vector<MidiChange> midiQueue;
};
} // namespace tonwerkrobovox
