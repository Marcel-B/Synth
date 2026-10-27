#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
 * Tonwerk Rumble's sound, plain C++: the techno kick's rumble. The kick (the effect's own input) goes, summed to mono,
 * through a lowpass that takes its click away into a hall, an echo or both; what rings on is driven hard, cut down to
 * the low end by a resonant 24 dB lowpass and a highpass, and ducked under the next kick, either by the kick itself
 * (a follower on the input, so no side chain is needed) or on the song's grid. The rumble stays mono, as a club's sub
 * wants it, and is added to the untouched kick. Borrows the tempo divisions and the state variable filter from Tonwerk
 * Wobble (`WobbleEngine.h`).
 *
 * Everything runs per sample on one mono path; the filters' coefficients follow every `kControl` samples. Cheap: one
 * eight-line hall, one echo line, one shaper, three filter stages.
 */
namespace tonwerkrumble
{
using tonwerkwobble::divisionBeats;
using tonwerkwobble::kDivisions;
using tonwerkwobble::SvfStage;

inline constexpr double kPi = 3.14159265358979323846;

/** What rings after the kick; the order of the menu, saved in projects: append only. */
enum class Space
{
    hall,
    echo,
    echoHall,
};
inline constexpr std::array<const char*, 3> kSpaceNames { "Hall", "Echo", "Echo + Hall" };

/** The distortion's curves; the order of the menu, saved in projects: append only. */
enum class Curve
{
    soft,
    tube,
    hard,
    fold,
};
inline constexpr std::array<const char*, 4> kCurveNames { "Weich", "Röhre", "Hart", "Falter" };

/** The kick level at or below which it is switched off (the plugin then gives the rumble alone). */
inline constexpr float kKickOffDb = -60.0f;
/** The longest echo; a whole note at 60 BPM. */
inline constexpr float kMaxEchoSeconds = 4.0f;
inline constexpr float kMaxPredelayMs = 250.0f;
/** How far the ducking closes the lowpass at `duckFilter` 100 %, in octaves. */
inline constexpr float kDuckOctaves = 3.0f;
/** The kick follower's range: from here (dBFS) up to `kDuckFullDb` the ducking grows to its full depth. */
inline constexpr float kDuckFloorDb = -42.0f;
inline constexpr float kDuckFullDb = -12.0f;

struct Settings
{
    /** The lowpass on the way into the room, Hz: the kick's click would only ring as noise. */
    float toneHz = 600.0f;
    Space space = Space::hall;
    float decaySeconds = 2.5f;
    /** 0 to 1: the hall's line lengths, from a small room to a hangar. */
    float size = 0.6f;
    float predelayMs = 0.0f;
    /** A `kDivisions` index; the echo's time on the song's tempo. */
    int echoDivision = tonwerkwobble::kSixteenth;
    /** 0 to 0.95. */
    float feedback = 0.6f;

    /** Gain into the distortion, dB; its output is made up so the level stays put and only the colour changes. */
    float driveDb = 18.0f;
    Curve curve = Curve::soft;

    /** The 24 dB lowpass after the distortion, Hz, and its resonance (0 to 1). */
    float cutoff = 140.0f;
    float resonance = 0.3f;
    /** The 12 dB highpass that keeps the rumble out of the sub-sonic, Hz. */
    float lowCut = 35.0f;

    /** 0 to 1: how far the rumble ducks under the kick. */
    float duck = 0.9f;
    /** How long the rumble takes to come back, ms. */
    float duckReleaseMs = 200.0f;
    /** Duck on the song's grid instead of following the kick. */
    bool duckSync = false;
    int duckDivision = tonwerkwobble::kQuarter;
    /** 0 to 1: how far the ducking also closes the lowpass, a share of `kDuckOctaves`. */
    float duckFilter = 0.3f;

    float rumbleDb = 0.0f;
    /** The dry kick's level; `kKickOffDb` or below switches it off. */
    float kickDb = 0.0f;
    float outputDb = 0.0f;
};

/** Where the song is at the start of a block, in beats, how far it goes per sample, and its tempo. */
struct Clock
{
    double beat = 0.0;
    double beatsPerSample = 0.0;
    double bpm = 120.0;
};

inline float decibelsToGain(float db) { return std::pow(10.0f, db / 20.0f); }

/** A delay line read between samples (linear), its size a power of two. */
class DelayLine
{
public:
    void prepare(std::size_t longest)
    {
        std::size_t size = 4;
        while (size < longest + 4)
            size <<= 1;
        buffer.assign(size, 0.0f);
        mask = size - 1;
        write = 0;
    }
    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        write = 0;
    }
    void push(float x)
    {
        buffer[write] = x;
        write = (write + 1) & mask;
    }
    /** The sample pushed `delay` samples ago (at least 1). */
    float read(float delay) const
    {
        const float at = (float) write - std::max(1.0f, delay) + (float) buffer.size();
        const auto i = (std::size_t) at;
        const float frac = at - (float) i;
        const float a = buffer[i & mask], b = buffer[(i + 1) & mask];
        return a + frac * (b - a);
    }
    float limit() const { return (float) buffer.size() - 3.0f; }

private:
    std::vector<float> buffer;
    std::size_t mask = 0, write = 0;
};

/**
 * The hall: a predelay, two allpasses that smear the kick's attack, and an eight-line feedback delay network mixed by
 * a Hadamard matrix, each line's gain from its length and the decay as RT60. Mono in, mono out: the rumble ends up
 * mono anyway, and one network is half the cost of two. The lines glide to a new size over about 50 ms instead of
 * jumping, so turning the knob does not click.
 */
class Hall
{
public:
    static constexpr int kLines = 8;

    void prepare(float sampleRate)
    {
        rate = sampleRate;
        predelay.prepare((std::size_t) (kMaxPredelayMs / 1000.0f * sampleRate) + 2);
        for (std::size_t i = 0; i < kLines; ++i)
            lines[i].prepare((std::size_t) (kLineMs[i] / 1000.0f * kLargest * sampleRate) + 2);
        for (std::size_t i = 0; i < diffusers.size(); ++i)
            diffusers[i].prepare((std::size_t) (kDiffuserMs[i] / 1000.0f * sampleRate) + 2);
        glide = 1.0f - std::exp(-1.0f / (0.015f * sampleRate));
        reset();
    }

    void reset()
    {
        predelay.reset();
        for (auto& line : lines)
            line.reset();
        for (auto& diffuser : diffusers)
            diffuser.reset();
        primed = false;
    }

    /** Called once per block before `process`. */
    void set(const Settings& s)
    {
        const float scale = kSmallest + (kLargest - kSmallest) * std::clamp(s.size, 0.0f, 1.0f);
        const float rt60 = std::max(0.1f, s.decaySeconds);
        float power = 0.0f;
        for (std::size_t i = 0; i < kLines; ++i)
        {
            target[i] = kLineMs[i] / 1000.0f * scale * rate;
            // -60 dB after `rt60` seconds: each pass through a line of n samples loses 60 n / (rt60 rate) dB.
            gains[i] = std::pow(10.0f, -3.0f * target[i] / (rt60 * rate));
            power += gains[i] * gains[i];
        }
        if (! primed)
        {
            length = target;
            primed = true;
        }
        // A long decay piles up energy; this keeps the hall's level about the same whatever the decay (as Tonwerk
        // Shimmer does).
        level = std::pow(std::max(1.0e-4f, 1.0f - power / kLines), 0.25f);
        predelaySamples = std::clamp(s.predelayMs, 0.0f, kMaxPredelayMs) / 1000.0f * rate;
    }

    float process(float x)
    {
        float y = predelaySamples >= 1.0f ? predelay.read(predelaySamples) : x;
        predelay.push(x);
        for (std::size_t i = 0; i < diffusers.size(); ++i)
        {
            const float delayed = diffusers[i].read(kDiffuserMs[i] / 1000.0f * rate);
            const float v = y + kDiffusion * delayed;
            diffusers[i].push(v);
            y = delayed - kDiffusion * v;
        }

        std::array<float, kLines> v {};
        float sum = 0.0f;
        for (std::size_t i = 0; i < kLines; ++i)
        {
            length[i] += glide * (target[i] - length[i]);
            v[i] = lines[i].read(length[i]);
            sum += (i & 1) ? -v[i] : v[i];
        }
        hadamard(v);
        for (std::size_t i = 0; i < kLines; ++i)
            lines[i].push(gains[i] * v[i] + ((i & 2) ? -y : y));
        return level * sum * 0.5f;
    }

private:
    static constexpr std::array<float, kLines> kLineMs { 31.7f, 37.3f, 41.9f, 47.1f, 53.9f, 59.3f, 67.1f, 73.1f };
    static constexpr std::array<float, 2> kDiffuserMs { 5.3f, 12.7f };
    static constexpr float kDiffusion = 0.6f;
    static constexpr float kSmallest = 0.3f, kLargest = 1.4f;

    /** The 8 x 8 Hadamard matrix, normalised, in 24 additions. */
    static void hadamard(std::array<float, kLines>& v)
    {
        for (std::size_t half = 1; half < kLines; half <<= 1)
            for (std::size_t i = 0; i < kLines; i += 2 * half)
                for (std::size_t j = i; j < i + half; ++j)
                {
                    const float a = v[j], b = v[j + half];
                    v[j] = a + b;
                    v[j + half] = a - b;
                }
        for (auto& x : v)
            x *= 0.35355339f;
    }

    float rate = 48000.0f;
    DelayLine predelay;
    std::array<DelayLine, kLines> lines;
    std::array<DelayLine, 2> diffusers;
    std::array<float, kLines> target {}, length {}, gains {};
    float glide = 0.001f, level = 1.0f, predelaySamples = 0.0f;
    bool primed = false;
};

/** An echo on the song's tempo whose time glides (20 ms) when the tempo or the note value changes. */
class Echo
{
public:
    void prepare(float sampleRate)
    {
        rate = sampleRate;
        line.prepare((std::size_t) (kMaxEchoSeconds * sampleRate) + 2);
        glide = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));
        reset();
    }
    void reset()
    {
        line.reset();
        delay = -1.0f;
    }
    void set(const Settings& s, double bpm)
    {
        const double seconds = divisionBeats(s.echoDivision) * 60.0 / std::max(20.0, bpm);
        target = std::clamp((float) seconds * rate, 1.0f, line.limit());
        if (delay < 0.0f)
            delay = target;
        feedback = std::clamp(s.feedback, 0.0f, 0.95f);
    }
    /** The repeats alone, without `x` itself. */
    float process(float x)
    {
        delay += glide * (target - delay);
        const float out = line.read(delay);
        // A soft ceiling in the loop: a full feedback with a loud kick saturates instead of growing.
        line.push(std::tanh(x + feedback * out));
        return out;
    }

private:
    float rate = 48000.0f, glide = 0.001f, target = 1.0f, delay = -1.0f, feedback = 0.0f;
    DelayLine line;
};

/** The curve itself, `f(x)`. */
inline float shape(Curve curve, float x)
{
    constexpr float bias = 0.3f;
    switch (curve)
    {
        case Curve::tube: return std::tanh(x + bias) - std::tanh(bias);
        case Curve::hard: return std::clamp(x, -1.0f, 1.0f);
        case Curve::fold: return std::sin(x);
        case Curve::soft:
        default: return std::tanh(x);
    }
}

/** log(cosh(x)) without overflow. */
inline double logCosh(double x)
{
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994531;
}

/** An antiderivative of `shape`, for the first-order antialiasing. */
inline double antiderivative(Curve curve, double x)
{
    constexpr double bias = 0.3;
    switch (curve)
    {
        case Curve::tube: return logCosh(x + bias) - x * std::tanh(bias);
        case Curve::hard: return std::abs(x) <= 1.0 ? 0.5 * x * x : std::abs(x) - 0.5;
        case Curve::fold: return -std::cos(x);
        case Curve::soft:
        default: return logCosh(x);
    }
}

/**
 * The distortion, with first-order antiderivative antialiasing: the difference of the antiderivative over the step
 * instead of the curve at the sample, which takes most of the folded-back hiss out of a hard drive. The rumble is
 * lowpassed afterwards, but aliases land anywhere, also under the cutoff. Its output is made up so a sine at -12 dBFS
 * keeps its RMS at any drive.
 */
class Shaper
{
public:
    void reset() { last = 0.0; }

    void set(Curve newCurve, float driveDb)
    {
        if (newCurve == curve && std::abs(driveDb - lastDb) < 1.0e-6f)
            return;
        curve = newCurve;
        lastDb = driveDb;
        // The folder doubles its folds with every 6 dB; half the drive keeps its top in the low end the lowpass keeps,
        // where the others clip harder instead.
        const float db = std::clamp(driveDb, 0.0f, 48.0f);
        gain = decibelsToGain(curve == Curve::fold ? 0.5f * db : db);
        // Measured on a sine of amplitude 0.25 rather than worked out per curve: right for every curve and drive.
        constexpr int points = 64;
        double sum = 0.0;
        for (int i = 0; i < points; ++i)
        {
            const float y = shape(curve, gain * 0.25f * (float) std::sin(2.0 * kPi * (i + 0.5) / points));
            sum += (double) y * y;
        }
        const double rms = std::sqrt(sum / points);
        // The tube's bias leaves a DC offset in the sum; the highpass after the shaper takes it out of the sound.
        makeup = rms > 1.0e-6 ? (float) (0.25 * 0.70710678 / rms) : 1.0f;
    }

    float process(float input)
    {
        const double x = (double) gain * input;
        const double dx = x - last;
        double y;
        if (std::abs(dx) > 1.0e-4)
            y = (antiderivative(curve, x) - antiderivative(curve, last)) / dx;
        else
            y = shape(curve, (float) (0.5 * (x + last)));
        last = x;
        return makeup * (float) y;
    }

private:
    Curve curve = Curve::soft;
    float lastDb = -1.0f, gain = 1.0f, makeup = 1.0f;
    double last = 0.0;
};

/**
 * The whole effect. `process` takes up to two channels in place; the kick is read from them, the rumble added to them.
 * After each call `kickLevel()`, `rumbleLevel()` and `duckGain()` say how loud each part was and how far the rumble was
 * down, for the editor.
 */
class Engine
{
public:
    static constexpr int kControl = 16;

    void prepare(float sampleRate)
    {
        rate = sampleRate;
        hall.prepare(sampleRate);
        echo.prepare(sampleRate);
        smoothing = 1.0f - std::exp(-1.0f / (0.001f * sampleRate));
        levelGlide = 1.0f - std::exp(-1.0f / (0.02f * sampleRate));
        reset();
    }

    void reset()
    {
        hall.reset();
        echo.reset();
        shaper.reset();
        tone.reset();
        highpass.reset();
        for (auto& stage : lowpass)
            stage.reset();
        follower = 0.0f;
        reduction = 0.0f;
        countdown = 0;
        primed = false;
    }

    void process(float* const* audio, int channelCount, int count, const Settings& s, const Clock& clock)
    {
        channelCount = std::min(channelCount, 2);
        if (channelCount <= 0 || count <= 0)
            return;
        hall.set(s);
        echo.set(s, clock.bpm);
        shaper.set(s.curve, s.driveDb);
        const float toneG = tuning(std::clamp(s.toneHz, 20.0f, 0.45f * rate));
        const float lowCutG = tuning(std::clamp(s.lowCut, 10.0f, 0.45f * rate));
        // Q from 0.7 to about 5: a peak that tunes the rumble, never a whistle.
        const float k = kButterworth - (kButterworth - 0.2f) * std::clamp(s.resonance, 0.0f, 1.0f);
        const float depth = std::clamp(s.duck, 0.0f, 1.0f);
        const float releaseSeconds = std::max(0.005f, s.duckReleaseMs / 1000.0f);
        // The follower falls `kDuckFullDb - kDuckFloorDb` dB within the release: 8.69 dB per time constant.
        const float fall = std::exp(-1.0f / (releaseSeconds * 8.6858896f / (kDuckFullDb - kDuckFloorDb) * rate));
        const double gridBeats = divisionBeats(s.duckDivision);
        const double secondsPerBeat = 60.0 / std::max(20.0, clock.bpm);
        const float filterOctaves = std::clamp(s.duckFilter, 0.0f, 1.0f) * kDuckOctaves;
        const float kickTarget = s.kickDb <= kKickOffDb ? 0.0f : decibelsToGain(s.kickDb);
        const float rumbleTarget = decibelsToGain(s.rumbleDb);
        const float outputTarget = decibelsToGain(s.outputDb);
        if (! primed)
        {
            kickGain = kickTarget;
            rumbleGain = rumbleTarget;
            outputGain = outputTarget;
            primed = true;
        }
        kickPeak = rumblePeak = 0.0f;
        lowestGain = 1.0f;

        for (int n = 0; n < count; ++n)
        {
            const float left = audio[0][n];
            const float right = channelCount > 1 ? audio[1][n] : left;
            const float kick = 0.5f * (left + right);

            // How far down the rumble is now.
            float wanted;
            if (s.duckSync)
            {
                const double beat = clock.beat + (double) n * clock.beatsPerSample;
                const double since = (beat - std::floor(beat / gridBeats) * gridBeats) * secondsPerBeat;
                const float remaining = 1.0f - (float) since / releaseSeconds;
                wanted = remaining > 0.0f ? depth * remaining * remaining : 0.0f;
            }
            else
            {
                follower = std::max(std::abs(kick), follower * fall);
                const float db = 20.0f * std::log10(std::max(follower, 1.0e-6f));
                wanted = depth * std::clamp((db - kDuckFloorDb) / (kDuckFullDb - kDuckFloorDb), 0.0f, 1.0f);
            }
            reduction += smoothing * (wanted - reduction);

            if (countdown-- <= 0)
            {
                countdown = kControl - 1;
                const float hz = std::clamp(s.cutoff * std::exp2(-filterOctaves * reduction), 20.0f, 0.45f * rate);
                cutoffNow = hz;
                cutoffG = tuning(hz);
            }

            // The room rings on the kick without its click; Echo + Hall puts the repeats into the hall too.
            const float sent = tone.process(kick, toneG, kButterworth).low;
            float room;
            switch (s.space)
            {
                case Space::echo: room = echo.process(sent); break;
                case Space::echoHall: room = hall.process(sent + echo.process(sent)); break;
                case Space::hall:
                default: room = hall.process(sent); break;
            }
            const float driven = shaper.process(room);
            const float cut = highpass.process(driven, lowCutG, kButterworth).high;
            const float low = lowpass[1].process(lowpass[0].process(cut, cutoffG, kButterworth).low, cutoffG, k).low;
            const float rumble = low * (1.0f - reduction);

            kickGain += levelGlide * (kickTarget - kickGain);
            rumbleGain += levelGlide * (rumbleTarget - rumbleGain);
            outputGain += levelGlide * (outputTarget - outputGain);
            const float added = rumbleGain * rumble;
            audio[0][n] = outputGain * (kickGain * left + added);
            if (channelCount > 1)
                audio[1][n] = outputGain * (kickGain * right + added);

            kickPeak = std::max(kickPeak, std::abs(kick));
            rumblePeak = std::max(rumblePeak, std::abs(added));
            lowestGain = std::min(lowestGain, 1.0f - reduction);
        }
    }

    /** The loudest kick (input, mono) and rumble (after its level) of the last call, and how far the rumble was down. */
    float kickLevel() const { return kickPeak; }
    float rumbleLevel() const { return rumblePeak; }
    float duckGain() const { return lowestGain; }
    /** Where the lowpass stands now, Hz. */
    float cutoff() const { return cutoffNow; }

private:
    static constexpr float kButterworth = 1.41421356f;

    float tuning(float hz) const { return (float) std::tan(kPi * hz / rate); }

    float rate = 48000.0f;
    Hall hall;
    Echo echo;
    Shaper shaper;
    SvfStage tone, highpass;
    std::array<SvfStage, 2> lowpass;
    float follower = 0.0f, reduction = 0.0f, smoothing = 0.01f, levelGlide = 0.001f;
    float cutoffG = 0.01f, cutoffNow = 140.0f;
    float kickGain = 1.0f, rumbleGain = 1.0f, outputGain = 1.0f;
    float kickPeak = 0.0f, rumblePeak = 0.0f, lowestGain = 1.0f;
    int countdown = 0;
    bool primed = false;
};
} // namespace tonwerkrumble
