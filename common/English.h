#pragma once

#include <vector>

/** The English of the plugins' German texts, read by `tr` (Language.h). */
namespace tonwerkui::english
{
struct Entry
{
    const char* german;
    /** nullptr: the same in both languages ("Cutoff", "LFO 1"). */
    const char* english;
};

const std::vector<Entry>& entries();
} // namespace tonwerkui::english
