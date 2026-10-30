#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkscope
{
/** The preset menu's groups. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kScopeCategories { "Welle", "Takt", "Stereo", "Stimmen|Scope" };

/** The host's programs, in order; the first is the scope at its defaults. New ones go at the end. */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkscope
