#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

/**
 * The groups of the plugins' preset menus. Every factory sound names one, and the editors show them as headings in
 * this order, so a long list reads by what a sound is for; within a group the sounds keep their program order. Plain
 * C++, so the sounds' own files (and the tests) can name them without JUCE. UI strings, UTF-8.
 */
namespace tonwerkui
{
/** The instruments' groups. */
inline constexpr std::array<const char*, 8> kPresetCategories { "Bässe",   "Leads",   "Flächen",            "Tasten",
                                                                 "Plucks", "Glocken", "Bläser & Streicher", "Effekte" };
/** Tonwerk Distortion's groups: by what goes through it. */
inline constexpr std::array<const char*, 5> kDistortionCategories { "Gitarre", "Bass", "Synths", "Drums", "Gesang" };
/** Tonwerk Groovebox's groups: by style. */
inline constexpr std::array<const char*, 8> kGrooveCategories { "House", "Techno",  "Hip-Hop", "Trap",
                                                                 "Electro", "Breaks", "Dubstep", "Experimentell" };
/** Chrome Glitch's groups: by how far the chrome fails. */
inline constexpr std::array<const char*, 4> kGlitchCategories { "Dezent", "Rhythmisch", "Zerstört", "Klangeffekte" };

/** The category's place among `groups`; -1 for none (as the Init sound), which goes first, without a heading. */
template <std::size_t N>
int categoryIndex(const char* category, const std::array<const char*, N>& groups)
{
    for (std::size_t i = 0; i < groups.size(); ++i)
        if (category != nullptr && std::strcmp(category, groups[i]) == 0)
            return (int) i;
    return -1;
}

inline int categoryIndex(const char* category) { return categoryIndex(category, kPresetCategories); }

/** The program numbers of `count` sounds in menu order: by group, and in program order within one. */
template <typename CategoryOf, std::size_t N>
std::vector<int> menuOrder(int count, CategoryOf categoryOf, const std::array<const char*, N>& groups)
{
    std::vector<int> order((std::size_t) std::max(0, count));
    for (int i = 0; i < count; ++i)
        order[(std::size_t) i] = i;
    std::stable_sort(order.begin(), order.end(), [&categoryOf, &groups](int a, int b) {
        return categoryIndex(categoryOf(a), groups) < categoryIndex(categoryOf(b), groups);
    });
    return order;
}

template <typename CategoryOf>
std::vector<int> menuOrder(int count, CategoryOf categoryOf)
{
    return menuOrder(count, categoryOf, kPresetCategories);
}
} // namespace tonwerkui
