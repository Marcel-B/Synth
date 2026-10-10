#pragma once

#include "GrainSources.h"
#include "WaveDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tonwerkgrain
{
using tonwerkwave::Adsr;
using tonwerkwave::EnvSettings;
using tonwerkwave::LfoShape;
using tonwerkwave::SvfStage;

inline constexpr double kPi = 3.141592653589793;
/** Grains one voice plays at once; a new grain waits for a free one. */
inline constexpr int kMaxGrains = 40;
inline constexpr int kLfos = 2;

// The order of every enum below is the order of a parameter's menu, saved in Logic projects: append, never reorder.

/** A grain's envelope: a smooth bell, a flat top with short fades, or a struck shape that dies away. */
enum class Window { bell, flat, struck, count };
/** Where the pitch scatter lands: anywhere, on octaves, or on fifths and octaves. */
enum class Scatter { free, octaves, fifths };
enum class FilterMode { low12, low24, high12, band12 };
enum class VoiceMode { poly, mono };
enum class Target { none, position, spray, size, density, pitch, cutoff, resonance, width, amp, count };

/** A modulation source's fixed route: where it goes and how far, -1 to 1 of the target's range. */
struct Route
{
    Target target = Target::none;
    float amount = 0.0f;
};

struct LfoSettings
{
    LfoShape shape = LfoShape::sine;
    bool sync = false;
    float rate = 0.5f;
    int division = 6;
    Route route;
};

/** Everything a note plays from, read from the parameters each block. Times in seconds, shares 0 to 1. */
struct Settings
{
    int source = 0;
    /** Where in the source the grains come from, 0 to 1. */
    float position = 0.3f;
    /** How far around the position a grain may start: 1 is anywhere in the source. */
    float spray = 0.1f;
    /** How fast the position moves on while a note is held, as a share of the source's own speed; negative backwards. */
    float scan = 0.0f;
    float size = 0.12f;
    /** Grains per second. */
    float density = 20.0f;
    /** How irregular the time between grains is: 0 a steady stream, 1 up to twice or no time apart. */
    float jitter = 0.3f;
    Window window = Window::bell;
    /** The share of grains played backwards. */
    float reverse = 0.0f;
    /** How far the grains spread across the stereo field. */
    float width = 0.6f;

    int octave = 0;
    int semitones = 0;
    float fine = 0.0f;
    /** Cents a grain's pitch may lie off the note, either way. */
    float scatter = 0.0f;
    Scatter scatterMode = Scatter::free;
    /** Off, every key plays the source at the same pitch: textures that should not follow the melody. */
    bool keyFollow = true;

    bool filterOn = false;
    FilterMode filterMode = FilterMode::low24;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float keytrack = 0.0f;

    EnvSettings env1 { 0.05f, 0.5f, 1.0f, 0.8f };
    EnvSettings env2 { 0.5f, 1.0f, 0.0f, 0.5f };
    Route env2Route;
    std::array<LfoSettings, kLfos> lfo {};
    Route wheel;
    /** Velocity counts from 0 at a full strike down to -1 at the softest, so a positive amount makes soft notes less. */
    Route velocity { Target::amp, 0.5f };

    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f;
    int bendRange = 2;
    float masterDb = -6.0f;
};

/** What the host says about time, at the start of the stretch being rendered. */
struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/** What all voices share during one control step. */
struct Shared
{
    double sampleRate = 48000.0;
    double bend = 0.0;
    float modWheel = 0.0f;
    Transport transport;
};

/** The grain envelopes as tables, made once; and how much power each carries next to the bell's. */
class Windows
{
public:
    static constexpr int kPoints = 1024;

    static const Windows& instance()
    {
        static const Windows windows;
        return windows;
    }

    /** The window at `phase` (0 to 1). */
    float at(Window w, float phase) const
    {
        const float x = phase * (float) kPoints;
        const int i = std::min((int) x, kPoints - 1);
        const float* t = tables[(std::size_t) w].data();
        return t[i] + (t[i + 1] - t[i]) * (x - (float) i);
    }

    /** Mean square of the window over the bell's, for keeping a cloud's level whichever window it uses. */
    float power(Window w) const { return powers[(std::size_t) w]; }

private:
    Windows()
    {
        for (int w = 0; w < (int) Window::count; ++w)
        {
            auto& t = tables[(std::size_t) w];
            double sum = 0.0;
            for (int i = 0; i <= kPoints; ++i)
            {
                const double p = (double) i / kPoints;
                double v = 0.0;
                switch ((Window) w)
                {
                    case Window::bell: v = 0.5 - 0.5 * std::cos(2.0 * kPi * p); break;
                    case Window::flat:
                        // Tukey: raised-cosine fades over the first and last fifth.
                        v = p < 0.2 ? 0.5 - 0.5 * std::cos(kPi * p / 0.2)
                            : p > 0.8 ? 0.5 - 0.5 * std::cos(kPi * (1.0 - p) / 0.2)
                                      : 1.0;
                        break;
                    case Window::struck:
                        // Up in 2 % of the grain, then falling; the last tenth fades to zero so the cut never clicks.
                        v = p < 0.02 ? p / 0.02 : std::exp(-5.0 * (p - 0.02) / 0.98);
                        if (p > 0.9)
                            v *= 0.5 + 0.5 * std::cos(kPi * (p - 0.9) / 0.1);
                        break;
                    case Window::count: break;
                }
                t[(std::size_t) i] = (float) v;
                if (i < kPoints)
                    sum += v * v;
            }
            powers[(std::size_t) w] = (float) (sum / kPoints);
        }
        const float bell = powers[(std::size_t) Window::bell];
        for (auto& p : powers)
            p /= bell;
    }

    std::array<std::array<float, kPoints + 1>, (std::size_t) Window::count> tables {};
    std::array<float, (std::size_t) Window::count> powers {};
};

/**
 * One note: a stream of grains cut from the source around a position that may scan along it, each with its own
 * window, pitch, direction and place in the stereo field; then the filter and the amp envelope. Modulation runs every
 * `kControl` samples; the cloud's level and the filter ramp across each step.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    Voice() : random(nextSeed()) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        amp.prepare(rate);
        mod.prepare(rate);
        reset();
    }

    void reset()
    {
        amp.reset();
        mod.reset();
        for (auto& g : grains)
            g.active = false;
        for (auto& f : filterLeft)
            f.reset();
        for (auto& f : filterRight)
            f.reset();
        held = false;
        sustained = false;
    }

    /** Starts `note`; a voice still sounding keeps its grains, and its envelopes climb from where they are. */
    void start(int newNote, float newVelocity, const Settings& s, bool glide)
    {
        const bool wasActive = isActive();
        note = newNote;
        velocity = newVelocity;
        held = true;
        sustained = false;
        if (! glide || ! wasActive)
            pitch = note;
        amp.set(s.env1);
        mod.set(s.env2);
        amp.noteOn();
        mod.noteOn();
        // A new strike reads from the position again, so a sound that scans through a source (a pluck read at its
        // own speed) plays it from the start each time; a glide carries on where it was.
        if (! glide || ! wasActive)
            playhead = 0.0;
        if (! wasActive)
        {
            untilNext = 0.0;
            first = true;
            for (std::size_t i = 0; i < lfoPhase.size(); ++i)
            {
                lfoPhase[i] = 0.0;
                lfoHeld[i] = (float) random.next();
                lfoSmooth[i] = 0.0f;
            }
        }
        ++age;
    }

    void release()
    {
        held = false;
        amp.noteOff();
        mod.noteOff();
    }

    bool isActive() const { return amp.isActive(); }
    bool isHeld() const { return held; }
    bool isReleasing() const { return amp.isReleasing(); }
    int currentNote() const { return note; }

    bool sustained = false;
    std::uint64_t age = 0;

    /** Where the voice's grains sit in the source (0 to 1) and how far into their window, for the display. */
    template <typename Visit>
    void forEachGrain(Visit visit) const
    {
        for (const auto& g : grains)
            if (g.active && g.delay <= 0)
                visit(g.place, g.phase);
    }
    /** The position the grains gather around, scan included, 0 to 1. */
    float centre() const { return shownCentre; }
    float spread() const { return shownSpray; }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const GrainSource& source, const Shared& shared)
    {
        if (! isActive())
            return;
        control(s, source, shared, count);

        std::array<float, kControl> l {}, r {};
        if (! source.empty())
            for (auto& g : grains)
                if (g.active)
                    play(g, source, l.data(), r.data(), count);

        const float step = 1.0f / (float) count;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            const float level = cloud.at(t);
            float x = l[(std::size_t) i] * level, y = r[(std::size_t) i] * level;
            if (s.filterOn)
                filter(s.filterMode, x, y, t);
            const float gain = amp.next();
            left[i] += x * gain;
            right[i] += y * gain;
        }
        for (int i = 0; i < count; ++i)
            mod.next();
        first = false;
        if (! isActive())
            reset();
    }

private:
    struct Ramp
    {
        float from = 0.0f, to = 0.0f;
        void set(float value, bool jump)
        {
            from = jump ? value : to;
            to = value;
        }
        float at(float t) const { return from + (to - from) * t; }
    };

    struct Grain
    {
        bool active = false;
        /** Samples into the step before it starts. */
        int delay = 0;
        /** Read position in the chosen level's samples, and the step per output sample there (negative backwards). */
        double position = 0.0;
        double increment = 1.0;
        int level = 0;
        float phase = 0.0f;
        float phaseStep = 0.0f;
        Window window = Window::bell;
        float gainLeft = 1.0f, gainRight = 1.0f;
        /** Its middle in the source, 0 to 1, for the display. */
        float place = 0.0f;
    };

    void play(Grain& g, const GrainSource& source, float* l, float* r, int count)
    {
        const auto& data = source.levels[(std::size_t) g.level];
        const auto length = (double) data.size();
        if (data.size() < 2)
        {
            g.active = false;
            return;
        }
        // The source may have changed under a sounding grain (a new file); keep the read inside it.
        if (g.position >= length || g.position < 0.0)
            g.position -= std::floor(g.position / length) * length;
        const auto& windows = Windows::instance();
        const int begin = std::max(0, g.delay);
        g.delay = std::max(0, g.delay - count);
        double position = g.position;
        float phase = g.phase;
        for (int i = begin; i < count; ++i)
        {
            const auto at = (std::size_t) position;
            const auto next = at + 1 < data.size() ? at + 1 : 0;
            const float frac = (float) (position - (double) at);
            const float sample = (data[at] + (data[next] - data[at]) * frac) * windows.at(g.window, phase);
            l[i] += sample * g.gainLeft;
            r[i] += sample * g.gainRight;
            position += g.increment;
            if (position >= length)
                position -= length;
            else if (position < 0.0)
                position += length;
            phase += g.phaseStep;
            if (phase >= 1.0f)
            {
                g.active = false;
                return;
            }
        }
        g.position = position;
        g.phase = phase;
    }

    /** -1 to 1 for the LFOs, 0 to 1 for the envelope and the wheel, -1 to 0 for velocity. */
    void route(std::array<float, (std::size_t) Target::count>& m, const Route& r, float value) const
    {
        if (r.target != Target::none)
            m[(std::size_t) r.target] += r.amount * value;
    }

    void control(const Settings& s, const GrainSource& source, const Shared& shared, int count)
    {
        amp.set(s.env1);
        mod.set(s.env2);
        const double seconds = count / sampleRate;
        const auto& transport = shared.transport;

        std::array<float, (std::size_t) Target::count> m {};
        const float smoothing = first ? 0.0f : (float) std::exp(-seconds / 0.002);
        for (std::size_t i = 0; i < kLfos; ++i)
        {
            const auto& lfo = s.lfo[i];
            const double beats = tonwerkwave::divisionBeats(lfo.division);
            double now = lfoPhase[i];
            if (lfo.sync && transport.playing && transport.hasPosition)
            {
                // Locked to the song: a sweep lands on the bar wherever playback starts.
                now = transport.ppq / beats;
                now -= std::floor(now);
            }
            else
            {
                const double rate = lfo.sync ? transport.bpm / 60.0 / beats : (double) lfo.rate;
                now += rate * seconds;
                now -= std::floor(now);
            }
            if (now < lfoPhase[i])
                lfoHeld[i] = (float) random.next();
            lfoPhase[i] = now;
            // Bipolar: a sweep or vibrato goes both ways around the knob's setting.
            const float value = lfo.shape == LfoShape::random ? lfoHeld[i]
                                                              : 2.0f * tonwerkwave::lfoShape(lfo.shape, now, 0.0f) - 1.0f;
            lfoSmooth[i] = value + (lfoSmooth[i] - value) * smoothing;
            route(m, lfo.route, lfoSmooth[i]);
        }
        route(m, s.env2Route, mod.current());
        route(m, s.wheel, shared.modWheel);
        route(m, s.velocity, velocity - 1.0f);
        auto at = [&m](Target t) { return m[(std::size_t) t]; };

        if (s.voiceMode == VoiceMode::mono && s.glide > 0.0f && ! first)
            pitch += (note - pitch) * (1.0 - std::exp(-seconds / (s.glide / 4.0)));
        else
            pitch = note;

        // The cloud's parameters for this step.
        const double length = (double) source.length();
        const double rateRatio = source.sampleRate / sampleRate;
        if (length > 0.0)
        {
            playhead += (double) s.scan * (double) count * rateRatio / length;
            playhead -= std::floor(playhead);
        }
        double centre = s.position + at(Target::position) + playhead;
        centre -= std::floor(centre);
        const double spray = std::clamp(s.spray + at(Target::spray), 0.0f, 1.0f);
        const double size = std::clamp((double) s.size * std::exp2(3.0 * at(Target::size)), 0.005, 2.0);
        const double density = std::clamp((double) s.density * std::exp2(3.0 * at(Target::density)), 0.5, 400.0);
        const float width = std::clamp(s.width + at(Target::width), 0.0f, 1.0f);
        const double played = (s.keyFollow ? pitch - source.rootNote : 0.0) + shared.bend + 12.0 * s.octave
                              + s.semitones + s.fine / 100.0 + 12.0 * at(Target::pitch);
        shownCentre = (float) centre;
        shownSpray = (float) spray;

        // Grains at random phases add up in power: the level falls with the square root of how many overlap, so a
        // dense cloud is as loud as a sparse one. Sparse ones (fewer than one at a time) keep the level of one.
        const auto& windows = Windows::instance();
        const double overlap = std::max(1.0, density * size) * windows.power(s.window);
        cloud.set((float) std::min(2.0, 1.0 / std::sqrt(overlap)) * std::clamp(1.0f + at(Target::amp), 0.0f, 2.0f), first);

        // New grains for this step, each waiting its offset into it.
        while (untilNext < (double) count)
        {
            const int offset = std::max(0, (int) untilNext);
            if (length > 1.0)
                spawn(s, source, offset, centre, spray, size, played, width, rateRatio);
            const double jitter = std::clamp(s.jitter, 0.0f, 1.0f) * random.next();
            untilNext += std::max(1.0, sampleRate / density * (1.0 + jitter));
        }
        untilNext -= (double) count;

        // The filter, as on Tonwerk Wavetable: the cutoff in semitones, ten octaves at 100 % of a route.
        const double cutoffNote = 12.0 * std::log2(std::max(20.0f, s.cutoff) / 440.0) + 69.0
                                  + s.keytrack * (pitch - 60.0) + 120.0 * at(Target::cutoff);
        const double cutoff = std::clamp(440.0 * std::exp2((cutoffNote - 69.0) / 12.0), 20.0,
                                         std::min(20000.0, 0.45 * sampleRate));
        const float resonance = std::clamp(s.resonance + at(Target::resonance), 0.0f, 1.0f);
        filterG.set((float) std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
    }

    void spawn(const Settings& s, const GrainSource& source, int offset, double centre, double spray, double size,
               double played, float width, double rateRatio)
    {
        Grain* g = nullptr;
        for (auto& candidate : grains)
            if (! candidate.active)
            {
                g = &candidate;
                break;
            }
        if (g == nullptr)
            return;

        double cents = s.scatter * random.next();
        if (s.scatterMode != Scatter::free && s.scatter > 0.0f)
        {
            // Snapped to the nearest octave, or to the nearest fifth or octave, within the scatter's reach.
            const double semis = cents / 100.0;
            const double grid = s.scatterMode == Scatter::octaves ? 12.0 : 0.0;
            if (grid > 0.0)
                cents = 100.0 * grid * std::round(semis / grid);
            else
            {
                const double octave = std::floor(semis / 12.0);
                const double within = semis - 12.0 * octave;
                const double snapped = within < 3.5 ? 0.0 : within < 9.5 ? 7.0 : 12.0;
                cents = 100.0 * (12.0 * octave + snapped);
            }
        }
        double rate = std::exp2((played + cents / 100.0) / 12.0) * rateRatio;
        rate = std::clamp(rate, 1.0 / 64.0, 64.0);
        int level = 0;
        while (rate > 1.0 && level < GrainSource::kLevels - 1)
        {
            rate *= 0.5;
            ++level;
        }
        const auto samples = std::max(16.0, size * sampleRate);
        const bool backwards = random.next() * 0.5 + 0.5 < (double) s.reverse;
        const double levelLength = (double) source.levels[(std::size_t) level].size();
        const double span = samples * rate;
        // The start chosen so the whole grain fits in the source, without wrapping around its end.
        double place = centre + 0.5 * spray * random.next();
        place -= std::floor(place);
        const double room = std::max(0.0, levelLength - span - 2.0);
        double start = place * room;
        if (backwards)
            start += span;

        g->active = true;
        g->delay = offset;
        g->level = level;
        g->position = start;
        g->increment = backwards ? -rate : rate;
        g->phase = 0.0f;
        g->phaseStep = (float) (1.0 / samples);
        g->window = s.window;
        const float pan = width * (float) random.next();
        g->gainLeft = std::sqrt(1.0f - pan);
        g->gainRight = std::sqrt(1.0f + pan);
        g->place = (float) ((place * room + 0.5 * span) / std::max(1.0, levelLength));
    }

    void filter(FilterMode mode, float& l, float& r, float t)
    {
        const float g = filterG.at(t);
        const float k = filterK.at(t);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        if (mode == FilterMode::low24)
        {
            constexpr float k1 = 1.414f;
            const float b1 = 1.0f / (1.0f + g * (g + k1));
            filterLeft[0].process(l, g, k1, b1);
            filterRight[0].process(r, g, k1, b1);
            filterLeft[1].process(filterLeft[0].low, g, k, a1);
            filterRight[1].process(filterRight[0].low, g, k, a1);
            l = filterLeft[1].low;
            r = filterRight[1].low;
            return;
        }
        filterLeft[0].process(l, g, k, a1);
        filterRight[0].process(r, g, k, a1);
        switch (mode)
        {
            case FilterMode::low12: l = filterLeft[0].low; r = filterRight[0].low; break;
            case FilterMode::high12: l = filterLeft[0].high; r = filterRight[0].high; break;
            case FilterMode::band12: l = k * filterLeft[0].band; r = k * filterRight[0].band; break;
            case FilterMode::low24: break;
        }
    }

    static std::uint32_t nextSeed()
    {
        static std::uint32_t count = 0;
        return 0x3c6ef372u * (++count * 2u + 1u);
    }

    double sampleRate = 48000.0;
    int note = 60;
    double pitch = 60.0;
    float velocity = 1.0f;
    bool held = false;
    bool first = true;

    Adsr amp, mod;
    std::array<Grain, kMaxGrains> grains {};
    double untilNext = 0.0;
    double playhead = 0.0;
    float shownCentre = 0.0f;
    float shownSpray = 0.0f;
    std::array<double, kLfos> lfoPhase {};
    std::array<float, kLfos> lfoHeld {};
    std::array<float, kLfos> lfoSmooth {};
    tonwerk::Noise random;

    Ramp cloud;
    std::array<SvfStage, 2> filterLeft {}, filterRight {};
    Ramp filterG, filterK;
};

/** The instrument without a host: voices, poly or mono with glide, the sustain pedal and the wheels. */
class Engine
{
public:
    static constexpr int kVoices = 8;

    void prepare(double rate)
    {
        shared.sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
        held.clear();
        held.reserve(128);
        sustainPedal = false;
    }

    void noteOn(int note, float velocity, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && v.currentNote() == note)
                {
                    v.start(note, velocity, s, false);
                    v.age = ++clock;
                    return;
                }
            auto& v = freeVoice();
            v.start(note, velocity, s, false);
            v.age = ++clock;
            return;
        }
        lastVelocity = velocity;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        held.push_back(note);
        for (std::size_t i = 1; i < voices.size(); ++i)
            if (voices[i].isActive() && ! voices[i].isReleasing())
                voices[i].release();
        voices[0].start(note, velocity, s, true);
        voices[0].age = ++clock;
    }

    void noteOff(int note, const Settings& s)
    {
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && v.isHeld() && v.currentNote() == note)
                {
                    if (sustainPedal)
                        v.sustained = true;
                    else
                        v.release();
                }
            return;
        }
        const bool wasSounding = ! held.empty() && held.back() == note;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        if (! wasSounding)
            return;
        if (! held.empty())
            voices[0].start(held.back(), lastVelocity, s, true);
        else
            voices[0].release();
    }

    void setSustain(bool down)
    {
        sustainPedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained)
                {
                    v.sustained = false;
                    v.release();
                }
    }

    void allNotesOff()
    {
        held.clear();
        sustainPedal = false;
        for (auto& v : voices)
        {
            v.sustained = false;
            v.release();
        }
    }

    void setBend(double semitones) { shared.bend = semitones; }
    void setModWheel(float value) { shared.modWheel = value; }

    /** Adds `count` samples of all voices to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const GrainSource& source,
                const Transport& transport)
    {
        int done = 0;
        while (done < count)
        {
            const int n = std::min(Voice::kControl, count - done);
            shared.transport = transport;
            shared.transport.ppq += (double) done / shared.sampleRate * transport.bpm / 60.0;
            for (auto& v : voices)
                v.render(left + done, right + done, n, s, source, shared);
            done += n;
        }
    }

    const Voice* newestVoice() const
    {
        const Voice* newest = nullptr;
        for (const auto& v : voices)
            if (v.isActive() && (newest == nullptr || v.age > newest->age))
                newest = &v;
        return newest;
    }

    int activeVoices() const
    {
        return (int) std::count_if(voices.begin(), voices.end(), [](const Voice& v) { return v.isActive(); });
    }

private:
    Voice& freeVoice()
    {
        for (auto& v : voices)
            if (! v.isActive())
                return v;
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isReleasing() && (best == nullptr || v.age < best->age))
                best = &v;
        if (best != nullptr)
            return *best;
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;
        return *best;
    }

    std::array<Voice, kVoices> voices;
    std::vector<int> held;
    Shared shared;
    bool sustainPedal = false;
    float lastVelocity = 1.0f;
    std::uint64_t clock = 0;
};
} // namespace tonwerkgrain
