#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tonwerkchorus
{
inline constexpr double kPi = 3.141592653589793;
/** The longest delay a line is read at: the delay knob's top plus the widest sweep, with room. */
inline constexpr double kMaxSeconds = 0.05;

struct Division
{
    const char* name;
    double beats;
};
/** The LFO's note values in sync, in the menu's order (saved in projects: append only). Slow ones for flanger sweeps. */
inline constexpr std::array<Division, 13> kDivisions { {
    { "4 Takte", 16.0 }, { "2 Takte", 8.0 }, { "1/1", 4.0 }, { "1/2.", 3.0 }, { "1/2", 2.0 }, { "1/2T", 4.0 / 3 },
    { "1/4.", 1.5 }, { "1/4", 1.0 }, { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3 },
    { "1/16", 0.25 },
} };
inline constexpr int kTwoBars = 1;
inline constexpr int kWhole = 2;
inline constexpr int kQuarter = 7;

/** The modes, in the menu's order (saved in projects: append only). */
enum class Mode { chorus, ensemble, flanger, phaser };
enum class Shape { sine, triangle };
/** The phaser's stage counts, in the menu's order. */
inline constexpr std::array<int, 4> kStages { 4, 6, 8, 12 };

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    Mode mode = Mode::chorus;
    Shape shape = Shape::sine;
    bool sync = false;
    float rate = 0.8f;         // Hz
    int division = kWhole;
    float depth = 0.5f;
    float spread = 90.0f;      // degrees between the left and right LFO
    float delay = 0.012f;      // the lines' middle (chorus, ensemble) or longest (flanger) delay
    int voices = 2;            // chorus: lines per side, 1 to 4
    float feedback = 0.0f;     // -0.95 to 0.95 (flanger, phaser; a little colour on chorus)
    float frequency = 800.0f;  // the phaser's centre
    int stages = 0;            // index into kStages
    float lowCut = 120.0f;     // the wet signal's low cut: the bass stays dry and in the middle
    float highCut = 12000.0f;  // the wet signal's high cut
    float width = 1.0f;
    float mix = 0.5f;
    float outputDb = -3.0f;  // dry and wet are both whole at mix 50 %: about 3 dB louder than the dry alone
};

/** The LFO's rate in Hz, free or from the tempo. */
inline double rateOf(const Settings& s, double bpm)
{
    if (! s.sync)
        return std::clamp((double) s.rate, 0.001, 50.0);
    const auto& d = kDivisions[(std::size_t) std::clamp(s.division, 0, (int) kDivisions.size() - 1)];
    return std::max(1.0, bpm) / 60.0 / d.beats;
}

/** The phaser's sweep reaches this many octaves either side of its centre at full depth. */
inline constexpr float kPhaserOctaves = 2.0f;
/** A chorus voice's sweep: the depth's share of this many seconds, at most 0.9 of the delay. */
inline constexpr float kChorusSweep = 0.005f;

/** The LFO at `phase` (cycles), -1 to 1, starting at 0 and rising. A polynomial sine: cheap, smooth, exact at the ends. */
inline float lfo(double phase, Shape shape)
{
    double p = phase - std::floor(phase);
    if (shape == Shape::triangle)
        return (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    // Fold onto a half wave, then Bhaskara's approximation of sin(pi x) (within 0.0016).
    const float sign = p < 0.5 ? 1.0f : -1.0f;
    const auto x = (float) (p < 0.5 ? 2.0 * p : 2.0 * p - 1.0);
    const float h = x * (1.0f - x);
    return sign * 16.0f * h / (5.0f - 4.0f * h);
}

/** A first-order TPT filter, low and high pass from one state. */
struct OnePole
{
    float g = 1.0f, state = 0.0f;
    void set(double frequency, double rate)
    {
        const double t = std::tan(kPi * std::min(frequency, 0.45 * rate) / rate);
        g = (float) (t / (1.0 + t));
    }
    float low(float x)
    {
        const float v = (x - state) * g;
        const float y = v + state;
        state = y + v;
        return y;
    }
    float high(float x) { return x - low(x); }
};

/**
 * Chorus, ensemble, flanger and phaser on one LFO, each side's LFO `spread` degrees from the other.
 *
 * Chorus: `voices` lines per side, their LFOs evenly apart, each swept around `delay` by up to 5 ms, summed at
 * 1/sqrt(voices) so more voices widen rather than raise the level. Ensemble: the string machines' three lines a
 * third of a cycle apart, each moved by the slow LFO and a fast one at 7 times its rate (the shimmer of a Solina's
 * strings; a whole multiple, so the fast one does not jump when the slow one wraps). Flanger: one line swept from
 * `delay` down towards nothing, `feedback` deepening the comb, through a soft saturation so 95 % rings without
 * blowing up. Phaser: `stages` first-order allpasses at one frequency swept around `frequency` by up to two octaves
 * either way, their coefficients every 16 samples, feedback from the last stage.
 *
 * The wet signal passes a low cut (the bass stays dry, so it stays in the middle and keeps its punch) and a high cut.
 * Changing the mode fades the wet out and in again over 10 ms each way, so a preset change never clicks.
 */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        int length = 1;
        while (length < (int) (kMaxSeconds * rate) + 8)
            length <<= 1;
        mask = length - 1;
        for (auto& line : lines)
            line.assign((std::size_t) length, 0.0f);
        writeAt = 0;
        phase = 0.0;
        smoothedDelay = -1.0f;
        smoothedDepth = -1.0f;
        fade = 1.0f;
        activeMode = Mode::chorus;
        modeChosen = false;
        clearState();
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill(line.begin(), line.end(), 0.0f);
        clearState();
    }

    /**
     * In place, stereo. `bpm` sets a synced rate; `beat` is the song position in quarter notes at the block's start
     * while the host plays (negative otherwise), so a synced LFO sits on the bars.
     */
    void process(float* left, float* right, int count, const Settings& s, double bpm, double beat = -1.0)
    {
        if (! modeChosen)
        {
            activeMode = s.mode;
            modeChosen = true;
        }
        const double hz = rateOf(s, bpm);
        if (s.sync && beat >= 0.0)
        {
            const auto& d = kDivisions[(std::size_t) std::clamp(s.division, 0, (int) kDivisions.size() - 1)];
            phase = beat / d.beats;
            phase -= std::floor(phase);
        }
        const double step = hz / sampleRate;
        const double spreadCycles = std::clamp((double) s.spread, 0.0, 180.0) / 360.0;

        for (auto& f : tone)
        {
            f.lowCut.set(s.lowCut, sampleRate);
            f.highCut.set(s.highCut, sampleRate);
        }
        const float smooth = (float) (1.0 - std::exp(-1.0 / (0.03 * sampleRate)));
        if (smoothedDelay < 0.0f)
        {
            smoothedDelay = s.delay;
            smoothedDepth = s.depth;
        }
        const float fadeStep = (float) (1.0 / (0.01 * sampleRate));
        const float feedback = std::clamp(s.feedback, -0.95f, 0.95f);
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float pan = std::clamp(s.width, 0.0f, 1.0f);
        const float dryGain = std::min(1.0f, 2.0f * (1.0f - s.mix));
        const float wetGain = std::min(1.0f, 2.0f * s.mix);
        const int voices = std::clamp(s.voices, 1, 4);
        const int stageCount = kStages[(std::size_t) std::clamp(s.stages, 0, (int) kStages.size() - 1)];
        const double maxDelay = kMaxSeconds * sampleRate - 4.0;

        for (int i = 0; i < count; ++i)
        {
            // A new mode waits until the wet has faded out, then fades in.
            if (s.mode != activeMode)
            {
                fade -= fadeStep;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    activeMode = s.mode;
                    // The lines keep running in every mode; only the phaser starts afresh (it is silent here).
                    for (auto& p : phasers)
                        p = {};
                }
            }
            else
                fade = std::min(1.0f, fade + fadeStep);
            smoothedDelay += (s.delay - smoothedDelay) * smooth;
            smoothedDepth += (s.depth - smoothedDepth) * smooth;

            const std::array<float, 2> in { left[i], right[i] };
            std::array<float, 2> wet {};
            const double base = (double) smoothedDelay * sampleRate;
            for (std::size_t c = 0; c < 2; ++c)
            {
                const double sidePhase = phase + (c == 1 ? spreadCycles : 0.0);
                auto& line = lines[c];
                switch (activeMode)
                {
                    case Mode::chorus:
                    {
                        const double sweep = std::min(0.9 * base, kChorusSweep * sampleRate) * smoothedDepth;
                        float sum = 0.0f;
                        for (int v = 0; v < voices; ++v)
                        {
                            const float u = lfo(sidePhase + (double) v / voices, s.shape);
                            sum += read(line, std::clamp(base + sweep * u, 1.0, maxDelay));
                        }
                        wet[c] = sum * kVoiceGain[(std::size_t) voices - 1];
                        // A little feedback colours a chorus; it comes from the voices' sum, at half the knob.
                        line[(std::size_t) writeAt] = in[c] + 0.5f * feedback * saturate(wet[c]);
                        break;
                    }
                    case Mode::ensemble:
                    {
                        const double slow = std::min(0.8 * base, 0.003 * sampleRate) * smoothedDepth;
                        const double fast = std::min(0.1 * base, 0.0004 * sampleRate) * smoothedDepth;
                        float sum = 0.0f;
                        for (int v = 0; v < 3; ++v)
                        {
                            const double at = sidePhase + v / 3.0;
                            const double offset = slow * lfo(at, Shape::sine) + fast * lfo(7.0 * at, Shape::sine);
                            sum += read(line, std::clamp(base + offset, 1.0, maxDelay));
                        }
                        wet[c] = sum * kVoiceGain[2];
                        line[(std::size_t) writeAt] = in[c];
                        break;
                    }
                    case Mode::flanger:
                    {
                        // From the delay down to a twentieth of it and back: the comb's teeth sweep up and down.
                        const float u = 0.5f - 0.5f * lfo(sidePhase, s.shape);
                        const double delay = base * (1.0 - 0.95 * smoothedDepth * u);
                        const float tap = read(line, std::clamp(delay, 1.0, maxDelay));
                        wet[c] = tap;
                        line[(std::size_t) writeAt] = in[c] + feedback * saturate(tap);
                        break;
                    }
                    case Mode::phaser:
                    {
                        auto& p = phasers[c];
                        if (p.countdown-- <= 0)
                        {
                            p.countdown = kPhaserStep - 1;
                            const float u = lfo(sidePhase, s.shape);
                            const double hzNow = std::clamp(
                                (double) s.frequency * std::exp2((double) (kPhaserOctaves * smoothedDepth * u)), 20.0,
                                0.45 * sampleRate);
                            const double t = std::tan(kPi * hzNow / sampleRate);
                            p.target = (float) ((t - 1.0) / (t + 1.0));
                            if (p.coefficient < -1.5f)
                                p.coefficient = p.target;
                            p.slope = (p.target - p.coefficient) / (float) kPhaserStep;
                        }
                        p.coefficient += p.slope;
                        float x = in[c] + feedback * saturate(p.last);
                        for (int k = 0; k < stageCount; ++k)
                        {
                            // y = a x + x[n-1] - a y[n-1], as one state.
                            const float y = p.coefficient * x + p.state[(std::size_t) k];
                            p.state[(std::size_t) k] = x - p.coefficient * y;
                            x = y;
                        }
                        p.last = x;
                        wet[c] = x;
                        // The line keeps the input, so a switch to a delay mode reads what was played.
                        line[(std::size_t) writeAt] = in[c];
                        break;
                    }
                }
                auto& f = tone[c];
                wet[c] = f.highCut.low(f.lowCut.high(wet[c])) * fade;
            }
            writeAt = (writeAt + 1) & mask;
            phase += step;
            if (phase >= 1.0)
                phase -= 1.0;

            const float mid = 0.5f * (wet[0] + wet[1]);
            const float wetL = mid + pan * (wet[0] - mid);
            const float wetR = mid + pan * (wet[1] - mid);
            left[i] = output * (dryGain * in[0] + wetGain * wetL);
            right[i] = output * (dryGain * in[1] + wetGain * wetR);
        }
        shownPhase = phase;
    }

    /** The LFO's phase (cycles, left side) after the last block, for the display. */
    double phaseNow() const { return shownPhase; }

private:
    static constexpr int kPhaserStep = 16;
    /** 1/sqrt(n): voices at different delays add up in power, not in amplitude. */
    static constexpr std::array<float, 4> kVoiceGain { 1.0f, 0.70710678f, 0.57735027f, 0.5f };

    void clearState()
    {
        for (auto& f : tone)
            f = {};
        for (auto& p : phasers)
            p = {};
        for (auto& line : lines)
            std::fill(line.begin(), line.end(), 0.0f);
    }

    /** tanh's shape for little cost: exact at 0, slope 1 there, ±1 from ±3 on. */
    static float saturate(float x)
    {
        if (x <= -3.0f)
            return -1.0f;
        if (x >= 3.0f)
            return 1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** `delay` samples behind the sample about to be written, on a cubic (Catmull-Rom). */
    float read(const std::vector<float>& line, double delay) const
    {
        const double position = (double) writeAt - delay;
        const double whole = std::floor(position);
        const int i = (int) whole;
        const auto t = (float) (position - whole);
        auto at = [&](int k) { return line[(std::size_t) (k & mask)]; };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    struct Tone
    {
        OnePole lowCut, highCut;
    };
    struct Phaser
    {
        std::array<float, 12> state {};
        float coefficient = -2.0f, target = 0.0f, slope = 0.0f, last = 0.0f;
        int countdown = 0;
    };

    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    int writeAt = 0, mask = 0;
    double phase = 0.0, shownPhase = 0.0;
    float smoothedDelay = -1.0f, smoothedDepth = -1.0f, fade = 1.0f;
    Mode activeMode = Mode::chorus;
    bool modeChosen = false;
    std::array<Tone, 2> tone {};
    std::array<Phaser, 2> phasers {};
};
} // namespace tonwerkchorus
