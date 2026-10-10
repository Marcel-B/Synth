#pragma once

#include "GrooveEngine.h"

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace tonwerkgroove
{
/**
 * A factory kit: the parameters it changes from their defaults, in the parameters' own units (percent as 0 to 1,
 * times in ms, menus as the index of their entry), and its patterns, A first. Each pattern is one text per track in
 * grid order, a character a step (`parseTrack`).
 */
struct FactoryKit
{
    const char* name;
    /** The menu's group, one of `tonwerkui::kGrooveCategories`; empty for Init, which comes first. */
    const char* category;
    std::vector<std::pair<juce::String, float>> values;
    std::vector<std::array<const char*, kTracks>> patterns;
};

/** The host's programs, in order; the first is the plain default kit. New kits go at the end. */
const std::vector<FactoryKit>& factoryKits();
} // namespace tonwerkgroove
