#pragma once

#include "DxSysex.h"

#include <vector>

namespace tonwerkdx
{
/**
 * The factory sounds, the host's programs in this order: classic DX colours (electric piano, bass, brass, bells and
 * so on) made for this plugin. None is a copy of a Yamaha ROM voice; their programs must keep their numbers once
 * released, so new sounds go at the end.
 */
const std::vector<NamedDxPatch>& factoryPresets();
} // namespace tonwerkdx
