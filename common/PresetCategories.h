#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

/**
 * The groups of the instruments' preset menus. Every factory sound names one, and the editors show them as headings in
 * this order, so a long list reads by what a sound is for; within a group the sounds keep their program order. Plain
 * C++, so the sounds' own files (and the tests) can name them without JUCE. UI strings, UTF-8.
 */
namespace tonwerkui
{
inline constexpr std::array<const char*, 8> kPresetCategories { "Bässe",   "Leads",   "Flächen",            "Tasten",
                                                                 "Plucks", "Glocken", "Bläser & Streicher", "Effekte" };

/** The category's place among the groups; -1 for none (as the Init sound), which goes first, without a heading. */
inline int categoryIndex(const char* category)
{
    for (std::size_t i = 0; i < kPresetCategories.size(); ++i)
        if (category != nullptr && std::strcmp(category, kPresetCategories[i]) == 0)
            return (int) i;
    return -1;
}

/** The program numbers of `count` sounds in menu order: by group, and in program order within one. */
template <typename CategoryOf>
std::vector<int> menuOrder(int count, CategoryOf categoryOf)
{
    std::vector<int> order((std::size_t) std::max(0, count));
    for (int i = 0; i < count; ++i)
        order[(std::size_t) i] = i;
    std::stable_sort(order.begin(), order.end(), [&categoryOf](int a, int b) {
        return categoryIndex(categoryOf(a)) < categoryIndex(categoryOf(b));
    });
    return order;
}
} // namespace tonwerkui
