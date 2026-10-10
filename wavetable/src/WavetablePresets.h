#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace tonwerkwave
{
/**
 * A factory sound: the parameters it changes from their defaults, in the parameters' own units (percent as 0 to 1,
 * times in ms, menus as the index of their entry).
 */
struct FactoryPreset
{
    const char* name;
    /** The menu's group, one of `tonwerkui::kPresetCategories`; empty for Init, which comes first. */
    const char* category;
    std::vector<std::pair<juce::String, float>> values;
};

/** The host's programs, in order; the first is the plain default sound. New sounds go at the end. */
const std::vector<FactoryPreset>& factoryPresets();
} // namespace tonwerkwave
