#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tonwerksampler
{
/**
 * A sample as the voices read it: one or two channels, each with copies at half, a quarter, ... of its rate, lowpassed
 * before they are thinned out. A voice pitched up reads the copy that keeps it about below Nyquist, so a chop played
 * two octaves up does not fold back, and the four-point interpolation stays cheap. Plain C++, no JUCE.
 */
struct Sample
{
    static constexpr int kLevels = 5;
    /** Columns of the overview the editor draws. */
    static constexpr int kOverview = 640;
    /** The longest file taken in, so a long recording cannot fill the memory. */
    static constexpr double kMaxSeconds = 60.0;

    int channels = 1;
    /** `levels[level][channel]`; level 0 is the sample itself. */
    std::array<std::array<std::vector<float>, 2>, kLevels> levels;
    double sampleRate = 48000.0;
    /** Peaks (min, max) of `kOverview` stretches of both channels, for the editor. */
    std::vector<std::pair<float, float>> overview;
    /** Where the sound starts anew (sample, strength), strongest first: slicing by transients takes the first few. */
    std::vector<std::pair<double, float>> onsets;
    std::string name;

    std::size_t length() const { return levels[0][0].size(); }
    bool empty() const { return levels[0][0].empty(); }

    /**
     * A sample from one or two channels of equal length: its lower copies, the overview and the onsets are made here.
     * Allocates; never on the audio thread.
     */
    static Sample fromChannels(std::vector<std::vector<float>> channels, double rate, std::string name);

    /**
     * One channel at `position` (in samples of level 0) from `level`, on a four-point Hermite curve; outside the
     * sample it reads silence, so a fade at an edge needs no special case.
     */
    float read(int channel, int level, double position) const;
};

/**
 * The sounds built into the plugin, computed from formulas when the first instance is made and shared by all, as
 * Tonwerk Granular's sources are; nothing is recorded or copied from elsewhere. The source menu's entry after them is
 * the user's own file, which the processor holds.
 */
class SampleBank
{
public:
    static constexpr int kBuiltIn = 9;
    /** The menu's index of the user's file. */
    static constexpr int kUserFile = kBuiltIn;

    static const SampleBank& instance();
    const Sample& sample(int index) const;
    /** The menu's entries, UTF-8: the built-in sounds, then the user's file. */
    static const std::vector<std::string>& names();
    /** An empty sample, for "Eigene Datei" before a file is loaded. */
    static const Sample& none();

private:
    SampleBank();
    std::array<Sample, kBuiltIn> samples;
};
} // namespace tonwerksampler
