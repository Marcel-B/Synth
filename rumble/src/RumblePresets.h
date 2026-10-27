#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkrumble
{
/** The preset menu's groups. Kept here, as Tonwerk Shimmer keeps its own. */
inline constexpr std::array<const char*, 4> kRumbleCategories { "Techno", "Hart", "Dub", "Raster" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled
 * with `rumble` so kick and rumble together come across a few dB louder than the kick alone (tests/RumbleTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkrumble
