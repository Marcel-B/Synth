#pragma once

#include "GrooveEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

/**
 * Tonwerk Drums' sound, plain C++: Tonwerk Groovebox's synthesised instruments without its sequencer, sixteen pads on
 * the notes of General MIDI's drum map. The first eight pads are the Groovebox's tracks, built and shaped exactly as
 * there; the other eight play the same models (`tonwerkgroove::Voice`) with constants of their own: a pedal hat, a
 * middle tom and a ride drawn from their machine's hat, toms and cymbal, and a rim, a cowbell, a conga, claves and a
 * shaker built only for this instrument.
 */
namespace tonwerkdrums
{
using tonwerkgroove::Instrument;
using tonwerkgroove::kMachines;
using tonwerkgroove::Machine;
using tonwerkgroove::TrackFilter;
using tonwerkgroove::TrackSettings;
using tonwerkgroove::Voice;
using tonwerkgroove::Voicing;

inline constexpr int kPads = 16;

/**
 * The pads. The first eight are the Groovebox's tracks in its order, so their parameter ids and its kits carry over.
 * Saved in projects as parameter ids, so the order never changes; the editor lays them out in its own (`kGrid`).
 */
enum class Pad
{
    kick,
    snare,
    clap,
    tomLow,
    tomHigh,
    hatClosed,
    hatOpen,
    cymbal,
    rim,
    hatPedal,
    tomMid,
    ride,
    cowbell,
    conga,
    clave,
    shaker
};

/** The pads' parameter id prefixes; the Groovebox's eight as there. Part of saved projects: never rename one. */
inline constexpr std::array<const char*, kPads> kPadIds { "kick",     "snare",   "clap",   "tomLow", "tomHigh", "hatClosed",
                                                          "hatOpen",  "cymbal",  "rim",    "hatPedal", "tomMid", "ride",
                                                          "cowbell",  "conga",   "clave",  "shaker" };
/** The pads' names in the editor and the host. */
inline constexpr std::array<const char*, kPads> kPadNames { "Bassdrum", "Snare",      "Clap",       "Tom tief",
                                                            "Tom hoch", "Hi-Hat zu",  "Hi-Hat offen", "Becken",
                                                            "Rim",      "Hi-Hat Pedal", "Tom mittel", "Ride",
                                                            "Cowbell",  "Conga",      "Claves",     "Shaker" };
/** What `tone` and `character` do on each pad, as the knobs are labelled; the model decides. */
inline constexpr std::array<std::pair<const char*, const char*>, kPads> kSoundKnobs { {
    { "Punch", "Klick" },
    { "Ton", "Snappy" },
    { "Schärfe", "Streuung" },
    { "Sweep", "Fell" },
    { "Sweep", "Fell" },
    { "Ton", "Rauschen" },
    { "Ton", "Rauschen" },
    { "Ton", "Rauschen" },
    { "Sweep", "Klick" },
    { "Ton", "Rauschen" },
    { "Sweep", "Fell" },
    { "Ton", "Rauschen" },
    { "Ton", "Rauschen" },
    { "Sweep", "Fell" },
    { "Sweep", "Fell" },
    { "Ton", "Rauschen" },
} };

/** The Groovebox instrument each pad is played on. */
inline constexpr std::array<Instrument, kPads> kModels {
    Instrument::kick,      Instrument::snare,   Instrument::clap,   Instrument::lowTom,  Instrument::highTom,
    Instrument::closedHat, Instrument::openHat, Instrument::cymbal, Instrument::highTom, Instrument::closedHat,
    Instrument::lowTom,    Instrument::cymbal,  Instrument::openHat, Instrument::lowTom, Instrument::highTom,
    Instrument::closedHat
};

/**
 * Each pad's note at the start, General MIDI's drum map (Logic's note names: 36 is C1): bass drum 1, side stick, the
 * acoustic snare, hand clap, low floor tom, hi-mid tom, the three hats, crash 1, ride 1, the low-mid tom, cowbell,
 * open high conga, claves and maracas.
 */
inline constexpr std::array<int, kPads> kNotes { 36, 38, 39, 41, 48, 42, 46, 49, 37, 44, 45, 51, 56, 63, 75, 70 };

/** The pads in the editor's four rows: drums, hats and cymbals, toms, percussion. */
inline constexpr std::array<Pad, kPads> kGrid { Pad::kick,      Pad::rim,    Pad::snare,   Pad::clap,
                                                Pad::hatClosed, Pad::hatPedal, Pad::hatOpen, Pad::cymbal,
                                                Pad::tomLow,    Pad::tomMid, Pad::tomHigh, Pad::ride,
                                                Pad::cowbell,   Pad::conga,  Pad::clave,   Pad::shaker };

/**
 * The pad a note plays, -1 for none: the pad set to that note, else the pad that General MIDI's neighbouring sound
 * falls to (electric snare to the snare, the other toms, crashes and rides, bongos and congas, woodblocks, cabasa,
 * tambourine), so a GM drum part plays whole. A note a pad was moved to is that pad's; the GM fallback only takes
 * notes no pad claims.
 */
inline int padForNote(int note, const std::array<int, kPads>& notes)
{
    for (int p = 0; p < kPads; ++p)
        if (notes[(std::size_t) p] == note)
            return p;
    switch (note)
    {
        case 35: return (int) Pad::kick;
        case 40: return (int) Pad::snare;
        case 43: return (int) Pad::tomLow;
        case 47: return (int) Pad::tomMid;
        case 50: return (int) Pad::tomHigh;
        case 52: case 55: case 57: return (int) Pad::cymbal;
        case 53: case 59: return (int) Pad::ride;
        case 54: case 69: case 82: return (int) Pad::shaker;
        case 60: case 61: case 62: case 64: return (int) Pad::conga;
        case 67: case 68: return (int) Pad::cowbell;
        case 76: case 77: return (int) Pad::clave;
        default: return -1;
    }
}

/** A note as Logic names it: C1 is 36, C3 is 60. */
inline int octaveOf(int note) { return note / 12 - 2; }

/** Each pad's starting sound; the Groovebox's eight as there. */
inline TrackSettings defaultPad(Pad pad)
{
    TrackSettings t;
    switch (pad)
    {
        case Pad::kick:
        case Pad::snare:
        case Pad::clap:
        case Pad::tomLow:
        case Pad::tomHigh:
        case Pad::hatClosed:
        case Pad::hatOpen:
        case Pad::cymbal:
            return tonwerkgroove::defaultTrack((Instrument) pad);
        case Pad::rim:
            t = { 0.0f, 0.06f, 0.3f, 0.5f };
            t.pan = 0.1f;
            t.reverbSend = 0.2f;
            break;
        case Pad::hatPedal:
            // Shorter and softer at its start than the closed hat: the pedal closing the pair.
            t = { -1.0f, 0.06f, 0.4f, 0.3f };
            t.attack = 0.002f;
            t.pan = 0.15f;
            break;
        case Pad::tomMid:
            t = { 0.0f, 0.42f, 0.4f, 0.25f };
            t.reverbSend = 0.2f;
            break;
        case Pad::ride:
            t = { 0.0f, 3.5f, 0.35f, 0.15f };
            t.pan = 0.3f;
            t.reverbSend = 0.2f;
            break;
        case Pad::cowbell:
            t = { 0.0f, 0.35f, 0.5f, 0.0f };
            t.pan = -0.2f;
            t.reverbSend = 0.15f;
            break;
        case Pad::conga:
            t = { 0.0f, 0.3f, 0.3f, 0.4f };
            t.pan = -0.35f;
            t.reverbSend = 0.15f;
            break;
        case Pad::clave:
            t = { 0.0f, 0.08f, 0.0f, 0.0f };
            t.pan = 0.35f;
            t.reverbSend = 0.2f;
            break;
        case Pad::shaker:
            t = { 0.0f, 0.07f, 0.6f, 1.0f };
            t.attack = 0.008f;
            t.pan = 0.3f;
            break;
    }
    return t;
}

/**
 * How a pad's instrument is built on `machine`. The Groovebox's eight take their machine's voicing as they do there.
 * The pedal hat, the middle tom, the ride and the shaker are their machine's hat, toms and cymbal changed by factors,
 * so they keep its character. The rim, cowbell, conga and claves have constants of their own; of the machine they take
 * its sampling (bits and rate) and how much louder or quieter its instruments are than Tonwerk's.
 */
inline Voicing padVoicing(Pad pad, Machine machine)
{
    const auto model = kModels[(std::size_t) pad];
    Voicing v = tonwerkgroove::voicingFor(machine, model);
    switch (pad)
    {
        case Pad::hatPedal:
            // A touch lower and duller than the closed hat, as two cymbals pressed together.
            v.band *= 0.8f;
            v.gain *= 1.2f;
            return v;
        case Pad::tomMid:
            // Between the machine's two toms.
            v.hz = std::sqrt(v.hz * tonwerkgroove::voicingFor(machine, Instrument::highTom).hz);
            return v;
        case Pad::ride:
            // Higher and narrower than the crash, so it pings rather than washes.
            v.hz *= 1.2f;
            v.band *= 1.35f;
            v.bandQ *= 1.5f;
            v.noiseHigh *= 1.6f;
            v.gain *= 0.9f;
            return v;
        case Pad::shaker:
            // Only noise, high up: seeds in a gourd.
            v.metalNoise = 1.0f;
            v.noiseHigh *= 1.4f;
            v.band *= 1.1f;
            v.gain *= 1.4f;
            return v;
        case Pad::rim:
        case Pad::cowbell:
        case Pad::conga:
        case Pad::clave:
            break;
        case Pad::kick:
        case Pad::snare:
        case Pad::clap:
        case Pad::tomLow:
        case Pad::tomHigh:
        case Pad::hatClosed:
        case Pad::hatOpen:
        case Pad::cymbal:
            return v;
    }

    const Voicing tonwerk = tonwerkgroove::tonwerkVoicing(model);
    Voicing own = tonwerk;
    switch (pad)
    {
        case Pad::rim:
            // The stick on the rim: two short tones far apart (as the classic analog rim shot's 455 and 1667 Hz) and
            // a click of noise.
            own.hz = 455.0f;
            own.ratio = 3.66f;
            own.second = 0.8f;
            own.sweep = 0.0f;
            own.sweepTone = 0.3f;
            own.sweepTime = 0.004f;
            own.clickTime = 0.012f;
            own.click = 1.2f;
            own.noiseHigh = 9000.0f;
            own.gain = 0.75f;
            break;
        case Pad::cowbell:
            // Two squares a fifth and a bit apart (540 and 800 Hz), band-passed, no noise at Rauschen 0.
            own.metal = { 540.0f, 800.0f, 540.0f, 800.0f, 540.0f, 800.0f };
            own.hz = 1.0f;
            own.band = 900.0f;
            own.bandQ = 1.0f;
            own.noiseHigh = 350.0f;
            own.noiseLow = 200.0f;
            own.metalNoise = 0.0f;
            own.gain = 0.6f;
            break;
        case Pad::conga:
            // A small skin tuned high: little sweep, a slap of noise, no second mode.
            own.hz = 330.0f;
            own.second = 0.0f;
            own.sweep = 0.06f;
            own.sweepTone = 0.3f;
            own.sweepTime = 0.02f;
            own.clickTime = 0.012f;
            own.click = 0.8f;
            own.noiseHigh = 5000.0f;
            own.gain = 0.7f;
            break;
        case Pad::clave:
            // Two hardwood sticks: a short, pure tone high up.
            own.hz = 2500.0f;
            own.second = 0.0f;
            own.sweep = 0.0f;
            own.sweepTone = 0.15f;
            own.sweepTime = 0.003f;
            own.click = 0.0f;
            own.gain = 0.5f;
            break;
        case Pad::kick:
        case Pad::snare:
        case Pad::clap:
        case Pad::tomLow:
        case Pad::tomHigh:
        case Pad::hatClosed:
        case Pad::hatOpen:
        case Pad::cymbal:
        case Pad::hatPedal:
        case Pad::tomMid:
        case Pad::ride:
        case Pad::shaker:
            break;
    }
    own.bits = v.bits;
    own.rate = v.rate;
    own.gain *= v.gain / tonwerk.gain;
    return own;
}

struct Settings
{
    Settings()
    {
        for (int p = 0; p < kPads; ++p)
        {
            pad[(std::size_t) p] = defaultPad((Pad) p);
            note[(std::size_t) p] = kNotes[(std::size_t) p];
        }
    }

    std::array<TrackSettings, kPads> pad;
    /** The note that plays each pad. */
    std::array<int, kPads> note;
    /** Saturation of the sum, 0 to 1. */
    float drive = 0.0f;
    float masterDb = 0.0f;
};

/** All sixteen pads, two voices each so a new hit can start while the last one fades. */
class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        reset();
        // The same variations in every run from here, so a bounce repeats.
        dice = tonwerk::Noise();
    }

    void reset()
    {
        for (auto& pad : pads)
            for (auto& voice : pad.voices)
            {
                voice = Voice();
                voice.prepare(rate);
            }
    }

    void trigger(int index, float velocity, const Settings& s)
    {
        if (index < 0 || index >= kPads || s.pad[(std::size_t) index].mute)
            return;
        auto& pad = pads[(std::size_t) index];
        const auto& ps = s.pad[(std::size_t) index];
        const float detune = ps.random * (float) dice.next();
        const float levelDb = 3.0f * ps.random * (float) dice.next();
        pad.voices[(std::size_t) pad.current].release();
        pad.current ^= 1;
        pad.voices[(std::size_t) pad.current].trigger(kModels[(std::size_t) index], padVoicing((Pad) index, ps.machine),
                                                      std::clamp(velocity, 0.0f, 1.0f), ps, detune, levelDb);
        // The closed and the pedal hat stop the open one, as on one pair of cymbals.
        if (index == (int) Pad::hatClosed || index == (int) Pad::hatPedal)
            for (auto& voice : pads[(std::size_t) Pad::hatOpen].voices)
                voice.release();
        ++hits[(std::size_t) index];
    }

    /** Adds `count` samples of every pad: dry and panned into `left` and `right`, mono into the two sends. */
    void render(float* left, float* right, float* delaySend, float* reverbSend, int count, const Settings& s)
    {
        for (int done = 0; done < count;)
        {
            const int chunk = std::min(kChunk, count - done);
            for (std::size_t p = 0; p < pads.size(); ++p)
            {
                auto& pad = pads[p];
                if (! pad.voices[0].active() && ! pad.voices[1].active())
                    continue;
                const auto& ps = s.pad[p];
                std::fill_n(scratch.begin(), chunk, 0.0f);
                for (auto& voice : pad.voices)
                    voice.render(scratch.data(), chunk, ps);
                const float level = kOutput * std::pow(10.0f, ps.levelDb / 20.0f);
                // Equal power, the middle as loud on each side as the pad alone.
                const float angle = (std::clamp(ps.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
                const float toLeft = level * std::cos(angle) * 1.41421356f;
                const float toRight = level * std::sin(angle) * 1.41421356f;
                float peak = 0.0f;
                for (int i = 0; i < chunk; ++i)
                {
                    const float x = scratch[(std::size_t) i];
                    left[done + i] += x * toLeft;
                    right[done + i] += x * toRight;
                    delaySend[done + i] += x * level * ps.delaySend;
                    reverbSend[done + i] += x * level * ps.reverbSend;
                    peak = std::max(peak, std::abs(x * level));
                }
                pad.peak = std::max(pad.peak, peak);
            }
            done += chunk;
        }
    }

    int activeVoices() const
    {
        int n = 0;
        for (const auto& pad : pads)
            for (const auto& voice : pad.voices)
                n += voice.active() ? 1 : 0;
        return n;
    }

    /** The loudest sample of a pad since the last call, for the editor's lights. */
    float takePeak(int index)
    {
        auto& pad = pads[(std::size_t) index];
        const float peak = pad.peak;
        pad.peak = 0.0f;
        return peak;
    }

    /** Hits a pad has played since it was prepared; the tests count them. */
    int hitCount(int index) const { return hits[(std::size_t) index]; }

private:
    static constexpr int kChunk = 256;
public:
    /**
     * 5.5 dB under the Groovebox (whose 0.63 keeps a kick, a snare and a hat on one step under full scale): a drum part
     * played from MIDI stacks more at once, a crash on the kick, fills and flams, and at full velocity where the
     * Groovebox's plain steps play softer. The tests' General MIDI beat comes out at the Groovebox's loudness.
     */
    static constexpr float kOutput = 0.63f * 0.5309f;

private:
    struct PadVoices
    {
        std::array<Voice, 2> voices;
        int current = 0;
        float peak = 0.0f;
    };

    double rate = 48000.0;
    std::array<PadVoices, kPads> pads;
    std::array<float, kChunk> scratch {};
    std::array<int, kPads> hits {};
    tonwerk::Noise dice;
};
} // namespace tonwerkdrums
