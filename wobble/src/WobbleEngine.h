#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

/**
 * Tonwerk Wobble's sound, plain C++: two LFOs whose shapes are drawn (one moves a filter, one the volume), locked to
 * the host's tempo, for the wobble basses of dubstep. The curves are breakpoints with a bend per segment, as the LFO
 * tools of the producers draw them; the audio thread reads them from tables the message thread fills.
 */
namespace tonwerkwobble
{
inline constexpr double kPi = 3.14159265358979323846;

/** Note values for a cycle of an LFO, in beats; the order of the menu, saved in projects: append only. */
struct Division
{
    const char* name;
    double beats;
};
inline constexpr std::array<Division, 18> kDivisions { {
    { "4/1", 16.0 },     { "2/1", 8.0 },       { "1/1", 4.0 },      { "1/2.", 3.0 },     { "1/2", 2.0 },
    { "1/2T", 4.0 / 3 }, { "1/4.", 1.5 },      { "1/4", 1.0 },      { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 },
    { "1/8", 0.5 },      { "1/8T", 1.0 / 3 },  { "1/16.", 0.375 },  { "1/16", 0.25 },    { "1/16T", 1.0 / 6 },
    { "1/32", 0.125 },   { "1/32T", 1.0 / 12 }, { "1/64", 0.0625 },
} };
inline constexpr int kWhole = 2;
inline constexpr int kQuarter = 7;
inline constexpr int kEighth = 10;
inline constexpr int kSixteenth = 13;

inline double divisionBeats(int division)
{
    return kDivisions[(std::size_t) std::clamp(division, 0, (int) kDivisions.size() - 1)].beats;
}

/** A corner of a drawn curve; `bend` shapes the segment from here to the next corner. */
struct CurvePoint
{
    float x = 0.0f;
    float y = 0.0f;
    /** -1 to 1: positive bulges the segment up, negative down, 0 is a straight line. */
    float bend = 0.0f;
};

/**
 * One cycle of an LFO as corners from x 0 to 1, values 0 to 1. The first corner sits at 0, the last at 1; two corners
 * at the same x make a step. A segment is y0 + (y1 - y0) * t^e with e up to 8 either way, so a bend of 1 is a curve as
 * sharp as a pumping side chain, and the inverse (`bendThrough`) lets the editor bend a segment through the mouse.
 */
class Curve
{
public:
    static constexpr std::size_t kMaxPoints = 64;
    static constexpr float kBendRange = 3.0f; // exponent 2^3 = 8 at full bend

    std::vector<CurvePoint> points;

    Curve() = default;
    explicit Curve(std::vector<CurvePoint> corners) : points(std::move(corners)) { tidy(); }

    /** The fraction of the way from y0 to y1 at t, for a segment bending `bend` and rising or not. */
    static float shape(float t, float bend, bool rising)
    {
        if (std::abs(bend) < 1.0e-6f)
            return t;
        const float exponent = std::exp2((rising ? -kBendRange : kBendRange) * bend);
        return std::pow(std::clamp(t, 0.0f, 1.0f), exponent);
    }

    /** The bend that takes the segment from y0 to y1 through `y` halfway along. */
    static float bendThrough(float y0, float y1, float y)
    {
        if (std::abs(y1 - y0) < 1.0e-4f)
            return 0.0f;
        const float fraction = std::clamp((y - y0) / (y1 - y0), 0.004f, 0.996f);
        const float exponent = std::log(fraction) / std::log(0.5f);
        const float bend = std::log2(exponent) / kBendRange;
        return std::clamp(y1 > y0 ? -bend : bend, -1.0f, 1.0f);
    }

    float segmentValue(std::size_t segment, float t) const
    {
        const auto& a = points[segment];
        const auto& b = points[segment + 1];
        return a.y + (b.y - a.y) * shape(t, a.bend, b.y > a.y);
    }

    float valueAt(float x) const
    {
        if (points.empty())
            return 0.0f;
        if (points.size() == 1)
            return points.front().y;
        x = std::clamp(x, 0.0f, 1.0f);
        // The last corner at or before x: of two at one place (a step), the later one holds from there on.
        std::size_t i = 0;
        while (i + 2 < points.size() && points[i + 1].x <= x)
            ++i;
        const auto& a = points[i];
        const auto& b = points[i + 1];
        const float width = b.x - a.x;
        if (width <= 0.0f)
            return b.y;
        return segmentValue(i, (x - a.x) / width);
    }

    /** Sorted, inside 0..1, the ends at 0 and 1, at least two corners and at most `kMaxPoints`. */
    void tidy()
    {
        for (auto& p : points)
        {
            p.x = std::isfinite(p.x) ? std::clamp(p.x, 0.0f, 1.0f) : 0.0f;
            p.y = std::isfinite(p.y) ? std::clamp(p.y, 0.0f, 1.0f) : 0.0f;
            p.bend = std::isfinite(p.bend) ? std::clamp(p.bend, -1.0f, 1.0f) : 0.0f;
        }
        std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
        if (points.size() > kMaxPoints)
            points.resize(kMaxPoints);
        if (points.empty())
            points.push_back({ 0.0f, 1.0f, 0.0f });
        if (points.size() == 1)
            points.push_back({ 1.0f, points.front().y, 0.0f });
        points.front().x = 0.0f;
        points.back().x = 1.0f;
    }

    /** "x y bend" per corner, separated by semicolons, three decimals: what the state and the presets keep. */
    std::string toString() const
    {
        std::string text;
        char number[64];
        for (const auto& p : points)
        {
            std::snprintf(number, sizeof number, "%s%.3f %.3f %.3f", text.empty() ? "" : ";", (double) p.x,
                          (double) p.y, (double) p.bend);
            text += number;
        }
        return text;
    }

    /** Reads `toString`'s text; anything it cannot read gives an empty curve, for the caller to replace. */
    static Curve fromString(const std::string& text)
    {
        Curve curve;
        std::stringstream corners(text);
        std::string corner;
        while (std::getline(corners, corner, ';'))
        {
            std::istringstream values(corner);
            CurvePoint p;
            if (! (values >> p.x >> p.y))
                return {};
            if (! (values >> p.bend))
                p.bend = 0.0f;
            curve.points.push_back(p);
        }
        if (curve.points.size() < 2)
            return {};
        curve.tidy();
        return curve;
    }

    /** The same shape upside down. */
    Curve inverted() const
    {
        auto copy = *this;
        for (auto& p : copy.points)
        {
            p.y = 1.0f - p.y;
            p.bend = -p.bend;
        }
        return copy;
    }

    /** The same shape backwards in time: a segment's bend now belongs to the corner before it. */
    Curve reversed() const
    {
        Curve copy;
        for (std::size_t i = points.size(); i-- > 0;)
        {
            CurvePoint p = points[i];
            p.x = 1.0f - p.x;
            // Read backwards, a segment bulging up still bulges up, but rises where it fell.
            p.bend = i > 0 ? points[i - 1].bend : 0.0f;
            copy.points.push_back(p);
        }
        copy.points.back().bend = 0.0f;
        copy.tidy();
        return copy;
    }
};

/** The starting shapes the editor's menu offers and the presets build on. Their order is the menu's. */
enum class Shape
{
    sine,
    triangle,
    sawDown,
    sawUp,
    square,
    pump,
    rampUp,
    stairs,
    flat,
};
inline constexpr std::array<const char*, 9> kShapeNames { "Sinus",   "Dreieck",   "Säge ab", "Säge auf", "Rechteck",
                                                           "Pumpen", "Anstieg",  "Treppe",  "Gerade" };

/** A function drawn as `corners` corners, each segment bent through the function's value halfway along. */
template <typename Function>
Curve traced(Function f, int corners)
{
    std::vector<CurvePoint> points;
    for (int i = 0; i <= corners; ++i)
    {
        const float x = (float) i / (float) corners;
        points.push_back({ x, (float) f(x), 0.0f });
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        points[i].bend = Curve::bendThrough(points[i].y, points[i + 1].y, (float) f((points[i].x + points[i + 1].x) / 2));
    return Curve(points);
}

/** A gate: `pattern` of x (on) and . (off) over the cycle, each on step open for `length` of its step. */
inline Curve gate(const std::string& pattern, float length = 0.8f, float low = 0.0f)
{
    std::vector<CurvePoint> points;
    const auto steps = (float) std::max<std::size_t>(1, pattern.size());
    auto add = [&points](float x, float y) {
        if (! points.empty() && std::abs(points.back().x - x) < 1.0e-6f && std::abs(points.back().y - y) < 1.0e-6f)
            return;
        points.push_back({ x, y, 0.0f });
    };
    add(0.0f, pattern.empty() || pattern[0] != 'x' ? low : 1.0f);
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        const float start = (float) i / steps;
        const float end = (float) (i + 1) / steps;
        if (pattern[i] == 'x')
        {
            if (i > 0)
            {
                add(start, low);
                add(start, 1.0f);
            }
            add(start + (end - start) * length, 1.0f);
            add(start + (end - start) * length, low);
        }
    }
    add(1.0f, low);
    return Curve(points);
}

inline Curve shapeCurve(Shape shape)
{
    switch (shape)
    {
        case Shape::sine:
            // From the bottom, as a closed filter opening: the wobble starts on the beat.
            return traced([](float x) { return 0.5 - 0.5 * std::cos(2.0 * kPi * x); }, 8);
        case Shape::triangle: return Curve({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
        case Shape::sawDown: return Curve({ { 0.0f, 1.0f, 0.3f }, { 1.0f, 0.0f, 0.0f } });
        case Shape::sawUp: return Curve({ { 0.0f, 0.0f, -0.3f }, { 1.0f, 1.0f, 0.0f } });
        case Shape::square:
            return Curve({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
        case Shape::pump:
            // A side chain's duck: down on the beat, back up within a third of the cycle.
            return Curve({ { 0.0f, 0.0f, 0.75f }, { 0.4f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        case Shape::rampUp: return Curve({ { 0.0f, 0.0f, 0.5f }, { 1.0f, 1.0f, 0.0f } });
        case Shape::stairs:
        {
            std::vector<CurvePoint> points;
            for (int i = 0; i < 4; ++i)
            {
                const float y = (float) (i + 1) / 4.0f;
                points.push_back({ (float) i / 4.0f, y, 0.0f });
                points.push_back({ (float) (i + 1) / 4.0f, y, 0.0f });
            }
            return Curve(points);
        }
        case Shape::flat: break;
    }
    return Curve({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
}

/**
 * A curve sampled for the audio thread. Atomics per value, so the message thread can draw into it while the audio
 * thread reads; a cycle read half old, half new is gone a cycle later and is not heard as more than the edit itself.
 */
class CurveTable
{
public:
    static constexpr int kSize = 1024;

    void fill(const Curve& curve)
    {
        for (int i = 0; i <= kSize; ++i)
            values[(std::size_t) i].store(curve.valueAt((float) i / kSize), std::memory_order_relaxed);
    }

    /** The value at `phase` cycles, any number of cycles in. */
    float read(double phase) const
    {
        const double wrapped = (phase - std::floor(phase)) * kSize;
        const auto index = std::min(kSize - 1, (int) wrapped);
        const auto fraction = (float) (wrapped - index);
        const float a = values[(std::size_t) index].load(std::memory_order_relaxed);
        const float b = values[(std::size_t) index + 1].load(std::memory_order_relaxed);
        return a + (b - a) * fraction;
    }

private:
    std::array<std::atomic<float>, kSize + 1> values {};
};

/** Where an LFO is at the start of a block, in cycles, and how far it goes per sample. */
struct Clock
{
    double phase = 0.0;
    double increment = 0.0;
};

enum class FilterType
{
    lowpass,
    bandpass,
    highpass,
    vowel,
};

struct Settings
{
    bool filterOn = true;
    FilterType filterType = FilterType::lowpass;
    bool steep = true;
    /** The cutoff at the curve's bottom and top, in Hz; between them the curve moves in octaves. */
    float low = 150.0f;
    float high = 3000.0f;
    /** 0 to 1. */
    float resonance = 0.35f;
    float driveDb = 0.0f;

    bool volumeOn = false;
    /** How far the volume curve's bottom takes the level down, 0 to 1. */
    float volumeDepth = 1.0f;

    float smoothMs = 2.0f;
    /** The right channel's LFOs run this many degrees ahead of the left's. */
    float spreadDegrees = 0.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

/** Zavalishin's topology-preserving state variable filter, which keeps its tune and stays stable when swept fast. */
struct SvfStage
{
    float ic1 = 0.0f, ic2 = 0.0f;

    struct Out
    {
        float low, band, high;
    };

    Out process(float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    void reset() { ic1 = ic2 = 0.0f; }
};

/**
 * The vowels the "Vokal" filter sweeps through, dark to bright (u, o, a, e, i), as three formants each: a low curve
 * says "u", a high one "i", and a wobble between them talks (the "yoi" of dubstep). Frequencies of an adult male voice.
 */
struct Vowel
{
    float formant[3];
};
inline constexpr std::array<Vowel, 5> kVowels { { { { 300.0f, 870.0f, 2240.0f } },
                                                  { { 570.0f, 840.0f, 2410.0f } },
                                                  { { 730.0f, 1090.0f, 2440.0f } },
                                                  { { 530.0f, 1840.0f, 2480.0f } },
                                                  { { 270.0f, 2290.0f, 3010.0f } } } };
/**
 * The formants' levels. Nearly even: the sawtooth under them already falls 6 dB an octave, as a voice's source does, so
 * the upper formants come out quieter by themselves.
 */
inline constexpr std::array<float, 3> kFormantGains { 1.0f, 0.9f, 0.7f };

/**
 * One channel: drive, the filter its LFO moves, the volume its LFO shapes. The LFO values are smoothed per sample (a
 * drawn step would click otherwise); the filter's coefficients follow every `kControl` samples, which at 48 kHz is
 * still 6000 times a second, far above the fastest wobble.
 */
class Channel
{
public:
    static constexpr int kControl = 8;

    void prepare(float sampleRate)
    {
        rate = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& stage : stages)
            stage.reset();
        for (auto& stage : formants)
            stage.reset();
        countdown = 0;
        primed = false;
    }

    float filterValue() const { return filterMod; }
    float volumeValue() const { return volumeMod; }

    /**
     * `filterLfo` and `volumeLfo` are the curves' values for this sample, 0 to 1. Returns the processed sample, before
     * the dry/wet mix.
     */
    float process(float x, float filterLfo, float volumeLfo, const Settings& s, float smoothing, float driveGain,
                  float driveMakeup)
    {
        if (! primed)
        {
            filterMod = filterLfo;
            volumeMod = volumeLfo;
            primed = true;
        }
        filterMod += smoothing * (filterLfo - filterMod);
        volumeMod += smoothing * (volumeLfo - volumeMod);

        float y = x;
        if (s.filterOn)
        {
            if (driveGain > 1.0f)
                y = std::tanh(driveGain * y) * driveMakeup;
            if (countdown-- <= 0)
            {
                countdown = kControl - 1;
                updateCoefficients(s);
            }
            y = filter(y, s);
        }
        if (s.volumeOn)
            y *= 1.0f - s.volumeDepth * (1.0f - volumeMod);
        return y;
    }

private:
    float tuning(float hz) const { return (float) std::tan(kPi * std::clamp(hz, 10.0f, 0.45f * rate) / rate); }

    void updateCoefficients(const Settings& s)
    {
        if (s.filterType == FilterType::vowel)
        {
            // Between two vowels the formants glide in octaves, as a voice's do.
            const float at = std::clamp(filterMod, 0.0f, 1.0f) * (float) (kVowels.size() - 1);
            const auto index = std::min((int) at, (int) kVowels.size() - 2);
            const float fraction = at - (float) index;
            // Resonance narrows the formants from 300 Hz wide to 50 Hz: the vowels get sharper and whistle.
            const float bandwidth = 300.0f - 250.0f * s.resonance;
            for (std::size_t f = 0; f < 3; ++f)
            {
                const float from = kVowels[(std::size_t) index].formant[f];
                const float to = kVowels[(std::size_t) index + 1].formant[f];
                const float hz = from * std::pow(to / from, fraction);
                formantG[f] = tuning(hz);
                formantK[f] = std::min(1.4f, bandwidth / hz);
            }
            return;
        }
        const float low = std::max(20.0f, s.low);
        const float high = std::max(20.0f, s.high);
        const float hz = low * std::pow(high / low, std::clamp(filterMod, 0.0f, 1.0f));
        g = tuning(hz);
        // Q from 0.7 (no peak) to about 17: the top of the knob sings without running away.
        k = kButterworth - (kButterworth - 0.06f) * s.resonance;
    }

    float filter(float x, const Settings& s)
    {
        switch (s.filterType)
        {
            case FilterType::vowel:
            {
                float sum = 0.0f;
                for (std::size_t f = 0; f < 3; ++f)
                    // The band output times k peaks at 0 dB whatever the width.
                    sum += kFormantGains[f] * formantK[f] * formants[f].process(x, formantG[f], formantK[f]).band;
                // Three narrow bands keep only a fraction of a bass's energy; this brings it back to about where it was.
                return sum * kVowelMakeup;
            }
            case FilterType::bandpass:
            {
                if (! s.steep)
                    return k * stages[0].process(x, g, k).band;
                // Two in a row: the first a plain one, the second with the resonance.
                const float first = kButterworth * stages[0].process(x, g, kButterworth).band;
                return k * stages[1].process(first, g, k).band;
            }
            case FilterType::highpass:
                if (! s.steep)
                    return stages[0].process(x, g, k).high;
                return stages[1].process(stages[0].process(x, g, kButterworth).high, g, k).high;
            case FilterType::lowpass: break;
        }
        if (! s.steep)
            return stages[0].process(x, g, k).low;
        return stages[1].process(stages[0].process(x, g, kButterworth).low, g, k).low;
    }

    static constexpr float kButterworth = 1.41421356f;
    static constexpr float kVowelMakeup = 4.0f;

    float rate = 48000.0f;
    std::array<SvfStage, 2> stages;
    std::array<SvfStage, 3> formants;
    float g = 0.1f, k = 1.0f;
    std::array<float, 3> formantG { 0.1f, 0.1f, 0.1f }, formantK { 1.0f, 1.0f, 1.0f };
    float filterMod = 0.0f, volumeMod = 1.0f;
    int countdown = 0;
    bool primed = false;
};

/** Both channels and the dry/wet mix. */
class Engine
{
public:
    void prepare(float sampleRate)
    {
        rate = sampleRate;
        for (auto& channel : channels)
            channel.prepare(sampleRate);
    }

    void reset()
    {
        for (auto& channel : channels)
            channel.reset();
    }

    /**
     * Processes `count` samples of up to two channels in place. The clocks say where each LFO is at the first sample;
     * the right channel reads both curves `spreadDegrees` further on.
     */
    void process(float* const* audio, int channelCount, int count, const Settings& s, const CurveTable& filterCurve,
                 const CurveTable& volumeCurve, const Clock& filterClock, const Clock& volumeClock)
    {
        const float smoothing = s.smoothMs <= 0.0f ? 1.0f : 1.0f - std::exp(-1000.0f / (s.smoothMs * rate));
        const float driveGain = std::pow(10.0f, s.driveDb / 20.0f);
        // A sample at -6 dBFS comes out where it went in, so turning the drive up thickens rather than just louder.
        const float driveMakeup = 0.5f / std::tanh(0.5f * driveGain);
        const float output = std::pow(10.0f, s.outputDb / 20.0f);
        const float wet = s.mix * output, dry = (1.0f - s.mix) * output;
        const double spread = s.spreadDegrees / 360.0;
        for (int ch = 0; ch < std::min(channelCount, 2); ++ch)
        {
            auto& channel = channels[(std::size_t) ch];
            float* samples = audio[ch];
            const double offset = ch == 1 ? spread : 0.0;
            for (int n = 0; n < count; ++n)
            {
                const float filterLfo = filterCurve.read(filterClock.phase + offset + n * filterClock.increment);
                const float volumeLfo = volumeCurve.read(volumeClock.phase + offset + n * volumeClock.increment);
                const float x = samples[n];
                const float y = channel.process(x, filterLfo, volumeLfo, s, smoothing, driveGain, driveMakeup);
                samples[n] = wet * y + dry * x;
            }
        }
    }

    const Channel& channel(int index) const { return channels[(std::size_t) std::clamp(index, 0, 1)]; }

private:
    float rate = 48000.0f;
    std::array<Channel, 2> channels;
};
} // namespace tonwerkwobble
