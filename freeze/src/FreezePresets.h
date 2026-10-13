#pragma once

#include "EffectPresets.h"

#include <array>

namespace tonwerkfreeze
{
/** Tonwerk Freeze's menu groups: by what the frozen pad does. UI strings, UTF-8. */
inline constexpr std::array<const char*, 5> kFreezeCategories { "Flächen", "Schimmer", "Bewegt", "Rhythmisch",
                                                                "Klangeffekte" };

/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. None switches the
 * freeze on: a sound chosen while the track is silent would freeze silence. Each is leveled so the frozen layer is
 * about as loud as what was frozen (tests/FreezeTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkfreeze
