#pragma once

#include "EffectPresets.h"
#include "WobbleEngine.h"
#include "WobbleProcessor.h"

#include <array>
#include <vector>

namespace tonwerkwobble
{
/** Tonwerk Wobble's preset groups: by what the LFOs do. Kept here, not in PresetCategories.h, as the plugin's own. */
inline constexpr std::array<const char*, 5> kWobbleCategories { "Wobble", "Yoi & Vokal", "Pumpen", "Gater",
                                                                "Bewegung" };

/** A sound's two drawn curves; an empty one is the lane's default. */
struct PresetCurves
{
    Curve filter;
    Curve volume;
};

/** The curves a new instance starts with: a sine for the filter, a side chain's pump for the volume. */
Curve defaultCurve(Lane lane);

/**
 * The host's programs, in order; the first is the defaults. New sounds go at the end. Each is leveled to sound about
 * as loud as its source without the effect (tests/WobbleTests.cpp), so trying one does not jump.
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
/** The curves of `factoryPresets()`, in the same order. */
const std::vector<PresetCurves>& factoryCurves();
} // namespace tonwerkwobble
