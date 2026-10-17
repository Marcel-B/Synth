#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerklfo
{
/** The preset menu's groups. UI strings, UTF-8. */
inline constexpr std::array<const char*, 3> kLfoCategories { "Sweeps", "Rhythmisch", "Zufall" };

/** The host's programs, in order; the first is the LFO at its defaults. New ones go at the end. */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerklfo
