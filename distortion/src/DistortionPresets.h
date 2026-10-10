#pragma once

#include "EffectPresets.h"

namespace tonwerkdistortion
{
/**
 * The host's programs, in order; the first is the pedal at its defaults. New sounds go at the end. Each is leveled
 * to sound about as loud as its source without the effect (tests/EffectPresetTests.cpp), so trying one does not jump.
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkdistortion
