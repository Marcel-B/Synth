#pragma once

#include "FilterEngine.h"
#include "WobbleEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * Tonwerk LFO's logic, plain C++: an LFO that moves a knob of the instrument after it by sending a MIDI controller.
 * The shapes are Tonwerk Filter's (`lfoValue`, `randomFor`, the same menu), the note values Tonwerk Wobble's.
 *
 * The value is `centre` ± half of `depth`, clamped to the controller's range, sent every `kStep` samples (750 times
 * a second at 48 kHz) when it changed. Controllers 0 to 31 go out in 14 bits when `fine` is on: the coarse half on the
 * controller, the fine half on the controller + 32, as the MIDI standard pairs them, so a slow sweep moves in 16384
 * steps instead of 128. Receivers that only know 7 bits read the coarse half and ignore the other.
 */
namespace tonwerklfo
{
using tonwerkfilter::Clock;
using tonwerkfilter::kShapeNames;
using Shape = tonwerkfilter::Shape;

inline constexpr int kStep = 64;
inline constexpr int kMaxFine = 16383;
/** Controllers 120 to 127 are channel mode messages; never send those. */
inline constexpr int kLastController = 119;

/** Where a cycle starts: on the song's grid (synced, while the host plays) or anew with the first key of a phrase. */
enum class Start
{
    song,
    note
};

struct Settings
{
    Shape shape = Shape::sine;
    bool sync = true;
    /** Cycles per second when free. */
    float rate = 1.0f;
    /** A `tonwerkwobble::kDivisions` index when synced. */
    int division = tonwerkwobble::kQuarter;
    /** Where in its cycle the LFO is at the cycle's start, in degrees. */
    float phaseDegrees = 0.0f;
    /** 0 to 1: how far it swings, the whole controller range at 1. */
    float depth = 1.0f;
    /** 0 to 1: the value it swings around. */
    float centre = 0.5f;
    int controller = 20;
    /** 1 to 16. */
    int channel = 1;
    bool fine = true;
    Start start = Start::song;
};

/** Whether `s` sends 14 bits: only controllers 0 to 31 have a fine half. */
inline bool sendsFine(const Settings& s) { return s.fine && s.controller >= 0 && s.controller < 32; }

/** The value at `phase` (in cycles), 0 to 1. */
inline float valueAt(const Settings& s, double phase)
{
    const double at = phase + s.phaseDegrees / 360.0;
    const float lfo = tonwerkfilter::lfoValue(s.shape, at, tonwerkfilter::randomFor((std::int64_t) std::floor(at)));
    return std::clamp(s.centre + 0.5f * std::clamp(s.depth, 0.0f, 1.0f) * lfo, 0.0f, 1.0f);
}

/**
 * Turns the LFO into controller messages. `process` walks a block in steps of `kStep`, restarting the cycle at the
 * offsets in `restarts` (sorted; for Start note), and calls `emit(offset, controller, value, channel)` for each
 * message; it returns the phase after the block, which the processor carries to the next one.
 */
class Engine
{
public:
    void reset() { last = -1; }

    template <typename Emit>
    double process(int count, const Settings& s, const Clock& clock, const int* restarts, int restartCount, Emit&& emit)
    {
        int origin = 0;
        double originPhase = clock.phase;
        int r = 0;
        auto sendAt = [&](int offset) {
            send(offset, valueAt(s, originPhase + (double) (offset - origin) * clock.increment), s, emit);
        };
        for (int n = 0; n < count; n += kStep)
        {
            while (r < restartCount && restarts[r] <= n)
            {
                origin = restarts[r++];
                originPhase = 0.0;
                if (origin < n)
                    sendAt(origin);
            }
            sendAt(n);
        }
        while (r < restartCount)
        {
            origin = std::min(restarts[r++], count - 1);
            originPhase = 0.0;
            sendAt(origin);
        }
        return originPhase + (double) (count - origin) * clock.increment;
    }

    /** The last value sent, 0 to 1 (-1 before the first). */
    float lastValue() const { return last < 0 ? -1.0f : (float) last / (float) kMaxFine; }

    /** Forgets the last value, so the next step sends again (after a change of controller or channel). */
    void resend() { last = -1; }

private:
    template <typename Emit>
    void send(int offset, float value, const Settings& s, Emit& emit)
    {
        const int fine = nearest(value * (float) kMaxFine);
        // 7 bits: the value the coarse half can hold, so a change too small for it sends nothing.
        const int sent = sendsFine(s) ? fine : (nearest(value * 127.0f) * kMaxFine + 63) / 127;
        if (sent == last)
            return;
        last = sent;
        const int controller = std::clamp(s.controller, 0, kLastController);
        if (sendsFine(s))
        {
            emit(offset, controller, fine >> 7, s.channel);
            emit(offset, controller + 32, fine & 127, s.channel);
        }
        else
            emit(offset, controller, nearest(value * 127.0f), s.channel);
    }

    static int nearest(float x) { return (int) std::lround(x); }

    int last = -1;
};
} // namespace tonwerklfo
