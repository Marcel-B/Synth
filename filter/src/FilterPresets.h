#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkfilter
{
/** The preset menu's groups. Kept here, as Tonwerk Shimmer keeps its own. */
inline constexpr std::array<const char*, 4> kFilterCategories { "Auto-Wah", "Rhythmisch", "Sweeps", "Klangfarbe" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled
 * with `output` to sound about as loud as the dry source without it (tests/FilterTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkfilter
