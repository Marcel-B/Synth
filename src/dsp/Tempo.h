#pragma once

#include "Patch.h"

#include <algorithm>

namespace tonwerk
{
/** The tempo when the host gives none, as in the standalone app. */
inline constexpr double kDefaultBpm = 120.0;
/** The longest delay the line holds; a synced delay at a slow tempo may go beyond the free knob's 1.5 s. */
inline constexpr double kMaxDelaySeconds = 4.0;

inline double divisionSeconds(int division, double bpm)
{
    const auto& entry = kDivisions[(std::size_t) std::clamp(division, 0, (int) kDivisions.size() - 1)];
    return entry.beats * 60.0 / std::max(1.0, bpm);
}

/**
 * The patch as the voices and the effects play it at `bpm`: a synced LFO or sample and hold gets the rate of its note
 * value, a synced delay its time. Everything downstream only knows rates and times, so sync needs nothing in the DSP
 * itself. The LFOs and the sample and hold still start with each note, as in Tonwerk, so they run in time but not on
 * the beat.
 */
inline Patch withTempo(Patch patch, double bpm)
{
    if (patch.analog.lfo.sync)
        patch.analog.lfo.rate = (float) (1.0 / divisionSeconds(patch.analog.lfo.division, bpm));
    if (patch.analog.sampleHold.sync)
        patch.analog.sampleHold.rate = (float) (1.0 / divisionSeconds(patch.analog.sampleHold.division, bpm));
    if (patch.fm.lfo.sync)
        patch.fm.lfo.rate = (float) (1.0 / divisionSeconds(patch.fm.lfo.division, bpm));
    if (patch.fx.delay.sync)
        patch.fx.delay.time = (float) std::clamp(divisionSeconds(patch.fx.delay.division, bpm), 0.02, kMaxDelaySeconds);
    return patch;
}
} // namespace tonwerk
