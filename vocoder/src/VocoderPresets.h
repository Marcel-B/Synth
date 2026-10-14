#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkvocoder
{
/** The preset menu's groups: by what the voice turns into. Kept here, as Tonwerk Freeze keeps its own. */
inline constexpr std::array<const char*, 4> kVocoderCategories { "Klassisch", "Chor", "Roboter", "Effekte" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled to
 * sound about as loud as the voice without it, a chord held under it (tests/VocoderTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkvocoder
