#include "ArpPresets.h"

#include "ArpEngine.h"

#include <cstring>
#include <string>

namespace tonwerkarp
{
namespace
{
using Values = std::vector<std::pair<const char*, float>>;

/** The steps' parameter ids, kept for the program's lifetime: a preset holds them as plain pointers. */
const char* stepId(int step, const char* field)
{
    static const auto ids = [] {
        std::array<std::array<std::string, 4>, kSteps> table;
        const char* fields[] { "On", "Note", "Velocity", "Tie" };
        for (int i = 0; i < kSteps; ++i)
            for (int f = 0; f < 4; ++f)
                table[(std::size_t) i][(std::size_t) f] = "step" + std::to_string(i + 1) + fields[f];
        return table;
    }();
    const char* fields[] { "On", "Note", "Velocity", "Tie" };
    for (std::size_t f = 0; f < 4; ++f)
        if (std::strcmp(field, fields[f]) == 0)
            return ids[(std::size_t) step][f].c_str();
    return "";
}

/**
 * A pattern as text, one character a step: `x` a note, `X` an accented one, `-` tied over from the step before, `.` a
 * rest; its length sets the pattern's. With accents in it, a plain note plays at 65 %. `notes` are each step's
 * semitones (the rest stay at 0).
 */
Values pattern(const char* rhythm, std::initializer_list<int> notes = {}, Values values = {})
{
    const int length = std::min((int) std::strlen(rhythm), kSteps);
    const bool accents = std::strchr(rhythm, 'X') != nullptr;
    if (length < kSteps)
        values.push_back({ "length", (float) length });
    for (int i = 0; i < length; ++i)
    {
        const char c = rhythm[i];
        if (c == '.')
            values.push_back({ stepId(i, "On"), 0.0f });
        if (c == '-')
            values.push_back({ stepId(i, "Tie"), 1.0f });
        if (c == 'x' && accents)
            values.push_back({ stepId(i, "Velocity"), 0.65f });
    }
    int i = 0;
    for (const int note : notes)
    {
        if (note != 0 && i < kSteps)
            values.push_back({ stepId(i, "Note"), (float) note });
        ++i;
    }
    return values;
}
} // namespace

const std::vector<tonwerkui::EffectPreset>& factoryPresets()
{
    // Menus as the index of their entry (mode: Arpeggiator, Sequencer; order: Hoch, Runter, Hoch-Runter, Runter-Hoch,
    // Gespielt, Zufall; division: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4 T, 1/8 T, 1/16 T, 1/4., 1/8., 1/16.;
    // velocitySource: Gespielt, Fest), percent as 0 to 1.
    static const std::vector<tonwerkui::EffectPreset> presets {
        { "Init", "", {} },

        { "Hoch und runter", "Klassisch", { { "order", 2 }, { "octaves", 2 } } },
        { "Gebrochener Akkord", "Klassisch", { { "division", 3 }, { "gate", 0.8f } } },
        { "Harfe", "Klassisch", { { "octaves", 3 }, { "division", 5 }, { "gate", 0.3f } } },
        { "Runter", "Klassisch", { { "order", 1 }, { "octaves", 2 } } },
        { "Wie gespielt", "Klassisch", { { "order", 4 }, { "division", 3 }, { "gate", 0.7f } } },
        { "Zufall", "Klassisch", { { "order", 5 }, { "octaves", 2 }, { "gate", 0.4f } } },

        // The held chord chopped in sixteenths, the gate of trance pads.
        { "Trance-Gate", "Trance & Techno", pattern("xx.xx.x.xx.xx.x.", {}, { { "mode", 1 }, { "gate", 0.45f } }) },
        { "Pluck 1/16", "Trance & Techno",
          pattern("XxxxXxxxXxxxXxxx", {}, { { "octaves", 3 }, { "gate", 0.25f } }) },
        { "Oktav-Roller", "Trance & Techno",
          pattern("XxxxXxxxXxxxXxxx", { 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12 },
                  { { "mode", 1 }, { "gate", 0.35f } }) },
        { "Techno-Stab", "Trance & Techno", pattern("..x...x...x..x.x", {}, { { "mode", 1 }, { "gate", 0.2f } }) },

        // One key held: the line plays on it. Ties make the slides of a TB-303 on a mono synth with glide.
        { "Acid-Linie", "Bass-Sequenzen",
          pattern("Xx-xX.xxX-x.xXxx", { 0, 12, 12, 0, 0, 0, 3, 5, 0, 0, 12, 0, 7, 0, 10, 12 },
                  { { "mode", 1 }, { "velocitySource", 1 }, { "fixedVelocity", 110 } }) },
        { "Offbeat-Bass", "Bass-Sequenzen", pattern("..x...x...x...x.", {}, { { "mode", 1 }, { "gate", 0.6f } }) },
        { "Berlin-Schule", "Bass-Sequenzen",
          pattern("xxxxxxxxxxxxxxxx", { 0, 7, 12, 7, 0, 7, 15, 7, 0, 7, 12, 7, 0, 7, 17, 15 },
                  { { "mode", 1 }, { "gate", 0.6f } }) },
        { "Rollender Bass", "Bass-Sequenzen", pattern(".xxx.xxx.xxx.xxx", {}, { { "mode", 1 } }) },
        { "Dubstep-Triolen", "Bass-Sequenzen",
          pattern("Xx-xXx.xX-xx", { 0, 0, 0, 0, 7, 0, 0, 0, 12, 12, 3, 0 },
                  { { "mode", 1 }, { "division", 7 }, { "gate", 0.7f } }) },

        { "Swing-Arp", "Rhythmisch", pattern("XxxxXxxxXxxxXxxx", {}, { { "octaves", 2 }, { "swing", 0.62f } }) },
        { "Triolen", "Rhythmisch", pattern("XxxXxxXxxXxx", {}, { { "order", 2 }, { "octaves", 2 }, { "division", 7 } }) },
        // Seven steps against the bar: the pattern shifts against the beat and meets it again every seven bars.
        { "Siebener", "Rhythmisch", pattern("Xx.xx.x", {}, { { "order", 4 } }) },
        // Each note reaches into the next: a mono synth with glide slides from note to note.
        { "Legato-Kette", "Rhythmisch", { { "octaves", 2 }, { "division", 3 }, { "gate", 1.1f } } },
        { "Punktiert", "Rhythmisch", { { "order", 2 }, { "division", 11 } } },
    };
    return presets;
}
} // namespace tonwerkarp
