#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkpump
{
/** The preset menu's groups. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kPumpCategories { "Sidechain", "Tempo", "Kompressor", "Kreativ" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled
 * with `output` so a bass and a pad under a kick on the side chain (tempo sounds without it) come out about as loud
 * as they went in (tests/PumpTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkpump
