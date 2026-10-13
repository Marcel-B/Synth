#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkarp
{
/** The preset menu's groups: by what the pattern is for. UI strings, UTF-8. */
inline constexpr std::array<const char*, 4> kArpCategories { "Klassisch", "Trance & Techno", "Bass-Sequenzen",
                                                             "Rhythmisch" };

/** The host's programs, in order; the first is the arpeggiator at its defaults. New sounds go at the end. */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkarp
