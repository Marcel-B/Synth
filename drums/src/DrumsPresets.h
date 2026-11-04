#pragma once

#include "DrumsEngine.h"

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace tonwerkdrums
{
/** The kit menu's groups: the machines, then the Groovebox's styles its kits come from. */
inline constexpr std::array<const char*, 9> kDrumsCategories { "Maschinen", "House",   "Techno",  "Hip-Hop",      "Trap",
                                                               "Electro",   "Breaks",  "Dubstep", "Experimentell" };

/**
 * A factory kit: the parameters it changes from their defaults, in the parameters' own units (percent as 0 to 1,
 * times in ms, menus as the index of their entry). Notes are the player's, never a kit's.
 */
struct DrumKit
{
    juce::String name;
    /** One of `kDrumsCategories`; empty for Init, which comes first. */
    juce::String category;
    std::vector<std::pair<juce::String, float>> values;
};

/**
 * The host's programs, in order: Init, a kit per machine, then Tonwerk Groovebox's kits (their sounds; the patterns
 * stay behind), each with the eight pads the Groovebox lacks dressed like their neighbours. New kits go at the end.
 */
const std::vector<DrumKit>& factoryKits();
} // namespace tonwerkdrums
