#pragma once

#include "WobbleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

/**
 * Tonwerk Stutter's sound, plain C++: a beat repeat on the song's grid. Each interval (a quarter to four bars) rolls
 * once whether it repeats; if it does, at `offset` sixteenths into it a slice of `grid` length is played live and
 * recorded, then repeated until `gate` sixteenths are over, getting quieter, pitched, reversed or filtered on the way.
 * The dice hang on the interval's number in the song, so a bounce repeats what was heard. Borrows `SvfStage` from
 * Tonwerk Wobble.
 */
namespace tonwerkstutter
{
using tonwerkwobble::SvfStage;

/** A note value in beats; the menus' orders are saved in projects: append only. */
struct Length
{
    const char* name;
    double beats;
};
inline constexpr std::array<Length, 5> kIntervals { {
    { "1/4", 1.0 }, { "1/2", 2.0 }, { "1 Takt", 4.0 }, { "2 Takte", 8.0 }, { "4 Takte", 16.0 },
} };
inline constexpr std::array<Length, 8> kGrids { {
    { "1/4", 1.0 },
    { "1/8", 0.5 },
    { "1/8T", 1.0 / 3 },
    { "1/16", 0.25 },
    { "1/16T", 1.0 / 6 },
    { "1/32", 0.125 },
    { "1/32T", 1.0 / 12 },
    { "1/64", 0.0625 },
} };

/** What the repeats do to the signal around them; a menu: append only. */
enum class Mode
{
    insert, // the repeats replace the signal while they run
    mix,    // the repeats over the signal
    gate,   // only the repeats; silence between them
};
inline constexpr std::array<const char*, 3> kModeNames { "Ersetzen", "Mischen", "Gate" };

struct Settings
{
    int interval = 2;        // `kIntervals` index: a bar
    int offset = 8;          // sixteenths into the interval where the repeat starts (at most its last sixteenth)
    int gate = 4;            // sixteenths it lasts, the live slice included
    float chance = 0.5f;     // 0 to 1, rolled once per interval
    int grid = 3;            // `kGrids` index: 1/16
    float variation = 0.0f;  // chance the grid moves a step or two for one repeat
    int seed = 1;            // another set of dice
    bool hold = false;       // repeats from the next grid line for as long as it is on
    float decay = 0.0f;      // 0 to 1: how far the repeats fade by the end of the gate
    float pitch = 0.0f;      // semitones the last repeat is moved, the ones between on the way
    float reverse = 0.0f;    // chance a repeat plays backwards
    float filter = 0.0f;     // -1 to 1: a low pass closing (negative) or a high pass opening over the gate
    Mode mode = Mode::insert;
    float outputDb = 0.0f;
};

/** Where the block starts in the song, in beats, and how far a sample moves it. */
struct Clock
{
    double beat = 0.0;
    double beatsPerSample = 120.0 / 60.0 / 48000.0;
};

/** splitmix64: the same number for the same interval, seed and question, in any run. */
inline double dice(std::int64_t interval, int seed, int question)
{
    std::uint64_t z = (std::uint64_t) interval * UINT64_C(0x9e3779b97f4a7c15) + (std::uint64_t) seed * UINT64_C(0xbf58476d1ce4e5b9)
                      + (std::uint64_t) question * UINT64_C(0x94d049bb133111eb) + UINT64_C(0x2545f4914f6cdd1d);
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31;
    return (double) (z >> 11) / 9007199254740992.0;
}

class Engine
{
public:
    /** Every edge (a slice's start and end, the event's start and end) fades over this long: no clicks. */
    static constexpr double kFadeSeconds = 0.0015;
    /** The longest slice it keeps: a quarter at 20 BPM. */
    static constexpr double kLongestSlice = 3.0;

    void prepare(double sampleRate)
    {
        rate = sampleRate;
        for (auto& channel : slices)
            channel.assign((std::size_t) std::ceil(kLongestSlice * sampleRate) + 2, 0.0f);
        fadeStep = (float) (1.0 / (kFadeSeconds * sampleRate));
        reset();
    }

    void reset()
    {
        event = {};
        wet = 0.0f;
        interval = std::numeric_limits<std::int64_t>::min();
        pending = false;
        expected = -1.0;
        heldBefore = false;
        for (auto& f : filters)
            f.reset();
        outputGain = -1.0f;
    }

    void process(float* const* audio, int channels, int count, const Settings& s, const Clock& clock)
    {
        channels = std::min(channels, 2);
        if (channels <= 0 || count <= 0)
            return;
        const double bps = std::max(1.0e-9, clock.beatsPerSample);
        // A jump in the song (a loop, a click in the ruler): what was scheduled no longer stands.
        if (expected >= 0.0 && std::abs(clock.beat - expected) > bps * 2.0)
        {
            interval = std::numeric_limits<std::int64_t>::min();
            pending = false;
            event.active = false;
        }
        expected = clock.beat + bps * count;

        const double intervalBeats = kIntervals[(std::size_t) std::clamp(s.interval, 0, (int) kIntervals.size() - 1)].beats;
        const double gridBeats = kGrids[(std::size_t) std::clamp(s.grid, 0, (int) kGrids.size() - 1)].beats;
        // The start inside the interval, at least a sixteenth before its end.
        const double offsetBeats = std::min(0.25 * std::max(0, s.offset), std::max(0.0, intervalBeats - 0.25));
        const float target = std::pow(10.0f, s.outputDb / 20.0f);
        if (outputGain < 0.0f)
            outputGain = target;
        const float gainStep = (target - outputGain) / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const double beat = clock.beat + bps * i;
            // Half a sample early, so a line the beat lands on to within rounding counts from that sample.
            const auto n = (std::int64_t) std::floor((beat + bps * 0.5) / intervalBeats);
            if (n != interval)
            {
                interval = n;
                start = (double) n * intervalBeats + offsetBeats;
                // Arriving after the start (the song began mid-interval) waits for the next one.
                pending = dice(n, s.seed, 0) < (double) s.chance && beat <= start + bps * 0.5;
            }
            if (pending && beat >= start - bps * 0.5)
            {
                pending = false;
                begin(s, gridBeats, 0.25 * std::max(1, s.gate) / bps, bps, n);
            }
            // Held: from the next grid line on, until let go.
            if (s.hold && ! heldBefore)
                holdWaiting = true;
            if (! s.hold)
                holdWaiting = false;
            heldBefore = s.hold;
            if (holdWaiting
                && (std::int64_t) std::floor((beat + bps * 0.5) / gridBeats)
                       != (std::int64_t) std::floor((beat - bps * 0.5) / gridBeats))
            {
                holdWaiting = false;
                begin(s, gridBeats, -1.0, bps, n);
            }
            if (event.active && event.held && ! s.hold)
                event.active = false;
            if (event.active && event.gateLength > 0.0 && event.elapsed >= event.gateLength)
                event.active = false;

            const float wetTarget = event.active ? 1.0f : 0.0f;
            wet = wet < wetTarget ? std::min(wetTarget, wet + fadeStep) : std::max(wetTarget, wet - fadeStep);
            outputGain += gainStep;

            if (wet <= 0.0f && ! event.active)
            {
                if (s.mode == Mode::gate)
                    for (int c = 0; c < channels; ++c)
                        audio[c][i] = 0.0f;
                else if (std::abs(outputGain - 1.0f) > 1.0e-6f)
                    for (int c = 0; c < channels; ++c)
                        audio[c][i] *= outputGain;
                continue;
            }
            render(audio, channels, i, s);
        }
        outputGain = target;
        const double end = clock.beat + bps * count;
        position = (float) ((end - std::floor(end / intervalBeats) * intervalBeats) / intervalBeats);
    }

    /** For the display: where the song is in the current interval (0 to 1), and whether a repeat sounds. */
    float intervalPosition() const { return position; }
    bool repeating() const { return event.active; }
    /** The repeat playing: 0 is the live slice, 1 the first repeat. */
    int repeatNumber() const { return event.active ? (int) (event.elapsed / std::max(1.0, event.sliceLength)) : -1; }

private:
    struct Event
    {
        bool active = false, held = false, reversed = false;
        double elapsed = 0.0;     // samples since the start
        double sliceLength = 1.0; // samples
        double gateLength = -1.0; // samples; negative while held
        int recorded = 0;
    };

    void begin(const Settings& s, double gridBeats, double gateSamples, double bps, std::int64_t n)
    {
        // Variation: now and then a shorter or longer slice, the next or second step along the menu.
        int grid = std::clamp(s.grid, 0, (int) kGrids.size() - 1);
        if (dice(n, s.seed, 1) < (double) s.variation)
        {
            const double which = dice(n, s.seed, 2);
            grid = std::clamp(grid + (which < 0.4 ? 1 : which < 0.7 ? 2 : -1), 0, (int) kGrids.size() - 1);
            gridBeats = kGrids[(std::size_t) grid].beats;
        }
        event = {};
        event.active = true;
        event.held = gateSamples < 0.0;
        event.gateLength = gateSamples;
        event.sliceLength = std::clamp(gridBeats / bps, 32.0, (double) slices[0].size() - 2.0);
        event.reversed = dice(n, s.seed, 3) < (double) s.reverse;
        // The pitch and fade run over the gate, or over a bar while held.
        span = event.held ? 4.0 / bps : gateSamples;
        for (auto& f : filters)
            f.reset();
    }

    void render(float* const* audio, int channels, int i, const Settings& s)
    {
        const double length = event.sliceLength;
        const auto repeat = (int) (event.elapsed / length);
        const double p = event.elapsed - repeat * length;
        const float fade = (float) std::min(kFadeSeconds * rate, length * 0.25);
        // How far along the gate this repeat started: the pitch and fade step with each repeat.
        const double along = span > 0.0 ? std::min(1.0, repeat * length / span) : 0.0;
        const double speed = std::exp2(s.pitch * along / 12.0);
        double q = repeat == 0 ? p : p * speed;
        q -= std::floor(q / length) * length;
        if (repeat > 0 && event.reversed)
            q = length - 1.0 - q;
        q = std::clamp(q, 0.0, length - 1.0);
        auto edge = [fade](double x, double size) {
            return std::min(1.0f, (float) std::min(x, size - x) / std::max(1.0f, fade));
        };
        // The live slice fades only at its end, where the first repeat comes in.
        const float window = repeat == 0 ? std::min(1.0f, (float) (length - p) / std::max(1.0f, fade))
                                         : std::min(edge(p, length), edge(q, length));
        const float level = repeat == 0 ? 1.0f : 1.0f - s.decay * (float) along;

        // The filter sweeps over the whole gate, smoothly rather than per repeat.
        const double sweep = span > 0.0 ? std::min(1.0, event.elapsed / span) : 0.0;
        const float amount = std::clamp(s.filter, -1.0f, 1.0f);
        const bool filtering = std::abs(amount) > 0.001f;
        if (filtering && countdown-- <= 0)
        {
            countdown = 15;
            const double hz = amount < 0.0f ? 20000.0 * std::exp2(-9.0 * -amount * sweep)
                                            : 20.0 * std::exp2(9.0 * amount * sweep);
            g = (float) std::tan(3.141592653589793 * std::min(hz, 0.45 * rate) / rate);
        }

        for (int c = 0; c < channels; ++c)
        {
            const float live = audio[c][i];
            auto& slice = slices[(std::size_t) c];
            float repeated;
            if (repeat == 0)
            {
                slice[(std::size_t) std::min((int) p, (int) slice.size() - 1)] = live;
                repeated = live;
            }
            else
            {
                const auto a = (std::size_t) q;
                const float frac = (float) (q - (double) a);
                const auto b = std::min(a + 1, (std::size_t) (length - 1.0));
                repeated = slice[a] + frac * (slice[b] - slice[a]);
            }
            if (filtering)
            {
                const auto out = filters[(std::size_t) c].process(repeated, g, 1.2f);
                // Only the repeats are coloured; the live slice keeps the filter warm.
                if (repeat > 0)
                    repeated = amount < 0.0f ? out.low : out.high;
            }
            const float sound = repeated * window * level;
            float out = live;
            switch (s.mode)
            {
                // Written so the live slice comes out bit for bit.
                case Mode::insert: out = live + wet * (sound - live); break;
                case Mode::mix: out = live + (repeat > 0 ? sound * wet : 0.0f); break;
                case Mode::gate: out = sound * wet; break;
            }
            audio[c][i] = out * outputGain;
        }
        event.elapsed += 1.0;
    }

    double rate = 48000.0;
    std::array<std::vector<float>, 2> slices;
    std::array<SvfStage, 2> filters;
    Event event;
    double span = 1.0, start = 0.0, expected = -1.0;
    std::int64_t interval = 0;
    bool pending = false, heldBefore = false, holdWaiting = false;
    float wet = 0.0f, fadeStep = 0.01f, outputGain = 1.0f, g = 1.0f, position = 0.0f;
    int countdown = 0;
};
} // namespace tonwerkstutter
