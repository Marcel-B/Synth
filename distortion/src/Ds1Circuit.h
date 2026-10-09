#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace tonwerkdistortion
{
/**
 * The circuit of BOSS's DS-1, stage by stage, with the part values from ElectroSmash's analysis of it. Plain C++, so
 * the tests run it without a host. One sample in a channel goes through `Drive` (booster, op-amp, diodes; the part that
 * makes harmonics, run oversampled by the processor) and then `Voice` (tone, level, output coupling; linear, run at the
 * host's rate). Voltages are the pedal's: 1.0 in the host is 1 V at the input jack, about what a guitar gives when
 * played hard.
 */
struct Settings
{
    /** Gain before the pedal, for sources hotter or quieter than a guitar. */
    float inputDb = 0.0f;
    /** The DIST pot, 0 to 1. A linear 100 kΩ pot, as in the pedal. */
    float distortion = 0.5f;
    /** The TONE pot, 0 (dark) to 1 (bright). */
    float tone = 0.5f;
    /** The LEVEL pot, as gain after the pedal. 0 dB puts a fully clipped note at about -6 dBFS with the tone centred. */
    float levelDb = 0.0f;
};

namespace parts
{
constexpr float kPi = 3.14159265358979f;
// Transistor booster (Q2): C3 47 nF into R5 100 kΩ, gain set to 56 (35 dB) by its shunt feedback.
constexpr float kBoosterHighPass = 33.0f;
constexpr float kBoosterGain = 56.0f;
// Op-amp stage: non-inverting, gain 1 + VR1 / R13, R13 = 4.7 kΩ in series with C8 0.47 µF, VR1 bridged by C7 100 pF.
constexpr float kOpAmpInputHighPass = 23.0f; // C5 68 nF, R10 100 kΩ
constexpr float kR13 = 4700.0f;
constexpr float kC8 = 0.47e-6f;
constexpr float kDistPot = 100000.0f;
constexpr float kC7 = 100.0e-12f;
// The op-amp is a slow single-supply part (NJM2904 class) with 0.6 MHz gain-bandwidth.
constexpr float kGainBandwidth = 0.6e6f;
// Clipper: R14 2.2 kΩ into C10 0.01 µF and two 1N4148 to (virtual) ground.
constexpr float kClipperLowPass = 7200.0f;
constexpr double kR14 = 2200.0;
constexpr double kDiodeSaturation = 2.52e-9;
constexpr double kDiodeThermal = 1.752 * 0.02585;
// Tone: R16 6.8 kΩ with C12 0.1 µF (low-pass) and C11 0.022 µF with R17 6.8 kΩ (high-pass), blended by VR3.
constexpr float kToneLowPass = 234.0f;
constexpr float kToneHighPass = 1063.0f;
// Output: C13 and C14 couple the emitter follower (3.4 Hz and 1.6 Hz); one pole at 5 Hz keeps the clipper's DC out.
constexpr float kOutputHighPass = 5.0f;
} // namespace parts

/** Zavalishin's one-pole (trapezoidal), low-pass and high-pass from one state. */
class OnePole
{
public:
    void setCutoff(float hz, float rate)
    {
        const float g = std::tan(parts::kPi * std::min(hz, 0.45f * rate) / rate);
        coefficient = g / (1.0f + g);
    }
    float lowPass(float x)
    {
        const float v = (x - state) * coefficient;
        const float out = v + state;
        state = out + v;
        return out;
    }
    float highPass(float x) { return x - lowPass(x); }
    void reset() { state = 0.0f; }

private:
    float coefficient = 0.0f;
    float state = 0.0f;
};

/** Moves to a knob's new value over about 20 ms, so turning or automating it does not click. */
class Smoothed
{
public:
    void prepare(float rate, float value)
    {
        step = 1.0f - std::exp(-1.0f / (0.02f * rate));
        current = target = value;
    }
    void set(float value) { target = value; }
    float next() { return current += (target - current) * step; }

private:
    float step = 1.0f;
    float current = 0.0f;
    float target = 0.0f;
};

/** tanh within 2.5 %, without the library call; at ±3 it reaches ±1 and stays there. */
inline float softClip(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** The integral of `softClip` from 0, for antiderivative anti-aliasing. */
inline double softClipIntegral(double x)
{
    const double size = std::abs(x);
    if (size > 3.0)
        return 0.5 + 4.0 / 3.0 * std::log(4.0) + size - 3.0;
    return x * x / 18.0 + 4.0 / 3.0 * std::log1p(x * x / 3.0);
}

/**
 * First-order antiderivative anti-aliasing (Parker, Zavalishin, Le Bivic 2016): the curve's mean between the last
 * input and this one instead of its value at this one. A hard clip sampled point by point puts each edge on the
 * sample grid and folds what lies above Nyquist back into the band; the mean keeps the edge where it falls between two
 * samples. Costs one evaluation of the integral per sample (the last one is kept) and half a sample of delay.
 */
template <typename Curve, typename Integral>
class AntiAliased
{
public:
    AntiAliased(Curve c, Integral i) : curve(c), integral(i) { reset(); }

    void reset()
    {
        last = 0.0;
        lastIntegral = integral(0.0);
    }

    float operator()(float x)
    {
        const double now = integral(x);
        const double delta = (double) x - last;
        // Where the input hardly moves, the quotient loses its digits; the curve at the midpoint is the same limit.
        const double out = std::abs(delta) > 1.0e-5 ? (now - lastIntegral) / delta : curve(0.5 * (x + last));
        last = x;
        lastIntegral = now;
        return (float) out;
    }

private:
    Curve curve;
    Integral integral;
    double last = 0.0;
    double lastIntegral = 0.0;
};

/**
 * The two diodes behind R14: the voltage across them for a voltage before R14, solved from Shockley's equation
 * (v + 2 R Is sinh(v / n Vt) = in) once into a table. Below 0.4 V it passes the signal, then it bends into the ±0.6 V
 * the DS-1 is known for: a hard clip, harder than a Tube Screamer's diodes in the feedback loop.
 */
class DiodeClipper
{
public:
    static const DiodeClipper& instance()
    {
        static const DiodeClipper table;
        return table;
    }

    float operator()(float in) const
    {
        const auto [index, fraction] = locate(in);
        return (float) (values[index] + fraction * (values[index + 1] - values[index]));
    }

    /**
     * The integral of the curve from -6 V, exact for the table's straight pieces, for antiderivative anti-aliasing.
     * Double, since the clipper divides the difference of two nearby values.
     */
    double antiderivative(double in) const
    {
        const double outside = std::abs(in) > kRange ? in - std::copysign(kRange, in) : 0.0;
        const auto [index, fraction] = locate(in);
        const double step = 1.0 / kScale;
        const double partial = fraction * step * (values[index] + 0.5 * fraction * (values[index + 1] - values[index]));
        // Beyond the table the curve is flat at its last value.
        return integrals[index] + partial + outside * (outside > 0.0 ? values[kSize - 1] : values[0]);
    }

    /** The exact solution, for the table and the tests. */
    static double solve(double in)
    {
        const double k = 2.0 * parts::kR14 * parts::kDiodeSaturation;
        const double vt = parts::kDiodeThermal;
        // Start from the asymptote of a conducting diode (or from the input, below it); Newton converges in a few steps.
        double v = std::abs(in) > 0.3 ? std::copysign(vt * std::asinh(std::abs(in) / k), in) : in;
        for (int i = 0; i < 50; ++i)
        {
            const double f = v + k * std::sinh(v / vt) - in;
            const double slope = 1.0 + k / vt * std::cosh(v / vt);
            const double delta = f / slope;
            v -= delta;
            if (std::abs(delta) < 1.0e-12)
                break;
        }
        return v;
    }

private:
    // The op-amp swings at most 4.5 V from its bias, so ±6 V covers it; 2048 points keep the table within 0.1 mV.
    static constexpr double kRange = 6.0;
    static constexpr std::size_t kSize = 2048;
    static constexpr double kScale = (double) (kSize - 1) / (2.0 * kRange);

    DiodeClipper()
    {
        for (std::size_t i = 0; i < kSize; ++i)
            values[i] = solve(-kRange + (double) i / kScale);
        for (std::size_t i = 1; i < kSize; ++i)
            integrals[i] = integrals[i - 1] + 0.5 * (values[i - 1] + values[i]) / kScale;
    }

    std::pair<std::size_t, double> locate(double in) const
    {
        const double position = (std::clamp(in, -kRange, kRange) + kRange) * kScale;
        const auto index = std::min((std::size_t) position, kSize - 2);
        return { index, position - (double) index };
    }

    std::array<double, kSize> values {};
    std::array<double, kSize> integrals {};
};

/**
 * Booster, op-amp and diodes for one channel. Runs at the oversampled rate, since every stage can clip.
 */
class Drive
{
public:
    void prepare(float sampleRate, const Settings& settings)
    {
        rate = sampleRate;
        boosterIn.setCutoff(parts::kBoosterHighPass, rate);
        opAmpIn.setCutoff(parts::kOpAmpInputHighPass, rate);
        gainLeg.setCutoff(1.0f / (2.0f * parts::kPi * parts::kR13 * parts::kC8), rate);
        clipper.setCutoff(parts::kClipperLowPass, rate);
        input.prepare(rate, inputGain(settings));
        dist.prepare(rate, settings.distortion);
        update(settings);
        reset();
    }

    void reset()
    {
        boosterIn.reset();
        opAmpIn.reset();
        gainLeg.reset();
        feedback.reset();
        bandwidth.reset();
        clipper.reset();
        railClip.reset();
        diodeClip.reset();
    }

    /** Takes new knob values; call once per block. */
    void update(const Settings& settings)
    {
        input.set(inputGain(settings));
        dist.set(std::clamp(settings.distortion, 0.0f, 1.0f));
        // C7 across the pot takes off the fizz at high gain (16 kHz at full DIST); at low gain it is out of the band.
        const float pot = std::max(settings.distortion, 0.01f) * parts::kDistPot;
        feedback.setCutoff(1.0f / (2.0f * parts::kPi * pot * parts::kC7), rate);
        bandwidth.setCutoff(parts::kGainBandwidth / (1.0f + pot / parts::kR13), rate);
    }

    float process(float x)
    {
        // Q2 saturates towards its supply: on one side against the 9 V rail, on the other into saturation, softer.
        float v = boosterIn.highPass(x * input.next()) * parts::kBoosterGain;
        v = v > 0.0f ? 4.0f * softClip(v * 0.25f) : 3.5f * softClip(v * (1.0f / 3.5f));

        // Gain 1 + VR1 / (R13 + 1 / sC8): the pot's share only reaches what passes C8, so the bass stays thin (72 Hz).
        v = opAmpIn.highPass(v);
        const float pot = dist.next() * (parts::kDistPot / parts::kR13);
        v += pot * feedback.lowPass(gainLeg.highPass(v));
        // The op-amp's closed-loop bandwidth shrinks with the gain (27 kHz at full DIST), and its output stops at its
        // rails: single supply at 9 V, 4.5 V down to ground, about 3 V up to where its output stage ends. Its slew rate
        // (0.5 V/µs) is left out: the clipper's 7.2 kHz low-pass hides it, and a slew limit sampled even at 192 kHz
        // folds back more than all the clipping together.
        v = railClip(bandwidth.lowPass(v));
        return diodeClip(clipper.lowPass(v));
    }

private:
    static float inputGain(const Settings& s) { return std::pow(10.0f, s.inputDb / 20.0f); }

    /** The op-amp's rails: 3 V up, 4.5 V down from its bias, both rounded. */
    static double rails(double v) { return v > 0.0 ? 3.0 * softClip((float) (v / 3.0)) : 4.5 * softClip((float) (v / 4.5)); }
    static double railsIntegral(double v)
    {
        const double rail = v > 0.0 ? 3.0 : 4.5;
        return rail * rail * softClipIntegral(v / rail);
    }
    static double diodes(double v) { return DiodeClipper::instance()((float) v); }
    static double diodesIntegral(double v) { return DiodeClipper::instance().antiderivative(v); }

    float rate = 48000.0f;
    AntiAliased<double (*)(double), double (*)(double)> railClip { &rails, &railsIntegral };
    AntiAliased<double (*)(double), double (*)(double)> diodeClip { &diodes, &diodesIntegral };
    OnePole boosterIn, opAmpIn, gainLeg, feedback, bandwidth, clipper;
    Smoothed input, dist;
};

/**
 * Tone, level and output coupling for one channel, at the host's rate: the tone pot blends a low-pass and a high-pass,
 * which leaves the DS-1's dip around 500 Hz with the knob centred.
 */
class Voice
{
public:
    void prepare(float rate, const Settings& settings)
    {
        low.setCutoff(parts::kToneLowPass, rate);
        high.setCutoff(parts::kToneHighPass, rate);
        dcBlock.setCutoff(parts::kOutputHighPass, rate);
        tone.prepare(rate, settings.tone);
        level.prepare(rate, levelGain(settings));
        reset();
    }

    void reset()
    {
        low.reset();
        high.reset();
        dcBlock.reset();
    }

    void update(const Settings& settings)
    {
        tone.set(std::clamp(settings.tone, 0.0f, 1.0f));
        level.set(levelGain(settings));
    }

    float process(float x)
    {
        const float t = tone.next();
        const float shaped = (1.0f - t) * low.lowPass(x) + t * high.highPass(x);
        return dcBlock.highPass(shaped) * level.next();
    }

private:
    // The stack loses about 12 dB of the clipper's ±0.6 V, minus the overshoot of its filters: 1.5 brings a clipped note
    // to about -6 dBFS peak at 0 dB.
    static constexpr float kMakeUp = 1.5f;
    static float levelGain(const Settings& s) { return kMakeUp * std::pow(10.0f, s.levelDb / 20.0f); }

    OnePole low, high, dcBlock;
    Smoothed tone, level;
};
} // namespace tonwerkdistortion
