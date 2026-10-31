#pragma once

#include "../dsp/Voice.h"
#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** What is kept across power cycles: the modes and the hidden layers' settings. */
struct Stored
{
    uint32_t version = 1;
    Warp warpMode = Warp::off;
    FilterMode filterMode = FilterMode::low24;
    float layer[kLayers][kPots] {};
};

/**
 * The module's logic without hardware: turns the panel's pots, buttons and CVs into the voice's parameters, keeps
 * the hidden layers and the modes, and says what the LEDs show. Runs on the control task every millisecond; plain
 * C++ so the host test checks it.
 */
class Module
{
public:
    /** The NVS key the modes and hidden layers are saved under. */
    static constexpr const char* kStoreKey = "stored";
    using Stored = twm::Stored;
    using Params = VoiceParams;

    Module()
    {
        // Defaults of the hidden layers, as pot positions.
        auto& o = stored.layer[oscLayer];
        o[tune] = 0.5f;          // oscillator B: no transposition
        o[fine] = 0.5f;          // and no fine offset
        o[table] = 0.0f;         // oscillator B off
        o[position] = 0.0f;
        o[warp] = 0.0f;          // one unison voice
        o[cutoff] = 0.2f;        // 20 cents of unison detune
        o[resonance] = 0.0f;     // sub off
        o[decay] = 0.0f;         // sub one octave down, sine
        o[positionAmount] = 0.5f;
        o[cutoffAmount] = 0.5f;
        auto& f = stored.layer[filterLayer];
        f[tune] = 0.0f;          // attack 0.5 ms
        f[fine] = 0.5f;
        f[table] = 0.5f;
        f[position] = 0.0f;      // no keytracking
        f[warp] = 0.5f;
        f[cutoff] = 0.0f;        // no envelope on the cutoff
        f[resonance] = 0.0f;     // no drive
        f[decay] = 1.0f;         // full sustain, so a patched gate holds the note and an empty one drones
        f[positionAmount] = 0.5f;
        f[cutoffAmount] = 0.5f;
    }

    Stored stored;
    Calibration calibration;

    /** Set when a mode or a hidden value changed; the control task saves after a quiet while. */
    bool dirty = false;

    /** One control tick of `ms` milliseconds. */
    void update(const PanelInputs& in, int ms, VoiceParams& out)
    {
        const auto result = layers.update(in, ms, panel, stored.layer);
        if (result.warpTapped)
            stored.warpMode = (Warp) (((int) stored.warpMode + 1) % (int) Warp::count);
        if (result.filterTapped)
            stored.filterMode = (FilterMode) (((int) stored.filterMode + 1) % (int) FilterMode::count);
        if (result.warpTapped || result.filterTapped || result.hiddenChanged)
            dirty = true;

        const bool normalLayer = ! in.warpButton && ! in.filterButton;
        if (normalLayer && std::abs(panel[table] - lastTablePot) > 0.03f)
        {
            lastTablePot = panel[table];
            tableShownMs = 1000;
        }
        tableShownMs = std::max(0, tableShownMs - ms);

        makeParams(in, out);
    }

    /** The pots of the normal layer as they stand (picked up), for the host test. */
    float panelValue(int pot) const { return panel[pot]; }

    /** LED 1: the warp mode, or for a second after the table knob moved, the table. LED 2: the filter mode. */
    Rgb led(int index, float envelope) const
    {
        Rgb c;
        if (index == 0)
            c = tableShownMs > 0 ? tableColour(shownTable) : warpColour(stored.warpMode);
        else
            c = filterColour(stored.filterMode);
        // Glows with the envelope, never quite dark, so the mode stays readable without a gate.
        const float glow = 0.25f + 0.75f * std::clamp(envelope, 0.0f, 1.0f);
        return { (uint8_t) (c.r * glow), (uint8_t) (c.g * glow), (uint8_t) (c.b * glow) };
    }

    static Rgb warpColour(Warp w)
    {
        // Night City: off white, sync cyan, bend yellow, pwm magenta, fm red.
        switch (w)
        {
            case Warp::sync: return { 0, 240, 255 };
            case Warp::bend: return { 252, 238, 10 };
            case Warp::pwm: return { 255, 42, 160 };
            case Warp::fm: return { 255, 0, 60 };
            default: return { 90, 90, 90 };
        }
    }

    static Rgb filterColour(FilterMode f)
    {
        switch (f)
        {
            case FilterMode::low12: return { 0, 240, 255 };
            case FilterMode::low24: return { 0, 80, 255 };
            case FilterMode::high12: return { 252, 238, 10 };
            case FilterMode::band12: return { 40, 255, 80 };
            case FilterMode::notch: return { 255, 42, 160 };
            case FilterMode::comb: return { 255, 0, 60 };
            default: return { 90, 90, 90 };
        }
    }

    static Rgb tableColour(int index)
    {
        // Ten hues around the wheel.
        const float h = (float) index / (float) kTables * 6.0f;
        const int sector = (int) h;
        const float f = h - (float) sector;
        const auto up = (uint8_t) (255.0f * f), down = (uint8_t) (255.0f * (1.0f - f));
        switch (sector % 6)
        {
            case 0: return { 255, up, 0 };
            case 1: return { down, 255, 0 };
            case 2: return { 0, 255, up };
            case 3: return { 0, down, 255 };
            case 4: return { up, 0, 255 };
            default: return { 255, 0, down };
        }
    }

private:
    void makeParams(const PanelInputs& in, VoiceParams& out)
    {
        const auto& o = stored.layer[oscLayer];
        const auto& f = stored.layer[filterLayer];

        // Pitch: 0 V is C2; TUNE ±2 octaves in semitones, FINE ±50 cents.
        const float tuneSemis = (float) tuneStepper.update(panel[tune], 49) - 24.0f;
        out.note = 36.0f + 12.0f * in.cv[pitchCv] + tuneSemis + bipolar(panel[fine]) * 0.5f;

        out.a.on = true;
        // TABLE CV: 1.8 tables per volt, so 5 V sweeps all ten.
        shownTable = tableStepper.update(std::clamp(panel[table] + in.cv[tableCv] * 0.2f, 0.0f, 1.0f), kTables);
        out.a.table = shownTable;
        out.a.position = std::clamp(panel[position] + bipolar(panel[positionAmount]) * in.cv[positionCv] / 5.0f, 0.0f,
                                    1.0f);
        out.a.warp = stored.warpMode;
        out.a.warpAmount = std::clamp(panel[warp] + in.cv[warpCv] / 5.0f, 0.0f, 1.0f);

        // Hidden layer of the WARP button: oscillator B, unison, sub.
        const int bTable = bTableStepper.update(o[table], kTables + 1) - 1;
        out.b.on = bTable >= 0;
        out.b.table = std::max(0, bTable);
        out.b.position = o[position];
        out.b.warp = stored.warpMode;
        out.b.warpAmount = out.a.warpAmount;
        out.b.transpose = (float) bTuneStepper.update(o[tune], 49) - 24.0f + bipolar(o[fine]) * 0.5f;
        const int unison = unisonStepper.update(o[warp], kMaxUnison) + 1;
        out.a.unison = out.b.unison = unison;
        out.a.detune = out.b.detune = 100.0f * o[cutoff];
        out.subOn = o[resonance] > 0.02f;
        out.subLevel = o[resonance];
        const int sub = subStepper.update(o[decay], 4);
        out.subOctave = 1 + (sub & 1);
        out.subSquare = sub >= 2;

        // Filter: CUTOFF 20 Hz to 20 kHz; its CV 1 V/Oct at full attenuverter, so it can track the pitch.
        out.filter = stored.filterMode;
        const float octaves = panel[cutoff] * 10.0f + bipolar(panel[cutoffAmount]) * in.cv[cutoffCv];
        out.cutoff = std::clamp(20.0f * std::exp2(octaves), 20.0f, 20000.0f);
        out.resonance = panel[resonance];
        out.decay = out.release = exponential(panel[decay], 0.005f, 5.0f);

        // Hidden layer of the FILTER button: envelope and filter extras.
        out.attack = exponential(f[tune], 0.0005f, 2.0f);
        out.keytrack = f[position];
        out.envToCutoff = 6.0f * f[cutoff];
        out.drive = f[resonance];
        out.sustain = f[decay];

        out.gate = in.gate;
    }

    float panel[kPots] {};
    Layers layers;
    float lastTablePot = -1.0f;
    int tableShownMs = 0;
    int shownTable = 0;
    Stepper tuneStepper, tableStepper, bTableStepper, bTuneStepper, unisonStepper, subStepper;
};
} // namespace twm
