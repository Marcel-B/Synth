#include "Wavetables.h"

#include <complex>
#include <cstdint>
#include <functional>

namespace tonwerkwave
{
namespace
{
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;
/** The cycle every frame is first drawn at; its spectrum then fills the levels. */
constexpr int kSourceSize = 2048;

using Complex = std::complex<double>;

/** An in-place radix-2 FFT; `sign` -1 forward, +1 inverse (unscaled). Only runs while the tables are made. */
void fft(std::vector<Complex>& x, int sign)
{
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = sign * kTwoPi / (double) length;
        const Complex step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < n; start += length)
        {
            Complex w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const Complex a = x[start + k];
                const Complex b = x[start + k + length / 2] * w;
                x[start + k] = a + b;
                x[start + k + length / 2] = a - b;
                w *= step;
            }
        }
    }
}

/** Harmonic amplitudes 1 to kMaxHarmonics of one frame (index 0 unused), as sines starting at phase 0. */
using Spectrum = std::vector<Complex>;

/** A frame drawn as a cycle of kSourceSize samples, `t` from 0 (first frame) to 1 (last). */
using Shape = std::function<double(double phase, double t)>;
/** A frame given by its harmonics instead: amplitude of harmonic `n` at `t`. */
using Partials = std::function<double(int n, double t)>;

Spectrum spectrumOf(const Shape& shape, double t)
{
    std::vector<Complex> x((std::size_t) kSourceSize);
    for (int i = 0; i < kSourceSize; ++i)
        x[(std::size_t) i] = shape((double) i / kSourceSize, t);
    fft(x, -1);
    Spectrum s((std::size_t) kMaxHarmonics + 1);
    for (int n = 1; n <= kMaxHarmonics; ++n)
        s[(std::size_t) n] = x[(std::size_t) n];
    return s;
}

Spectrum spectrumOf(const Partials& partials, double t)
{
    Spectrum s((std::size_t) kMaxHarmonics + 1);
    for (int n = 1; n <= kMaxHarmonics; ++n)
        // A sine of amplitude a is the bin -i a N / 2 in the forward transform's terms.
        s[(std::size_t) n] = Complex(0.0, -partials(n, t) * kSourceSize / 2.0);
    return s;
}

/** The cycle of `spectrum` with its harmonics up to `harmonics`, at `size` samples plus the wrap sample. */
std::vector<float> render(const Spectrum& spectrum, int harmonics, int size)
{
    std::vector<Complex> x((std::size_t) size);
    // Bins scaled from the source size to this one; a real cycle needs the mirror image in the upper half.
    const double scale = (double) size / kSourceSize;
    for (int n = 1; n <= harmonics && n < size / 2; ++n)
    {
        x[(std::size_t) n] = spectrum[(std::size_t) n] * scale;
        x[(std::size_t) (size - n)] = std::conj(x[(std::size_t) n]);
    }
    fft(x, +1);
    std::vector<float> out((std::size_t) size + 1);
    for (int i = 0; i < size; ++i)
        out[(std::size_t) i] = (float) (x[(std::size_t) i].real() / size);
    out[(std::size_t) size] = out[0];
    return out;
}

Wavetable make(std::string name, const std::function<Spectrum(double)>& frameSpectrum)
{
    Wavetable table;
    table.name = std::move(name);
    std::size_t total = 0;
    for (int level = 0; level < kLevels; ++level)
    {
        table.offsets[(std::size_t) level] = total;
        total += (std::size_t) kFrames * (std::size_t) (Wavetable::size(level) + 1);
    }
    table.data.resize(total);
    for (int frame = 0; frame < kFrames; ++frame)
    {
        const auto spectrum = frameSpectrum((double) frame / (kFrames - 1));
        // Every frame peaks at 1 in its richest form, so moving through the table does not jump in level; the
        // poorer levels keep the same gain, so a high note is as loud as a low one.
        const auto full = render(spectrum, Wavetable::harmonics(0), Wavetable::size(0));
        float peak = 1.0e-6f;
        for (float v : full)
            peak = std::max(peak, std::abs(v));
        for (int level = 0; level < kLevels; ++level)
        {
            const auto cycle = level == 0 ? full : render(spectrum, Wavetable::harmonics(level), Wavetable::size(level));
            float* out = table.data.data() + (table.cycle(level, frame) - table.data.data());
            for (std::size_t i = 0; i < cycle.size(); ++i)
                out[i] = cycle[i] / peak;
        }
    }
    return table;
}

Wavetable fromShape(std::string name, Shape shape)
{
    return make(std::move(name), [&shape](double t) { return spectrumOf(shape, t); });
}

Wavetable fromPartials(std::string name, Partials partials)
{
    return make(std::move(name), [&partials](double t) { return spectrumOf(partials, t); });
}

double sine(double p) { return std::sin(kTwoPi * p); }
double triangle(double p) { return p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0; }
/** Rising from 0 like the sine, the jump half a cycle in, so the shapes crossfade without cancelling. */
double saw(double p) { return p < 0.5 ? 2.0 * p : 2.0 * p - 2.0; }
double square(double p) { return p < 0.5 ? 1.0 : -1.0; }
double fold(double v)
{
    double t = (v + 1.0) / 4.0;
    t -= std::floor(t);
    return 1.0 - 4.0 * std::abs(t - 0.5);
}

/** Formants of sung vowels (a, e, i, o, u) for a low male voice, in Hz. */
constexpr double kVowels[5][3] = {
    { 650.0, 1080.0, 2650.0 }, { 400.0, 1700.0, 2600.0 }, { 290.0, 1870.0, 2800.0 },
    { 400.0, 800.0, 2600.0 },  { 350.0, 600.0, 2700.0 },
};
} // namespace

std::vector<std::string> WavetableBank::names()
{
    return { "Basis", "Sync", "PWM", "Vokal", "FM-Growl", "Falter", "Harmonisch", "Kamm", "Bitcrush", "Rauh" };
}

const WavetableBank& WavetableBank::instance()
{
    static const WavetableBank bank;
    return bank;
}

WavetableBank::WavetableBank()
{
    const auto n = names();
    // Sine, triangle, saw, square, each crossfading into the next.
    tables.push_back(fromShape(n[0], [](double p, double t) {
        const double u = t * 3.0;
        const int segment = std::min(2, (int) u);
        const double f = u - segment;
        const double shapes[4] = { sine(p), triangle(p), saw(p), square(p) };
        return shapes[segment] * (1.0 - f) + shapes[segment + 1] * f;
    }));
    // A saw hard-synced to the cycle, its own rate rising from 1 to 8 times: the classic tearing sync sweep.
    tables.push_back(fromShape(n[1], [](double p, double t) {
        double q = p * (1.0 + 7.0 * t);
        q -= std::floor(q);
        return 2.0 * q - 1.0;
    }));
    // A pulse narrowing from square to a thin spike.
    tables.push_back(fromShape(n[2], [](double p, double t) { return p < 0.5 - 0.46 * t ? 1.0 : -1.0; }));
    // A saw-like voice through the formants of a, e, i, o, u in turn, as if the note were sung at about 110 Hz:
    // moving the position makes the bass talk.
    tables.push_back(fromPartials(n[3], [](int harmonic, double t) {
        const double u = t * 4.0;
        const int vowel = std::min(3, (int) u);
        const double f = u - vowel;
        const double gains[3] = { 1.0, 0.7, 0.35 };
        const double widths[3] = { 90.0, 120.0, 160.0 };
        const double hz = harmonic * 110.0;
        double a = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const double formant = kVowels[vowel][k] * (1.0 - f) + kVowels[vowel + 1][k] * f;
            const double d = (hz - formant) / widths[k];
            a += gains[k] / (1.0 + d * d);
        }
        return a * std::pow((double) harmonic, -0.5);
    }));
    // Two-operator FM, modulator an octave up, the index rising to 8: from a sine to a nasal, metallic growl.
    tables.push_back(fromShape(n[4], [](double p, double t) { return std::sin(kTwoPi * p + 8.0 * t * sine(2.0 * p)); }));
    // A sine driven ever harder into a wavefolder.
    tables.push_back(fromShape(n[5], [](double p, double t) { return fold(sine(p) * (1.0 + 7.0 * t) + 0.15 * t); }));
    // The fundamental and a band of harmonics a third of an octave wide sweeping up through eight octaves.
    tables.push_back(fromPartials(n[6], [](int harmonic, double t) {
        const double centre = 0.5 + 7.5 * t;
        const double d = (std::log2((double) harmonic) - centre) / 0.35;
        return (harmonic == 1 ? 0.6 : 0.0) + std::exp(-0.5 * d * d) / std::sqrt((double) harmonic);
    }));
    // A saw spectrum with comb notches closing in, ending on the even harmonics only: a phaser frozen into frames.
    tables.push_back(fromPartials(n[7], [](int harmonic, double t) {
        return (0.5 + 0.5 * std::cos(kTwoPi * harmonic * 0.5 * t)) / harmonic;
    }));
    // Saw and sine in fewer and fewer steps, in time and in level.
    tables.push_back(fromShape(n[8], [](double p, double t) {
        const int stepsInTime = 256 >> std::min(4, (int) (t * 5.0));
        const double q = std::floor(p * stepsInTime) / stepsInTime;
        const double levels = std::pow(2.0, 6.0 - 5.0 * t);
        return std::round((0.5 * sine(q) + 0.5 * saw(q)) * levels) / levels;
    }));
    // A saw with a sine underneath, saturated harder and lopsided: tube-like dirt.
    tables.push_back(fromShape(n[9], [](double p, double t) {
        return std::tanh((saw(p) + 0.5 * sine(p)) * (1.0 + 12.0 * t) + 0.4 * t);
    }));
}
} // namespace tonwerkwave
