#pragma once

#include "Ds1Circuit.h"

#include <cmath>

namespace tonwerkdistortion
{
/**
 * The modes beside the pedal: each one curve, driven by DIST from 0 to its own maximum gain, with first-order
 * antiderivative anti-aliasing (`AntiAliased`) on top of the processor's 4x oversampling. They are cheaper than the
 * pedal's circuit, which matters more than accuracy here: none models a particular device. All of them are bounded, so
 * DIST makes them denser, not louder, and each is scaled so a fully driven note lands where the pedal's does.
 */
namespace curves
{
/**
 * A triode stage: the grid conducts and flattens the top early, the bottom runs into cut-off later. The bias moves the
 * curve's centre off zero, so the halves clip unequally and the even harmonics (the "warm" ones) come in from the first
 * touch of DIST. The DC this leaves is taken out by the output's DC block.
 */
constexpr double kTubeBias = 0.35;
inline double tube(double v) { return softClip((float) (v + kTubeBias)) - softClip((float) kTubeBias); }
inline double tubeIntegral(double v) { return softClipIntegral(v + kTubeBias) - softClip((float) kTubeBias) * v; }

/** A plain hard clip at ±1, as mastering clippers and drum busses use it: flat tops, no softening. */
inline double clipper(double v) { return std::clamp(v, -1.0, 1.0); }
inline double clipperIntegral(double v) { return std::abs(v) <= 1.0 ? 0.5 * v * v : std::abs(v) - 0.5; }

/**
 * A sine wavefolder, as in a West Coast synth: past ±1 the wave turns back on itself instead of flattening, so
 * turning DIST up does not square the sound but adds folds, and with them ever higher partials that move with the
 * level of the note.
 */
inline double folder(double v) { return std::sin(v); }
inline double folderIntegral(double v) { return -std::cos(v); }

/**
 * A rectifier, as in octave fuzzes: the lower half is flipped up at half its size before a soft clip. A full flip
 * would leave only the octave; half keeps the root about as strong as the octave, and the two halves clipping
 * differently add the gated spit of a fuzz on decaying notes.
 */
constexpr double kFlip = -0.5;
inline double rectifier(double v) { return softClip((float) (v >= 0.0 ? v : kFlip * v)); }
inline double rectifierIntegral(double v)
{
    return v >= 0.0 ? softClipIntegral(v) : softClipIntegral(kFlip * v) / kFlip;
}
} // namespace curves

/** One of the curves for one channel, at the oversampled rate. */
class Shaper
{
public:
    /** `maxDb` is the gain at full DIST, `makeUp` the output's scale. */
    Shaper(double (*curve)(double), double (*integral)(double), float maxDb, float makeUp)
        : clip(curve, integral), maxGainDb(maxDb), outputGain(makeUp)
    {
    }

    void prepare(float rate, const Settings& settings)
    {
        gain.prepare(rate, gainFor(settings));
        reset();
    }
    void reset() { clip.reset(); }
    void update(const Settings& settings) { gain.set(gainFor(settings)); }
    float process(float x) { return outputGain * clip(x * gain.next()); }

private:
    float gainFor(const Settings& s) const
    {
        return std::pow(10.0f, (s.inputDb + std::clamp(s.distortion, 0.0f, 1.0f) * maxGainDb) / 20.0f);
    }

    AntiAliased<double (*)(double), double (*)(double)> clip;
    float maxGainDb, outputGain;
    Smoothed gain;
};

/**
 * Every mode's drive for one channel, at the oversampled rate. Only the chosen one runs; on a change the old and the
 * new run side by side for 20 ms and cross-fade, the new one from a reset state, so switching while a note sounds
 * does not click.
 */
class Distortion
{
public:
    void prepare(float sampleRate, const Settings& settings)
    {
        pedal.prepare(sampleRate, settings);
        for (auto* shaper : shapers())
            shaper->prepare(sampleRate, settings);
        current = previous = settings.mode;
        fade = 1.0f;
        fadeStep = 1.0f / (0.02f * sampleRate);
    }

    /** Takes new knob values; call once per block. */
    void update(const Settings& settings)
    {
        pedal.update(settings);
        for (auto* shaper : shapers())
            shaper->update(settings);
        if (settings.mode != current)
        {
            previous = current;
            current = settings.mode;
            fade = 0.0f;
            reset(current);
        }
    }

    float process(float x)
    {
        if (fade >= 1.0f)
            return render(current, x);
        fade = std::min(fade + fadeStep, 1.0f);
        const float before = render(previous, x);
        return before + fade * (render(current, x) - before);
    }

private:
    std::array<Shaper*, kModes - 1> shapers() { return { &tube, &clipper, &folder, &rectifier }; }

    void reset(Mode mode)
    {
        if (mode == Mode::pedal)
            pedal.reset();
        else
            shapers()[(std::size_t) mode - 1]->reset();
    }

    float render(Mode mode, float x)
    {
        switch (mode)
        {
            case Mode::tube: return tube.process(x);
            case Mode::clipper: return clipper.process(x);
            case Mode::folder: return folder.process(x);
            case Mode::rectifier: return rectifier.process(x);
            case Mode::pedal:
            default: return pedal.process(x);
        }
    }

    Drive pedal;
    // Gains up to 30 dB; 24 for the clipper, whose tops are flat long before, and for the rectifier, whose kink sits
    // at the zero crossing, where the wave is steepest, so more gain folds back more. The output scales put a driven
    // note within a few dB of the pedal's loudness: the squared-off waves are denser than the pedal's scooped one and
    // get less, the rectifier's lopsided one more.
    Shaper tube { &curves::tube, &curves::tubeIntegral, 30.0f, 0.38f };
    Shaper clipper { &curves::clipper, &curves::clipperIntegral, 24.0f, 0.38f };
    Shaper folder { &curves::folder, &curves::folderIntegral, 30.0f, 0.5f };
    Shaper rectifier { &curves::rectifier, &curves::rectifierIntegral, 24.0f, 0.8f };
    Mode current = Mode::pedal, previous = Mode::pedal;
    float fade = 1.0f, fadeStep = 1.0f;
};
} // namespace tonwerkdistortion
