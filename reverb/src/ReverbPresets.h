#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkreverb
{
/** The preset menu's groups. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kReverbCategories { "Räume", "Hallen", "Platten", "Effekte" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled to
 * sound about as loud as the dry source without it (tests/ReverbTests.cpp). None freezes.
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkreverb
