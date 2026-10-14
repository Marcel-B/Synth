#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tonwerkdelay
{
inline constexpr double kPi = 3.141592653589793;
/** The longest time a line holds: a whole note at 60 BPM. */
inline constexpr double kMaxSeconds = 4.0;

struct Division
{
    const char* name;
    double beats;
};
/** The note values the sync menu offers, in its order (saved in projects: append only). */
inline constexpr std::array<Division, 14> kDivisions { {
    { "1/1", 4.0 }, { "1/2.", 3.0 }, { "1/2", 2.0 }, { "1/2T", 4.0 / 3 }, { "1/4.", 1.5 }, { "1/4", 1.0 },
    { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3 }, { "1/16.", 0.375 }, { "1/16", 0.25 },
    { "1/16T", 1.0 / 6 }, { "1/32", 0.125 },
} };
inline constexpr int kQuarter = 5;
inline constexpr int kDottedEighth = 7;
inline constexpr int kEighth = 8;

enum class Mode { stereo, pingPong, mono };

/** The knobs, in the parameters' units except times, which are seconds. */
struct Settings
{
    Mode mode = Mode::pingPong;
    bool sync = true;
    float time = 0.375f;
    int division = kDottedEighth;
    float offset = 0.0f;   // the right side's time against the left, -0.5 to 0.5 of it (stereo mode)
    float feedback = 0.45f;
    float lowCut = 150.0f;   // Hz, in the feedback
    float highCut = 6000.0f; // Hz, in the feedback
    float drive = 0.0f;      // saturation in the feedback
    float wow = 0.0f;        // the repeats' pitch wandering, as from tape
    float duck = 0.0f;       // how far the repeats drop while the input plays
    float duckRelease = 0.25f;
    bool hold = false;       // the repeats circulate unchanged, nothing new goes in
    float width = 1.0f;
    float mix = 0.3f;
    float outputDb = 0.0f;
};

inline double secondsOf(const Settings& s, double bpm)
{
    if (! s.sync)
        return std::clamp((double) s.time, 0.001, kMaxSeconds);
    const auto& d = kDivisions[(std::size_t) std::clamp(s.division, 0, (int) kDivisions.size() - 1)];
    return std::clamp(d.beats * 60.0 / std::max(1.0, bpm), 0.001, kMaxSeconds);
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
 * A delay for rhythm and dub: two lines (left, right) read on a cubic, the time following the knob or the host's tempo
 * and gliding to a new one over about 60 ms (a jump would click; the glide sounds like a tape machine changing speed).
 * Stereo keeps each side in its own line, the right one longer or shorter by `offset`; ping-pong puts the input
 * into the left line only and crosses the feedback, so the repeats bounce from side to side; mono sends the sum
 * through one line to both sides. In the feedback: a low cut and a high cut (each two poles), soft saturation by
 * `drive` (always a little, so a feedback at 100 % sustains rather than grows), and `wow`, a slow wander of the
 * read position. Ducking follows the input's level and pulls the repeats down while it plays, back up over
 * `duckRelease`, so they fill the gaps without muddying what is sung or played. `hold` keeps what circulates
 * forever and takes nothing new.
 */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        const auto length = (std::size_t) (kMaxSeconds * rate) + 8;
        for (auto& line : lines)
            line.assign(length, 0.0f);
        writeAt = 0;
        currentDelay = { -1.0, -1.0 };
        for (auto& f : filters)
            f = {};
        follower = 0.0f;
        duckGain = 1.0f;
        wowPhase = 0.0;
        holdMix = 0.0f;
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill(line.begin(), line.end(), 0.0f);
        for (auto& f : filters)
            f = {};
        follower = 0.0f;
    }

    /** In place, stereo; `bpm` sets a synced time. */
    void process(float* left, float* right, int count, const Settings& s, double bpm)
    {
        const double base = secondsOf(s, bpm) * sampleRate;
        std::array<double, 2> target { base, base };
        if (s.mode == Mode::stereo)
            target[1] = std::clamp(base * (1.0 + (double) s.offset), 1.0, kMaxSeconds * sampleRate);
        for (std::size_t c = 0; c < 2; ++c)
            if (currentDelay[c] < 0.0)
                currentDelay[c] = target[c];
        const double glide = 1.0 - std::exp(-1.0 / (0.06 * sampleRate / 3.0));
        for (auto& f : filters)
        {
            f.lowCut[0].set(s.lowCut, sampleRate);
            f.lowCut[1].set(s.lowCut, sampleRate);
            f.highCut[0].set(s.highCut, sampleRate);
            f.highCut[1].set(s.highCut, sampleRate);
        }
        const float feedback = s.hold ? 1.0f : std::clamp(s.feedback, 0.0f, 1.0f);
        const float driveGain = 1.0f + 4.0f * s.drive;
        const float attack = (float) std::exp(-1.0 / (0.005 * sampleRate));
        const float release = (float) std::exp(-1.0 / (std::max(0.01, (double) s.duckRelease) * sampleRate));
        const float duckSmooth = (float) std::exp(-1.0 / (0.01 * sampleRate));
        const float holdStep = (float) (1.0 / (0.02 * sampleRate));
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const double wowStep = 0.7 / sampleRate;
        const double wowDepth = s.wow * 0.004 * sampleRate; // up to 4 ms either way
        const auto length = (int) lines[0].size();
        const float pan = std::clamp(s.width, 0.0f, 1.0f);
        // The dry stays whole up to a mix of 50 % and the wet reaches whole there, as on most delays: a little mix
        // adds echoes without taking the source down.
        const float dryGain = std::min(1.0f, 2.0f * (1.0f - s.mix));
        const float wetGain = std::min(1.0f, 2.0f * s.mix);

        for (int i = 0; i < count; ++i)
        {
            const float inL = left[i], inR = right[i];
            const float inMono = 0.5f * (inL + inR);

            // Ducking: the input's level, fast up, slow down; full depth from -12 dBFS on, none below -42.
            const float level = std::max(std::abs(inL), std::abs(inR));
            follower = level + (follower - level) * (level > follower ? attack : release);
            const float db = 20.0f * std::log10(follower + 1.0e-9f);
            const float amount = std::clamp((db + 42.0f) / 30.0f, 0.0f, 1.0f);
            const float wantGain = 1.0f - s.duck * amount;
            duckGain = wantGain + (duckGain - wantGain) * duckSmooth;

            // The hold fades the input out of the lines over 20 ms rather than cutting it.
            holdMix = s.hold ? std::min(1.0f, holdMix + holdStep) : std::max(0.0f, holdMix - holdStep);

            wowPhase += wowStep;
            if (wowPhase >= 1.0)
                wowPhase -= 1.0;
            const double wander = wowDepth * (0.7 * std::sin(2.0 * kPi * wowPhase) + 0.3 * std::sin(2.0 * kPi * 2.7 * wowPhase));

            std::array<float, 2> taps {};
            for (std::size_t c = 0; c < 2; ++c)
            {
                currentDelay[c] += (target[c] - currentDelay[c]) * glide;
                const double delay = std::clamp(currentDelay[c] + (c == 0 ? wander : -wander), 1.0,
                                                (double) length - 4.0);
                taps[c] = read(lines[c], delay);
            }
            if (s.mode == Mode::mono)
                taps[1] = taps[0];

            // What goes back in: through the filters and the saturation.
            std::array<float, 2> back {};
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto& f = filters[c];
                float x = taps[c];
                if (! s.hold)
                {
                    x = f.lowCut[1].high(f.lowCut[0].high(x));
                    x = f.highCut[1].low(f.highCut[0].low(x));
                }
                back[c] = saturate(x * driveGain) / driveGain * feedback;
            }

            const float send = 1.0f - holdMix;
            float writeL, writeR;
            switch (s.mode)
            {
                case Mode::pingPong:
                    writeL = send * inMono + back[1];
                    writeR = back[0];
                    break;
                case Mode::mono:
                    writeL = send * inMono + back[0];
                    writeR = 0.0f;
                    break;
                case Mode::stereo:
                default:
                    writeL = send * inL + back[0];
                    writeR = send * inR + back[1];
                    break;
            }
            lines[0][(std::size_t) writeAt] = writeL;
            lines[1][(std::size_t) writeAt] = writeR;
            writeAt = writeAt + 1 == length ? 0 : writeAt + 1;

            // Width narrows the two sides towards their middle.
            const float mid = 0.5f * (taps[0] + taps[1]);
            const float wetL = (mid + pan * (taps[0] - mid)) * duckGain;
            const float wetR = (mid + pan * (taps[1] - mid)) * duckGain;
            left[i] = output * (dryGain * inL + wetGain * wetL);
            right[i] = output * (dryGain * inR + wetGain * wetR);
        }
        shownDuck = duckGain;
    }

    /** The repeats' gain from ducking, 1 when none, for the display. */
    float duckGainNow() const { return shownDuck; }

private:
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

    /** `delay` samples behind the write position, on a cubic (Catmull-Rom). */
    float read(const std::vector<float>& line, double delay) const
    {
        const auto length = (int) line.size();
        double position = (double) writeAt - delay;
        if (position < 0.0)
            position += length;
        const int i = (int) position;
        const auto t = (float) (position - i);
        auto at = [&](int k) {
            k %= length;
            if (k < 0)
                k += length;
            return line[(std::size_t) k];
        };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    struct Filters
    {
        std::array<OnePole, 2> lowCut, highCut;
    };

    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    int writeAt = 0;
    std::array<double, 2> currentDelay { -1.0, -1.0 };
    std::array<Filters, 2> filters {};
    float follower = 0.0f, duckGain = 1.0f, holdMix = 0.0f, shownDuck = 1.0f;
    double wowPhase = 0.0;
};
} // namespace tonwerkdelay
