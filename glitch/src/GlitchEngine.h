#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace chromeglitch
{
/** The grid's note values in beats: 1/8, 1/16, 1/32. */
inline constexpr std::array<double, 3> kGridBeats { 0.5, 0.25, 0.125 };
/** The tempo when the host gives none, as in the standalone app. */
inline constexpr double kDefaultBpm = 120.0;

/** The knobs as the engine reads them each block; times in milliseconds, pitch in semitones. */
struct Settings
{
    /** The macro: 0 leaves the voice untouched, 1 is the chrome falling apart. Scales every chance and the crusher. */
    float amount = 0.5f;
    float stutterChance = 0.5f;
    float stutterMs = 50.0f;
    int repeats = 4;
    /** Each repeat shorter than the last, so a syllable stutters faster and faster. */
    float accelerate = 0.0f;
    float dropoutChance = 0.3f;
    float dropoutMs = 100.0f;
    float crush = 0.5f;
    float pitchChance = 0.3f;
    float pitchRange = 7.0f;
    bool sync = true;
    int division = 1;
    /** In sync, how often a glitch also falls between the grid lines; also how much slice lengths wander. */
    float chaos = 0.3f;
    /** Loops the last slice for as long as it is on: the voice hangs. */
    bool freeze = false;
    float mix = 1.0f;
};

struct Transport
{
    bool playing = false;
    bool hasPosition = false;
    /** The position at the block's first sample, in quarter notes. */
    double ppq = 0.0;
    double bpm = kDefaultBpm;
};

/** A small generator of its own, so a seed gives the same glitches on every platform. */
class Random
{
public:
    void seed(std::uint32_t value) { state = 0x9E3779B97F4A7C15ull ^ (std::uint64_t) value * 0xBF58476D1CE4E5B9ull; }

    float uniform()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return (float) (state >> 40) / (float) (1u << 24);
    }

private:
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
};

/**
 * A voice processor whose "chrome" fails: at each tick (a grid line of the host's tempo in sync, random moments of
 * 40-250 ms apart otherwise) it may replay the last few tens of milliseconds several times (stutter, each repeat
 * possibly pitched), cut the voice out for a moment (dropout), and crush it while it does. Works without latency: a
 * stutter repeats what was just heard, so "body" becomes "bo-bo-body".
 *
 * Every switch between the voice and a glitch crossfades over 1.5 ms, and every repeat has 1.5 ms edges, so the
 * glitches chop without clicking. The engine draws its dice from a fixed seed each time the transport starts, so a
 * bounce from the same position comes out the same.
 */
class GlitchEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr double kHistorySeconds = 1.0;
    static constexpr double kMaxSliceSeconds = 0.25;
    static constexpr double kFadeSeconds = 0.0015;
    static constexpr double kMinRepeatSeconds = 0.008;
    static constexpr std::uint32_t kSeed = 0xC0FFEE;

    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& channel : history)
            channel.assign((std::size_t) std::ceil(kHistorySeconds * rate) + 1, 0.0f);
        for (auto& channel : slice)
            channel.assign((std::size_t) std::ceil(kMaxSliceSeconds * rate) + 2, 0.0f);
        fadeSamples = std::max(1.0, kFadeSeconds * rate);
        reset();
    }

    void reset()
    {
        for (auto& channel : history)
            std::fill(channel.begin(), channel.end(), 0.0f);
        writePos = 0;
        written = 0;
        state = State::idle;
        frozen = false;
        eventMix = 0.0f;
        crushDepth = 0.0f;
        holdCounter = 0.0f;
        held = {};
        lastGridIndex = kNoGrid;
        wasPlaying = false;
        rng.seed(kSeed);
        untilRandomTick = 0;
    }

    enum class Event { none, stutter, dropout, freeze };

    /** True while a stutter, dropout or freeze sounds. */
    bool isGlitching() const { return state != State::idle; }
    /**
     * How many glitches have started so far, and the kind of the latest. A glitch can begin and end inside one block,
     * so a display that only asked `isGlitching()` after each block would miss it.
     */
    std::uint32_t eventsStarted() const { return started; }
    Event lastEvent() const { return last; }

    void process(float* const* channels, int numChannels, int numSamples, const Settings& s, const Transport& t)
    {
        if (history[0].empty())
            return;
        const int used = std::clamp(numChannels, 0, kMaxChannels);
        if (used == 0)
            return;

        if (t.playing && ! wasPlaying)
        {
            // Same start, same dice: a bounce sounds like the playback before it.
            rng.seed(kSeed);
            untilRandomTick = 0;
            lastGridIndex = kNoGrid;
        }
        wasPlaying = t.playing;

        const bool gridActive = s.sync && t.playing && t.hasPosition;
        if (! gridActive)
            lastGridIndex = kNoGrid;
        const double beatsPerSample = std::max(1.0, t.bpm) / 60.0 / sampleRate;
        const double gridBeats = kGridBeats[(std::size_t) std::clamp(s.division, 0, (int) kGridBeats.size() - 1)];
        const float amount = std::clamp(s.amount, 0.0f, 1.0f);
        const float mix = std::clamp(s.mix, 0.0f, 1.0f);
        const float fadeStep = (float) (1.0 / fadeSamples);
        // The crusher's depth glides over about 3 ms, so it sets in fast but without a step.
        const float crushGlide = (float) (1.0 - std::exp(-1.0 / (0.003 * sampleRate)));
        const auto historySize = (int) history[0].size();

        if (s.freeze && ! frozen)
            startStutter(s, used, true);
        else if (! s.freeze && frozen)
        {
            frozen = false;
            state = State::idle;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            std::array<float, kMaxChannels> in {};
            for (int ch = 0; ch < kMaxChannels; ++ch)
                in[(std::size_t) ch] = channels[std::min(ch, used - 1)][i];
            for (int ch = 0; ch < kMaxChannels; ++ch)
                history[(std::size_t) ch][(std::size_t) writePos] = in[(std::size_t) ch];
            writePos = (writePos + 1) % historySize;
            written = std::min(written + 1, historySize);

            bool tick = false;
            if (gridActive)
            {
                const auto index = (std::int64_t) std::floor((t.ppq + i * beatsPerSample) / gridBeats);
                if (index != lastGridIndex)
                {
                    tick = lastGridIndex != kNoGrid;
                    lastGridIndex = index;
                }
            }
            if (--untilRandomTick <= 0)
            {
                untilRandomTick = (int) ((0.04 + 0.21 * rng.uniform()) * sampleRate);
                // Free, every random moment counts; in sync only the chaos share, which makes the off-grid glitches.
                if (! gridActive || rng.uniform() < s.chaos)
                    tick = true;
            }

            if (tick && state == State::idle && ! frozen)
            {
                if (rng.uniform() < s.stutterChance * amount)
                    startStutter(s, used, false);
                else if (rng.uniform() < s.dropoutChance * amount)
                {
                    state = State::dropout;
                    ++started;
                    last = Event::dropout;
                    dropoutRemaining = std::max(1, (int) (s.dropoutMs * 0.001 * sampleRate));
                }
            }

            std::array<float, kMaxChannels> event {};
            if (state == State::stutter)
            {
                const auto i0 = (int) readPos;
                const auto frac = (float) (readPos - i0);
                const int i1 = (i0 + 1) % sliceLength;
                const double edge = std::min(fadeSamples, repeatLength / 4.0);
                const auto window = (float) std::min({ 1.0, repeatPos / edge, (repeatLength - repeatPos) / edge });
                for (std::size_t ch = 0; ch < kMaxChannels; ++ch)
                {
                    const float a = slice[ch][(std::size_t) i0];
                    const float b = slice[ch][(std::size_t) i1];
                    event[ch] = (a + (b - a) * frac) * window;
                }
                readPos = std::min(readPos + playbackRate, (double) (sliceLength - 1));
                if (++repeatPos >= repeatLength)
                    nextRepeat(s);
            }
            else if (state == State::dropout)
            {
                if (--dropoutRemaining <= 0)
                    state = State::idle;
            }

            const float target = state == State::idle ? 0.0f : 1.0f;
            eventMix = eventMix < target ? std::min(target, eventMix + fadeStep) : std::max(target, eventMix - fadeStep);

            // Between glitches the crusher only nibbles (growing with the amount); during one it bites in full.
            const float crushTarget = std::clamp(s.crush, 0.0f, 1.0f)
                                      * (state == State::idle ? 0.35f * amount * amount : amount);
            crushDepth += (crushTarget - crushDepth) * crushGlide;
            const bool crushing = crushDepth > 0.001f;
            bool sampleNow = true;
            float levels = 0.0f;
            if (crushing)
            {
                // 16 bits down to 4, and holding each sample for up to 16.
                levels = std::exp2(15.0f - 12.0f * crushDepth);
                const float hold = 1.0f + 15.0f * crushDepth * crushDepth;
                holdCounter += 1.0f;
                sampleNow = holdCounter >= hold;
                if (sampleNow)
                    holdCounter -= hold;
            }

            for (std::size_t ch = 0; ch < (std::size_t) used; ++ch)
            {
                float wet = in[ch] + (event[ch] - in[ch]) * eventMix;
                if (crushing)
                {
                    if (sampleNow)
                        held[ch] = std::round(wet * levels) / levels;
                    wet = held[ch];
                }
                channels[ch][i] = in[ch] + (wet - in[ch]) * mix;
            }
        }
    }

private:
    enum class State { idle, stutter, dropout };
    static constexpr std::int64_t kNoGrid = std::numeric_limits<std::int64_t>::min();

    void startStutter(const Settings& s, int used, bool freeze)
    {
        const double wander = 1.0 + s.chaos * (rng.uniform() - 0.5);
        const int capacity = (int) slice[0].size() - 2;
        const int length = std::clamp((int) (s.stutterMs * 0.001 * wander * sampleRate),
                                      (int) (kMinRepeatSeconds * sampleRate), capacity);
        // Right after a start there is not enough voice to repeat yet; a freeze tries again next block.
        if (length > written)
            return;
        const auto size = (int) history[0].size();
        const int start = (writePos - length + size) % size;
        for (std::size_t ch = 0; ch < kMaxChannels; ++ch)
        {
            const auto& from = history[std::min(ch, (std::size_t) used - 1)];
            for (int k = 0; k < length; ++k)
                slice[ch][(std::size_t) k] = from[(std::size_t) ((start + k) % size)];
        }
        sliceLength = length;
        repeatSpan = length;
        repeatLength = length;
        repeatPos = 0;
        readPos = 0.0;
        repeatIndex = 0;
        playbackRate = 1.0;
        state = State::stutter;
        frozen = freeze;
        ++started;
        last = freeze ? Event::freeze : Event::stutter;
    }

    void nextRepeat(const Settings& s)
    {
        ++repeatIndex;
        if (! frozen && repeatIndex >= std::max(1, s.repeats))
        {
            state = State::idle;
            return;
        }
        repeatSpan = std::max((int) (kMinRepeatSeconds * sampleRate),
                              (int) (repeatSpan * (1.0f - 0.3f * std::clamp(s.accelerate, 0.0f, 1.0f))));
        repeatPos = 0;
        readPos = 0.0;
        // The first repeat keeps the pitch, so the syllable is recognisable before it jumps.
        playbackRate = 1.0;
        if (rng.uniform() < s.pitchChance * std::clamp(s.amount, 0.0f, 1.0f))
            playbackRate = std::exp2(std::round((rng.uniform() * 2.0f - 1.0f) * s.pitchRange) / 12.0);
        // Pitched up, the slice runs out early; the repeat ends there instead of jumping back to its start mid-window.
        repeatLength = std::max(1, std::min(repeatSpan, (int) ((sliceLength - 1) / playbackRate) + 1));
    }

    double sampleRate = 44100.0;
    double fadeSamples = 1.0;
    std::array<std::vector<float>, kMaxChannels> history;
    std::array<std::vector<float>, kMaxChannels> slice;
    int writePos = 0;
    int written = 0;

    State state = State::idle;
    bool frozen = false;
    std::uint32_t started = 0;
    Event last = Event::none;
    int sliceLength = 1;
    /** A repeat's length before pitch: the slice, shortened by each acceleration. */
    int repeatSpan = 1;
    /** This repeat's length in output samples. */
    int repeatLength = 1;
    int repeatPos = 0;
    int repeatIndex = 0;
    double readPos = 0.0;
    double playbackRate = 1.0;
    int dropoutRemaining = 0;
    float eventMix = 0.0f;

    float crushDepth = 0.0f;
    float holdCounter = 0.0f;
    std::array<float, kMaxChannels> held {};

    std::int64_t lastGridIndex = kNoGrid;
    bool wasPlaying = false;
    int untilRandomTick = 0;
    Random rng;
};
} // namespace chromeglitch
