#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tonwerkgrain
{
/**
 * A sound the grains are cut from: mono, with copies at half, a quarter, ... of its rate, each lowpassed before it is
 * thinned out. A grain played faster than the source's rate reads the copy that keeps it below Nyquist, so pitching
 * a grain up three octaves does not alias, at the cost of linear interpolation only.
 */
struct GrainSource
{
    static constexpr int kLevels = 5;
    /** Columns of the overview the editor draws. */
    static constexpr int kOverview = 512;

    std::array<std::vector<float>, kLevels> levels;
    double sampleRate = 48000.0;
    /** The note the source sounds at when played at its own rate; a key plays it transposed from there. */
    int rootNote = 60;
    /** Peaks (min, max) of `kOverview` stretches, for the editor. */
    std::vector<std::pair<float, float>> overview;
    std::string name;

    std::size_t length() const { return levels[0].size(); }
    bool empty() const { return levels[0].empty(); }

    /**
     * A source from mono samples: brought to a common loudness (RMS -12 dBFS, peak at most -0.5 dBFS), so switching
     * sources keeps the level, then its lower copies and the overview are made. Allocates; never on the audio thread.
     */
    static GrainSource fromSamples(std::vector<float> samples, double rate, int root, std::string name);
};

/**
 * The sources built into the plugin, computed from formulas when the first instance is made and shared by all, as the
 * wavetables are. Nothing is recorded or copied from elsewhere. The entry after them in the source menu is the
 * user's own file, which the processor holds.
 */
class SourceBank
{
public:
    static constexpr int kBuiltIn = 9;
    /** The menu's index of the user's file. */
    static constexpr int kUserFile = kBuiltIn;

    static const SourceBank& instance();
    const GrainSource& source(int index) const;
    /** The menu's entries, UTF-8: the built-in sources, then the user's file. */
    static const std::vector<std::string>& names();

private:
    SourceBank();
    std::array<GrainSource, kBuiltIn> sources;
};
} // namespace tonwerkgrain
