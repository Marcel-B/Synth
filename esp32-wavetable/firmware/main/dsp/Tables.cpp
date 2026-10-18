#include "Tables.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace twm
{
Table Bank::tables[kTables];

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;
/** The cycle every frame is first drawn at; its spectrum then fills the levels. */
constexpr int kSourceSize = 2048;

using Complex = std::complex<float>;

/** cos and sin of 2 pi k / kSourceSize, so the FFTs need no trigonometry (the ESP32's FPU has none). */
Complex twiddle[kSourceSize / 2];

void makeTwiddles()
{
    for (int k = 0; k < kSourceSize / 2; ++k)
    {
        const double angle = 2.0 * 3.141592653589793 * k / kSourceSize;
        twiddle[k] = Complex((float) std::cos(angle), (float) std::sin(angle));
    }
}

/** An in-place radix-2 FFT of `n` (a power of two up to kSourceSize); `sign` -1 forward, +1 inverse (unscaled). */
void fft(Complex* x, int n, int sign)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (int length = 2; length <= n; length <<= 1)
    {
        const int stride = kSourceSize / length;
        for (int start = 0; start < n; start += length)
            for (int k = 0; k < length / 2; ++k)
            {
                const Complex t = twiddle[k * stride];
                const Complex w(t.real(), (float) sign * t.imag());
                const Complex a = x[start + k];
                const Complex b = x[start + k + length / 2] * w;
                x[start + k] = a + b;
                x[start + k + length / 2] = a - b;
            }
    }
}

/**
 * The plugin keeps 512 harmonics and scales each frame by the peak of that richest cycle. The module plays only 256,
 * but takes its scale from the same 512, or a saw's Gibbs ripple would make it 0.2 dB louder than the plugin's.
 */
constexpr int kPeakHarmonics = 512;

/** Harmonics 1 to kPeakHarmonics of one frame (index 0 unused), in the forward transform's terms at kSourceSize. */
using Spectrum = Complex[kPeakHarmonics + 1];

/** A frame drawn as a cycle, `t` from 0 (first frame) to 1 (last). */
using Shape = float (*)(float phase, float t);
/** A frame given by its harmonics instead: amplitude of harmonic `n` at `t`. */
using Partials = float (*)(int n, float t);

Complex work[kSourceSize];

void spectrumOfShape(Shape shape, float t, Spectrum& s)
{
    for (int i = 0; i < kSourceSize; ++i)
        work[i] = Complex(shape((float) i / kSourceSize, t), 0.0f);
    fft(work, kSourceSize, -1);
    for (int n = 1; n <= kPeakHarmonics; ++n)
        s[n] = work[n];
}

void spectrumOfPartials(Partials partials, float t, Spectrum& s)
{
    for (int n = 1; n <= kPeakHarmonics; ++n)
        // A sine of amplitude a is the bin -i a N / 2 in the forward transform's terms.
        s[n] = Complex(0.0f, -partials(n, t) * kSourceSize / 2.0f);
}

/** The cycle of `spectrum` with its harmonics up to `harmonics`, at `size` samples, into `work`. */
void render(const Spectrum& spectrum, int harmonics, int size)
{
    std::fill(work, work + size, Complex());
    const float scale = (float) size / kSourceSize;
    for (int n = 1; n <= harmonics && n < size / 2; ++n)
    {
        work[n] = spectrum[n] * scale;
        work[size - n] = std::conj(work[n]);
    }
    fft(work, size, +1);
}

// The plugin's formulas (wavetable/src/Wavetables.cpp in Marcel-B/Synth), in float.
float sine(float p) { return std::sin(kTwoPi * p); }
float triangle(float p) { return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f; }
/** Rising from 0 like the sine, the jump half a cycle in, so the shapes crossfade without cancelling. */
float saw(float p) { return p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f; }
float square(float p) { return p < 0.5f ? 1.0f : -1.0f; }
float fold(float v)
{
    float t = (v + 1.0f) / 4.0f;
    t -= std::floor(t);
    return 1.0f - 4.0f * std::abs(t - 0.5f);
}

/** Formants of sung vowels (a, e, i, o, u) for a low male voice, in Hz. */
constexpr float kVowels[5][3] = {
    { 650.0f, 1080.0f, 2650.0f }, { 400.0f, 1700.0f, 2600.0f }, { 290.0f, 1870.0f, 2800.0f },
    { 400.0f, 800.0f, 2600.0f },  { 350.0f, 600.0f, 2700.0f },
};

/** Sine, triangle, saw, square, each crossfading into the next. */
float basic(float p, float t)
{
    const float u = t * 3.0f;
    const int segment = std::min(2, (int) u);
    const float f = u - (float) segment;
    const float shapes[4] = { sine(p), triangle(p), saw(p), square(p) };
    return shapes[segment] * (1.0f - f) + shapes[segment + 1] * f;
}
/** A saw hard-synced to the cycle, its own rate rising from 1 to 8 times. */
float sync(float p, float t)
{
    float q = p * (1.0f + 7.0f * t);
    q -= std::floor(q);
    return 2.0f * q - 1.0f;
}
/** A pulse narrowing from square to a thin spike. */
float pwm(float p, float t) { return p < 0.5f - 0.46f * t ? 1.0f : -1.0f; }
/** A saw-like voice through the formants of a, e, i, o, u in turn, as if sung at about 110 Hz. */
float vowel(int harmonic, float t)
{
    const float u = t * 4.0f;
    const int v = std::min(3, (int) u);
    const float f = u - (float) v;
    const float gains[3] = { 1.0f, 0.7f, 0.35f };
    const float widths[3] = { 90.0f, 120.0f, 160.0f };
    const float hz = (float) harmonic * 110.0f;
    float a = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float formant = kVowels[v][k] * (1.0f - f) + kVowels[v + 1][k] * f;
        const float d = (hz - formant) / widths[k];
        a += gains[k] / (1.0f + d * d);
    }
    return a / std::sqrt((float) harmonic);
}
/** Two-operator FM, modulator an octave up, the index rising to 8. */
float fmGrowl(float p, float t) { return std::sin(kTwoPi * p + 8.0f * t * sine(2.0f * p)); }
/** A sine driven ever harder into a wavefolder. */
float folder(float p, float t) { return fold(sine(p) * (1.0f + 7.0f * t) + 0.15f * t); }
/** The fundamental and a band of harmonics a third of an octave wide sweeping up through eight octaves. */
float harmonicBand(int harmonic, float t)
{
    const float centre = 0.5f + 7.5f * t;
    const float d = (std::log2((float) harmonic) - centre) / 0.35f;
    return (harmonic == 1 ? 0.6f : 0.0f) + std::exp(-0.5f * d * d) / std::sqrt((float) harmonic);
}
/** A saw spectrum with comb notches closing in, ending on the even harmonics only. */
float comb(int harmonic, float t) { return (0.5f + 0.5f * std::cos(kTwoPi * (float) harmonic * 0.5f * t)) / (float) harmonic; }
/** Saw and sine in fewer and fewer steps, in time and in level. */
float bitcrush(float p, float t)
{
    const int stepsInTime = 256 >> std::min(4, (int) (t * 5.0f));
    const float q = std::floor(p * (float) stepsInTime) / (float) stepsInTime;
    const float levels = std::pow(2.0f, 6.0f - 5.0f * t);
    return std::round((0.5f * sine(q) + 0.5f * saw(q)) * levels) / levels;
}
/** A saw with a sine underneath, saturated harder and lopsided: tube-like dirt. */
float rough(float p, float t) { return std::tanh((saw(p) + 0.5f * sine(p)) * (1.0f + 12.0f * t) + 0.4f * t); }

struct Recipe
{
    const char* name;
    Shape shape;
    Partials partials;
};
constexpr Recipe kRecipes[kTables] = {
    { "Basis", basic, nullptr },        { "Sync", sync, nullptr },         { "PWM", pwm, nullptr },
    { "Vokal", nullptr, vowel },        { "FM-Growl", fmGrowl, nullptr },  { "Falter", folder, nullptr },
    { "Harmonisch", nullptr, harmonicBand }, { "Kamm", nullptr, comb },   { "Bitcrush", bitcrush, nullptr },
    { "Rauh", rough, nullptr },
};

Spectrum spectrum;
} // namespace

std::size_t Table::samples()
{
    std::size_t total = 0;
    for (int level = 0; level < kLevels; ++level)
        total += (std::size_t) kFrames * (std::size_t) (size(level) + 1);
    return total;
}

const char* Bank::name(int index) { return kRecipes[std::clamp(index, 0, kTables - 1)].name; }

void Bank::layout(int16_t* block)
{
    for (int index = 0; index < kTables; ++index)
    {
        Table& table = tables[index];
        table.data = block + (std::size_t) index * Table::samples();
        std::size_t offset = 0;
        for (int level = 0; level < kLevels; ++level)
        {
            table.offsets[level] = offset;
            offset += (std::size_t) kFrames * (std::size_t) (Table::size(level) + 1);
        }
    }
}

void Bank::compute()
{
    makeTwiddles();
    for (int index = 0; index < kTables; ++index)
    {
        Table& table = tables[index];
        const Recipe& recipe = kRecipes[index];
        for (int frame = 0; frame < kFrames; ++frame)
        {
            const float t = (float) frame / (kFrames - 1);
            if (recipe.shape != nullptr)
                spectrumOfShape(recipe.shape, t, spectrum);
            else
                spectrumOfPartials(recipe.partials, t, spectrum);
            // Every frame peaks at full scale in its richest form, so moving through the table does not jump in
            // level; the poorer levels keep the same gain, so a high note is as loud as a low one.
            render(spectrum, kPeakHarmonics, kSourceSize);
            float peak = 1.0e-6f;
            for (int i = 0; i < kSourceSize; ++i)
                peak = std::max(peak, std::abs(work[i].real()));
            // The inverse FFT is unscaled: a cycle of n samples comes out n times too loud.
            peak /= (float) kSourceSize;
            for (int level = 0; level < kLevels; ++level)
            {
                const int n = Table::size(level);
                render(spectrum, Table::harmonics(level), n);
                const float gain = Table::kToInt / (peak * (float) n);
                auto* out = const_cast<int16_t*>(table.cycle(level, frame));
                for (int i = 0; i < n; ++i)
                    out[i] = (int16_t) std::clamp(std::lround(work[i].real() * gain), -32767L, 32767L);
                out[n] = out[0];
            }
        }
    }
}

bool Bank::build(Allocator allocate)
{
    auto* block = static_cast<int16_t*>(allocate(bytes()));
    if (block == nullptr)
        return false;
    layout(block);
    compute();
    return true;
}
} // namespace twm
