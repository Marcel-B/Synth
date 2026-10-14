#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkchorus
{
/** The preset menu's groups, one per mode. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kChorusCategories { "Chorus", "Ensemble", "Flanger", "Phaser" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled to
 * sound about as loud as the dry source without it (tests/ChorusTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkchorus
