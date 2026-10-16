#pragma once

#include "BassEngine.h"
#include "WaveDsp.h"
#include "dsp/Oscillators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Tonwerk Neuro's sound, plain C++: one voice for the Reese and neuro basses of drum and bass and dubstep. Two
 * sawtooths a few cents apart (the Reese: their beating makes the slow phasing roll), a two-operator FM pair with
 * feedback (the neuro grit, its index moved by an envelope and the LFO), a sine sub that stays clean, then drive and
 * a filter per side, moved by the mod envelope and a tempo LFO (the wobble). Borrows `Adsr`, `SvfStage`, `Comb`,
 * `lfoShape`, `shape` and the divisions from Tonwerk Wavetable's `WaveDsp.h`, the ladder from Tonwerk Bass and
 * `polyBlep` from `src/dsp/Oscillators.h`.
 */
namespace tonwerkneuro
{
using tonwerkwave::DistortionMode;
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;

inline constexpr double kPi = 3.141592653589793;
inline constexpr int kHeld = 16;
/** The FM index at full `fmIndex`, as Tonwerk FM's `kMaxIndex`. */
inline constexpr float kMaxIndex = 8.0f;

/** Modulator to carrier ratios; the order of the menu, saved in projects: append only. */
struct Ratio
{
    const char* name;
    double value;
};
inline constexpr std::array<Ratio, 9> kRatios { { { "0.25", 0.25 },
                                                  { "0.5", 0.5 },
                                                  { "1", 1.0 },
                                                  { "1.5", 1.5 },
                                                  { "2", 2.0 },
                                                  { "3", 3.0 },
                                                  { "4", 4.0 },
                                                  { "5", 5.0 },
                                                  { "7", 7.0 } } };

/** The filter types; the order of the menu, saved in projects: append only. */
enum class FilterType
{
    lowpass,
    bandpass,
    notch,
    comb,
};
inline constexpr std::array<const char*, 4> kFilterNames { "Tiefpass", "Bandpass", "Notch", "Kamm" };
inline constexpr std::array<const char*, 4> kDriveNames { "Weich", "Hart", "Falter", "Röhre" };

/** One sound, in the parameters' units except times, which are seconds. */
struct Settings
{
    float reese = 0.8f;        // the two saws' level, 0 to 1
    float detune = 18.0f;      // cents between them
    float width = 0.6f;        // how far apart the saws sit left and right
    float fm = 0.0f;           // the FM pair's level
    int ratio = 2;             // `kRatios` index: 1
    float fmIndex = 0.3f;      // 0 to 1, times `kMaxIndex`
    float feedback = 0.0f;     // the modulator on itself
    float sub = 0.5f;          // a sine, clean, under everything
    int subOctave = -1;        // 0 or -1
    int tune = 0;              // semitones

    DistortionMode driveMode = DistortionMode::soft;
    float drive = 0.3f;        // 0 to 1, up to 30 dB into the curve

    FilterType filterType = FilterType::lowpass;
    float cutoff = 400.0f;     // Hz
    float resonance = 0.3f;
    float envCutoff = 0.4f;    // -1 to 1, times 6 octaves
    float envFm = 0.0f;        // 0 to 1 of the index added by the mod envelope

    EnvSettings amp { 0.003f, 0.3f, 1.0f, 0.12f };
    EnvSettings mod { 0.001f, 0.35f, 0.0f, 0.2f };

    LfoShape lfoShape = LfoShape::sine;
    bool lfoSync = true;
    float lfoRate = 2.0f;
    int lfoDivision = 9;       // 1/8
    bool lfoRetrigger = true;
    float lfoCutoff = 0.0f;    // 0 to 1, times 5 octaves
    float lfoFm = 0.0f;        // 0 to 1 of the index

    float glide = 0.04f;       // s
    bool legato = true;
    int bendRange = 2;
    float masterDb = -6.0f;
};

struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/** Drive and filter for one side, after the oscillators. */
class Side
{
public:
    void prepare(double sampleRate)
    {
        comb.prepare(sampleRate);
        reset();
    }
    void reset()
    {
        ladder.reset();
        svf.reset();
        comb.reset();
    }

    float process(float x, const Settings& s, float driveGain, float driveMakeup, float g, float k, float a1,
                  float combDelay, float combFeedback)
    {
        float y = s.drive > 0.0f ? tonwerkwave::shape(s.driveMode, driveGain * x) * driveMakeup : x;
        switch (s.filterType)
        {
            case FilterType::bandpass:
                svf.process(y, g, k, a1);
                return k * svf.band;
            case FilterType::notch:
                svf.process(y, g, k, a1);
                return svf.low + svf.high;
            case FilterType::comb: return std::sqrt(1.0f - combFeedback * combFeedback) * comb.process(y, combDelay, combFeedback);
            case FilterType::lowpass:
            default: return ladder.process(y, g, 4.0f * std::clamp(s.resonance, 0.0f, 1.0f) * 0.98f);
        }
    }

private:
    tonwerkbass::Ladder ladder;
    tonwerkwave::SvfStage svf;
    tonwerkwave::Comb comb;
};

/**
 * The voice. Mono: a key pressed while another is held glides to it (legato keeps the envelopes running), releasing it
 * glides back to the newest key still held. Renders stereo: the saws spread by `width`, everything else in the middle.
 */
class Engine
{
public:
    static constexpr int kControl = 16;

    void prepare(double rate)
    {
        sampleRate = rate;
        ampEnv.prepare(rate);
        modEnv.prepare(rate);
        for (auto& side : sides)
            side.prepare(rate);
        dcCoefficient = (float) std::exp(-2.0 * kPi * 12.0 / rate);
        reset();
    }

    void reset()
    {
        heldCount = 0;
        ampEnv.reset();
        modEnv.reset();
        for (auto& side : sides)
            side.reset();
        sawA = 0.0;
        sawB = 0.37;
        carrier = modulator = subPhase = 0.0;
        lastMod = lastMod2 = 0.0f;
        lfoPhase = 0.0;
        dcIn = { 0.0f, 0.0f };
        dcOut = { 0.0f, 0.0f };
    }

    void noteOn(int note, const Settings& s)
    {
        const bool held = heldCount > 0;
        remove(note);
        if (heldCount < kHeld)
            notes[(std::size_t) heldCount++] = note;
        target = note;
        if (! held || ! ampEnv.isActive())
            pitch = note;
        if (! held || ! s.legato)
        {
            ampEnv.noteOn();
            modEnv.noteOn();
            if (s.lfoRetrigger && ! s.lfoSync)
                lfoPhase = 0.0;
            if (s.lfoRetrigger)
                retriggered = true;
        }
    }

    void noteOff(int note)
    {
        remove(note);
        if (heldCount > 0)
            target = notes[(std::size_t) heldCount - 1];
        else
        {
            ampEnv.noteOff();
            modEnv.noteOff();
        }
    }

    void allNotesOff()
    {
        heldCount = 0;
        ampEnv.noteOff();
        modEnv.noteOff();
    }

    void setBend(double semitones) { bend = semitones; }

    /** Adds the bass to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Transport& transport)
    {
        ampEnv.set(s.amp);
        modEnv.set(s.mod);
        const float glideCoefficient = s.glide <= 0.0005f ? 0.0f : (float) std::exp(-3.0 / (s.glide * sampleRate));
        const float driveGain = std::pow(10.0f, 1.5f * std::clamp(s.drive, 0.0f, 1.0f));
        // A full swing comes out about where it went in, so the drive makes it denser rather than louder.
        const float driveMakeup = 1.0f / std::max(0.2f, std::abs(tonwerkwave::shape(s.driveMode, driveGain)));
        const double ratio = kRatios[(std::size_t) std::clamp(s.ratio, 0, (int) kRatios.size() - 1)].value;
        const double nyquistGuard = 0.45 * sampleRate;
        const float w = std::clamp(s.width, 0.0f, 1.0f);
        // The saw nearer a side gets (1 + w) / 2 of it; at 0 both sides hear both alike.
        const float near = 0.5f * (1.0f + w), far = 0.5f * (1.0f - w);

        const double beats = tonwerkwave::divisionBeats(s.lfoDivision);
        const double lfoStep = (s.lfoSync ? transport.bpm / 60.0 / beats : (double) s.lfoRate) / sampleRate;
        // A synced LFO sits on the song's bars while it plays, unless it starts with each note.
        if (s.lfoSync && transport.playing && transport.hasPosition && ! s.lfoRetrigger)
            lfoPhase = transport.ppq / beats - std::floor(transport.ppq / beats);
        if (retriggered)
        {
            lfoPhase = 0.0;
            retriggered = false;
        }

        for (int i = 0; i < count; ++i)
        {
            const double before = lfoPhase;
            lfoPhase += lfoStep;
            if (lfoPhase >= 1.0)
                lfoPhase -= std::floor(lfoPhase);
            if (lfoPhase < before)
                lfoHeld = random.nextFloat();
            lfo = tonwerkwave::lfoShape(s.lfoShape, lfoPhase, lfoHeld);

            pitch = target + (pitch - target) * glideCoefficient;
            const float amp = ampEnv.next();
            const float mod = modEnv.next();

            if (countdown-- <= 0)
            {
                countdown = kControl - 1;
                updateFilter(s, mod);
            }

            const double note = pitch + s.tune + bend;
            const double hz = 440.0 * std::exp2((note - 69.0) / 12.0);

            // The Reese: two band-limited saws, `detune` cents apart around the note.
            const double spread = std::exp2(s.detune / 2400.0);
            const double dtA = std::min(0.45, hz / spread / sampleRate), dtB = std::min(0.45, hz * spread / sampleRate);
            sawA += dtA;
            if (sawA >= 1.0)
                sawA -= 1.0;
            sawB += dtB;
            if (sawB >= 1.0)
                sawB -= 1.0;
            const auto a = (float) (2.0 * sawA - 1.0 - tonwerk::polyBlep(sawA, dtA));
            const auto b = (float) (2.0 * sawB - 1.0 - tonwerk::polyBlep(sawB, dtB));

            // FM: a sine carrier at the note, its modulator at the ratio with feedback averaged over two samples, as
            // Tonwerk FM's (a single sample's feedback whistles).
            const float index = kMaxIndex * std::clamp(s.fmIndex + s.envFm * mod + s.lfoFm * lfo, 0.0f, 1.5f);
            const double modHz = std::min(hz * ratio, nyquistGuard);
            modulator += modHz / sampleRate;
            modulator -= std::floor(modulator);
            const float modOut = (float) std::sin(2.0 * kPi * modulator + s.feedback * 1.5 * 0.5 * (lastMod + lastMod2));
            lastMod2 = lastMod;
            lastMod = modOut;
            carrier += hz / sampleRate;
            carrier -= std::floor(carrier);
            const auto fm = (float) std::sin(2.0 * kPi * carrier + index * modOut);

            const float middle = s.fm * fm;
            const float inLeft = s.reese * (near * a + far * b) + middle;
            const float inRight = s.reese * (far * a + near * b) + middle;
            float outLeft = sides[0].process(0.5f * inLeft, s, driveGain, driveMakeup, g, k, a1, combDelay, combFeedback);
            float outRight = sides[1].process(0.5f * inRight, s, driveGain, driveMakeup, g, k, a1, combDelay, combFeedback);

            // The sub, a sine an octave down (or at the note), past drive and filter so it stays round.
            subPhase += hz * (s.subOctave < 0 ? 0.5 : 1.0) / sampleRate;
            subPhase -= std::floor(subPhase);
            const auto sub = (float) (0.5 * s.sub * std::sin(2.0 * kPi * subPhase));
            outLeft = (outLeft + sub) * amp;
            outRight = (outRight + sub) * amp;

            // No DC into the effects: the tube curve and the fold lean.
            const float blockedLeft = outLeft - dcIn[0] + dcCoefficient * dcOut[0];
            dcIn[0] = outLeft;
            dcOut[0] = blockedLeft;
            const float blockedRight = outRight - dcIn[1] + dcCoefficient * dcOut[1];
            dcIn[1] = outRight;
            dcOut[1] = blockedRight;
            left[i] += blockedLeft;
            right[i] += blockedRight;
        }
    }

    /** What the display shows: the note sounding (-1 none), the LFO (0 to 1) and the filter's cutoff in Hz. */
    int sounding() const { return ampEnv.isActive() ? (int) std::lround(target) : -1; }
    float lfoNow() const { return lfo; }
    float cutoffNow() const { return cutoff; }

private:
    void updateFilter(const Settings& s, float mod)
    {
        const double note = pitch + s.tune + bend;
        const float octaves = 6.0f * s.envCutoff * mod + 5.0f * s.lfoCutoff * lfo;
        const double top = std::min(18000.0, 0.45 * sampleRate);
        cutoff = (float) std::clamp((double) s.cutoff * std::exp2((double) octaves), 20.0, top);
        g = (float) std::tan(kPi * cutoff / sampleRate);
        const float r = std::clamp(s.resonance, 0.0f, 1.0f);
        k = 2.0f - 1.94f * r;
        a1 = 1.0f / (1.0f + g * (g + k));
        // The comb's teeth on the cutoff, which follows the note so the comb stays in tune with it.
        const double tuned = cutoff * std::exp2((note - 48.0) / 12.0) / 2.0;
        combDelay = (float) std::clamp(sampleRate / std::max(20.0, tuned), 2.0, sampleRate / 20.0);
        combFeedback = 0.95f * r;
    }

    void remove(int note)
    {
        const auto end = notes.begin() + heldCount;
        const auto at = std::remove(notes.begin(), end, note);
        heldCount = (int) std::distance(notes.begin(), at);
    }

    struct Random
    {
        std::uint32_t state = 0x2545f491u;
        float nextFloat()
        {
            state = state * 1664525u + 1013904223u;
            return (float) (state >> 8) / 16777216.0f;
        }
    };

    double sampleRate = 48000.0;
    std::array<int, kHeld> notes {};
    int heldCount = 0;
    double target = 36.0, pitch = 36.0, bend = 0.0;
    double sawA = 0.0, sawB = 0.37, carrier = 0.0, modulator = 0.0, subPhase = 0.0, lfoPhase = 0.0;
    float lastMod = 0.0f, lastMod2 = 0.0f, lfo = 0.0f, lfoHeld = 0.5f;
    bool retriggered = false;
    tonwerkwave::Adsr ampEnv, modEnv;
    std::array<Side, 2> sides;
    int countdown = 0;
    float cutoff = 400.0f, g = 0.03f, k = 1.4f, a1 = 1.0f, combDelay = 100.0f, combFeedback = 0.0f;
    float dcCoefficient = 0.998f;
    std::array<float, 2> dcIn {}, dcOut {};
    Random random;
};
} // namespace tonwerkneuro
