#pragma once

#include "EffectPresets.h"

namespace chromeglitch
{
/** The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace chromeglitch
