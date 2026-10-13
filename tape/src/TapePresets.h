#pragma once

#include "EffectPresets.h"

namespace tonwerktape
{
/**
 * The host's programs, in order; the first is the machine at its defaults. New sounds go at the end. Each is leveled
 * to sound about as loud as its source without the effect (tests/TapeTests.cpp), so trying one does not jump.
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerktape
