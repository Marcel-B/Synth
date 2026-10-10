#pragma once

#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Tonwerk Groovebox's sound and clock, plain C++: eight drum voices synthesised from oscillators and noise (no
 * samples), and the step sequencer that plays them in time with the host. The processor feeds both from its
 * parameters; the tests drive them directly.
 */
namespace tonwerkgroove
{
inline constexpr int kTracks = 8;
inline constexpr int kSteps = 32;
inline constexpr int kPatterns = 8;
inline constexpr double kTwoPi = 6.283185307179586;

/** The tracks, in the order of the grid. Saved in projects as parameter ids, so the order never changes. */
enum class Instrument
{
    kick,
    snare,
    clap,
    lowTom,
    highTom,
    closedHat,
    openHat,
    cymbal
};

/** Menu orders, saved in projects: append only. */
enum class SeqMode
{
    off,
    host,
    free
};

enum class StepRate
{
    sixteenth,
    eighth,
    sixteenthTriplet,
    thirtySecond
};

struct TrackSettings
{
    /** Semitones from the instrument's own pitch. */
    float tune = 0.0f;
    /** Seconds to -60 dB. */
    float decay = 0.3f;
    /** The two sound knobs, 0 to 1; what they do depends on the instrument (see `Voice`). */
    float tone = 0.5f;
    float character = 0.5f;
    float levelDb = 0.0f;
    /** -1 left to 1 right. */
    float pan = 0.0f;
    /** Shares sent to the delay and the hall, 0 to 1. */
    float delaySend = 0.0f;
    float reverbSend = 0.0f;
    bool mute = false;
};

/** Each instrument's starting sound. */
inline TrackSettings defaultTrack(Instrument instrument)
{
    TrackSettings t;
    switch (instrument)
    {
        case Instrument::kick:
            t = { 0.0f, 0.5f, 0.45f, 0.3f };
            break;
        case Instrument::snare:
            t = { 0.0f, 0.25f, 0.5f, 0.5f };
            t.reverbSend = 0.25f;
            break;
        case Instrument::clap:
            t = { 0.0f, 0.3f, 0.4f, 0.5f };
            t.reverbSend = 0.35f;
            break;
        case Instrument::lowTom:
            t = { 0.0f, 0.45f, 0.4f, 0.25f };
            t.pan = -0.25f;
            t.reverbSend = 0.2f;
            break;
        case Instrument::highTom:
            t = { 0.0f, 0.4f, 0.4f, 0.25f };
            t.pan = 0.25f;
            t.reverbSend = 0.2f;
            break;
        case Instrument::closedHat:
            t = { 0.0f, 0.07f, 0.5f, 0.3f };
            t.pan = 0.15f;
            break;
        case Instrument::openHat:
            t = { 0.0f, 0.45f, 0.5f, 0.3f };
            t.pan = 0.15f;
            break;
        case Instrument::cymbal:
            t = { 0.0f, 1.8f, 0.5f, 0.4f };
            t.pan = -0.15f;
            t.reverbSend = 0.3f;
            break;
    }
    return t;
}

struct Settings
{
    Settings()
    {
        for (int i = 0; i < kTracks; ++i)
            track[(std::size_t) i] = defaultTrack((Instrument) i);
    }

    std::array<TrackSettings, kTracks> track;
    SeqMode seqMode = SeqMode::host;
    int pattern = 0;
    /** Steps before the pattern repeats, 1 to 32. */
    int length = 16;
    StepRate rate = StepRate::sixteenth;
    /** Where the second step of each pair falls within the pair: 0.5 straight, 0.75 dotted. */
    float swing = 0.5f;
    /** How much quieter a plain step is than an accented one: at 1, half as loud. */
    float accent = 0.5f;
    /** Saturation of the sum, 0 to 1; 0 leaves it untouched. */
    float drive = 0.0f;
    float masterDb = 0.0f;
};

/** One track of a pattern: bit `s` of `on` sets step `s`, the same bit of `accent` makes it loud. */
struct TrackPattern
{
    std::uint32_t on = 0;
    std::uint32_t accent = 0;

    bool operator==(const TrackPattern&) const = default;
};
using Pattern = std::array<TrackPattern, kTracks>;

/** A pattern track as text, one character a step: `.` off, `x` on, `X` accented. Spaces and `|` are left out. */
inline TrackPattern parseTrack(const char* text)
{
    TrackPattern t;
    int step = 0;
    for (const char* c = text; *c != 0 && step < kSteps; ++c)
    {
        if (*c == ' ' || *c == '|')
            continue;
        if (*c == 'x' || *c == 'X')
            t.on |= 1u << step;
        if (*c == 'X')
            t.accent |= 1u << step;
        ++step;
    }
    return t;
}

/** The velocity a step plays with. */
inline float stepVelocity(bool accented, float accentAmount) { return accented ? 1.0f : 1.0f - 0.5f * accentAmount; }

/** The track a MIDI note plays, as General MIDI's drum map lays them out; -1 for notes that play none. */
inline int trackForNote(int note)
{
    switch (note)
    {
        case 35: case 36: return (int) Instrument::kick;
        case 37: case 38: case 40: return (int) Instrument::snare;
        case 39: return (int) Instrument::clap;
        case 41: case 43: case 45: return (int) Instrument::lowTom;
        case 47: case 48: case 50: return (int) Instrument::highTom;
        case 42: case 44: return (int) Instrument::closedHat;
        case 46: return (int) Instrument::openHat;
        case 49: case 51: case 52: case 55: case 57: case 59: return (int) Instrument::cymbal;
        default: return -1;
    }
}

/** The note the editor and the README name for a track. */
inline int noteForTrack(int track)
{
    constexpr std::array<int, kTracks> notes { 36, 38, 39, 41, 48, 42, 46, 49 };
    return notes[(std::size_t) track];
}

/** Per-sample factor that takes a level to 1/1000 (-60 dB) in `seconds`. */
inline float fallFor(double seconds, double rate)
{
    return (float) std::exp(-6.907755278982137 / std::max(1.0, seconds * rate));
}

/** Per-sample factor of an exponential with time constant `seconds`. */
inline float tauFor(double seconds, double rate) { return (float) std::exp(-1.0 / std::max(1.0, seconds * rate)); }

inline float pitchRatio(float semitones) { return std::exp2(semitones / 12.0f); }

/**
 * One hit of one instrument. Every instrument is built the way the classic analog drum machines build theirs: a
 * sine whose pitch falls for the kick and the toms, a tuned body under filtered noise for the snare, bursts of
 * band-passed noise for the clap, six square waves at clashing ratios for the hats and the cymbal. The amplitude falls
 * exponentially, so `decay` is the time to -60 dB. Knobs act on a sounding hit; only the pitch sweep's depth and the
 * velocity are taken at the strike.
 *
 * - Kick: tone is the punch (how far the pitch starts above the note), character the click and drive of the beater.
 * - Snare: tone the brightness of the snares, character how much snare against drum body.
 * - Clap: tone the sharpness (a narrower band), character the spread of the hands.
 * - Toms: tone the pitch sweep, character the noise of the skin.
 * - Hats and cymbal: tone the brightness, character noise against metal.
 */
class Voice
{
public:
    void prepare(double sampleRate) { rate = sampleRate; }

    void trigger(Instrument which, float velocity, const TrackSettings& s)
    {
        instrument = which;
        gain = velocity;
        playing = true;
        fading = false;
        fade = 1.0f;
        time = 0;
        const bool tom = which == Instrument::lowTom || which == Instrument::highTom;
        amp = 1.0f;
        // Only the envelopes an instrument uses start; the others stay at 0, so the hit ends when its own have died.
        second = which == Instrument::snare || tom ? 1.0f : 0.0f;
        click = which == Instrument::kick || tom ? 1.0f : 0.0f;
        pitchEnv = 1.0f;
        burst = 1.0f;
        tail = 0.0f;
        phase = phase2 = 0.0;
        // The sweep's depth is set by the strike, as a harder hit stretches a skin further.
        sweep = sweepFor(s) * (0.6f + 0.4f * velocity);
        for (auto& o : metal)
            o.reset();
        filterA.reset();
        filterB.reset();
        filterC.reset();
    }

    /** Lets the hit die within a few milliseconds: a new hit on the same track, or the closed hat choking the open. */
    void release() { fading = playing; }
    bool active() const { return playing; }

    /** Adds `count` samples of this hit to `out`. */
    void render(float* out, int count, const TrackSettings& s)
    {
        if (! playing)
            return;
        const float fall = fallFor(s.decay, rate);
        const float fadeFall = fallFor(0.005, rate);
        const float base = pitchRatio(s.tune);
        setFilters(s, base);
        for (int i = 0; i < count; ++i)
        {
            float x = 0.0f;
            switch (instrument)
            {
                case Instrument::kick: x = kick(s, base, fall); break;
                case Instrument::snare: x = snare(s, base, fall); break;
                case Instrument::clap: x = clap(s, fall); break;
                case Instrument::lowTom: x = tom(s, base * 95.0f, fall); break;
                case Instrument::highTom: x = tom(s, base * 150.0f, fall); break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal: x = metallic(s, base, fall); break;
            }
            if (fading)
                fade *= fadeFall;
            out[i] += x * gain * fade;
            ++time;
        }
        if (fade < 1.0e-4f || (amp < 1.0e-5f && second < 1.0e-5f && click < 1.0e-5f && tail < 1.0e-5f && ! clapBursting()))
            playing = false;
    }

private:
    float sweepFor(const TrackSettings& s) const
    {
        switch (instrument)
        {
            // Up to four times the note at the start: from a soft thud to the zap of an electro kick.
            case Instrument::kick: return 0.3f + 3.7f * s.tone * s.tone;
            case Instrument::snare: return 0.15f;
            case Instrument::lowTom:
            case Instrument::highTom: return 0.05f + 0.95f * s.tone;
            case Instrument::clap:
            case Instrument::closedHat:
            case Instrument::openHat:
            case Instrument::cymbal: break;
        }
        return 0.0f;
    }

    void setFilters(const TrackSettings& s, float base)
    {
        using tonwerk::FilterType;
        switch (instrument)
        {
            case Instrument::kick:
                filterA.set(FilterType::highpass, 1500.0, 0.0, rate);
                break;
            case Instrument::snare:
                // The snares' rattle: noise from a few hundred hertz up to where tone opens it.
                filterA.set(FilterType::highpass, 400.0 * base, 0.0, rate);
                filterB.set(FilterType::lowpass, 2000.0 * std::exp2(3.0 * s.tone), 0.0, rate);
                break;
            case Instrument::clap:
                filterA.set(FilterType::bandpass, 1150.0 * base, 0.6 + 3.4 * s.tone, rate);
                filterB.set(FilterType::highpass, 500.0, 0.0, rate);
                break;
            case Instrument::lowTom:
            case Instrument::highTom:
                filterA.set(FilterType::lowpass, 3000.0 * base, 0.0, rate);
                break;
            case Instrument::closedHat:
            case Instrument::openHat:
            case Instrument::cymbal:
            {
                const bool crash = instrument == Instrument::cymbal;
                filterA.set(FilterType::bandpass, (crash ? 6000.0 : 9000.0) * base, crash ? 0.6 : 0.9, rate);
                filterB.set(FilterType::highpass, (crash ? 2500.0 : 4000.0) * std::exp2(1.6 * s.tone), 0.0, rate);
                filterC.set(FilterType::highpass, 400.0, 0.0, rate);
                break;
            }
        }
    }

    float sine(double& p, double hz)
    {
        const float value = (float) std::sin(kTwoPi * p);
        p += hz / rate;
        p -= std::floor(p);
        return value;
    }

    float kick(const TrackSettings& s, float base, float fall)
    {
        pitchEnv *= tauFor(0.02 + 0.03 * (1.0 - s.tone), rate);
        const double hz = 50.0 * base * (1.0 + sweep * pitchEnv);
        amp *= fall;
        click *= tauFor(0.0012, rate);
        const float body = sine(phase, hz) * amp;
        const float beater = (float) filterA.process(noise.next()) * click * s.character * 1.5f;
        float x = body + beater;
        // The character also drives the body into a rounder, louder square, as an overdriven kick does.
        const float drive = 1.0f + 3.0f * s.character;
        x = std::tanh(drive * x) / std::tanh(drive);
        return 0.8f * x;
    }

    float snare(const TrackSettings& s, float base, float fall)
    {
        pitchEnv *= tauFor(0.015, rate);
        const double hz = 185.0 * base * (1.0 + sweep * pitchEnv);
        // The drum's head rings shorter than the snares rattle.
        second *= fallFor(0.04 + 0.35 * s.decay, rate);
        amp *= fall;
        const float body = (sine(phase, hz) + 0.55f * sine(phase2, hz * 1.59)) * second;
        const float rattle = (float) filterB.process(filterA.process(noise.next())) * amp;
        return 0.5f * (body * (1.0f - 0.6f * s.character) + rattle * (0.4f + 1.6f * s.character));
    }

    bool clapBursting() const { return instrument == Instrument::clap && time < clapTailStart; }

    float clap(const TrackSettings& s, float fall)
    {
        // Three hands a few milliseconds apart, then the room: each burst rises at once and dies in a few ms.
        const int gap = (int) ((0.004 + 0.014 * s.character) * rate);
        clapTailStart = 3 * gap;
        if (time < clapTailStart)
        {
            if (gap > 0 && time % gap == 0)
                burst = 1.0f;
            burst *= tauFor(0.0035, rate);
        }
        else
        {
            if (time == clapTailStart)
                tail = 1.0f;
            burst *= tauFor(0.0035, rate);
            tail *= fall;
        }
        amp = std::max(burst, tail);
        const float n = (float) filterB.process(filterA.process(noise.next()));
        return 2.8f * n * (burst + tail);
    }

    float tom(const TrackSettings& s, float hz, float fall)
    {
        pitchEnv *= tauFor(0.08, rate);
        const double f = hz * (1.0 + sweep * pitchEnv);
        amp *= fall;
        second *= fallFor(0.5 * s.decay, rate);
        click *= fallFor(0.06, rate);
        const float body = sine(phase, f) * amp + 0.3f * sine(phase2, f * 1.5) * second;
        const float skin = (float) filterA.process(noise.next()) * click * s.character;
        return 0.6f * (body + skin);
    }

    float metallic(const TrackSettings& s, float base, float fall)
    {
        // Six squares at the clashing ratios the 808's hats are known for; band-passed, they ring like metal.
        static constexpr std::array<double, 6> hz { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        const double scale = instrument == Instrument::cymbal ? 0.85 * base : base;
        double sum = 0.0;
        for (std::size_t k = 0; k < metal.size(); ++k)
            sum += metal[k].next(tonwerk::Wave::square, hz[k] * scale, rate);
        const float source = (float) (sum / 6.0) * (1.0f - s.character) + (float) noise.next() * s.character;
        amp *= fall;
        const float shaped = (float) filterC.process(filterB.process(filterA.process(source)));
        return 2.2f * shaped * amp;
    }

    double rate = 48000.0;
    Instrument instrument = Instrument::kick;
    bool playing = false, fading = false;
    float fade = 1.0f, gain = 1.0f;
    int time = 0, clapTailStart = 0;
    float amp = 0.0f, second = 0.0f, pitchEnv = 0.0f, click = 0.0f, burst = 0.0f, tail = 0.0f, sweep = 0.0f;
    double phase = 0.0, phase2 = 0.0;
    std::array<tonwerk::BlepOscillator, 6> metal;
    tonwerk::Noise noise;
    tonwerk::Biquad filterA, filterB, filterC;
};

/**
 * The step clock: which steps start in a stretch of beats. Steps count from beat 0 of the song, so a pattern sits on
 * the host's bars wherever playback starts or loops; swing delays every second step.
 */
struct Clock
{
    static double stepBeats(StepRate rate)
    {
        switch (rate)
        {
            case StepRate::eighth: return 0.5;
            case StepRate::sixteenthTriplet: return 1.0 / 6.0;
            case StepRate::thirtySecond: return 0.125;
            case StepRate::sixteenth: break;
        }
        return 0.25;
    }

    /** The beat at which step `n` (counted from the song's start) sounds. */
    static double stepStart(long long n, double length, double late) { return (double) n * length + ((n & 1) != 0 ? late : 0.0); }

    /**
     * Calls `fire(offset, step)` for each step starting in [from, to) beats: `offset` in samples from `from`, `step`
     * the step of a pattern `length` steps long.
     */
    template <typename Fire>
    static void steps(double from, double to, double samplesPerBeat, int count, int length, StepRate rate, float swing,
                      Fire&& fire)
    {
        const double step = stepBeats(rate);
        const double late = (std::clamp((double) swing, 0.5, 0.75) - 0.5) * 2.0 * step;
        length = std::clamp(length, 1, kSteps);
        for (auto n = (long long) std::floor((from - late) / step) - 1;; ++n)
        {
            const double at = stepStart(n, step, late);
            if ((double) n * step >= to)
                break;
            if (at < from || at >= to)
                continue;
            const int offset = std::clamp((int) ((at - from) * samplesPerBeat), 0, std::max(0, count - 1));
            fire(offset, (int) (((n % length) + length) % length));
        }
    }
};

/** All eight tracks, two voices each so a new hit can start while the last one fades. */
class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        for (auto& track : tracks)
            for (auto& voice : track.voices)
                voice.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& track : tracks)
            for (auto& voice : track.voices)
            {
                voice = Voice();
                voice.prepare(rate);
            }
    }

    void trigger(int index, float velocity, const Settings& s)
    {
        if (index < 0 || index >= kTracks || s.track[(std::size_t) index].mute)
            return;
        auto& track = tracks[(std::size_t) index];
        track.voices[(std::size_t) track.current].release();
        track.current ^= 1;
        track.voices[(std::size_t) track.current].trigger((Instrument) index, std::clamp(velocity, 0.0f, 1.0f),
                                                           s.track[(std::size_t) index]);
        // The closed hat stops the open one, as on one pair of cymbals.
        if (index == (int) Instrument::closedHat)
            for (auto& voice : tracks[(std::size_t) Instrument::openHat].voices)
                voice.release();
        ++hits[(std::size_t) index];
    }

    /**
     * Adds `count` samples of every track: dry and panned into `left` and `right`, mono into the two sends.
     */
    void render(float* left, float* right, float* delaySend, float* reverbSend, int count, const Settings& s)
    {
        for (int done = 0; done < count;)
        {
            const int chunk = std::min(kChunk, count - done);
            for (std::size_t t = 0; t < tracks.size(); ++t)
            {
                auto& track = tracks[t];
                if (! track.voices[0].active() && ! track.voices[1].active())
                    continue;
                const auto& ts = s.track[t];
                std::fill_n(scratch.begin(), chunk, 0.0f);
                for (auto& voice : track.voices)
                    voice.render(scratch.data(), chunk, ts);
                const float level = kOutput * std::pow(10.0f, ts.levelDb / 20.0f);
                // Equal power, the middle as loud on each side as the track alone.
                const float angle = (std::clamp(ts.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
                const float toLeft = level * std::cos(angle) * 1.41421356f;
                const float toRight = level * std::sin(angle) * 1.41421356f;
                float peak = 0.0f;
                for (int i = 0; i < chunk; ++i)
                {
                    const float x = scratch[(std::size_t) i];
                    left[done + i] += x * toLeft;
                    right[done + i] += x * toRight;
                    delaySend[done + i] += x * level * ts.delaySend;
                    reverbSend[done + i] += x * level * ts.reverbSend;
                    peak = std::max(peak, std::abs(x * level));
                }
                track.peak = std::max(track.peak, peak);
            }
            done += chunk;
        }
    }

    /**
     * Saturates the sum when `drive` is up: tanh of the signal driven up to five times, brought back down by the square
     * root of that, so quiet parts come up about 7 dB at most while the peaks are rounded off below full scale. Mixed
     * in by `drive` itself, so the first turn of the knob does not jump.
     */
    static void saturate(float* samples, int count, float drive)
    {
        if (drive <= 0.0f)
            return;
        const float g = 1.0f + 4.0f * drive;
        const float back = 1.0f / std::sqrt(g);
        for (int i = 0; i < count; ++i)
            samples[i] += drive * (std::tanh(g * samples[i]) * back - samples[i]);
    }

    int activeVoices() const
    {
        int n = 0;
        for (const auto& track : tracks)
            for (const auto& voice : track.voices)
                n += voice.active() ? 1 : 0;
        return n;
    }

    /** The loudest sample of a track since the last call, for the editor's lights. */
    float takePeak(int index)
    {
        auto& track = tracks[(std::size_t) index];
        const float peak = track.peak;
        track.peak = 0.0f;
        return peak;
    }

    /** Hits a track has played since it was prepared; the tests count them. */
    int hitCount(int index) const { return hits[(std::size_t) index]; }

private:
    static constexpr int kChunk = 256;
    /** 4 dB of headroom: a kick, a snare and a hat on one step stay under full scale at their default levels. */
    static constexpr float kOutput = 0.63f;
    struct Track
    {
        std::array<Voice, 2> voices;
        int current = 0;
        float peak = 0.0f;
    };

    double rate = 48000.0;
    std::array<Track, kTracks> tracks;
    std::array<float, kChunk> scratch {};
    std::array<int, kTracks> hits {};
};
} // namespace tonwerkgroove
