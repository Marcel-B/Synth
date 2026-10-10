#pragma once

#include "dsp/Patch.h"

#include <vector>

namespace tonwerk
{
/** A factory sound of the plugin's own, beyond Tonwerk's four starting sounds per engine. */
struct FactorySound
{
    /** UTF-8, as the menu shows it. */
    const char* name;
    /** The menu's group, one of `tonwerkui::kPresetCategories`. */
    const char* category;
    Patch patch;
};

/**
 * The plugin's own sounds for `engine`, in program order. They also show what Tonwerk lacks: the wavefolder, the
 * sample and hold and tempo sync. Plain C++, so the sounds can be read without JUCE; `PresetLibrary` turns them into
 * the host's programs. Programs keep their numbers once released, so new sounds go at the end.
 */
const std::vector<FactorySound>& factorySounds(Engine engine);
} // namespace tonwerk
