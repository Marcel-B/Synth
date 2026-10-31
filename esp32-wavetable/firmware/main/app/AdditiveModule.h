#pragma once

#include "../dsp/AdditiveVoice.h"
#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** What the Additiv firmware keeps across power cycles: both spectra and the hidden layers. */
struct AdditiveStored
{
    uint32_t version = 1;
    add::Spectrum spectrumA = add::Spectrum::saw;
    add::Spectrum spectrumB = add::Spectrum::square;
    float layer[kLayers][kPots] {};
};

/**
 * The fifth firmware for the same panel, Tonwerk Additiv: the WARP button becomes KLANG A (tap: spectrum A, the
 * next of twelve; hold: the envelope, odd/even, beating, width and the partials' own envelope), the FILTER button
 * KLANG B (tap: spectrum B; hold: filter, comb, formant shift, phases). The pots of the normal layer, in panel order:
 * TUNE, MORPH, OBERTÖNE / FINE, NEIGUNG, SPREIZ. / ABKLING., OBT CV, VOKAL / SPR CV; the jacks TABLE, POS, WARP and
 * CUTOFF take the morph, the partial count, the tilt (NEIG.) and the stretch (SPREIZ.). Plain C++ like Module, for
 * the host test.
 */
class AdditiveModule
{
public:
    static constexpr const char* kStoreKey = "additiv";
    using Stored = AdditiveStored;
    using Params = add::PlayParams;

    static constexpr float kAttackLow = 0.001f, kTimeLow = 0.005f, kTimeHigh = 10.0f;
    static constexpr float kCutoffLow = 20.0f, kCutoffHigh = 20000.0f;
    static constexpr float kDecayLow = 0.05f, kDecayHigh = 20.0f;
    static constexpr float kBloomLow = 0.01f, kBloomHigh = 5.0f;
    /** ABKLINGEN turned fully right: the partials never fall, the spectral envelope off unless EINSCHWINGEN is set. */
    static constexpr float kEndless = 0.98f;
    /** EINSCHWINGEN this far left is 0: every partial in at once. */
    static constexpr float kBloomOff = 0.02f;
    static constexpr float kMaxTilt = 12.0f, kMaxBeating = 50.0f;
    static constexpr float kCombLow = 2.0f, kCombHigh = 16.0f;
    /** VOKAL: the first part of the turn fades the formants in on A, the rest moves through E, I, O to U. */
    static constexpr float kVowelFade = 0.15f;
    /** Filter choices: off, then the plugin's four modes. */
    static constexpr int kFilters = (int) add::FilterMode::count + 1;

    AdditiveModule()
    {
        const add::Settings d;
        auto& a = stored.layer[oscLayer];
        a[tune] = exponentialPot(d.env.attack, kAttackLow, kTimeHigh);
        a[fine] = exponentialPot(d.env.decay, kTimeLow, kTimeHigh);
        a[table] = d.env.sustain;
        a[position] = exponentialPot(d.env.release, kTimeLow, kTimeHigh);
        a[warp] = 0.5f;     // odd and even alike
        a[cutoff] = 0.0f;   // no beating
        a[resonance] = d.width;
        a[decay] = 0.0f;    // every partial in at once
        a[positionAmount] = d.spectralSustain;
        a[cutoffAmount] = d.spectralDamping;
        auto& b = stored.layer[filterLayer];
        b[tune] = 0.0f;     // filter off
        b[fine] = 1.0f;     // cutoff 20 kHz
        b[table] = d.resonance;
        b[position] = d.keytrack;
        b[warp] = d.combDepth;
        b[cutoff] = (d.combSpacing - kCombLow) / (kCombHigh - kCombLow);
        b[resonance] = d.combShift;
        b[decay] = 0.5f;    // formants where the plugin has them
        b[positionAmount] = 1.0f; // random phases, as the plugin's default
        b[cutoffAmount] = 0.5f;
    }

    Stored stored;
    Calibration calibration;
    bool dirty = false;

    void update(const PanelInputs& in, int ms, Params& out)
    {
        const auto result = layers.update(in, ms, panel, stored.layer);
        if (result.warpTapped)
            stored.spectrumA = next(stored.spectrumA);
        if (result.filterTapped)
            stored.spectrumB = next(stored.spectrumB);
        if (result.warpTapped || result.filterTapped || result.hiddenChanged)
            dirty = true;
        makeParams(in, out);
    }

    float panelValue(int pot) const { return panel[pot]; }

    /** LED 1: spectrum A, LED 2: spectrum B, both glowing with the note. */
    Rgb led(int index, float envelope) const
    {
        const Rgb c = spectrumColour(index == 0 ? stored.spectrumA : stored.spectrumB);
        const float glow = 0.25f + 0.75f * std::clamp(envelope, 0.0f, 1.0f);
        return { (uint8_t) (c.r * glow), (uint8_t) (c.g * glow), (uint8_t) (c.b * glow) };
    }

    static Rgb spectrumColour(add::Spectrum s)
    {
        // Sinus, Säge, Rechteck, Dreieck, Puls, Orgel, Hohl, Hell, Oktaven, Glas, Primzahlen, Zufall.
        static constexpr Rgb colours[(int) add::Spectrum::count] = {
            { 255, 255, 255 }, { 252, 238, 10 }, { 0, 240, 255 }, { 40, 255, 80 },  { 255, 42, 160 }, { 255, 110, 0 },
            { 0, 80, 255 },    { 255, 200, 120 }, { 160, 0, 255 }, { 120, 255, 220 }, { 255, 0, 60 },   { 120, 120, 120 },
        };
        return colours[std::clamp((int) s, 0, (int) add::Spectrum::count - 1)];
    }

private:
    static add::Spectrum next(add::Spectrum s) { return (add::Spectrum) (((int) s + 1) % (int) add::Spectrum::count); }

    void makeParams(const PanelInputs& in, Params& out)
    {
        const auto& a = stored.layer[oscLayer];
        const auto& b = stored.layer[filterLayer];
        auto& s = out.settings;

        // Pitch as on the wavetable firmware: 0 V is C2; TUNE ±2 octaves in semitones, FINE ±50 cents.
        const float tuneSemis = (float) tuneStepper.update(panel[tune], 49) - 24.0f;
        out.note = 36.0f + 12.0f * in.cv[pitchCv] + tuneSemis + bipolar(panel[fine]) * 0.5f;
        out.gate = in.gate;

        s.spectrumA = stored.spectrumA;
        s.spectrumB = stored.spectrumB;
        // MORPH and NEIGUNG with their jacks, 5 V over the whole travel (NEIGUNG's jack goes on to ±24 dB/oct).
        s.morph = std::clamp(panel[table] + in.cv[tableCv] / 5.0f, 0.0f, 1.0f);
        s.tilt = kMaxTilt * std::clamp(bipolar(panel[warp]) + in.cv[warpCv] / 2.5f, -2.0f, 2.0f);
        // OBERTÖNE and SPREIZUNG through their attenuverters; the count on an exponential scale, so the first few
        // partials have room on the knob.
        const float count = std::clamp(panel[position] + bipolar(panel[positionAmount]) * in.cv[positionCv] / 5.0f, 0.0f, 1.0f);
        s.partials = exponential(count, 1.0f, (float) add::kPartials);
        s.stretch = std::clamp(bipolar(panel[cutoff]) + bipolar(panel[cutoffAmount]) * in.cv[cutoffCv] / 5.0f, -1.0f, 1.0f);
        const float vowel = panel[resonance];
        s.formantDepth = std::clamp(vowel / kVowelFade, 0.0f, 1.0f);
        s.vowel = (float) (add::kVowels - 1) * std::clamp((vowel - kVowelFade) / (1.0f - kVowelFade), 0.0f, 1.0f);

        // ABKLINGEN: how long the fundamental takes to fall 60 dB, the higher partials sooner by DÄMPFUNG.
        const bool endless = panel[decay] >= kEndless;
        s.bloom = a[decay] < kBloomOff ? 0.0f : exponential((a[decay] - kBloomOff) / (1.0f - kBloomOff), kBloomLow, kBloomHigh);
        s.spectralDecay = endless ? 1.0e9f : exponential(panel[decay] / kEndless, kDecayLow, kDecayHigh);
        s.spectralOn = ! endless || s.bloom > 0.0f;
        s.spectralSustain = a[positionAmount];
        s.spectralDamping = a[cutoffAmount];

        s.env = { exponential(a[tune], kAttackLow, kTimeHigh), exponential(a[fine], kTimeLow, kTimeHigh), a[table],
                  exponential(a[position], kTimeLow, kTimeHigh) };
        s.oddEven = bipolar(a[warp]);
        s.beating = kMaxBeating * a[cutoff] * a[cutoff];
        s.width = a[resonance];

        const int filter = filterStepper.update(b[tune], kFilters);
        s.filterOn = filter > 0;
        s.filterMode = (add::FilterMode) std::max(0, filter - 1);
        s.cutoff = exponential(b[fine], kCutoffLow, kCutoffHigh);
        s.resonance = b[table];
        s.keytrack = b[position];
        s.combDepth = b[warp];
        s.combSpacing = kCombLow + (kCombHigh - kCombLow) * b[cutoff];
        s.combShift = b[resonance];
        s.formantShift = 12.0f * bipolar(b[decay]);
        s.phases = (add::PhaseMode) phaseStepper.update(b[positionAmount], 2);
    }

    float panel[kPots] {};
    Layers layers;
    Stepper tuneStepper, filterStepper, phaseStepper;
};
} // namespace twm
