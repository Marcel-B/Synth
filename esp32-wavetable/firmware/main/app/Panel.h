#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twm
{
/**
 * The panel's pots, in the order of the ESP32-S3's ADC1 channels they sit on (hw/Pins.h). The names are Tonwerk
 * Wavetable's; another firmware gives the same pots other jobs (app/PhysicalModule.h).
 */
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

/** Steps of a pot with a little hysteresis, so a knob resting on a boundary does not flicker between two steps. */
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
 * The two buttons and three layers every firmware's panel shares: a short press without turning a knob is a tap
 * (it switches a mode), a hold opens the button's layer, where each pot sets a hidden value. Every pot picks up its
 * value whenever the layer changes.
 */
class Layers
{
public:
    static constexpr int kHoldMs = 300;
    /** How far a knob must move while a button is held before the press no longer counts as a tap. */
    static constexpr float kTurnedBy = 0.03f;

    struct Result
    {
        bool warpTapped = false;
        bool filterTapped = false;
        /** A hidden value moved: worth saving. */
        bool hiddenChanged = false;
    };

    /** One control tick of `ms` milliseconds: the pots go into `panel` (normal layer) or `hidden[layer]`. */
    Result update(const PanelInputs& in, int ms, float (&panel)[kPots], float (&hidden)[kLayers][kPots])
    {
        Result result;
        press(in, in.warpButton, warpWas, warpHeldMs, ms, result.warpTapped);
        press(in, in.filterButton, filterWas, filterHeldMs, ms, result.filterTapped);

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
            float& value = layer == normal ? panel[p] : hidden[layer][p];
            const float before = value;
            pickup[layer][p].update(in.pot[p], value);
            if (layer != normal && std::abs(value - before) > 1.0e-4f)
                result.hiddenChanged = true;
            // Only a real turn swallows the tap: a still pot that is caught at once (resting on its hidden value)
            // jitters by a few ADC steps every read.
            if (layer != normal && std::abs(in.pot[p] - potAtPress[p]) > kTurnedBy)
                turnedWhileHeld = true;
        }
        return result;
    }

private:
    void press(const PanelInputs& in, bool down, bool& was, int& heldMs, int ms, bool& tapped)
    {
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
            tapped = true;
        was = down;
    }

    Pickup pickup[kLayers][kPots] {};
    Layer activeLayer = normal;
    bool warpWas = false, filterWas = false, turnedWhileHeld = false;
    float potAtPress[kPots] {};
    int warpHeldMs = 0, filterHeldMs = 0;
};

/** A pot on an exponential scale from `low` to `high`. */
inline float exponential(float pot, float low, float high) { return low * std::pow(high / low, pot); }
/** Where an exponential pot from `low` to `high` stands for `value`: the default positions of hidden values. */
inline float exponentialPot(float value, float low, float high) { return std::log(value / low) / std::log(high / low); }
/** A pot centred at 0: -1 at the left, +1 at the right, with a little dead zone in the middle. */
inline float bipolar(float pot)
{
    const float v = 2.0f * pot - 1.0f;
    return std::abs(v) < 0.03f ? 0.0f : v;
}
} // namespace twm
