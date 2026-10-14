#include "SampleData.h"

#include "dsp/Oscillators.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace tonwerksampler
{
namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;
/** The built-in sounds sound at C3 (Logic's name for note 60) when played at their own rate, but for the 808. */
const double kC3 = tonwerk::noteFrequency(60);

using Channel = std::vector<float>;

Channel silence(double seconds) { return Channel((std::size_t) (seconds * kRate), 0.0f); }

/** RBJ's biquads, enough for formants, bodies and hats. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    /** Peak gain 1 at `hz`, whatever the bandwidth. */
    void bandpass(double hz, double q)
    {
        const double w = kTwoPi * hz / kRate;
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
        const double w = kTwoPi * hz / kRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - c) / 2.0 / a0;
        b1 = (1.0 - c) / a0;
        b2 = b0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void highpass(double hz, double q)
    {
        const double w = kTwoPi * hz / kRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + c) / 2.0 / a0;
        b1 = -(1.0 + c) / a0;
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

/** A vowel's first three formants in Hz and their levels. */
struct Vowel
{
    double f[3];
    double g[3];
};
constexpr Vowel kA { { 800.0, 1150.0, 2900.0 }, { 1.0, 0.5, 0.25 } };
constexpr Vowel kE { { 420.0, 1900.0, 2600.0 }, { 1.0, 0.35, 0.3 } };
constexpr Vowel kI { { 290.0, 2250.0, 3000.0 }, { 1.0, 0.25, 0.3 } };
constexpr Vowel kO { { 450.0, 800.0, 2830.0 }, { 1.0, 0.6, 0.15 } };
constexpr Vowel kU { { 325.0, 700.0, 2530.0 }, { 1.0, 0.4, 0.1 } };

/** A singer: a sawtooth glottis with breath through three formants; `vowelAt` and `hzAt` say what is sung when. */
struct Singer
{
    tonwerk::BlepOscillator glottis;
    tonwerk::Noise breath { 0x5a3c11u };
    std::array<Biquad, 3> formants;
    Biquad tilt;

    Singer() { tilt.lowpass(3500.0, 0.7); }

    double next(double hz, const Vowel& vowel, bool retune)
    {
        if (retune)
            for (std::size_t i = 0; i < 3; ++i)
                formants[i].bandpass(vowel.f[i], 5.0 + 3.0 * (double) i);
        const double source = tilt.process(glottis.next(tonwerk::Wave::sawtooth, hz, kRate) + 0.12 * breath.next());
        double y = 0.0;
        for (std::size_t i = 0; i < 3; ++i)
            y += formants[i].process(source) * vowel.g[i];
        return y;
    }
};

Vowel blend(const Vowel& a, const Vowel& b, double t)
{
    Vowel v {};
    for (std::size_t i = 0; i < 3; ++i)
    {
        v.f[i] = a.f[i] + (b.f[i] - a.f[i]) * t;
        v.g[i] = a.g[i] + (b.g[i] - a.g[i]) * t;
    }
    return v;
}

/** A long sung "ah" at C3, its vibrato growing in after half a second, for playing chromatically and looping. */
Channel vocalAh()
{
    auto out = silence(3.5);
    Singer singer;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const double vibrato = std::min(1.0, std::max(0.0, t - 0.5) / 0.8) * 22.0 * std::sin(kTwoPi * 5.3 * t);
        const double env = std::min(1.0, t / 0.06) * std::min(1.0, (3.5 - t) / 0.3);
        out[n] = (float) (env * singer.next(kC3 * tonwerk::centsToRatio(vibrato), kA, n % 256 == 0));
    }
    return out;
}

/** Eight sung syllables on a minor pentatonic phrase, each a vowel gliding to the next: made for slicing. */
Channel vocalChops()
{
    struct Syllable
    {
        int semitones;
        const Vowel* from;
        const Vowel* to;
    };
    constexpr std::array<Syllable, 8> phrase { { { 0, &kI, &kA },
                                                 { 3, &kO, &kO },
                                                 { 7, &kE, &kI },
                                                 { 5, &kA, &kA },
                                                 { 10, &kU, &kO },
                                                 { 7, &kA, &kE },
                                                 { 3, &kI, &kA },
                                                 { 0, &kO, &kU } } };
    constexpr double kStep = 0.375;
    auto out = silence(kStep * (double) phrase.size() + 0.1);
    Singer singer;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const auto index = std::min<std::size_t>((std::size_t) (t / kStep), phrase.size() - 1);
        const auto& syllable = phrase[index];
        const double local = t - (double) index * kStep;
        // A consonant-like attack, a held vowel, then a gap before the next one so each starts clean.
        const double env = std::min(1.0, local / 0.012) * std::clamp((0.3 - local) / 0.05, 0.0, 1.0);
        const auto vowel = blend(*syllable.from, *syllable.to, std::clamp(local / 0.2, 0.0, 1.0));
        const double scoop = -60.0 * std::exp(-local / 0.03);
        const double hz = kC3 * std::pow(2.0, syllable.semitones / 12.0) * tonwerk::centsToRatio(scoop);
        out[n] = (float) (env * singer.next(hz, vowel, n % 128 == 0));
    }
    return out;
}

/** Two bars of a broken beat at 120 BPM: kick, snare, ghost notes and hats, synthesised, made for slicing in 16ths. */
Channel breakbeat()
{
    constexpr double kSixteenth = 0.125;
    auto out = silence(32 * kSixteenth + 0.3);
    tonwerk::Noise noise(0x0b7eau);
    auto add = [&out](double at, double seconds, const std::function<double(double)>& voice) {
        const auto from = (std::size_t) (at * kRate);
        for (std::size_t n = 0; n < (std::size_t) (seconds * kRate) && from + n < out.size(); ++n)
            out[from + n] += (float) voice((double) n / kRate);
    };
    // x kick, s snare, g ghost snare, h hat, o open hat; one character a sixteenth.
    const char* kicks = "x.x.......x.....x.x...x...x.....";
    const char* snares = "....s..g.g..s..g....s..g.g..s.g.";
    const char* hats = "h.h.h.h.h.h.h.o.h.h.h.h.h.h.h.o.";
    for (int step = 0; step < 32; ++step)
    {
        const double at = step * kSixteenth;
        if (kicks[step] == 'x')
            add(at, 0.35, [](double t) {
                const double phase = kTwoPi * (50.0 * t + 120.0 * 0.025 * (1.0 - std::exp(-t / 0.025)));
                return 0.9 * std::sin(phase) * std::exp(-t / 0.12);
            });
        if (snares[step] == 's' || snares[step] == 'g')
        {
            const double level = snares[step] == 's' ? 0.75 : 0.22;
            auto body = std::make_shared<Biquad>();
            body->bandpass(2500.0, 0.8);
            add(at, 0.25, [level, body, &noise](double t) {
                const double tone = std::sin(kTwoPi * 190.0 * t) * std::exp(-t / 0.04);
                const double rattle = body->process(noise.next()) * std::exp(-t / 0.07);
                return level * (0.5 * tone + 1.4 * rattle);
            });
        }
        if (hats[step] == 'h' || hats[step] == 'o')
        {
            const double decay = hats[step] == 'o' ? 0.12 : 0.025;
            auto high = std::make_shared<Biquad>();
            high->highpass(7000.0, 0.7);
            add(at, decay * 6.0, [decay, high, &noise](double t) {
                return 0.35 * high->process(noise.next()) * std::exp(-t / decay);
            });
        }
    }
    return out;
}

/** A tine piano at C3: two-operator FM whose index falls, a bright strike and a round tail. */
Channel electricPiano()
{
    auto out = silence(4.0);
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        const double index = 2.2 * std::exp(-t / 0.3) + 0.3;
        const double tine = std::sin(kTwoPi * 14.0 * kC3 * t) * 0.08 * std::exp(-t / 0.05);
        const double env = std::exp(-t / 1.6) * std::min(1.0, t / 0.002);
        out[n] = (float) (env * (std::sin(kTwoPi * kC3 * t + index * std::sin(kTwoPi * kC3 * t)) + tine));
    }
    return out;
}

/** A string section at C3 in stereo: detuned saws, each side its own players, a slow bow and a dark top. */
std::vector<Channel> strings()
{
    std::vector<Channel> out { silence(4.0), silence(4.0) };
    for (std::size_t side = 0; side < 2; ++side)
    {
        std::array<tonwerk::BlepOscillator, 4> players;
        const std::array<double, 4> cents { -14.0 + 3.0 * (double) side, -5.0, 6.0 - 2.0 * (double) side, 13.0 };
        std::array<Biquad, 2> body;
        body[0].lowpass(2800.0, 0.6);
        body[1].lowpass(4200.0, 0.5);
        for (std::size_t n = 0; n < out[side].size(); ++n)
        {
            const double t = (double) n / kRate;
            double y = 0.0;
            for (std::size_t p = 0; p < players.size(); ++p)
            {
                const double drift = 4.0 * std::sin(kTwoPi * (0.3 + 0.07 * (double) p) * t + (double) (p + side));
                y += players[p].next(tonwerk::Wave::sawtooth, kC3 * tonwerk::centsToRatio(cents[p] + drift), kRate);
            }
            const double env = std::min(1.0, t / 0.35) * std::min(1.0, (4.0 - t) / 0.2);
            out[side][n] = (float) (0.3 * env * body[1].process(body[0].process(y)));
        }
    }
    return out;
}

/** A bell at C3: inharmonic partials, the higher ones dying first. */
Channel bell()
{
    constexpr std::array<double, 6> ratio { 0.5, 1.0, 1.19, 1.56, 2.0, 2.74 };
    constexpr std::array<double, 6> level { 0.5, 1.0, 0.6, 0.45, 0.35, 0.25 };
    constexpr std::array<double, 6> decay { 3.5, 2.5, 1.8, 1.2, 0.9, 0.6 };
    auto out = silence(4.0);
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        double y = 0.0;
        for (std::size_t k = 0; k < ratio.size(); ++k)
            y += level[k] * std::sin(kTwoPi * kC3 * ratio[k] * t) * std::exp(-t / decay[k]);
        out[n] = (float) (y * std::min(1.0, t / 0.001));
    }
    return out;
}

/** An 808 at C1 (note 36): a sine falling an octave into its note, a long tail, a touch of drive. */
Channel eightOhEight()
{
    const double hz = tonwerk::noteFrequency(36);
    auto out = silence(2.5);
    double phase = 0.0;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const double t = (double) n / kRate;
        phase += kTwoPi * hz * (1.0 + 1.0 * std::exp(-t / 0.04)) / kRate;
        const double env = std::exp(-t / 0.9) * std::min(1.0, t / 0.001);
        out[n] = (float) std::tanh(1.6 * env * std::sin(phase));
    }
    return out;
}

/** A choir singing "oo" at C3 in stereo: three singers a side, slightly apart in pitch and vibrato. */
std::vector<Channel> choir()
{
    std::vector<Channel> out { silence(4.0), silence(4.0) };
    for (std::size_t side = 0; side < 2; ++side)
    {
        std::array<Singer, 3> singers;
        const std::array<double, 3> cents { -8.0 + 4.0 * (double) side, 2.0, 9.0 - 3.0 * (double) side };
        const auto vowel = blend(kU, kO, 0.4);
        for (std::size_t n = 0; n < out[side].size(); ++n)
        {
            const double t = (double) n / kRate;
            double y = 0.0;
            for (std::size_t s = 0; s < singers.size(); ++s)
            {
                const double vibrato = 15.0 * std::sin(kTwoPi * (4.8 + 0.4 * (double) s + 0.2 * (double) side) * t
                                                       + (double) s);
                y += singers[s].next(kC3 * tonwerk::centsToRatio(cents[s] + vibrato), vowel, n % 256 == 0);
            }
            const double env = std::min(1.0, t / 0.3) * std::min(1.0, (4.0 - t) / 0.2);
            out[side][n] = (float) (env * y);
        }
    }
    return out;
}

/** A plucked nylon string at C3, Karplus-Strong with a soft pluck. */
Channel guitar()
{
    auto out = silence(3.0);
    const double period = kRate / kC3;
    const auto length = (std::size_t) period;
    const double fraction = period - (double) length;
    std::vector<double> line(length + 2, 0.0);
    tonwerk::Noise noise(0x9017au);
    Biquad soft;
    soft.lowpass(2500.0, 0.7);
    for (auto& v : line)
        v = soft.process(noise.next());
    std::size_t at = 0;
    double previous = 0.0;
    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const std::size_t next = (at + 1) % line.size();
        // The loop's average, slightly lossy; the fraction tunes it between whole samples.
        const double y = line[at] * (1.0 - fraction) + line[next] * fraction;
        const double filtered = 0.996 * 0.5 * (y + previous);
        previous = y;
        line[at] = filtered;
        at = next;
        out[n] = (float) y;
    }
    return out;
}

/** Scales channels so their loudest peak sits at `peak`. */
void scaleTo(std::vector<Channel>& channels, float peak)
{
    float loudest = 0.0f;
    for (const auto& c : channels)
        for (const float v : c)
            loudest = std::max(loudest, std::abs(v));
    if (loudest > 0.0f)
        for (auto& c : channels)
            for (auto& v : c)
                v *= peak / loudest;
}

Sample make(std::vector<Channel> channels, const char* name)
{
    scaleTo(channels, 0.8f);
    return Sample::fromChannels(std::move(channels), kRate, name);
}

/** Half the rate: a windowed-sinc lowpass at about 0.45 of the new rate's Nyquist, then every second sample. */
std::vector<float> halve(const std::vector<float>& in)
{
    constexpr int kTaps = 31;
    constexpr int kHalf = kTaps / 2;
    static const std::array<float, kTaps> taps = [] {
        std::array<float, kTaps> h {};
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double x = i - kHalf;
            const double cutoff = 0.225;
            const double sinc = i == kHalf ? 2.0 * cutoff : std::sin(kTwoPi * cutoff * x) / (kPi * x);
            const double window = 0.42 - 0.5 * std::cos(kTwoPi * i / (kTaps - 1)) + 0.08 * std::cos(2.0 * kTwoPi * i / (kTaps - 1));
            h[(std::size_t) i] = (float) (sinc * window);
            sum += sinc * window;
        }
        for (auto& v : h)
            v = (float) (v / sum);
        return h;
    }();
    std::vector<float> out((in.size() + 1) / 2);
    const auto n = (long) in.size();
    for (std::size_t o = 0; o < out.size(); ++o)
    {
        const long centre = (long) (2 * o);
        double y = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const long i = centre + k - kHalf;
            if (i >= 0 && i < n)
                y += taps[(std::size_t) k] * in[(std::size_t) i];
        }
        out[o] = (float) y;
    }
    return out;
}

/** Onsets from the rise of the energy in 5 ms frames over the frames before it, strongest first. */
std::vector<std::pair<double, float>> findOnsets(const Sample& sample)
{
    const auto hop = (std::size_t) std::max(32.0, 0.005 * sample.sampleRate);
    const std::size_t frames = sample.length() / hop;
    std::vector<double> energy(frames, 0.0);
    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < sample.channels; ++c)
            for (std::size_t i = f * hop; i < (f + 1) * hop; ++i)
                energy[f] += (double) sample.levels[0][(std::size_t) c][i] * sample.levels[0][(std::size_t) c][i];
    std::vector<double> rise(frames, 0.0);
    double loudest = 1.0e-12;
    for (std::size_t f = 0; f < frames; ++f)
    {
        double before = 1.0e-9;
        for (std::size_t k = 1; k <= 4 && k <= f; ++k)
            before = std::max(before, energy[f - k]);
        // In decibels, so a quiet ghost note after silence counts as much as its loudness says.
        rise[f] = std::max(0.0, 10.0 * std::log10((energy[f] + 1.0e-9) / before)) * std::sqrt(energy[f]);
        loudest = std::max(loudest, rise[f]);
    }
    std::vector<std::pair<double, float>> onsets;
    // A peak of the rise, the highest within 40 ms, and above a floor.
    const auto spacing = (std::size_t) std::max<double>(1.0, 0.04 * sample.sampleRate / (double) hop);
    for (std::size_t f = 1; f < frames; ++f)
    {
        if (rise[f] < 0.02 * loudest)
            continue;
        bool peak = true;
        for (std::size_t k = f > spacing ? f - spacing : 0; k < std::min(frames, f + spacing + 1) && peak; ++k)
            peak = k == f || rise[k] < rise[f] || (! (rise[k] > rise[f]) && k > f);
        if (peak)
            onsets.push_back({ (double) (f * hop) - 0.5 * (double) hop, (float) (rise[f] / loudest) });
    }
    std::sort(onsets.begin(), onsets.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (onsets.size() > 64)
        onsets.resize(64);
    return onsets;
}
} // namespace

Sample Sample::fromChannels(std::vector<std::vector<float>> input, double rate, std::string sampleName)
{
    Sample s;
    s.sampleRate = rate;
    s.name = std::move(sampleName);
    s.channels = std::clamp((int) input.size(), 1, 2);
    input.resize((std::size_t) s.channels);
    const std::size_t length = std::min(input[0].size(), (std::size_t) (kMaxSeconds * rate));
    for (int c = 0; c < s.channels; ++c)
    {
        auto& channel = input[(std::size_t) c];
        channel.resize(length, 0.0f);
        s.levels[0][(std::size_t) c] = std::move(channel);
        for (int level = 1; level < kLevels; ++level)
            s.levels[(std::size_t) level][(std::size_t) c] = halve(s.levels[(std::size_t) level - 1][(std::size_t) c]);
    }
    s.overview.assign(kOverview, { 0.0f, 0.0f });
    if (length > 0)
        for (std::size_t column = 0; column < (std::size_t) kOverview; ++column)
        {
            const std::size_t from = column * length / kOverview;
            const std::size_t to = std::max(from + 1, (column + 1) * length / kOverview);
            float low = 0.0f, high = 0.0f;
            for (int c = 0; c < s.channels; ++c)
                for (std::size_t i = from; i < std::min(to, length); ++i)
                {
                    low = std::min(low, s.levels[0][(std::size_t) c][i]);
                    high = std::max(high, s.levels[0][(std::size_t) c][i]);
                }
            s.overview[column] = { low, high };
        }
    s.onsets = findOnsets(s);
    return s;
}

float Sample::read(int channel, int level, double position) const
{
    const auto& data = levels[(std::size_t) level][(std::size_t) std::min(channel, channels - 1)];
    const double at = position / (double) (1 << level);
    const double floorAt = std::floor(at);
    const auto i = (long) floorAt;
    const auto t = (float) (at - floorAt);
    const auto n = (long) data.size();
    float xm1, x0, x1, x2;
    if (i >= 1 && i + 2 < n)
    {
        // Inside, as nearly every read is: no checks.
        const float* p = data.data() + i;
        xm1 = p[-1];
        x0 = p[0];
        x1 = p[1];
        x2 = p[2];
    }
    else
    {
        auto get = [&data, n](long k) { return k >= 0 && k < n ? data[(std::size_t) k] : 0.0f; };
        xm1 = get(i - 1);
        x0 = get(i);
        x1 = get(i + 1);
        x2 = get(i + 2);
    }
    // Hermite: through x0 and x1 with the slopes of their neighbours.
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

SampleBank::SampleBank()
{
    samples[0] = make({ vocalAh() }, "Vokal Ah");
    samples[1] = make({ vocalChops() }, "Vokal-Chops");
    samples[2] = make({ breakbeat() }, "Breakbeat");
    samples[3] = make({ electricPiano() }, "E-Piano");
    samples[4] = make(strings(), "Streicher");
    samples[5] = make({ bell() }, "Glocke");
    samples[6] = make({ eightOhEight() }, "808");
    samples[7] = make(choir(), "Chor");
    samples[8] = make({ guitar() }, "Gitarre");
}

const SampleBank& SampleBank::instance()
{
    static const SampleBank bank;
    return bank;
}

const Sample& SampleBank::sample(int index) const
{
    return samples[(std::size_t) std::clamp(index, 0, kBuiltIn - 1)];
}

const std::vector<std::string>& SampleBank::names()
{
    static const std::vector<std::string> list { "Vokal Ah", "Vokal-Chops", "Breakbeat", "E-Piano", "Streicher",
                                                 "Glocke",   "808 (C1)",    "Chor",      "Gitarre",  "Eigene Datei" };
    return list;
}

const Sample& SampleBank::none()
{
    static const Sample empty;
    return empty;
}
} // namespace tonwerksampler
