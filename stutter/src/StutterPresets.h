#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkstutter
{
/** The preset menu's groups. Kept here, as Tonwerk Shimmer keeps its own. */
inline constexpr std::array<const char*, 4> kStutterCategories { "Beat Repeat", "Glitch", "Fills", "Effekte" };

/** The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkstutter
