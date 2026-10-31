#pragma once

#include "../dsp/PdVoice.h"
#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** What the PD firmware keeps across power cycles: both lines' waves and the hidden layers. */
struct PdStored
{
    uint32_t version = 1;
    pd::Wave wave1 = pd::Wave::saw;
    pd::Wave wave2 = pd::Wave::square;
    float layer[kLayers][kPots] {};
};

/**
 * The third firmware for the same panel, Tonwerk PD: the WARP button becomes WELLE 1 (tap: line 1's wave; hold: the
 * envelopes, line 1's second wave, key follow), the FILTER button WELLE 2 (tap: line 2's wave; hold: its detune and
 * second wave, which lines sound, ring or noise, width, chorus). The pots of the normal layer, in panel order: TUNE,
 * LINIE 2, HÜLLK. / FINE, MIX, DCW / DECAY, POS CV, CHORUS / DCW CV; the jacks TABLE, POS, WARP and CUTOFF take line
 * 2's interval (1 V/Oct), the envelope's depth, the mix and the DCW. Plain C++ like Module, for the host test.
 */
class PdModule
{
public:
    /** The NVS key: another than the other firmwares', so a module flashed from one to the other starts clean. */
    static constexpr const char* kStoreKey = "pd";
    using Stored = PdStored;
    using Params = pd::PlayParams;

    static constexpr float kAttackLow = 0.001f, kAttackHigh = 2.0f;
    static constexpr float kTimeLow = 0.005f, kTimeHigh = 5.0f;
    static constexpr float kChorusLow = 0.1f, kChorusHigh = 5.0f;
    /** Line 2's interval knob: unison to two octaves up, in semitones. */
    static constexpr int kIntervals = 25;
    /** Second waves: off (the first every cycle), then the eight waves. */
    static constexpr int kSecondWaves = (int) pd::Wave::count + 1;

    PdModule()
    {
        const pd::LineSettings d;
        auto& a = stored.layer[oscLayer];
        a[tune] = exponentialPot(d.dcw.attack, kAttackLow, kAttackHigh);
        a[fine] = d.dcw.sustain;
        a[table] = 0.0f;             // no second wave
        a[position] = d.keyFollow;
        a[warp] = exponentialPot(d.amp.attack, kAttackLow, kAttackHigh);
        a[cutoff] = d.amp.sustain;
        a[resonance] = exponentialPot(d.amp.release, kTimeLow, kTimeHigh);
        a[decay] = exponentialPot(d.amp.decay, kTimeLow, kTimeHigh);
        a[positionAmount] = 0.5f;
        a[cutoffAmount] = 0.5f;
        auto& b = stored.layer[filterLayer];
        b[tune] = 0.57f;             // line 2 seven cents up, as in the plugin
        b[fine] = 1.0f;              // both lines
        b[table] = 0.0f;
        b[position] = 0.0f;          // no ring, no noise
        b[warp] = 0.5f;              // width
        b[cutoff] = exponentialPot(0.6f, kChorusLow, kChorusHigh);
        b[resonance] = 0.5f;         // chorus depth
        b[decay] = exponentialPot(d.dcw.release, kTimeLow, kTimeHigh);
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
            stored.wave1 = next(stored.wave1);
        if (result.filterTapped)
            stored.wave2 = next(stored.wave2);
        if (result.warpTapped || result.filterTapped || result.hiddenChanged)
            dirty = true;
        makeParams(in, out);
    }

    float panelValue(int pot) const { return panel[pot]; }

    /** LED 1: line 1's wave, LED 2: line 2's, both glowing with the note. */
    Rgb led(int index, float envelope) const
    {
        const Rgb c = waveColour(index == 0 ? stored.wave1 : stored.wave2);
        const float glow = 0.25f + 0.75f * std::clamp(envelope, 0.0f, 1.0f);
        return { (uint8_t) (c.r * glow), (uint8_t) (c.g * glow), (uint8_t) (c.b * glow) };
    }

    static Rgb waveColour(pd::Wave w)
    {
        switch (w)
        {
            case pd::Wave::square: return { 0, 240, 255 };
            case pd::Wave::pulse: return { 255, 42, 160 };
            case pd::Wave::doubleSine: return { 40, 255, 80 };
            case pd::Wave::sawPulse: return { 255, 110, 0 };
            case pd::Wave::resonanceSaw: return { 255, 0, 60 };
            case pd::Wave::resonanceTriangle: return { 0, 80, 255 };
            case pd::Wave::resonanceTrapezoid: return { 160, 0, 255 };
            default: return { 252, 238, 10 };
        }
    }

private:
    static pd::Wave next(pd::Wave w) { return (pd::Wave) (((int) w + 1) % (int) pd::Wave::count); }

    void makeParams(const PanelInputs& in, Params& out)
    {
        const auto& a = stored.layer[oscLayer];
        const auto& b = stored.layer[filterLayer];
        auto& s = out.settings;

        // Pitch as on the wavetable firmware: 0 V is C2; TUNE ±2 octaves in semitones, FINE ±50 cents.
        const float tuneSemis = (float) tuneStepper.update(panel[tune], 49) - 24.0f;
        out.note = 36.0f + 12.0f * in.cv[pitchCv] + tuneSemis + bipolar(panel[fine]) * 0.5f;
        out.gate = in.gate;

        s.lines = (pd::LineSelect) linesStepper.update(b[fine], (int) pd::LineSelect::count);
        s.modulation = (pd::Modulation) modulationStepper.update(b[position], (int) pd::Modulation::count);
        // LINIE 2: line 2's interval in semitones, its jack 1 V/Oct on top, and the fine detune of the hidden layer.
        const float interval = (float) intervalStepper.update(panel[table], kIntervals);
        // HÜLLK.: how far the DCW envelope bends both lines; DCW: the bend without it. Both with their jacks.
        const float depth = std::clamp(panel[position] + bipolar(panel[positionAmount]) * in.cv[positionCv] / 5.0f,
                                       0.0f, 1.0f);
        s.bend = std::clamp(panel[cutoff] + bipolar(panel[cutoffAmount]) * in.cv[cutoffCv] / 5.0f, -1.0f, 1.0f);
        s.balance = std::clamp(bipolar(panel[warp]) + in.cv[warpCv] / 5.0f, -1.0f, 1.0f);
        s.chorusMix = panel[resonance];
        s.width = b[warp];
        s.chorusRate = exponential(b[cutoff], kChorusLow, kChorusHigh);
        s.chorusDepth = b[resonance];

        const pd::Envelope dcw { exponential(a[tune], kAttackLow, kAttackHigh), exponential(panel[decay], kTimeLow, kTimeHigh),
                                 a[fine], exponential(b[decay], kTimeLow, kTimeHigh) };
        const pd::Envelope amp { exponential(a[warp], kAttackLow, kAttackHigh), exponential(a[decay], kTimeLow, kTimeHigh),
                                 a[cutoff], exponential(a[resonance], kTimeLow, kTimeHigh) };
        const int second[2] = { secondStepper[0].update(a[table], kSecondWaves),
                                secondStepper[1].update(b[table], kSecondWaves) };
        for (int k = 0; k < pd::kLines; ++k)
        {
            auto& line = s.line[(std::size_t) k];
            line.first = k == 0 ? stored.wave1 : stored.wave2;
            line.alternate = second[k] > 0;
            line.second = (pd::Wave) std::max(0, second[k] - 1);
            line.depth = depth;
            line.keyFollow = a[position];
            line.level = 0.8f;
            line.dcw = dcw;
            line.amp = amp;
        }
        s.line[1].semitones = interval + 12.0f * in.cv[tableCv] + bipolar(b[tune]) * 0.5f;
    }

    float panel[kPots] {};
    Layers layers;
    Stepper tuneStepper, intervalStepper, linesStepper, modulationStepper;
    Stepper secondStepper[2];
};
} // namespace twm
