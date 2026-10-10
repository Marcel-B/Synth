#include "GrainSources.h"

#include "dsp/Oscillators.h"

#include <algorithm>
#include <cmath>

namespace tonwerkgrain
{
namespace
{
constexpr double kRate = 48000.0;
constexpr double kSeconds = 4.0;
constexpr double kPi = 3.141592653589793;
constexpr int kRoot = 60;
const double kRootHz = tonwerk::noteFrequency(kRoot);

std::vector<float> silence() { return std::vector<float>((std::size_t) (kSeconds * kRate), 0.0f); }

/** RBJ's biquads, enough for the formants and the wind. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    /** Peak gain 1 at `hz`, whatever the bandwidth. */
    void bandpass(double hz, double q)
    {
        const double w = 2.0 * kPi * hz / kRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b1 = 0.0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos(w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void lowpass(double hz, double q)
    {
        const double w = 2.0 * kPi * hz / kRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - c) / 2.0 / a0;
        b1 = (1.0 - c) / a0;
        b2 = b0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }

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

/** A sung "a e i o u": three slightly detuned sawtooth voices with vibrato through three formants per vowel. */
std::vector<float> choir()
{
    // F1, F2, F3 in Hz and their levels, for a, e, i, o, u; one vowel a second, gliding between.
    struct Vowel
    {
        double f[3];
        double g[3];
    };
    constexpr std::array<Vowel, 5> vowels { {
        { { 800.0, 1150.0, 2900.0 }, { 1.0, 0.5, 0.25 } },
        { { 420.0, 1900.0, 2600.0 }, { 1.0, 0.35, 0.3 } },
        { { 290.0, 2250.0, 3000.0 }, { 1.0, 0.25, 0.3 } },
        { { 450.0, 800.0, 2830.0 }, { 1.0, 0.6, 0.15 } },
        { { 325.0, 700.0, 2530.0 }, { 1.0, 0.4, 0.1 } },
    } };
    auto out = silence();
    std::array<tonwerk::BlepOscillator, 3> voices;
    const std::array<double, 3> detune { 0.0, -9.0, 8.0 };
    const std::array<double, 3> vibratoRate { 5.1, 5.6, 4.7 };
    std::array<Biquad, 3> formants;
    tonwerk::Noise breath(0x51ed27u);
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        if (n % 64 == 0)
        {
            const double at = std::clamp(t, 0.0, 3.999);
            const auto from = (std::size_t) at;
            const auto to = std::min<std::size_t>(from + 1, vowels.size() - 1);
            // Holds each vowel for most of its second, then glides to the next.
            const double blend = std::clamp((at - (double) from - 0.6) / 0.4, 0.0, 1.0);
            for (std::size_t i = 0; i < 3; ++i)
                formants[i].bandpass(vowels[from].f[i] + (vowels[to].f[i] - vowels[from].f[i]) * blend, 6.0 + 3.0 * (double) i);
        }
        double glottis = 0.0;
        for (std::size_t v = 0; v < voices.size(); ++v)
        {
            const double cents = detune[v] + 18.0 * std::sin(2.0 * kPi * vibratoRate[v] * t + (double) v);
            glottis += voices[v].next(tonwerk::Wave::sawtooth, kRootHz * tonwerk::centsToRatio(cents), kRate);
        }
        glottis += 0.15 * breath.next();
        const double at = std::clamp(t, 0.0, 3.999);
        const auto from = (std::size_t) at;
        const auto to = std::min<std::size_t>(from + 1, vowels.size() - 1);
        const double blend = std::clamp((at - (double) from - 0.6) / 0.4, 0.0, 1.0);
        double y = 0.0;
        for (std::size_t i = 0; i < 3; ++i)
            y += formants[i].process(glottis) * (vowels[from].g[i] + (vowels[to].g[i] - vowels[from].g[i]) * blend);
        out[n] = (float) y;
    }
    return out;
}

/** Struck glass: inharmonic partials dying away, a strike every half second, at the note and its octaves. */
std::vector<float> glass()
{
    constexpr std::array<double, 5> ratio { 1.0, 2.32, 4.25, 6.63, 9.38 };
    constexpr std::array<double, 5> level { 1.0, 0.55, 0.35, 0.22, 0.12 };
    constexpr std::array<double, 5> decay { 2.4, 1.5, 0.9, 0.55, 0.35 };
    constexpr std::array<int, 8> octave { 0, 1, 0, 2, 1, 0, 1, 0 };
    constexpr std::array<double, 8> strength { 1.0, 0.7, 0.85, 0.5, 0.75, 0.9, 0.6, 1.0 };
    auto out = silence();
    for (std::size_t s = 0; s < octave.size(); ++s)
    {
        const auto start = (std::size_t) (0.5 * (double) s * kRate);
        const double hz = kRootHz * std::exp2(octave[s]);
        for (std::size_t p = 0; p < ratio.size(); ++p)
        {
            const double f = hz * ratio[p];
            if (f > 0.45 * kRate)
                continue;
            const double w = 2.0 * kPi * f / kRate;
            const double fall = std::exp(-1.0 / (decay[p] * kRate));
            double gain = strength[s] * level[p];
            for (std::size_t n = start; n < out.size() && gain > 1.0e-5; ++n)
            {
                // A 1 ms rise, so the strike is crisp but not a click.
                const double rise = std::min(1.0, (double) (n - start) / (0.001 * kRate));
                out[n] += (float) (gain * rise * std::sin(w * (double) (n - start)));
                gain *= fall;
            }
        }
    }
    return out;
}

/** A string section: seven detuned saws an octave apart in part, bowed in slowly, through a breathing lowpass. */
std::vector<float> strings()
{
    auto out = silence();
    std::array<tonwerk::BlepOscillator, 7> saws;
    const std::array<double, 7> cents { 0.0, -11.0, 12.0, -5.0, 6.0, -1200.0 + 4.0, -1200.0 - 7.0 };
    const std::array<double, 7> gain { 1.0, 0.8, 0.8, 0.9, 0.9, 0.5, 0.5 };
    Biquad tone;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        if (n % 32 == 0)
            tone.lowpass(1800.0 + 900.0 * std::sin(2.0 * kPi * 0.3 * t), 0.8);
        double sum = 0.0;
        for (std::size_t v = 0; v < saws.size(); ++v)
        {
            const double vibrato = 7.0 * std::sin(2.0 * kPi * (5.2 + 0.3 * (double) v) * t + (double) v);
            sum += gain[v] * saws[v].next(tonwerk::Wave::sawtooth, kRootHz * tonwerk::centsToRatio(cents[v] + vibrato), kRate);
        }
        const double swell = std::min(1.0, t / 0.6) * (0.85 + 0.15 * std::sin(2.0 * kPi * 0.7 * t));
        out[n] = (float) (tone.process(sum) * swell);
    }
    return out;
}

/** Plucked strings (Karplus-Strong): eight plucks, at the note and an octave up, each its own string. */
std::vector<float> plucks()
{
    auto out = silence();
    constexpr std::array<int, 8> octave { 0, 1, 0, 1, 0, 2, 1, 0 };
    tonwerk::Noise noise(0x2468aceu);
    for (std::size_t s = 0; s < octave.size(); ++s)
    {
        const auto start = (std::size_t) (0.5 * (double) s * kRate);
        const double period = kRate / (kRootHz * std::exp2(octave[s]));
        // The loop's lowpass delays by half a sample; the rest of the period is the line, read fractionally.
        const double delay = period - 0.5;
        std::vector<double> line((std::size_t) std::ceil(delay) + 2, 0.0);
        // A noise burst, lowpassed a little so the attack is woody rather than hissing.
        double smooth = 0.0;
        for (auto& x : line)
        {
            smooth += 0.5 * (noise.next() - smooth);
            x = smooth;
        }
        const auto size = line.size();
        std::size_t write = 0;
        double previous = 0.0;
        for (std::size_t n = start; n < std::min(out.size(), start + (std::size_t) (2.0 * kRate)); ++n)
        {
            double read = (double) write - delay;
            while (read < 0.0)
                read += (double) size;
            const auto i = (std::size_t) read;
            const double frac = read - (double) i;
            const double y = line[i % size] + (line[(i + 1) % size] - line[i % size]) * frac;
            const double next = 0.996 * 0.5 * (y + previous);
            previous = y;
            line[write] = next;
            write = (write + 1) % size;
            out[n] += (float) y;
        }
    }
    return out;
}

/** A drawbar organ, every bar a little out, a slow rotary wobble on top. */
std::vector<float> organ()
{
    auto out = silence();
    constexpr std::array<double, 7> ratio { 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };
    constexpr std::array<double, 7> level { 0.8, 1.0, 0.7, 0.5, 0.45, 0.25, 0.2 };
    std::array<double, 7> phase {};
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        // The rotor speeds up from chorale to tremolo halfway.
        const double rotor = t < 2.0 ? 0.8 : 0.8 + std::min(1.0, (t - 2.0) / 1.2) * 5.6;
        const double wobble = 1.0 + 0.002 * std::sin(2.0 * kPi * rotor * t);
        double sum = 0.0;
        for (std::size_t b = 0; b < ratio.size(); ++b)
        {
            phase[b] += kRootHz * ratio[b] * wobble * tonwerk::centsToRatio(0.7 * (double) b) / kRate;
            phase[b] -= std::floor(phase[b]);
            sum += level[b] * std::sin(2.0 * kPi * phase[b]);
        }
        out[n] = (float) (sum * (0.85 + 0.15 * std::sin(2.0 * kPi * rotor * t + 1.0)));
    }
    return out;
}

/** Wind: noise through a resonant band wandering between low and high, over a quiet rumble. */
std::vector<float> wind()
{
    auto out = silence();
    tonwerk::Noise noise(0x13579bdu);
    Biquad band, rumble;
    rumble.lowpass(180.0, 0.7);
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        if (n % 32 == 0)
        {
            const double wander = 0.5 + 0.35 * std::sin(2.0 * kPi * 0.21 * t) + 0.15 * std::sin(2.0 * kPi * 0.67 * t + 2.0);
            band.bandpass(250.0 * std::exp2(4.0 * wander), 4.0);
        }
        const double x = noise.next();
        const double gust = 0.6 + 0.4 * std::sin(2.0 * kPi * 0.35 * t);
        out[n] = (float) (band.process(x) * gust + 0.6 * rumble.process(x));
    }
    return out;
}

/** Metal: two FM pairs at inharmonic ratios, the index swelling up and back so the clang changes along the source. */
std::vector<float> metal()
{
    auto out = silence();
    double carrier = 0.0, modulator = 0.0, carrier2 = 0.0, modulator2 = 0.0;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const double index = 0.5 + 5.5 * std::sin(kPi * t / kSeconds);
        modulator += kRootHz * 1.4142 / kRate;
        modulator -= std::floor(modulator);
        carrier += kRootHz / kRate;
        carrier -= std::floor(carrier);
        modulator2 += kRootHz * 3.53 / kRate;
        modulator2 -= std::floor(modulator2);
        carrier2 += kRootHz * 2.0 / kRate;
        carrier2 -= std::floor(carrier2);
        const double a = std::sin(2.0 * kPi * carrier + index * std::sin(2.0 * kPi * modulator));
        const double b = std::sin(2.0 * kPi * carrier2 + 0.6 * index * std::sin(2.0 * kPi * modulator2));
        out[n] = (float) (a + 0.4 * b);
    }
    return out;
}

/** A plain sine, for clean clouds and basses whose colour comes from the grains alone. */
std::vector<float> sine()
{
    auto out = silence();
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = (float) std::sin(2.0 * kPi * kRootHz * (double) n / kRate);
    return out;
}

/**
 * Two bars of sixteenths at 120 BPM: a kick, closed hats and filtered saw stabs. Grains from it stutter and
 * scatter into rhythmic textures.
 */
std::vector<float> rhythm()
{
    auto out = silence();
    const auto step = (std::size_t) (0.125 * kRate);
    tonwerk::Noise noise(0x9abcdefu);
    for (std::size_t s = 0; s < 32; ++s)
    {
        const auto start = s * step;
        const int beat = (int) (s % 16);
        if (beat % 4 == 0)
        {
            // A kick: a sine falling from 150 to 50 Hz.
            double phase = 0.0;
            for (std::size_t n = 0; n < step * 2 && start + n < out.size(); ++n)
            {
                const double t = (double) n / kRate;
                phase += (50.0 + 100.0 * std::exp(-t / 0.03)) / kRate;
                out[start + n] += (float) (0.9 * std::sin(2.0 * kPi * phase) * std::exp(-t / 0.15));
            }
        }
        if (beat % 2 == 1)
        {
            // A hat: noise with its low end taken off by a difference, gone in 30 ms.
            double last = 0.0;
            for (std::size_t n = 0; n < step && start + n < out.size(); ++n)
            {
                const double x = noise.next();
                out[start + n] += (float) (0.25 * (x - last) * std::exp(-(double) n / kRate / 0.03));
                last = x;
            }
        }
        if (beat == 2 || beat == 6 || beat == 10 || beat == 13)
        {
            // A stab: root, fifth and octave as saws, the lowpass closing fast.
            std::array<tonwerk::BlepOscillator, 3> saws;
            Biquad filter;
            for (std::size_t n = 0; n < step * 2 && start + n < out.size(); ++n)
            {
                const double t = (double) n / kRate;
                if (n % 16 == 0)
                    filter.lowpass(300.0 + 4000.0 * std::exp(-t / 0.05), 2.0);
                const double sum = saws[0].next(tonwerk::Wave::sawtooth, kRootHz, kRate)
                                   + saws[1].next(tonwerk::Wave::sawtooth, kRootHz * 1.4983, kRate)
                                   + saws[2].next(tonwerk::Wave::sawtooth, kRootHz * 2.0, kRate);
                out[start + n] += (float) (0.35 * filter.process(sum) * std::exp(-t / 0.12));
            }
        }
    }
    return out;
}

/** Half the rate: a windowed-sinc lowpass at 0.45 of the new Nyquist, then every second sample. */
std::vector<float> decimate(const std::vector<float>& in)
{
    constexpr int kTaps = 47;
    constexpr int kHalf = kTaps / 2;
    static const auto taps = [] {
        std::array<double, kTaps> h {};
        const double cutoff = 0.225;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double x = (double) (i - kHalf);
            const double sinc = i == kHalf ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * x) / (kPi * x);
            const double blackman = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[(std::size_t) i] = sinc * blackman;
            sum += h[(std::size_t) i];
        }
        for (auto& v : h)
            v /= sum;
        return h;
    }();
    std::vector<float> out(in.size() / 2);
    const auto size = (long) in.size();
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const long centre = (long) (2 * n);
        double y = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const long at = centre + i - kHalf;
            if (at >= 0 && at < size)
                y += taps[(std::size_t) i] * in[(std::size_t) at];
        }
        out[n] = (float) y;
    }
    return out;
}
} // namespace

GrainSource GrainSource::fromSamples(std::vector<float> samples, double rate, int root, std::string sourceName)
{
    GrainSource source;
    source.sampleRate = rate;
    source.rootNote = root;
    source.name = std::move(sourceName);
    double sum = 0.0;
    float peak = 0.0f;
    for (const float x : samples)
    {
        sum += (double) x * x;
        peak = std::max(peak, std::abs(x));
    }
    if (! samples.empty() && peak > 0.0f)
    {
        const double rms = std::sqrt(sum / (double) samples.size());
        const float gain = (float) std::min(0.25 / std::max(rms, 1.0e-9), 0.944 / peak);
        for (auto& x : samples)
            x *= gain;
    }
    source.levels[0] = std::move(samples);
    for (int k = 1; k < kLevels; ++k)
        source.levels[(std::size_t) k] = decimate(source.levels[(std::size_t) k - 1]);

    const auto& full = source.levels[0];
    source.overview.assign(kOverview, { 0.0f, 0.0f });
    if (! full.empty())
        for (int c = 0; c < kOverview; ++c)
        {
            const auto from = full.size() * (std::size_t) c / kOverview;
            const auto to = std::max(from + 1, full.size() * (std::size_t) (c + 1) / kOverview);
            const auto [low, high] = std::minmax_element(full.begin() + (long) from, full.begin() + (long) std::min(to, full.size()));
            source.overview[(std::size_t) c] = { *low, *high };
        }
    return source;
}

SourceBank::SourceBank()
{
    using Maker = std::vector<float> (*)();
    const std::array<Maker, kBuiltIn> makers { choir, glass, strings, plucks, organ, wind, metal, sine, rhythm };
    for (std::size_t i = 0; i < makers.size(); ++i)
        sources[i] = GrainSource::fromSamples(makers[i](), kRate, kRoot, names()[i]);
}

const SourceBank& SourceBank::instance()
{
    static const SourceBank bank;
    return bank;
}

const GrainSource& SourceBank::source(int index) const
{
    return sources[(std::size_t) std::clamp(index, 0, kBuiltIn - 1)];
}

const std::vector<std::string>& SourceBank::names()
{
    static const std::vector<std::string> list { "Chor",  "Glas",  "Streicher", "Zupfen",   "Orgel",
                                                 "Wind",  "Metall", "Sinus",    "Rhythmus", "Eigene Datei" };
    return list;
}
} // namespace tonwerkgrain
