#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Tonwerk Tape: a tape machine as an effect, plain C++. What goes through it is recorded and played back the way a
 * worn machine does: the record head saturates (harder on the highs, which the record EQ lifts), the tape hisses and
 * loses contact now and then, the capstan does not run quite even (wow, slow; flutter, fast), and the playback head
 * adds its bump in the lows while the narrow track takes off the highs.
 */
namespace tonwerktape
{
constexpr double kPi = 3.14159265358979323846;

/** The tape in the machine; each has its own lows, highs and saturation. Saved in projects: append only. */
enum class TapeType
{
    reel,
    chrome,
    ferric,
    dictaphone
};

/** The knobs, in their own units. */
struct Settings
{
    float inputDb = 0.0f;
    float drive = 0.4f;
    TapeType type = TapeType::ferric;
    float toneHz = 12000.0f;
    float wow = 0.2f;
    float flutter = 0.2f;
    float noiseDb = -66.0f;
    float dropouts = 0.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** The hiss knob's lowest value turns it off. */
constexpr float kNoiseOff = -90.0f;
/** Wow at 100 %: about 1.6 % speed (28 cents) at its peak, the sound of a cassette deck with a stretched belt. */
constexpr double kWowSpeed = 0.016;
constexpr double kWowHz = 0.55;
/** Flutter at 100 %: 0.4 % speed, about ten times what a good deck is specified for. */
constexpr double kFlutterSpeed = 0.004;
/**
 * The playback point sits this far behind the record point, so the wobble has room either side. The dry signal is
 * read just as far back, and the processor reports it to the host as latency.
 */
constexpr double kCentreSeconds = 0.008;

/** What a tape type does, apart from the knobs. */
struct Character
{
    /** The playback head's bump in the lows. */
    double bumpHz, bumpDb;
    /** Below this the head gives nothing (second order). */
    double highpassHz;
    /** A fixed first-order roll-off on top of the tone knob; 0 for none. */
    double ceilingHz;
    /** The magnetic bias's offset: the curve's asymmetry, which brings even harmonics. */
    double bias;
    /** How early it saturates, against the reel. */
    double hardness;
};

inline const Character& characterOf(TapeType type)
{
    static const std::array<Character, 4> table { {
        // Reel to reel at 15 ips: a deep bump, the widest band, a clean curve.
        { 48.0, 2.5, 18.0, 0.0, 0.04, 1.0 },
        // Type II cassette: tight lows, bright.
        { 70.0, 1.5, 28.0, 0.0, 0.08, 1.15 },
        // Type I cassette, ferric: a fuller bump, more asymmetric and earlier into saturation.
        { 85.0, 2.0, 32.0, 0.0, 0.14, 1.35 },
        // A micro-cassette dictaphone: a small speaker's band and a hard curve.
        { 450.0, 3.0, 220.0, 4500.0, 0.2, 1.8 },
    } };
    return table[(std::size_t) std::clamp((int) type, 0, 3)];
}

inline double fromDecibels(double db) { return std::pow(10.0, db / 20.0); }

/** RBJ's biquad in transposed direct form II, in double: a 20 Hz highpass in float would hum with rounding. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;

    double process(double x)
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void clear() { s1 = s2 = 0.0; }

    void set(double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0;
        b1 = nb1 / a0;
        b2 = nb2 / a0;
        a1 = na1 / a0;
        a2 = na2 / a0;
    }
    void lowpass(double rate, double hz, double q)
    {
        const double w = 2.0 * kPi * std::min(hz, 0.49 * rate) / rate, alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        set((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void highpass(double rate, double hz, double q)
    {
        const double w = 2.0 * kPi * std::min(hz, 0.49 * rate) / rate, alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        set((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void peak(double rate, double hz, double q, double db)
    {
        const double a = std::pow(10.0, db / 40.0);
        const double w = 2.0 * kPi * std::min(hz, 0.49 * rate) / rate, alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        set(1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a);
    }
};

/** A first-order filter: shelves (bilinear, so the de-emphasis undoes the pre-emphasis exactly) and a lowpass. */
struct OnePole
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, s = 0.0;

    double process(double x)
    {
        const double y = b0 * x + s;
        s = b1 * x - a1 * y;
        return y;
    }
    void clear() { s = 0.0; }

    /** Unity below `hz`, `gain` far above it; `inverse` gives the shelf that undoes it. */
    void highShelf(double rate, double hz, double gain, bool inverse)
    {
        const double k = std::tan(kPi * std::min(hz, 0.45 * rate) / rate);
        if (! inverse)
        {
            b0 = (gain + k) / (1.0 + k);
            b1 = (k - gain) / (1.0 + k);
            a1 = (k - 1.0) / (1.0 + k);
        }
        else
        {
            b0 = (1.0 + k) / (gain + k);
            b1 = (k - 1.0) / (gain + k);
            a1 = (k - gain) / (gain + k);
        }
    }
    void lowpass(double rate, double hz)
    {
        const double k = std::tan(kPi * std::min(hz, 0.45 * rate) / rate);
        b0 = b1 = k / (1.0 + k);
        a1 = (k - 1.0) / (1.0 + k);
    }
};

/** Small, fast and repeatable noise: the same seed gives the same hiss, wobble and dropouts in every bounce. */
struct Random
{
    std::uint32_t state = 1;

    void seed(std::uint32_t value) { state = value != 0 ? value : 0x9e3779b9u; }
    std::uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** Uniform in [0, 1). */
    double unit() { return (double) (next() >> 8) * (1.0 / 16777216.0); }
    /** Uniform in [-1, 1). */
    double bipolar() { return 2.0 * unit() - 1.0; }
};

/**
 * The record head's saturation: a tanh moved off centre by the bias. Scaled so a swing of ±0.25 (a −12 dBFS peak,
 * about where a mix's body sits) comes out ±0.25 at any drive: more drive squashes the peaks and lifts what is below,
 * as hitting tape harder does, and the knob changes the colour more than the loudness. First-order antiderivative anti-aliasing: what the curve adds above Nyquist
 * folds back far less, without oversampling.
 */
class Saturator
{
public:
    static constexpr double kReference = 0.25;

    void set(double drive, const Character& c)
    {
        k = c.hardness * (0.25 + 6.0 * drive * drive);
        bias = c.bias;
        offset = std::tanh(k * bias);
        scale = 2.0 * kReference / (std::tanh(k * (kReference + bias)) - std::tanh(k * (bias - kReference)));
        // A change of curve would make the antiderivative's difference jump; start from the next sample.
        hasPrevious = false;
    }

    double process(double x)
    {
        const double fx = antiderivative(x);
        double y;
        if (hasPrevious && std::abs(x - previous) > 1.0e-5)
            y = (fx - previousF) / (x - previous);
        else
            y = curve(hasPrevious ? 0.5 * (x + previous) : x);
        previous = x;
        previousF = fx;
        hasPrevious = true;
        return y;
    }
    void clear() { hasPrevious = false; }

    double curve(double x) const { return scale * (std::tanh(k * (x + bias)) - offset); }

private:
    static double logCosh(double u)
    {
        const double a = std::abs(u);
        return a + std::log1p(std::exp(-2.0 * a)) - std::log(2.0);
    }
    double antiderivative(double x) const { return scale * (logCosh(k * (x + bias)) / k - offset * x); }

    double k = 1.0, bias = 0.0, offset = 0.0, scale = 1.0;
    double previous = 0.0, previousF = 0.0;
    bool hasPrevious = false;
};

/**
 * The capstan and the tape's own movement, shared by both channels as on a real machine: how far the playback point
 * is off its centre, in seconds. Wow is a slow sine whose speed drifts (a belt does not stretch evenly) plus a slower
 * one; flutter two faster ones. Depths glide, so turning a knob never jumps the pitch.
 */
class Motion
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        glide = 1.0 - std::exp(-1.0 / (0.03 * rate));
        driftGlide = 1.0 - std::exp(-1.0 / (0.4 * rate));
        carryDecay = std::exp(-1.0 / (0.4 * rate));
    }

    void reset(std::uint32_t seed)
    {
        random.seed(seed);
        wowPhase = random.unit();
        slowPhase = random.unit();
        flutterPhase = random.unit();
        fastPhase = random.unit();
        drift = driftTarget = 0.0;
        untilDrift = 0;
        // The playback point would jump to the new tape's place; it glides there instead, so a sound already
        // running when the transport starts does not click.
        carried = last - offset();
    }

    /** The offset for the next sample, in seconds, for wow and flutter from 0 to 1. */
    double next(double wow, double flutter)
    {
        wowDepth += glide * (wow - wowDepth);
        flutterDepth += glide * (flutter - flutterDepth);
        if (--untilDrift <= 0)
        {
            driftTarget = random.bipolar();
            untilDrift = (int) ((0.5 + random.unit()) * rate);
        }
        drift += driftGlide * (driftTarget - drift);

        advance(wowPhase, kWowHz * (1.0 + 0.3 * drift));
        advance(slowPhase, 0.23);
        advance(flutterPhase, 7.3);
        advance(fastPhase, 12.1);
        carried *= carryDecay;
        last = offset() + carried;
        return last;
    }

private:
    void advance(double& phase, double hz)
    {
        phase += hz / rate;
        phase -= std::floor(phase);
    }
    static double sine(double phase) { return std::sin(2.0 * kPi * phase); }

    double offset() const
    {
        // An offset of A·sin(2πft) changes the speed by 2πfA at its steepest; these amplitudes give the stated speeds.
        const double wowAmplitude = wowDepth * kWowSpeed / (2.0 * kPi * kWowHz);
        const double flutterAmplitude = flutterDepth * kFlutterSpeed / (2.0 * kPi * 7.3);
        const double wobble = wowAmplitude * (0.8 * sine(wowPhase) + 0.3 * sine(slowPhase));
        const double shake = flutterAmplitude * (0.75 * sine(flutterPhase) + 0.25 * 7.3 / 12.1 * sine(fastPhase));
        return wobble + shake;
    }

    double rate = 48000.0, glide = 0.0, driftGlide = 0.0;
    double wowDepth = 0.0, flutterDepth = 0.0;
    double wowPhase = 0.0, slowPhase = 0.0, flutterPhase = 0.0, fastPhase = 0.0;
    double drift = 0.0, driftTarget = 0.0;
    double last = 0.0, carried = 0.0, carryDecay = 0.0;
    int untilDrift = 0;
    Random random;
};

/**
 * Where the tape loses contact with the head: now and then the level sinks for 30 to 200 ms, deeper and more often
 * the further the knob is up. Shared by both channels (it is the whole tape that lifts off).
 */
class Dropouts
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        glide = 1.0 - std::exp(-1.0 / (0.006 * rate));
    }
    void reset(std::uint32_t seed)
    {
        random.seed(seed);
        gain = target = 1.0;
        remaining = 0;
    }

    double next(double amount)
    {
        if (remaining > 0)
        {
            if (--remaining == 0)
                target = 1.0;
        }
        else if (amount > 0.0 && random.unit() < amount * 1.5 / rate)
        {
            // Up to one and a half a second at 100 %.
            remaining = (int) ((0.03 + 0.17 * random.unit()) * rate);
            target = fromDecibels(-(3.0 + 15.0 * random.unit()) * (0.4 + 0.6 * amount));
        }
        gain += glide * (target - gain);
        return gain;
    }

private:
    double rate = 48000.0, glide = 0.0, gain = 1.0, target = 1.0;
    int remaining = 0;
    Random random;
};

/** The whole machine, for one or two channels. */
class TapeEngine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate)
    {
        rate = sampleRate;
        centre = kCentreSeconds * rate;
        std::size_t size = 1;
        while ((double) size < 2.0 * centre + 8.0)
            size <<= 1;
        mask = size - 1;
        for (auto& channel : channels)
        {
            channel.tape.assign(size, 0.0f);
            channel.dry.assign(size, 0.0f);
        }
        motion.prepare(rate);
        dropouts.prepare(rate);
        smoothing = 1.0 - std::exp(-1.0 / (0.02 * rate));
        reset(1);
        configured = false;
    }

    /** Clears the tape and starts the wobble, dropouts and hiss from `seed`, so a bounce repeats. */
    void reset(std::uint32_t seed)
    {
        resetRandom(seed);
        for (auto& channel : channels)
        {
            std::fill(channel.tape.begin(), channel.tape.end(), 0.0f);
            std::fill(channel.dry.begin(), channel.dry.end(), 0.0f);
            channel.clear();
        }
        write = 0;
    }

    /** Starts the dice again without touching the tape: the transport started, and the bounce should repeat. */
    void resetRandom(std::uint32_t seed)
    {
        motion.reset(seed * 2654435761u + 1u);
        dropouts.reset(seed * 2246822519u + 7u);
        for (std::size_t c = 0; c < channels.size(); ++c)
            channels[c].hiss.seed(seed * 3266489917u + 11u * (std::uint32_t) (c + 1));
    }

    void update(const Settings& s)
    {
        target = s;
        const auto& character = characterOf(s.type);
        const bool curveChanged = ! configured || s.type != current.type || std::abs(s.drive - current.drive) > 1.0e-6f;
        const bool typeChanged = ! configured || s.type != current.type;
        if (! configured)
        {
            tone = s.toneHz;
            inputGain = fromDecibels(s.inputDb);
            outputGain = fromDecibels(s.outputDb);
            mix = s.mix;
        }
        for (auto& channel : channels)
        {
            if (curveChanged)
                channel.saturator.set(s.drive, character);
            if (typeChanged)
            {
                channel.highpass.highpass(rate, character.highpassHz, 0.7071);
                channel.bump.peak(rate, character.bumpHz, 0.9, character.bumpDb);
                if (character.ceilingHz > 0.0)
                    channel.ceiling.lowpass(rate, character.ceilingHz);
                // The record EQ lifts the highs by 6 dB from 3 kHz, so they saturate first; playback takes it off.
                channel.preEmphasis.highShelf(rate, 3000.0, 2.0, false);
                channel.deEmphasis.highShelf(rate, 3000.0, 2.0, true);
            }
        }
        hasCeiling = character.ceilingHz > 0.0;
        current = s;
        configured = true;
    }

    /** Samples the output lags the input, the same for the dry part. */
    int latency() const { return (int) std::lround(centre); }

    /** The speed against nominal at the end of the last block, as a fraction (0.01 = 1 % fast). */
    double speed() const { return lastSpeed; }

    void process(float* const* audio, int numChannels, int numSamples)
    {
        numChannels = std::clamp(numChannels, 0, kMaxChannels);
        for (int start = 0; start < numSamples; start += kChunk)
        {
            const int count = std::min(kChunk, numSamples - start);
            prepareChunk(count);
            for (int c = 0; c < numChannels; ++c)
                processChannel(channels[(std::size_t) c], audio[c] + start, count);
            write = (write + (std::size_t) count) & mask;
        }
    }

private:
    static constexpr int kChunk = 64;

    struct Channel
    {
        std::vector<float> tape, dry;
        OnePole preEmphasis, deEmphasis, ceiling;
        Saturator saturator;
        Biquad highpass, bump, lowpass;
        Random hiss;

        void clear()
        {
            preEmphasis.clear();
            deEmphasis.clear();
            ceiling.clear();
            saturator.clear();
            highpass.clear();
            bump.clear();
            lowpass.clear();
        }
    };

    /** What both channels share for the next `count` samples: the wobble, the dropouts, the gains and the tone. */
    void prepareChunk(int count)
    {
        const double inputTarget = fromDecibels(target.inputDb), outputTarget = fromDecibels(target.outputDb);
        const double hissLevel = target.noiseDb <= kNoiseOff ? 0.0 : fromDecibels(target.noiseDb) / std::sqrt(2.0 / 3.0);
        for (int n = 0; n < count; ++n)
        {
            const double offset = motion.next(target.wow, target.flutter);
            readDelay[(std::size_t) n] = std::clamp(centre + offset * rate, 2.0, 2.0 * centre - 2.0);
            contact[(std::size_t) n] = dropouts.next(target.dropouts);
            inputGain += smoothing * (inputTarget - inputGain);
            outputGain += smoothing * (outputTarget - outputGain);
            mix += smoothing * ((double) target.mix - mix);
            inputGains[(std::size_t) n] = inputGain;
            wetGains[(std::size_t) n] = outputGain * mix;
            dryGains[(std::size_t) n] = outputGain * (1.0 - mix);
        }
        hissGain = hissLevel;
        // The playback point moving towards the record point plays faster.
        lastSpeed = (readDelay[(std::size_t) std::max(0, count - 2)] - readDelay[(std::size_t) (count - 1)]);
        // The tone glides chunk by chunk, so a sweep of the knob does not step.
        tone += 0.2 * ((double) target.toneHz - tone);
        for (auto& channel : channels)
            channel.lowpass.lowpass(rate, tone, 0.7071);
    }

    void processChannel(Channel& ch, float* samples, int count)
    {
        const auto size = mask + 1;
        for (int n = 0; n < count; ++n)
        {
            const auto i = (std::size_t) n;
            const auto at = (write + i) & mask;
            const double in = samples[n];
            ch.dry[at] = (float) in;

            // Record: the head saturates what the record EQ lifted, then the tape adds its hiss.
            double x = ch.preEmphasis.process(in * inputGains[i]);
            x = ch.deEmphasis.process(ch.saturator.process(x));
            // Two uniforms make a triangle: closer to a tape's Gaussian hiss than one, still cheap.
            x += hissGain * (ch.hiss.bipolar() + ch.hiss.bipolar());
            ch.tape[at] = (float) (x * contact[i]);

            // Play back from where the wobbling tape is now, between samples on a cubic.
            const double delay = readDelay[i];
            const auto whole = (std::size_t) delay;
            const double frac = delay - (double) whole;
            const auto base = (at + size - whole) & mask;
            const double ym1 = ch.tape[(base + 1) & mask], y0 = ch.tape[base], y1 = ch.tape[(base + size - 1) & mask],
                         y2 = ch.tape[(base + size - 2) & mask];
            double y = hermite(ym1, y0, y1, y2, frac);

            // The playback head: its bump, its band.
            y = ch.bump.process(ch.highpass.process(y));
            if (hasCeiling)
                y = ch.ceiling.process(y);
            y = ch.lowpass.process(y);

            const double dry = ch.dry[(at + size - (std::size_t) latency()) & mask];
            samples[n] = (float) (y * wetGains[i] + dry * dryGains[i]);
        }
    }

    /** Catmull-Rom between y0 (at 0) and y1 (at 1); ym1 and y2 are the neighbours. */
    static double hermite(double ym1, double y0, double y1, double y2, double t)
    {
        const double c1 = 0.5 * (y1 - ym1);
        const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    double rate = 48000.0, centre = 384.0, smoothing = 0.0;
    std::size_t mask = 0, write = 0;
    std::array<Channel, kMaxChannels> channels;
    Motion motion;
    Dropouts dropouts;
    Settings target, current;
    bool configured = false, hasCeiling = false;
    double inputGain = 1.0, outputGain = 1.0, mix = 1.0, tone = 12000.0, hissGain = 0.0, lastSpeed = 0.0;
    std::array<double, kChunk> readDelay {}, contact {}, inputGains {}, wetGains {}, dryGains {};
};
} // namespace tonwerktape
