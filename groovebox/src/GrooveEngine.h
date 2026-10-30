#pragma once

#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

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

enum class TrackFilter
{
    off,
    lowpass,
    highpass,
    bandpass
};

/**
 * The drum machine a track's instrument is modelled on, from the menu under the grid. Menu order, saved in projects:
 * append only. The names lean on the classics without naming them; `tonwerk` is the Groovebox's own sound, as before.
 */
enum class Machine
{
    tonwerk,
    boom88,
    punch99,
    mini66,
    neon77,
    kompakt78,
    studio80,
    street81,
    sechseck
};
inline constexpr int kMachines = 9;

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

    // The shaping every track has besides its instrument's own knobs. At these defaults it leaves the sound as it was,
    // so kits and projects from before keep sounding the same.

    /** Seconds the hit takes to rise; 0 strikes at once. */
    float attack = 0.0f;
    /** Seconds the main level stays up before it falls: a gated sound. */
    float hold = 0.0f;
    /** The mod envelope: semitones it bends the whole instrument at the strike, falling to 0 in `modDecay`. */
    float pitchMod = 0.0f;
    /** Seconds the mod envelope takes to -60 dB. */
    float modDecay = 0.15f;
    TrackFilter filter = TrackFilter::off;
    float cutoff = 2000.0f;
    /** 0 to 1. */
    float resonance = 0.0f;
    /** -1 to 1: how far the mod envelope moves the cutoff, six octaves at the ends. */
    float filterMod = 0.0f;
    /** Saturation of the track, 0 to 1. */
    float drive = 0.0f;
    /** Bit depth, 16 and above untouched. */
    float bits = 16.0f;
    /** The rate the track is held at, in Hz; 48000 and above untouched. */
    float crush = 48000.0f;
    /** How much the velocity sets the level, 0 to 1: at 0 every hit is as loud as the loudest. */
    float velocity = 1.0f;
    /** How much each hit differs from the last in pitch (a semitone at 1) and level (3 dB), 0 to 1. */
    float random = 0.0f;
    /** The machine the instrument is built like; taken at the strike. */
    Machine machine = Machine::tonwerk;
    /** How much the dice change this track's pattern, 0 to 1, added to `Settings::roll`; see `rollStep`. */
    float roll = 0.0f;
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
    /** How much the dice change every track's pattern, 0 to 1; a track's own `roll` adds to it. */
    float roll = 0.0f;
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

/** A step after the dice: whether it plays, and whether accented. */
struct RolledStep
{
    bool plays;
    bool accent;
};

/**
 * Rolls the dice for step `n` (counted from the song's start, so every bar rolls anew) of `track` at `amount` 0 to 1,
 * the way the DrumBrute Impact's randomness varies a pattern: a step may fall out (half of them at 1), an empty step
 * of a track that plays at all may sound (a quarter at 1), and an accent may come or go (three in ten at 1). The dice
 * are a hash of the step and track, so a bounce, a loop or a start in the middle of the song plays the same rolls.
 */
inline RolledStep rollStep(bool on, bool accent, bool trackPlays, float amount, long long n, int track)
{
    if (amount <= 0.0f)
        return { on, accent };
    // SplitMix64 on the step and track: cheap, and neighbouring steps roll independently.
    constexpr std::uint64_t golden = 0x9E3779B97F4A7C15u, other = 0xD1B54A32D192ED03u;
    constexpr std::uint64_t mixA = 0xBF58476D1CE4E5B9u, mixB = 0x94D049BB133111EBu;
    const auto steps = static_cast<std::uint64_t>(n), lane = static_cast<std::uint64_t>(track + 1);
    auto z = steps * golden + lane * other;
    z = (z ^ (z >> 30)) * mixA;
    z = (z ^ (z >> 27)) * mixB;
    z ^= z >> 31;
    const float play = (float) (z & 0xFFFFFF) / 16777216.0f;
    const float flip = (float) ((z >> 24) & 0xFFFFFF) / 16777216.0f;
    const float a = std::min(amount, 1.0f);
    const bool plays = on ? play >= 0.5f * a : (trackPlays && play < 0.25f * a);
    return { plays, flip < 0.3f * a ? ! accent : accent };
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
 * The constants an instrument is built from, the ones the machines differ in. `Machine::tonwerk` keeps the Groovebox's
 * own values, so a track on Tonwerk sounds exactly as it did before the machines came; the knobs mean the same on
 * every machine and move these constants, they do not replace them.
 */
struct Voicing
{
    /** The body's pitch at Stimmung 0, in Hz; for the hats and the cymbal the factor on the metal's frequencies. */
    float hz = 50.0f;
    /** A second body oscillator (snare, toms): its ratio to the first and its level. */
    float ratio = 1.5f, second = 0.0f;
    /**
     * The pitch sweep: its depth (how far the start lies above the note, in multiples of it), `sweep` with the tone
     * knob down and `sweep + sweepTone` with it up, and its time constant, `sweepTime + sweepSlow` with the knob down
     * and `sweepTime` with it up.
     */
    float sweep = 0.0f, sweepTone = 0.0f, sweepTime = 0.02f, sweepSlow = 0.0f;
    /** The beater (kick, a time constant) or the stick (toms, the time to -60 dB): its time, its level and the highpass
     * on its noise. */
    float clickTime = 0.0012f, click = 0.0f, clickHz = 1500.0f;
    /** How hard Klick drives the kick's body. */
    float bodyDrive = 3.0f;
    /** Noise filters: the snare's rattle band, the toms' lowpass (`noiseHigh`), the metal's two highpasses. */
    float noiseLow = 400.0f, noiseHigh = 2000.0f;
    /** How long the snare's head rings before Abklingen adds to it, in seconds. */
    float head = 0.04f;
    /** The band the clap and the metal pass, and its Q (the clap's at Schärfe 0). */
    float band = 1150.0f, bandQ = 0.6f;
    /** The clap: its hands, their gap at Streuung 0 and what Streuung adds to it. */
    int hands = 3;
    float gap = 0.004f, spread = 0.014f;
    /** Hats and cymbal: the six squares at Stimmung 0, and the noise in the mix before Rauschen adds its share. */
    std::array<float, 6> metal { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    float metalNoise = 0.0f;
    /** The samples a sampling machine played from, bits and rate; 16 and 48000 leave the sound untouched. */
    float bits = 16.0f, rate = 48000.0f;
    float gain = 1.0f;
};

/** The Groovebox's own instruments. */
inline Voicing tonwerkVoicing(Instrument instrument)
{
    Voicing v;
    switch (instrument)
    {
        case Instrument::kick:
            v.sweep = 0.3f;
            v.sweepTone = 3.7f;
            v.sweepTime = 0.02f;
            v.sweepSlow = 0.03f;
            v.click = 1.5f;
            v.gain = 0.8f;
            break;
        case Instrument::snare:
            v.hz = 185.0f;
            v.ratio = 1.59f;
            v.second = 0.55f;
            v.sweep = 0.15f;
            v.sweepTime = 0.015f;
            v.gain = 0.5f;
            break;
        case Instrument::clap:
            v.noiseLow = 500.0f;
            v.gain = 2.8f;
            break;
        case Instrument::lowTom:
        case Instrument::highTom:
            v.hz = instrument == Instrument::lowTom ? 95.0f : 150.0f;
            v.ratio = 1.5f;
            v.second = 0.3f;
            v.sweep = 0.05f;
            v.sweepTone = 0.95f;
            v.sweepTime = 0.08f;
            v.clickTime = 0.06f;
            v.click = 1.0f;
            v.noiseHigh = 3000.0f;
            v.gain = 0.6f;
            break;
        case Instrument::closedHat:
        case Instrument::openHat:
        case Instrument::cymbal:
        {
            const bool crash = instrument == Instrument::cymbal;
            v.hz = crash ? 0.85f : 1.0f;
            v.band = crash ? 6000.0f : 9000.0f;
            v.bandQ = crash ? 0.6f : 0.9f;
            v.noiseHigh = crash ? 2500.0f : 4000.0f;
            v.noiseLow = 400.0f;
            v.gain = 2.2f;
            break;
        }
    }
    return v;
}

/** Each machine's instruments against Tonwerk's, in dB, so a machine is as loud as the next (see the tests). */
inline constexpr std::array<std::array<float, kTracks>, kMachines> kMachineLevels { {
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 2.6f, -1.0f, 0.9f, 0.6f, 0.4f, 0.5f, 0.5f, 0.7f },
    { -1.1f, -2.2f, -0.4f, -0.1f, -0.1f, -4.6f, -4.8f, -3.8f },
    { 0.9f, -2.4f, -0.6f, 0.0f, 0.1f, 0.8f, 0.8f, 0.4f },
    { 0.5f, -1.7f, -0.5f, -0.3f, -0.1f, -5.4f, -5.4f, -4.4f },
    { 2.1f, -1.1f, -0.3f, -0.2f, 0.1f, -6.7f, -6.9f, -4.1f },
    { 0.4f, -2.1f, -1.6f, 0.0f, 0.0f, -7.8f, -8.0f, -5.5f },
    { -0.2f, -1.4f, 0.3f, 0.3f, 0.1f, -4.3f, -4.5f, -3.7f },
    { -0.7f, -0.2f, 1.4f, -0.4f, -0.1f, -3.3f, -3.6f, -2.4f },
} };

/**
 * Each machine's instruments, leaning on what made the classic it is named after sound as it did. All of them are
 * synthesised; the sampling machines of the eighties are held at their samples' bits and rate.
 */
inline Voicing voicingFor(Machine machine, Instrument instrument)
{
    Voicing v = tonwerkVoicing(instrument);
    const bool low = instrument == Instrument::lowTom;
    const bool hat = instrument == Instrument::closedHat || instrument == Instrument::openHat;
    const bool crash = instrument == Instrument::cymbal;
    auto samples = [&v](float bits, float rate) {
        v.bits = bits;
        v.rate = rate;
    };
    switch (machine)
    {
        case Machine::tonwerk:
            break;

        // Boom 88: the analog boom. A deep kick that barely bends and has no beater, a snare of two ringing tones
        // under bright noise, a clap of four hands, toms that ring like sines, and the six squares of the hats.
        case Machine::boom88:
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 47.0f;
                    v.sweep = 0.12f;
                    v.sweepTone = 1.2f;
                    v.sweepTime = 0.03f;
                    v.sweepSlow = 0.02f;
                    v.click = 0.35f;
                    v.bodyDrive = 1.0f;
                    break;
                case Instrument::snare:
                    v.hz = 180.0f;
                    v.ratio = 1.83f;
                    v.second = 0.8f;
                    v.sweep = 0.05f;
                    v.noiseLow = 650.0f;
                    v.noiseHigh = 2500.0f;
                    break;
                case Instrument::clap:
                    v.hands = 4;
                    v.gap = 0.007f;
                    v.spread = 0.008f;
                    v.band = 1000.0f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 88.0f : 140.0f;
                    v.second = 0.1f;
                    v.sweep = 0.04f;
                    v.sweepTone = 0.35f;
                    v.click = 0.4f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.bandQ = crash ? 0.8f : 1.1f;
                    break;
            }
            break;

        // Punch 99: the house machine. A kick that drops fast from high up with a hard beater, a tight snare of two
        // tones and a lot of noise, toms with a short sweep, and hats and cymbal from six-bit samples.
        case Machine::punch99:
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 54.0f;
                    v.sweep = 0.9f;
                    v.sweepTone = 3.1f;
                    v.sweepTime = 0.01f;
                    v.sweepSlow = 0.015f;
                    v.click = 2.6f;
                    v.clickTime = 0.002f;
                    v.clickHz = 3000.0f;
                    v.bodyDrive = 5.0f;
                    break;
                case Instrument::snare:
                    v.hz = 195.0f;
                    v.ratio = 1.66f;
                    v.second = 0.6f;
                    v.sweep = 0.35f;
                    v.sweepTime = 0.012f;
                    v.noiseLow = 500.0f;
                    v.noiseHigh = 4000.0f;
                    v.head = 0.03f;
                    break;
                case Instrument::clap:
                    v.hands = 4;
                    v.gap = 0.005f;
                    v.spread = 0.006f;
                    v.band = 1300.0f;
                    v.bandQ = 0.9f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 100.0f : 160.0f;
                    v.sweep = 0.25f;
                    v.sweepTone = 0.9f;
                    v.sweepTime = 0.04f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.metalNoise = 0.35f;
                    v.band = crash ? 7000.0f : 10000.0f;
                    samples(6.0f, 32000.0f);
                    break;
            }
            break;

        // Mini 66: the small one. A short thin kick, a bright snare that is mostly noise, high toms, and hats
        // brighter and tighter than the big brother's.
        case Machine::mini66:
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 62.0f;
                    v.sweep = 0.4f;
                    v.sweepTone = 1.0f;
                    v.sweepTime = 0.012f;
                    v.sweepSlow = 0.01f;
                    v.click = 0.8f;
                    v.bodyDrive = 1.5f;
                    break;
                case Instrument::snare:
                    v.hz = 230.0f;
                    v.ratio = 1.5f;
                    v.second = 0.35f;
                    v.noiseLow = 800.0f;
                    v.noiseHigh = 4500.0f;
                    v.head = 0.025f;
                    break;
                case Instrument::clap:
                    v.band = 1500.0f;
                    v.bandQ = 1.0f;
                    v.gap = 0.003f;
                    v.spread = 0.008f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 120.0f : 180.0f;
                    v.second = 0.15f;
                    v.sweep = 0.06f;
                    v.sweepTone = 0.4f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.hz *= 1.2f;
                    v.band = crash ? 8000.0f : 11000.0f;
                    v.bandQ = crash ? 0.8f : 1.2f;
                    v.noiseHigh = crash ? 3500.0f : 5500.0f;
                    break;
            }
            break;

        // Neon 77: the first all-sample machine of the house. Clean, bright and a little thin, from eight-bit samples
        // at a low rate, the hats from six.
        case Machine::neon77:
            samples(hat || crash ? 6.0f : 8.0f, 25000.0f);
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 58.0f;
                    v.sweep = 0.7f;
                    v.sweepTone = 1.5f;
                    v.sweepTime = 0.012f;
                    v.sweepSlow = 0.01f;
                    v.click = 2.0f;
                    v.bodyDrive = 2.0f;
                    break;
                case Instrument::snare:
                    v.hz = 210.0f;
                    v.second = 0.6f;
                    v.noiseHigh = 3500.0f;
                    break;
                case Instrument::clap:
                    v.band = 1200.0f;
                    v.gap = 0.005f;
                    v.spread = 0.007f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 110.0f : 170.0f;
                    v.sweep = 0.3f;
                    v.sweepTone = 0.6f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.metalNoise = 0.45f;
                    v.band = crash ? 6500.0f : 8000.0f;
                    break;
            }
            break;

        // Kompakt 78: the rhythm box of the late seventies. A soft thump, a hollow snare, toms that sound like bongos,
        // and a hiss of filtered noise for the hats.
        case Machine::kompakt78:
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 62.0f;
                    v.sweep = 0.05f;
                    v.sweepTone = 0.2f;
                    v.sweepTime = 0.01f;
                    v.click = 0.0f;
                    v.bodyDrive = 0.5f;
                    break;
                case Instrument::snare:
                    v.hz = 290.0f;
                    v.ratio = 2.03f;
                    v.second = 0.3f;
                    v.sweep = 0.05f;
                    v.noiseLow = 1200.0f;
                    v.noiseHigh = 3000.0f;
                    v.head = 0.02f;
                    break;
                case Instrument::clap:
                    v.band = 1800.0f;
                    v.bandQ = 1.5f;
                    v.hands = 2;
                    v.gap = 0.003f;
                    v.spread = 0.006f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 170.0f : 250.0f;
                    v.second = 0.0f;
                    v.sweep = 0.02f;
                    v.sweepTone = 0.1f;
                    v.click = 0.3f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.hz *= 1.4f;
                    v.metalNoise = 0.6f;
                    v.band = crash ? 8000.0f : 11000.0f;
                    v.bandQ = 1.2f;
                    break;
            }
            break;

        // Studio 80: the sampled studio kit of the early eighties. A kick with a big beater, a fat snare with a long
        // rattle, a clap of many hands, toms with a stick, mostly noise in the hats, all from eight bits at 28 kHz.
        case Machine::studio80:
            samples(8.0f, 28000.0f);
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 58.0f;
                    v.sweep = 1.0f;
                    v.sweepTone = 2.0f;
                    v.sweepTime = 0.008f;
                    v.sweepSlow = 0.01f;
                    v.click = 3.0f;
                    v.clickTime = 0.0025f;
                    v.clickHz = 2500.0f;
                    v.bodyDrive = 2.0f;
                    break;
                case Instrument::snare:
                    v.hz = 190.0f;
                    v.ratio = 1.47f;
                    v.second = 0.7f;
                    v.noiseLow = 300.0f;
                    v.noiseHigh = 3500.0f;
                    v.head = 0.06f;
                    break;
                case Instrument::clap:
                    v.hands = 5;
                    v.gap = 0.003f;
                    v.spread = 0.006f;
                    v.band = 1400.0f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 85.0f : 130.0f;
                    v.sweep = 0.25f;
                    v.sweepTone = 0.8f;
                    v.click = 1.5f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.metal = { 317.0f, 457.0f, 593.0f, 731.0f, 874.0f, 1043.0f };
                    v.metalNoise = 0.6f;
                    v.band = crash ? 6000.0f : 8000.0f;
                    v.bandQ = 0.5f;
                    break;
            }
            break;

        // Street 81: the hip-hop box. Hard and short, a kick that knocks, a fat snare, low toms, all eight bit at
        // 24 kHz.
        case Machine::street81:
            samples(8.0f, 24000.0f);
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 52.0f;
                    v.sweep = 0.6f;
                    v.sweepTone = 2.0f;
                    v.sweepTime = 0.012f;
                    v.sweepSlow = 0.012f;
                    v.click = 2.2f;
                    v.bodyDrive = 4.0f;
                    break;
                case Instrument::snare:
                    v.hz = 175.0f;
                    v.second = 0.9f;
                    v.noiseLow = 350.0f;
                    v.noiseHigh = 2500.0f;
                    v.head = 0.07f;
                    break;
                case Instrument::clap:
                    v.hands = 4;
                    v.gap = 0.005f;
                    v.spread = 0.008f;
                    v.band = 1000.0f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 80.0f : 125.0f;
                    v.sweep = 0.15f;
                    v.sweepTone = 0.7f;
                    v.click = 1.2f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.metalNoise = 0.4f;
                    v.band = crash ? 6000.0f : 7500.0f;
                    break;
            }
            break;

        // Sechseck: the hexagonal pads of the electronic kit. Everything sweeps: the kick, the snare's tone, and the
        // toms falling an octave and more, the sound of the eighties' drum fills.
        case Machine::sechseck:
            switch (instrument)
            {
                case Instrument::kick:
                    v.hz = 58.0f;
                    v.sweep = 1.6f;
                    v.sweepTone = 2.4f;
                    v.sweepTime = 0.035f;
                    v.sweepSlow = 0.03f;
                    v.click = 1.0f;
                    v.bodyDrive = 2.0f;
                    break;
                case Instrument::snare:
                    v.hz = 220.0f;
                    v.second = 0.25f;
                    v.sweep = 1.2f;
                    v.sweepTime = 0.03f;
                    v.noiseLow = 500.0f;
                    v.noiseHigh = 2200.0f;
                    break;
                case Instrument::clap:
                    v.hands = 2;
                    v.band = 900.0f;
                    break;
                case Instrument::lowTom:
                case Instrument::highTom:
                    v.hz = low ? 90.0f : 145.0f;
                    v.second = 0.1f;
                    v.sweep = 1.2f;
                    v.sweepTone = 1.8f;
                    v.sweepTime = 0.12f;
                    v.click = 0.8f;
                    break;
                case Instrument::closedHat:
                case Instrument::openHat:
                case Instrument::cymbal:
                    v.hz *= 0.9f;
                    v.metalNoise = 0.25f;
                    break;
            }
            break;
    }
    // Each machine's instrument about as loud as Tonwerk's (measured by the tests), so switching keeps the mix.
    v.gain *= std::pow(10.0f, kMachineLevels[(std::size_t) machine][(std::size_t) instrument] / 20.0f);
    return v;
}

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
 *
 * After the instrument every track has the same shaping (`TrackSettings` from `attack` on): a mod envelope that bends
 * the pitch and sweeps the filter, saturation, bit and rate reduction, a multimode filter, and attack and hold on the
 * level. Hold keeps the main level up (the snares, not the snare's head; a clap's tail after its hands).
 */
class Voice
{
public:
    void prepare(double sampleRate) { rate = sampleRate; }

    /** `detune` (semitones) and `levelDb` vary this one hit, as the track's `random` asks. */
    void trigger(Instrument which, float velocity, const TrackSettings& s, float detune = 0.0f, float levelDb = 0.0f)
    {
        instrument = which;
        voicing = voicingFor(s.machine, which);
        gain = (1.0f - s.velocity * (1.0f - velocity)) * std::pow(10.0f, levelDb / 20.0f);
        variation = pitchRatio(detune);
        playing = true;
        fading = false;
        fade = 1.0f;
        rising = 0.0f;
        time = 0;
        const bool tom = which == Instrument::lowTom || which == Instrument::highTom;
        amp = 1.0f;
        // Only the envelopes an instrument uses start; the others stay at 0, so the hit ends when its own have died.
        second = which == Instrument::snare || tom ? 1.0f : 0.0f;
        click = which == Instrument::kick || tom ? 1.0f : 0.0f;
        pitchEnv = 1.0f;
        modEnv = 1.0f;
        burst = 1.0f;
        tail = 0.0f;
        phase = phase2 = 0.0;
        // The first sample is taken at once, so a crushed hit starts on its strike.
        crushPhase = 1.0;
        held = 0.0f;
        // The sweep's depth is set by the strike, as a harder hit stretches a skin further.
        sweep = sweepFor(s) * (0.6f + 0.4f * velocity);
        for (auto& o : metal)
            o.reset();
        filterA.reset();
        filterB.reset();
        filterC.reset();
        shaper.reset();
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
        const float modFall = fallFor(s.modDecay, rate);
        const float tuned = pitchRatio(s.tune) * variation;
        const float rise = s.attack > 0.0f ? (float) (1.0 / std::max(1.0, s.attack * rate)) : 1.0f;
        holdFor = (int) (s.hold * rate);
        const Shape shape(s, voicing, rate);
        const bool bends = std::abs(s.pitchMod) > 1.0e-3f;
        const bool sweeps = shape.filters && std::abs(s.filterMod) > 1.0e-4f;
        for (int start = 0; start < count; start += kControl)
        {
            const int end = std::min(count, start + kControl);
            // The mod envelope moves pitch and cutoff every few samples; when it moves neither, the filters are set once.
            const float base = bends ? tuned * pitchRatio(s.pitchMod * modEnv) : tuned;
            if (start == 0 || bends)
                setFilters(s, base);
            if (shape.filters && (start == 0 || sweeps))
                setShaper(s);
            for (int i = start; i < end; ++i)
            {
                const float level = time < holdFor ? 1.0f : fall;
                float x = 0.0f;
                switch (instrument)
                {
                    case Instrument::kick: x = kick(s, base, level); break;
                    case Instrument::snare: x = snare(s, base, level); break;
                    case Instrument::clap: x = clap(s, fall); break;
                    case Instrument::lowTom:
                    case Instrument::highTom: x = tom(s, base * voicing.hz, level); break;
                    case Instrument::closedHat:
                    case Instrument::openHat:
                    case Instrument::cymbal: x = metallic(s, base, level); break;
                }
                x = shapeSample(x, shape);
                if (fading)
                    fade *= fadeFall;
                rising = std::min(1.0f, rising + rise);
                modEnv *= modFall;
                out[i] += x * gain * fade * rising;
                ++time;
            }
        }
        if (fade < 1.0e-4f || (amp < 1.0e-5f && second < 1.0e-5f && click < 1.0e-5f && tail < 1.0e-5f && ! clapBursting()))
            playing = false;
    }

private:
    float sweepFor(const TrackSettings& s) const
    {
        switch (instrument)
        {
            // Tonwerk's kick starts up to four times above the note: from a soft thud to the zap of an electro kick.
            case Instrument::kick: return voicing.sweep + voicing.sweepTone * s.tone * s.tone;
            case Instrument::snare: return voicing.sweep;
            case Instrument::lowTom:
            case Instrument::highTom: return voicing.sweep + voicing.sweepTone * s.tone;
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
                filterA.set(FilterType::highpass, voicing.clickHz, 0.0, rate);
                break;
            case Instrument::snare:
                // The snares' rattle: noise from a few hundred hertz up to where tone opens it.
                filterA.set(FilterType::highpass, voicing.noiseLow * base, 0.0, rate);
                filterB.set(FilterType::lowpass, voicing.noiseHigh * std::exp2(3.0 * s.tone), 0.0, rate);
                break;
            case Instrument::clap:
                filterA.set(FilterType::bandpass, voicing.band * base, voicing.bandQ + 3.4 * s.tone, rate);
                filterB.set(FilterType::highpass, voicing.noiseLow, 0.0, rate);
                break;
            case Instrument::lowTom:
            case Instrument::highTom:
                filterA.set(FilterType::lowpass, voicing.noiseHigh * base, 0.0, rate);
                break;
            case Instrument::closedHat:
            case Instrument::openHat:
            case Instrument::cymbal:
                filterA.set(FilterType::bandpass, voicing.band * base, voicing.bandQ, rate);
                filterB.set(FilterType::highpass, voicing.noiseHigh * std::exp2(1.6 * s.tone), 0.0, rate);
                filterC.set(FilterType::highpass, voicing.noiseLow, 0.0, rate);
                break;
        }
    }

    /** The track's shaping, worked out once a block. */
    struct Shape
    {
        // The machine's samples and the track's crusher are one stage: the coarser of the two counts.
        Shape(const TrackSettings& s, const Voicing& v, double rate)
            : drive(s.drive > 0.0f),
              // Driven up to ten times and brought back by the square root, as the sum's drive; faded in over the first
              // quarter of the knob, so its first turn does not jump.
              gain(1.0f + 9.0f * s.drive),
              back(1.0f / std::sqrt(gain)),
              wet(std::min(1.0f, 4.0f * s.drive)),
              crushes(std::min(s.crush, v.rate) < 48000.0f && std::min(s.crush, v.rate) < rate),
              crushStep(std::max(1.0, (double) std::min(s.crush, v.rate)) / rate),
              quantises(std::min(s.bits, v.bits) < 16.0f),
              steps(std::exp2(std::max(1.0f, std::min(s.bits, v.bits)) - 1.0f)),
              filters(s.filter != TrackFilter::off)
        {
        }

        bool drive;
        float gain, back, wet;
        bool crushes;
        double crushStep;
        bool quantises;
        float steps;
        bool filters;
    };

    float shapeSample(float x, const Shape& shape)
    {
        if (shape.drive)
            x += shape.wet * (std::tanh(shape.gain * x) * shape.back - x);
        // Rate before bits, as an old sampler takes a sample and then rounds it; the filter after both, so it can smooth
        // the steps away.
        if (shape.crushes)
        {
            crushPhase += shape.crushStep;
            if (crushPhase >= 1.0)
            {
                crushPhase -= std::floor(crushPhase);
                held = x;
            }
            x = held;
        }
        if (shape.quantises)
            x = std::round(x * shape.steps) / shape.steps;
        if (shape.filters)
            x = (float) shaper.process(x);
        return x;
    }

    void setShaper(const TrackSettings& s)
    {
        using tonwerk::FilterType;
        const double hz = s.cutoff * std::exp2(6.0 * s.filterMod * modEnv);
        // Lowpass and highpass take their resonance in decibels (Web Audio's biquad): -3 dB is flat, 18 dB rings.
        const double db = -3.0 + 21.0 * s.resonance;
        switch (s.filter)
        {
            case TrackFilter::lowpass: shaper.set(FilterType::lowpass, hz, db, rate); break;
            case TrackFilter::highpass: shaper.set(FilterType::highpass, hz, db, rate); break;
            case TrackFilter::bandpass: shaper.set(FilterType::bandpass, hz, 0.7 + 14.0 * s.resonance * s.resonance, rate); break;
            case TrackFilter::off: break;
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
        pitchEnv *= tauFor(voicing.sweepTime + voicing.sweepSlow * (1.0 - s.tone), rate);
        const double hz = voicing.hz * base * (1.0 + sweep * pitchEnv);
        amp *= fall;
        click *= tauFor(voicing.clickTime, rate);
        const float body = sine(phase, hz) * amp;
        const float beater = (float) filterA.process(noise.next()) * click * s.character * voicing.click;
        float x = body + beater;
        // The character also drives the body into a rounder, louder square, as an overdriven kick does.
        const float drive = 1.0f + voicing.bodyDrive * s.character;
        x = std::tanh(drive * x) / std::tanh(drive);
        return voicing.gain * x;
    }

    float snare(const TrackSettings& s, float base, float fall)
    {
        pitchEnv *= tauFor(voicing.sweepTime, rate);
        const double hz = voicing.hz * base * (1.0 + sweep * pitchEnv);
        // The drum's head rings shorter than the snares rattle.
        second *= fallFor(voicing.head + 0.35 * s.decay, rate);
        amp *= fall;
        const float body = (sine(phase, hz) + voicing.second * sine(phase2, hz * voicing.ratio)) * second;
        const float rattle = (float) filterB.process(filterA.process(noise.next())) * amp;
        return voicing.gain * (body * (1.0f - 0.6f * s.character) + rattle * (0.4f + 1.6f * s.character));
    }

    bool clapBursting() const { return instrument == Instrument::clap && time < clapTailStart; }

    float clap(const TrackSettings& s, float fall)
    {
        // A few hands (three on Tonwerk) milliseconds apart, then the room: each burst rises at once and dies in a few ms.
        const int gap = (int) ((voicing.gap + voicing.spread * s.character) * rate);
        clapTailStart = voicing.hands * gap;
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
            if (time >= clapTailStart + holdFor)
                tail *= fall;
        }
        amp = std::max(burst, tail);
        const float n = (float) filterB.process(filterA.process(noise.next()));
        return voicing.gain * n * (burst + tail);
    }

    float tom(const TrackSettings& s, float hz, float fall)
    {
        pitchEnv *= tauFor(voicing.sweepTime, rate);
        const double f = hz * (1.0 + sweep * pitchEnv);
        amp *= fall;
        second *= fallFor(0.5 * s.decay, rate);
        click *= fallFor(voicing.clickTime, rate);
        const float body = sine(phase, f) * amp + voicing.second * sine(phase2, f * voicing.ratio) * second;
        const float skin = (float) filterA.process(noise.next()) * click * s.character * voicing.click;
        return voicing.gain * (body + skin);
    }

    float metallic(const TrackSettings& s, float base, float fall)
    {
        // Six squares at clashing ratios (Tonwerk's are the ones the classic analog hats are known for); band-passed,
        // they ring like metal.
        const double scale = voicing.hz * base;
        double sum = 0.0;
        for (std::size_t k = 0; k < metal.size(); ++k)
            sum += metal[k].next(tonwerk::Wave::square, voicing.metal[k] * scale, rate);
        const float noisy = std::min(1.0f, voicing.metalNoise + (1.0f - voicing.metalNoise) * s.character);
        const float source = (float) (sum / 6.0) * (1.0f - noisy) + (float) noise.next() * noisy;
        amp *= fall;
        const float shaped = (float) filterC.process(filterB.process(filterA.process(source)));
        return voicing.gain * shaped * amp;
    }

    /** Samples between settings of the mod envelope's pitch and cutoff. */
    static constexpr int kControl = 32;

    double rate = 48000.0;
    Instrument instrument = Instrument::kick;
    Voicing voicing;
    bool playing = false, fading = false;
    float fade = 1.0f, gain = 1.0f, variation = 1.0f, rising = 1.0f;
    int time = 0, clapTailStart = 0, holdFor = 0;
    float amp = 0.0f, second = 0.0f, pitchEnv = 0.0f, click = 0.0f, burst = 0.0f, tail = 0.0f, sweep = 0.0f;
    float modEnv = 0.0f, held = 0.0f;
    double phase = 0.0, phase2 = 0.0, crushPhase = 1.0;
    std::array<tonwerk::BlepOscillator, 6> metal;
    tonwerk::Noise noise;
    tonwerk::Biquad filterA, filterB, filterC, shaper;
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
     * the step of a pattern `length` steps long; `fire(offset, step, n)` also gets the step counted from beat 0.
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
            const int inPattern = (int) (((n % length) + length) % length);
            if constexpr (std::is_invocable_v<Fire, int, int, long long>)
                fire(offset, inPattern, n);
            else
                fire(offset, inPattern);
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
        // The same variations in every run from here, so a bounce repeats.
        dice = tonwerk::Noise();
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
        const auto& ts = s.track[(std::size_t) index];
        const float detune = ts.random * (float) dice.next();
        const float levelDb = 3.0f * ts.random * (float) dice.next();
        track.voices[(std::size_t) track.current].release();
        track.current ^= 1;
        track.voices[(std::size_t) track.current].trigger((Instrument) index, std::clamp(velocity, 0.0f, 1.0f), ts,
                                                           detune, levelDb);
        // The closed hat stops the open one, as on one pair of cymbals.
        if (index == (int) Instrument::closedHat)
            for (auto& voice : tracks[(std::size_t) Instrument::openHat].voices)
                voice.release();
        ++hits[(std::size_t) index];
    }

    /**
     * Adds `count` samples of every track: dry and panned into `left` and `right`, mono into the two sends. With
     * `trackOut` (left and right of each track, `2 * kTracks` pointers), a track whose pair is set goes there instead
     * of into `left` and `right` (the separate outputs); its sends still feed the shared delay and hall.
     */
    void render(float* left, float* right, float* delaySend, float* reverbSend, int count, const Settings& s,
                float* const* trackOut = nullptr)
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
                float* outLeft = left;
                float* outRight = right;
                if (trackOut != nullptr && trackOut[2 * t] != nullptr)
                {
                    outLeft = trackOut[2 * t];
                    outRight = trackOut[2 * t + 1];
                }
                float peak = 0.0f;
                for (int i = 0; i < chunk; ++i)
                {
                    const float x = scratch[(std::size_t) i];
                    outLeft[done + i] += x * toLeft;
                    outRight[done + i] += x * toRight;
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
    tonwerk::Noise dice;
};
} // namespace tonwerkgroove
