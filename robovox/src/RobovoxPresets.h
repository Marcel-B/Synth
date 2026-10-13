#pragma once

#include "EffectPresets.h"

namespace tonwerkrobovox
{
/**
 * The host's programs, in order; the first is the effect at its defaults. New sounds go at the end. Each is leveled to
 * sound about as loud as the voice without it (tests/EffectPresetTests.cpp).
 */
const std::vector<tonwerkui::EffectPreset>& factoryPresets();
} // namespace tonwerkrobovox
