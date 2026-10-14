#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkshimmer
{
/** The preset menu's groups. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kShimmerCategories { "Shimmer", "Hall", "Ambient", "Effekte" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled to
 * sound about as loud as the dry source without it (tests/ShimmerTests.cpp). None switches hold on.
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkshimmer
