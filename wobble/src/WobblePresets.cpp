#include "WobblePresets.h"

namespace tonwerkwobble
{
namespace
{
/** Menu indices of the note values, as `kDivisions` lists them. */
enum Note
{
    fourBars = 0,
    bar = 2,
    half = 4,
    quarter = 7,
    eighth = 10,
    eighthTriplet = 11,
    sixteenth = 13,
};

enum Type
{
    lowpass = 0,
    bandpass = 1,
    highpass = 2,
    vowel = 3,
};

/** One value per step, held for the step: a vowel line or a staircase over the cycle. */
Curve steps(std::initializer_list<float> levels)
{
    std::vector<CurvePoint> points;
    const auto count = (float) levels.size();
    int i = 0;
    for (const float level : levels)
    {
        points.push_back({ (float) i / count, level, 0.0f });
        points.push_back({ (float) (i + 1) / count, level, 0.0f });
        ++i;
    }
    return Curve(points);
}

/** A bar of wobbles speeding up: four eighths, four sixteenths, then six sixteenth triplets. */
Curve wobbleRide()
{
    return traced(
        [](float x) {
            double cycles;
            if (x < 0.5f)
                cycles = x * 8.0;
            else if (x < 0.75f)
                cycles = 4.0 + (x - 0.5) * 16.0;
            else
                cycles = 8.0 + (x - 0.75) * 24.0;
            return 0.5 - 0.5 * std::cos(2.0 * kPi * cycles);
        },
        56);
}

struct Entry
{
    tonwerkui::EffectPreset preset;
    PresetCurves curves;
};

const std::vector<Entry>& entries()
{
    // Percent as 0 to 1, frequencies in Hz, menus as their index. `output` levels each sound to its dry source.
    static const std::vector<Entry> list {
        { { "Init", "", {} }, {} },

        // The classic: a low pass opening and closing on the eighths, the resonance singing at the top.
        { { "Klassik 1/8", "Wobble",
            { { "filterDivision", eighth }, { "filterLow", 120 }, { "filterHigh", 2500 }, { "resonance", 0.45f },
              { "output", 0 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Langsam 1/4", "Wobble",
            { { "filterDivision", quarter }, { "filterLow", 100 }, { "filterHigh", 1800 }, { "resonance", 0.5f },
              { "output", -1 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Halftime 1/2", "Wobble",
            { { "filterDivision", half }, { "filterLow", 80 }, { "filterHigh", 3000 }, { "resonance", 0.4f },
              { "output", 0 } } },
          { shapeCurve(Shape::triangle), {} } },
        { { "Triolen 1/8T", "Wobble",
            { { "filterDivision", eighthTriplet }, { "filterLow", 150 }, { "filterHigh", 3500 }, { "resonance", 0.5f },
              { "output", 0 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Sechzehntel", "Wobble",
            { { "filterDivision", sixteenth }, { "filterLow", 200 }, { "filterHigh", 2500 }, { "resonance", 0.4f },
              { "output", 0 } } },
          { shapeCurve(Shape::triangle), {} } },
        // Open on the beat, closing over the eighth: a bite on every hit.
        { { "Säge-Wobble", "Wobble",
            { { "filterDivision", eighth }, { "filterLow", 100 }, { "filterHigh", 4000 }, { "resonance", 0.55f },
              { "output", 0 } } },
          { shapeCurve(Shape::sawDown), {} } },
        // Driven into the filter, the resonance growls instead of whistling.
        { { "Dreckig", "Wobble",
            { { "filterDivision", eighth }, { "filterLow", 150 }, { "filterHigh", 3000 }, { "resonance", 0.7f },
              { "drive", 15 }, { "output", -4 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Zweiklang 1/16", "Wobble",
            { { "filterDivision", sixteenth }, { "filterLow", 300 }, { "filterHigh", 2200 }, { "resonance", 0.5f },
              { "smooth", 4 }, { "output", 0 } } },
          { shapeCurve(Shape::square), {} } },
        // A drop's bar in one curve: eighths, sixteenths, then triplets.
        { { "Wobble-Ritt", "Wobble",
            { { "filterDivision", bar }, { "filterLow", 120 }, { "filterHigh", 3000 }, { "resonance", 0.5f },
              { "output", 0 } } },
          { wobbleRide(), {} } },

        // The vowel filter: u at the bottom of the curve, i at the top.
        { { "Yoi", "Yoi & Vokal",
            { { "filterType", vowel }, { "filterDivision", quarter }, { "resonance", 0.6f }, { "output", 1 } } },
          { shapeCurve(Shape::rampUp), {} } },
        { { "Wau-Wau", "Yoi & Vokal",
            { { "filterType", vowel }, { "filterDivision", eighth }, { "resonance", 0.5f }, { "output", 0 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Sprechender Bass", "Yoi & Vokal",
            { { "filterType", vowel }, { "filterDivision", bar }, { "resonance", 0.55f }, { "smooth", 12 },
              { "output", 0 } } },
          { steps({ 0.0f, 0.5f, 1.0f, 0.75f, 0.25f, 1.0f, 0.5f, 0.0f, 0.5f, 0.75f, 1.0f, 0.25f, 0.0f, 1.0f, 0.5f,
                    0.75f }),
            {} } },

        // The volume ducks on every beat as under a kick's side chain.
        { { "Sidechain 1/4", "Pumpen",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", quarter }, { "output", 0 } } },
          { {}, shapeCurve(Shape::pump) } },
        { { "Sidechain 1/8", "Pumpen",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", eighth }, { "volumeDepth", 0.8f },
              { "output", 0 } } },
          { {}, shapeCurve(Shape::pump) } },
        { { "Pumpender Filter", "Pumpen",
            { { "filterDivision", quarter }, { "filterLow", 300 }, { "filterHigh", 8000 }, { "resonance", 0.25f },
              { "volumeOn", 1 }, { "volumeDivision", quarter }, { "volumeDepth", 0.6f }, { "output", 0 } } },
          { shapeCurve(Shape::pump), shapeCurve(Shape::pump) } },

        // Sixteenths over a bar, cut by the volume.
        { { "Trance-Gate", "Gater",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", bar }, { "output", 2 } } },
          { {}, gate("x.xxx.xxx.xx.x.x", 0.7f) } },
        { { "Stotter-Gate", "Gater",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", bar }, { "output", 3 } } },
          { {}, gate("xxxx..x.x.xx..x.", 0.5f) } },
        // Six steps over a half note: eighth triplets.
        { { "Triolen-Gate", "Gater",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", half }, { "output", 2 } } },
          { {}, gate("xx.x.x", 0.75f) } },
        { { "Gate + Wobble", "Gater",
            { { "filterDivision", eighth }, { "filterLow", 150 }, { "filterHigh", 3000 }, { "resonance", 0.45f },
              { "volumeOn", 1 }, { "volumeDivision", bar }, { "output", 2 } } },
          { shapeCurve(Shape::sine), gate("x.xxx.x.xx.xxx.x", 0.8f) } },

        // Over four bars the filter opens and closes: a build-up under a pad.
        { { "Langsamer Sweep", "Bewegung",
            { { "filterDivision", fourBars }, { "filterLow", 200 }, { "filterHigh", 12000 }, { "resonance", 0.3f },
              { "output", 0 } } },
          { shapeCurve(Shape::triangle), {} } },
        // The right channel a quarter cycle ahead: the band circles between the speakers.
        { { "Stereo-Kreisen", "Bewegung",
            { { "filterType", bandpass }, { "filterSlope", 0 }, { "filterDivision", bar }, { "filterLow", 300 },
              { "filterHigh", 4000 }, { "resonance", 0.4f }, { "spread", 90 }, { "output", 4 } } },
          { shapeCurve(Shape::sine), {} } },
        { { "Tremolo", "Bewegung",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeSync", 0 }, { "volumeRate", 5.5f },
              { "volumeDepth", 0.5f }, { "output", 2 } } },
          { {}, shapeCurve(Shape::sine) } },
        // The two sides half a cycle apart: what one loses, the other gains.
        { { "Auto-Pan", "Bewegung",
            { { "filterOn", 0 }, { "volumeOn", 1 }, { "volumeDivision", quarter }, { "volumeDepth", 0.7f },
              { "spread", 180 }, { "output", 3 } } },
          { {}, shapeCurve(Shape::sine) } },
        { { "Hochpass-Riser", "Bewegung",
            { { "filterType", highpass }, { "filterDivision", fourBars }, { "filterLow", 20 }, { "filterHigh", 2000 },
              { "resonance", 0.4f }, { "output", 0 } } },
          { shapeCurve(Shape::rampUp), {} } },
    };
    return list;
}
} // namespace

Curve defaultCurve(Lane lane) { return shapeCurve(lane == Lane::filter ? Shape::sine : Shape::pump); }

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    static const std::vector<tonwerkui::EffectPreset> presets = [] {
        std::vector<tonwerkui::EffectPreset> list;
        for (const auto& entry : entries())
            list.push_back(entry.preset);
        return list;
    }();
    return presets;
}

const std::vector<PresetCurves>& factoryCurves()
{
    static const std::vector<PresetCurves> curves = [] {
        std::vector<PresetCurves> list;
        for (const auto& entry : entries())
            list.push_back(entry.curves);
        return list;
    }();
    return curves;
}
} // namespace tonwerkwobble
