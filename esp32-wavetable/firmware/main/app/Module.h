#pragma once

#include "../dsp/Voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/** The panel's pots, in the order of the ESP32-S3's ADC1 channels they sit on (hw/Pins.h). */
enum Pot : int { tune, fine, table, position, warp, cutoff, resonance, decay, positionAmount, cutoffAmount, kPots };
/** The CV inputs, in the order of the MCP3208's channels. */
enum Cv : int { pitchCv, tableCv, positionCv, warpCv, cutoffCv, kCvs };

/** What the control task reads each tick: pots 0 to 1, CVs in volts at the jack. */
struct PanelInputs
{
    float pot[kPots] {};
    float cv[kCvs] {};
    bool gate = true;
    bool warpButton = false;
    bool filterButton = false;
};

/** The two layers a held button opens: each pot then sets something else (see the README's panel table). */
enum Layer : int { normal, oscLayer, filterLayer, kLayers };

/** The colours of the two LEDs, 0 to 255 per channel. */
struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
};

/** What is kept across power cycles: the modes and the hidden layers' settings. */
struct Stored
{
    uint32_t version = 1;
    Warp warpMode = Warp::off;
    FilterMode filterMode = FilterMode::low24;
    float layer[kLayers][kPots] {};
};

/**
 * The V/Oct input's calibration: the MCP3208's codes at 1 V and 3 V, measured in the calibration mode (hold both
 * buttons while switching on), and the codes of the other CV inputs at 0 V. The defaults are the circuit's nominal
 * values (hardware/schaltplan.svg): the input stage maps -1..+7 V to 3.0..0 V on a 3.0 V reference.
 */
struct Calibration
{
    uint32_t version = 1;
    float pitchCode1V = 3063.0f;
    float pitchCode3V = 2042.0f;
    float zeroCode[kCvs] { 0.0f, 2048.0f, 2048.0f, 2048.0f, 2048.0f };

    float pitchVolts(float code) const { return 1.0f + (pitchCode1V - code) * 2.0f / (pitchCode1V - pitchCode3V); }
    /** The ±5 V inputs: 0.3 V at the ADC per volt at the jack, inverted. */
    float cvVolts(int cv, float code) const { return (zeroCode[cv] - code) * (3.0f / 4096.0f) / 0.3f; }
};

/**
 * A pot that drives a value only once it has reached it ("pick-up"), so turning a knob in one layer does not make
 * another layer's value jump when the layer changes.
 */
struct Pickup
{
    bool caught = true;
    float lastPot = 0.0f;

    /** Moves `value` with `pot` once caught. */
    void update(float pot, float& value)
    {
        if (! caught && (std::abs(pot - value) < 0.02f || (lastPot - value) * (pot - value) < 0.0f))
            caught = true;
        if (caught)
            value = pot;
        lastPot = pot;
    }
    void release(float pot)
    {
        caught = false;
        lastPot = pot;
    }
};

/** Steps of a pot with a little hysteresis, so a knob resting on a boundary does not flicker between two tables. */
struct Stepper
{
    int current = 0;
    int update(float value, int steps)
    {
        const float scaled = std::clamp(value, 0.0f, 1.0f) * (float) (steps - 1);
        if (std::abs(scaled - (float) current) > 0.6f)
            current = std::clamp((int) std::lround(scaled), 0, steps - 1);
        return current;
    }
};

/**
 * The module's logic without hardware: turns the panel's pots, buttons and CVs into the voice's parameters, keeps
 * the hidden layers and the modes, and says what the LEDs show. Runs on the control task every millisecond; plain
 * C++ so the host test checks it.
 */
class Module
{
public:
    static constexpr int kHoldMs = 300;
    /** How far a knob must move while a button is held before the press no longer counts as a tap. */
    static constexpr float kTurnedBy = 0.03f;

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
        handleButtons(in, ms);

        const Layer layer = in.warpButton ? oscLayer : in.filterButton ? filterLayer : normal;
        if (layer != activeLayer)
        {
            // Every pot has to pick up its value in the new layer, and again in the old one when it comes back.
            for (int p = 0; p < kPots; ++p)
                pickup[layer][p].release(in.pot[p]);
            activeLayer = layer;
        }
        for (int p = 0; p < kPots; ++p)
        {
            float& value = layer == normal ? panel[p] : stored.layer[layer][p];
            const float before = value;
            pickup[layer][p].update(in.pot[p], value);
            if (layer != normal && std::abs(value - before) > 1.0e-4f)
                dirty = true;
            // Only a real turn swallows the tap: a still pot that is caught at once (resting on its hidden value)
            // jitters by a few ADC steps every read.
            if (layer != normal && std::abs(in.pot[p] - potAtPress[p]) > kTurnedBy)
                turnedWhileHeld = true;
        }
        if (layer == normal && std::abs(panel[table] - lastTablePot) > 0.03f)
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
    void handleButtons(const PanelInputs& in, int ms)
    {
        // A short press without turning a knob switches the mode; a hold opens the layer.
        auto press = [&](bool down, bool& was, int& heldMs, auto onTap) {
            if (down)
            {
                if (! was)
                {
                    heldMs = 0;
                    turnedWhileHeld = false;
                    std::copy(in.pot, in.pot + kPots, potAtPress);
                }
                heldMs += ms;
            }
            else if (was && heldMs < kHoldMs && ! turnedWhileHeld)
            {
                onTap();
                dirty = true;
            }
            was = down;
        };
        press(in.warpButton, warpWas, warpHeldMs, [this] {
            stored.warpMode = (Warp) (((int) stored.warpMode + 1) % (int) Warp::count);
        });
        press(in.filterButton, filterWas, filterHeldMs, [this] {
            stored.filterMode = (FilterMode) (((int) stored.filterMode + 1) % (int) FilterMode::count);
        });
    }

    static float exponential(float pot, float low, float high) { return low * std::pow(high / low, pot); }
    /** A pot centred at 0: -1 at the left, +1 at the right, with a little dead zone in the middle. */
    static float bipolar(float pot)
    {
        const float v = 2.0f * pot - 1.0f;
        return std::abs(v) < 0.03f ? 0.0f : v;
    }

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
    Pickup pickup[kLayers][kPots] {};
    Layer activeLayer = normal;
    bool warpWas = false, filterWas = false, turnedWhileHeld = false;
    float potAtPress[kPots] {};
    int warpHeldMs = 0, filterHeldMs = 0;
    float lastTablePot = -1.0f;
    int tableShownMs = 0;
    int shownTable = 0;
    Stepper tuneStepper, tableStepper, bTableStepper, bTuneStepper, unisonStepper, subStepper;
};
} // namespace twm
