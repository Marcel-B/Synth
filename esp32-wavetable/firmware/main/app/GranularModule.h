#pragma once

#include "../dsp/GrainVoice.h"
#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** What the Granular firmware keeps across power cycles: the source, the window and the hidden layers. */
struct GranularStored
{
    uint32_t version = 1;
    int32_t source = 0;
    grain::Window window = grain::Window::bell;
    float layer[kLayers][kPots] {};
};

/**
 * The fourth firmware for the same panel, Tonwerk Granular: the WARP button becomes QUELLE (tap: the next of the nine
 * sources; hold: the envelope, randomness, reverse, width, pitch grid), the FILTER button FENSTER (tap: the grain's
 * window; hold: filter and LFO). The pots of the normal layer, in panel order: TUNE, GRÖSSE, POSITION / FINE, DICHTE,
 * STREUUNG / SCAN, POS CV, TONSTREU / STR CV; the jacks TABLE, POS, WARP and CUTOFF take the grain size, the position,
 * the density and the spray. Plain C++ like Module, for the host test.
 */
class GranularModule
{
public:
    static constexpr const char* kStoreKey = "granular";
    using Stored = GranularStored;
    using Params = grain::PlayParams;

    static constexpr float kSizeLow = 0.005f, kSizeHigh = 1.0f;
    static constexpr float kDensityLow = 1.0f, kDensityHigh = 200.0f;
    static constexpr float kAttackLow = 0.001f, kTimeLow = 0.005f, kTimeHigh = 10.0f;
    static constexpr float kCutoffLow = 20.0f, kCutoffHigh = 20000.0f;
    static constexpr float kLfoLow = 0.05f, kLfoHigh = 20.0f;
    static constexpr float kMaxScatter = 2400.0f;
    /** SCAN stands still within this much of the middle, so the position can be frozen by hand. */
    static constexpr float kScanDeadBand = 0.05f;
    /** Filter choices: off, then the plugin's four modes. */
    static constexpr int kFilters = (int) grain::FilterMode::count + 1;
    /** LFO targets: none, then the plugin's position to cutoff. */
    static constexpr grain::Target kLfoTargets[] = { grain::Target::none,    grain::Target::position, grain::Target::spray,
                                                     grain::Target::size,    grain::Target::density,  grain::Target::pitch,
                                                     grain::Target::cutoff };
    static constexpr int kTargets = (int) (sizeof kLfoTargets / sizeof kLfoTargets[0]);

    GranularModule()
    {
        const grain::Settings d;
        auto& a = stored.layer[oscLayer];
        a[tune] = exponentialPot(d.env.attack, kAttackLow, kTimeHigh);
        a[fine] = exponentialPot(d.env.decay, kTimeLow, kTimeHigh);
        a[table] = d.env.sustain;
        a[position] = exponentialPot(d.env.release, kTimeLow, kTimeHigh);
        a[warp] = d.jitter;
        a[cutoff] = d.reverse;
        a[resonance] = d.width;
        a[decay] = 0.0f; // free pitch scatter
        a[positionAmount] = 0.5f;
        a[cutoffAmount] = 0.5f;
        auto& b = stored.layer[filterLayer];
        b[tune] = 0.0f; // filter off
        b[fine] = 1.0f; // cutoff 20 kHz
        b[table] = d.resonance;
        b[position] = d.keytrack;
        b[warp] = exponentialPot(d.lfoRate, kLfoLow, kLfoHigh);
        b[cutoff] = 0.0f;    // LFO to nothing
        b[resonance] = 0.5f; // LFO amount 0
        b[decay] = 0.0f;     // sine
        b[positionAmount] = 0.5f;
        b[cutoffAmount] = 0.5f;
    }

    Stored stored;
    Calibration calibration;
    bool dirty = false;

    void update(const PanelInputs& in, int ms, Params& out)
    {
        const auto result = layers.update(in, ms, panel, stored.layer);
        if (result.warpTapped)
            stored.source = (stored.source + 1) % grain::Sources::kCount;
        if (result.filterTapped)
            stored.window = (grain::Window) (((int) stored.window + 1) % (int) grain::Window::count);
        if (result.warpTapped || result.filterTapped || result.hiddenChanged)
            dirty = true;
        makeParams(in, out);
    }

    float panelValue(int pot) const { return panel[pot]; }

    /** LED 1: the source, LED 2: the window, both glowing with the envelope. */
    Rgb led(int index, float envelope) const
    {
        const Rgb c = index == 0 ? sourceColour(stored.source) : windowColour(stored.window);
        const float glow = 0.25f + 0.75f * std::clamp(envelope, 0.0f, 1.0f);
        return { (uint8_t) (c.r * glow), (uint8_t) (c.g * glow), (uint8_t) (c.b * glow) };
    }

    static Rgb sourceColour(int source)
    {
        // Chor, Glas, Streicher, Zupfen, Orgel, Wind, Metall, Sinus, Rhythmus.
        static constexpr Rgb colours[grain::Sources::kCount] = {
            { 252, 238, 10 }, { 0, 240, 255 }, { 255, 42, 160 }, { 0, 255, 0 },   { 255, 110, 0 },
            { 0, 80, 255 },   { 160, 0, 255 }, { 255, 255, 255 }, { 255, 0, 0 },
        };
        return colours[std::clamp(source, 0, grain::Sources::kCount - 1)];
    }

    static Rgb windowColour(grain::Window w)
    {
        switch (w)
        {
            case grain::Window::flat: return { 0, 240, 255 };
            case grain::Window::struck: return { 255, 42, 160 };
            default: return { 252, 238, 10 };
        }
    }

private:
    void makeParams(const PanelInputs& in, Params& out)
    {
        const auto& a = stored.layer[oscLayer];
        const auto& b = stored.layer[filterLayer];
        auto& s = out.settings;

        // 0 V plays the sources at their own pitch (C4); TUNE ±2 octaves in semitones, FINE ±50 cents.
        const float tuneSemis = (float) tuneStepper.update(panel[tune], 49) - 24.0f;
        out.note = 60.0f + 12.0f * in.cv[pitchCv] + tuneSemis;
        out.gate = in.gate;
        s.fine = bipolar(panel[fine]) * 50.0f;

        s.source = stored.source;
        s.window = stored.window;
        // GRÖSSE and DICHTE with their jacks, 5 V over the whole travel.
        s.size = exponential(std::clamp(panel[table] + in.cv[tableCv] / 5.0f, 0.0f, 1.0f), kSizeLow, kSizeHigh);
        s.density = exponential(std::clamp(panel[warp] + in.cv[warpCv] / 5.0f, 0.0f, 1.0f), kDensityLow, kDensityHigh);
        // POSITION and STREUUNG through their attenuators; the position wraps round the source as in the plugin.
        s.position = panel[position] + bipolar(panel[positionAmount]) * in.cv[positionCv] / 5.0f;
        s.spray = std::clamp(panel[cutoff] + bipolar(panel[cutoffAmount]) * in.cv[cutoffCv] / 5.0f, 0.0f, 1.0f);
        // SCAN: the middle holds the position still, the ends run through the source at twice its speed.
        const float scan = bipolar(panel[decay]);
        s.scan = std::abs(scan) < kScanDeadBand
                     ? 0.0f
                     : std::copysign((std::abs(scan) - kScanDeadBand) / (1.0f - kScanDeadBand) * 2.0f, scan);
        // TONSTREU: cents each grain may lie off the note, most of the turn for the first few semitones.
        const float p = panel[resonance];
        s.scatter = kMaxScatter * p * p * p;

        s.env = { exponential(a[tune], kAttackLow, kTimeHigh), exponential(a[fine], kTimeLow, kTimeHigh), a[table],
                  exponential(a[position], kTimeLow, kTimeHigh) };
        s.jitter = a[warp];
        s.reverse = a[cutoff];
        s.width = a[resonance];
        s.scatterMode = (grain::Scatter) scatterStepper.update(a[decay], (int) grain::Scatter::count);

        const int filter = filterStepper.update(b[tune], kFilters);
        s.filterOn = filter > 0;
        s.filterMode = (grain::FilterMode) std::max(0, filter - 1);
        s.cutoff = exponential(b[fine], kCutoffLow, kCutoffHigh);
        s.resonance = b[table];
        s.keytrack = b[position];
        s.lfoRate = exponential(b[warp], kLfoLow, kLfoHigh);
        s.lfoTarget = kLfoTargets[targetStepper.update(b[cutoff], kTargets)];
        s.lfoAmount = bipolar(b[resonance]);
        s.lfoShape = (grain::LfoShape) shapeStepper.update(b[decay], (int) grain::LfoShape::count);
    }

    float panel[kPots] {};
    Layers layers;
    Stepper tuneStepper, scatterStepper, filterStepper, targetStepper, shapeStepper;
};
} // namespace twm
