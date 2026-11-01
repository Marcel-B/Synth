#pragma once

#include "../dsp/PhysicalVoice.h"
#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** What the Physical firmware keeps across power cycles: exciter, resonator and the hidden layers. */
struct PhysicalStored
{
    uint32_t version = 1;
    phys::Exciter exciter = phys::Exciter::pluck;
    phys::Resonator resonator = phys::Resonator::string;
    float layer[kLayers][kPots] {};
};

/**
 * The second firmware for the same panel, Tonwerk Physical: the WARP button becomes ERREGER (exciter: tap for pluck,
 * strike, bow, breath; hold for its envelope, noise, dynamics and the number of voices), the FILTER button RESONATOR
 * (tap for string, tubes, bar, bell, skin, bowl; hold for the filter, the body and the damper). The pots of the
 * normal layer, in panel order: TUNE, HÄRTE, POSITION / FINE, KRAFT, DÄMPFUNG / DECAY, POS CV, INHARMONIE / DMP CV;
 * the jacks TABLE, WARP and CUTOFF take HÄRTE, KRAFT and DÄMPFUNG. Plain C++ like Module, for the host test.
 */
class PhysicalModule
{
public:
    /** The NVS key: another than the wavetable's, so a module flashed from one to the other starts clean. */
    static constexpr const char* kStoreKey = "physical";
    using Stored = PhysicalStored;
    using Params = phys::PlayParams;

    // Ranges of the pots on an exponential scale, in seconds.
    static constexpr float kDecayLow = 0.02f, kDecayHigh = 20.0f;
    static constexpr float kAttackLow = 0.001f, kAttackHigh = 2.0f;
    static constexpr float kEnvLow = 0.005f, kEnvHigh = 5.0f;
    static constexpr float kReleaseLow = 0.005f, kReleaseHigh = 60.0f;

    PhysicalModule()
    {
        const phys::Settings d;
        auto& e = stored.layer[oscLayer];
        e[tune] = exponentialPot(d.attack, kAttackLow, kAttackHigh);
        e[fine] = exponentialPot(d.envDecay, kEnvLow, kEnvHigh);
        e[table] = d.sustain;
        e[position] = exponentialPot(d.envRelease, kEnvLow, kEnvHigh);
        e[warp] = d.noise;
        e[cutoff] = d.dynamics;
        e[resonance] = 0.5f;
        e[decay] = 1.0f / 3.0f;  // two voices
        e[positionAmount] = 0.5f;
        e[cutoffAmount] = 0.5f;
        auto& r = stored.layer[filterLayer];
        r[tune] = 0.7f;          // cutoff about 2.6 kHz once a filter is on
        r[fine] = d.resonance;
        r[table] = 0.0f;         // filter off
        r[position] = 0.0f;      // no keytracking
        r[warp] = 0.0f;          // no body
        r[cutoff] = d.bodyMix;
        r[resonance] = 0.5f;
        r[decay] = 1.0f;         // the damper never comes down: a struck note rings out after its gate
        r[positionAmount] = 0.5f;
        r[cutoffAmount] = 0.5f;
    }

    Stored stored;
    Calibration calibration;
    bool dirty = false;

    void update(const PanelInputs& in, int ms, Params& out)
    {
        const auto result = layers.update(in, ms, panel, stored.layer);
        if (result.warpTapped)
            stored.exciter = (phys::Exciter) (((int) stored.exciter + 1) % (int) phys::Exciter::count);
        if (result.filterTapped)
            stored.resonator = (phys::Resonator) (((int) stored.resonator + 1) % (int) phys::Resonator::count);
        if (result.warpTapped || result.filterTapped || result.hiddenChanged)
            dirty = true;
        makeParams(in, out);
    }

    float panelValue(int pot) const { return panel[pot]; }

    /** LED 1: the exciter, LED 2: the resonator, both glowing with the newest note. */
    Rgb led(int index, float envelope) const
    {
        const Rgb c = index == 0 ? exciterColour(stored.exciter) : resonatorColour(stored.resonator);
        const float glow = 0.25f + 0.75f * std::clamp(envelope, 0.0f, 1.0f);
        return { (uint8_t) (c.r * glow), (uint8_t) (c.g * glow), (uint8_t) (c.b * glow) };
    }

    static Rgb exciterColour(phys::Exciter e)
    {
        switch (e)
        {
            case phys::Exciter::strike: return { 0, 240, 255 };
            case phys::Exciter::bow: return { 255, 42, 160 };
            case phys::Exciter::blow: return { 0, 255, 0 };
            default: return { 252, 238, 10 };
        }
    }

    static Rgb resonatorColour(phys::Resonator r)
    {
        switch (r)
        {
            case phys::Resonator::closedTube: return { 255, 110, 0 };
            case phys::Resonator::openTube: return { 0, 255, 0 };
            case phys::Resonator::bar: return { 0, 240, 255 };
            case phys::Resonator::bell: return { 0, 80, 255 };
            case phys::Resonator::membrane: return { 255, 0, 0 };
            case phys::Resonator::bowl: return { 255, 42, 160 };
            default: return { 252, 238, 10 };
        }
    }

private:
    void makeParams(const PanelInputs& in, Params& out)
    {
        const auto& e = stored.layer[oscLayer];
        const auto& r = stored.layer[filterLayer];
        auto& s = out.settings;

        // Pitch as on the wavetable firmware: 0 V is C2; TUNE ±2 octaves in semitones, FINE ±50 cents.
        const float tuneSemis = (float) tuneStepper.update(panel[tune], 49) - 24.0f;
        out.note = 36.0f + 12.0f * in.cv[pitchCv] + tuneSemis + bipolar(panel[fine]) * 0.5f;

        s.exciter = stored.exciter;
        s.resonator = stored.resonator;
        // The jacks with ±5 V sweep their knob's whole range; POS and DÄMPFUNG through their attenuverters.
        s.hardness = std::clamp(panel[table] + in.cv[tableCv] / 5.0f, 0.0f, 1.0f);
        const float place = std::clamp(panel[position] + bipolar(panel[positionAmount]) * in.cv[positionCv] / 5.0f,
                                       0.0f, 1.0f);
        s.position = 0.02f + 0.48f * place;
        // KRAFT: a bow's force or the breath; for a pluck or strike how hard it is, read when it starts.
        const float force = std::clamp(panel[warp] + in.cv[warpCv] / 5.0f, 0.0f, 1.0f);
        s.pressure = force;
        out.velocity = force;
        s.damping = std::clamp(panel[cutoff] + bipolar(panel[cutoffAmount]) * in.cv[cutoffCv] / 5.0f, 0.0f, 1.0f);
        s.inharmonicity = panel[resonance];
        s.decay = exponential(panel[decay], kDecayLow, kDecayHigh);

        // Hidden layer of ERREGER: the bow's or breath's envelope, noise, dynamics, voices.
        s.attack = exponential(e[tune], kAttackLow, kAttackHigh);
        s.envDecay = exponential(e[fine], kEnvLow, kEnvHigh);
        s.sustain = e[table];
        s.envRelease = exponential(e[position], kEnvLow, kEnvHigh);
        s.noise = e[warp];
        s.dynamics = e[cutoff];
        out.voices = voicesStepper.update(e[decay], phys::Player::kMaxVoices) + 1;

        // Hidden layer of RESONATOR: filter, body, damper.
        s.cutoff = std::clamp(20.0f * std::exp2(r[tune] * 10.0f), 20.0f, 20000.0f);
        s.resonance = r[fine];
        const int filter = filterStepper.update(r[table], (int) phys::FilterMode::count + 1);
        s.filterOn = filter > 0;
        s.filterMode = (phys::FilterMode) std::max(0, filter - 1);
        s.keytrack = r[position];
        s.body = (phys::Body) bodyStepper.update(r[warp], (int) phys::Body::count);
        s.bodyMix = r[cutoff];
        s.release = exponential(r[decay], kReleaseLow, kReleaseHigh);

        out.gate = in.gate;
    }

    float panel[kPots] {};
    Layers layers;
    Stepper tuneStepper, voicesStepper, filterStepper, bodyStepper;
};
} // namespace twm
