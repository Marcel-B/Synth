#pragma once

#include "Voice.h" // Adsr, SvfStage

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Tonwerk Granular (granular/src/GranularEngine.h of the Synth repository) for the Eurorack module: one voice of the
// same grain cloud, its windows, pitch scatter, filter and envelope, with one LFO on a fixed route. The control step
// (every 32 samples) keeps the plugin's double arithmetic: it is cheap there and keeps every grain's start, length
// and pitch where the plugin puts them. The grains themselves read with 32.32 fixed-point positions and float, since
// the S3's FPU has no double. The sources are the plugin's own, computed on a computer and flashed with the firmware
// as 16-bit samples (Sources below).
namespace twm::grain
{
inline constexpr double kPi = 3.141592653589793;
/** Grains one voice plays at once, as in the plugin; a new grain waits for a free one. */
inline constexpr int kMaxGrains = 40;

// Menu orders as in Tonwerk Granular.
enum class Window : uint8_t { bell, flat, struck, count };
enum class Scatter : uint8_t { free, octaves, fifths, count };
enum class FilterMode : uint8_t { low12, low24, high12, band12, count };
enum class Target : uint8_t { none, position, spray, size, density, pitch, cutoff, resonance, width, amp, count };
enum class LfoShape : uint8_t { sine, triangle, rampDown, rampUp, square, random, count };

struct Envelope
{
    float attack, decay, sustain, release;
};

/** Everything a note plays from, as in the plugin's Settings, with one LFO and without the modulation sources a
 *  modular system patches (second envelope, wheel, velocity). */
struct Settings
{
    int source = 0;
    float position = 0.3f;
    float spray = 0.1f;
    float scan = 0.0f;
    float size = 0.12f;
    float density = 20.0f;
    float jitter = 0.3f;
    Window window = Window::bell;
    float reverse = 0.0f;
    float width = 0.6f;
    float fine = 0.0f; // cents
    float scatter = 0.0f;
    Scatter scatterMode = Scatter::free;
    bool filterOn = false;
    FilterMode filterMode = FilterMode::low24;
    float cutoff = 20000.0f;
    float resonance = 0.1f;
    float keytrack = 0.0f;
    Envelope env { 0.05f, 0.5f, 1.0f, 0.8f };
    LfoShape lfoShape = LfoShape::sine;
    float lfoRate = 0.5f;
    Target lfoTarget = Target::none;
    float lfoAmount = 0.0f;
};

/**
 * One source: 16-bit samples with copies at half, a quarter, ... of the rate, each lowpassed before it is thinned out,
 * as the plugin's GrainSource. A grain played faster than the source's rate reads the copy that keeps it below Nyquist.
 */
struct Source
{
    static constexpr int kLevels = 5;
    std::array<const int16_t*, kLevels> level {};
    std::array<uint32_t, kLevels> length {};
    double sampleRate = 48000.0;
    int rootNote = 60;

    bool empty() const { return length[0] == 0; }
};

/**
 * The plugin's nine built-in sources (choir, glass, strings, plucks, organ, wind, metal, sine, rhythm), 4 s each at
 * 48 kHz with the root C4. Their full-rate samples come from the flash (the host test writes them from the plugin,
 * `make grain-sources`); `build` makes the lower copies in PSRAM at start, in well under a second.
 */
class Sources
{
public:
    static constexpr int kCount = 9;
    static constexpr uint32_t kSamples = 192000;
    static constexpr int kRoot = 60;
    /** What the flashed block's header says it holds; a new layout or new sources need a new version. */
    static constexpr uint32_t kMagic = 0x52475754; // "TWGR"
    static constexpr uint32_t kVersion = 1;
    static constexpr float kScale = 1.0f / 32767.0f;

    /** Samples of one source with all its copies. */
    static constexpr uint32_t perSource()
    {
        uint32_t total = 0;
        for (int k = 0; k < Source::kLevels; ++k)
            total += kSamples >> k;
        return total;
    }
    /** The full-rate samples of all sources, as flashed. */
    static constexpr std::size_t flashedBytes() { return (std::size_t) kCount * kSamples * sizeof(int16_t); }
    /** Everything in PSRAM: the full-rate samples first (as flashed), then the copies of each source. */
    static constexpr std::size_t bytes() { return (std::size_t) kCount * perSource() * sizeof(int16_t); }

    /** Points the sources into `block` (bytes() long, the full-rate samples already in its first flashedBytes()) and
     *  computes the lower copies. */
    void build(int16_t* block)
    {
        int16_t* copies = block + (std::size_t) kCount * kSamples;
        for (int i = 0; i < kCount; ++i)
        {
            auto& s = sources[(std::size_t) i];
            s.rootNote = kRoot;
            s.level[0] = block + (std::size_t) i * kSamples;
            s.length[0] = kSamples;
            for (int k = 1; k < Source::kLevels; ++k)
            {
                s.length[(std::size_t) k] = kSamples >> k;
                decimate(s.level[(std::size_t) k - 1], s.length[(std::size_t) k - 1], copies);
                s.level[(std::size_t) k] = copies;
                copies += s.length[(std::size_t) k];
            }
        }
    }

    const Source& source(int index) const { return sources[(std::size_t) std::clamp(index, 0, kCount - 1)]; }

    /** Half the rate: the plugin's windowed sinc (47 taps, cutoff at 0.45 of the new Nyquist), then every second sample. */
    static void decimate(const int16_t* in, uint32_t length, int16_t* out)
    {
        constexpr int kTaps = 47, kHalf = kTaps / 2;
        std::array<float, kTaps> taps {};
        double sum = 0.0;
        std::array<double, kTaps> h {};
        for (int i = 0; i < kTaps; ++i)
        {
            const double x = (double) (i - kHalf);
            const double cutoff = 0.225;
            const double sinc = i == kHalf ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * x) / (kPi * x);
            const double blackman = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[(std::size_t) i] = sinc * blackman;
            sum += h[(std::size_t) i];
        }
        for (int i = 0; i < kTaps; ++i)
            taps[(std::size_t) i] = (float) (h[(std::size_t) i] / sum);
        for (uint32_t n = 0; n < length / 2; ++n)
        {
            const long centre = (long) (2 * n);
            float y = 0.0f;
            for (int i = 0; i < kTaps; ++i)
            {
                const long at = centre + i - kHalf;
                if (at >= 0 && at < (long) length)
                    y += taps[(std::size_t) i] * (float) in[at];
            }
            out[n] = (int16_t) std::clamp(std::lround(y), -32767L, 32767L);
        }
    }

private:
    std::array<Source, kCount> sources {};
};

/** The plugin's noise (xorshift), with its double result: the grains' dice must fall as in the plugin. */
class Random
{
public:
    explicit Random(uint32_t seed) : state(seed) {}
    double next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (double) state / 2147483648.0 - 1.0;
    }

private:
    uint32_t state;
};

/** The grain envelopes as tables, and the power of each next to the bell's, as in the plugin. */
class Windows
{
public:
    static constexpr int kPoints = 1024;

    static const Windows& instance()
    {
        static const Windows windows;
        return windows;
    }

    float at(Window w, float phase) const
    {
        const float x = phase * (float) kPoints;
        const int i = std::min((int) x, kPoints - 1);
        const float* t = tables[(std::size_t) w].data();
        return t[i] + (t[i + 1] - t[i]) * (x - (float) i);
    }
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
                        v = p < 0.2 ? 0.5 - 0.5 * std::cos(kPi * p / 0.2)
                            : p > 0.8 ? 0.5 - 0.5 * std::cos(kPi * (1.0 - p) / 0.2)
                                      : 1.0;
                        break;
                    case Window::struck:
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

/** The plugin's LFO shapes, 0 to 1 (random is the held value). */
inline float lfoShape(LfoShape shape, double phase, float held)
{
    switch (shape)
    {
        case LfoShape::sine: return (float) (0.5 - 0.5 * std::cos(2.0 * kPi * phase));
        case LfoShape::triangle: return (float) (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
        case LfoShape::rampDown: return (float) (1.0 - phase);
        case LfoShape::rampUp: return (float) phase;
        case LfoShape::square: return phase < 0.5 ? 1.0f : 0.0f;
        case LfoShape::random: return held;
        case LfoShape::count: break;
    }
    return 0.0f;
}

/**
 * One note, as the plugin's Voice: a stream of grains cut from the source around a position that may scan along it,
 * each with its own window, pitch, direction and place in the stereo field; then the filter and the envelope.
 */
class Voice
{
public:
    static constexpr int kControl = 32;

    explicit Voice(uint32_t seed = 0x3c6ef372u * 3u) : random(seed) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        amp.prepare((float) rate);
        reset();
    }

    void reset()
    {
        amp.reset();
        for (auto& g : grains)
            g.active = false;
        for (auto& f : filterLeft)
            f.reset();
        for (auto& f : filterRight)
            f.reset();
    }

    /** Starts a note: the envelope climbs from where it is, the grains read from the position again. */
    void start(const Settings& s)
    {
        const bool wasActive = isActive();
        amp.set(s.env.attack, s.env.decay, s.env.sustain, s.env.release);
        amp.noteOn();
        playhead = 0.0;
        if (! wasActive)
        {
            untilNext = 0.0;
            first = true;
            lfoPhase = 0.0;
            lfoHeld = (float) random.next();
            // The plugin draws for its second LFO here too; the dice must stay in step with it.
            random.next();
            lfoSmooth = 0.0f;
        }
    }

    void release() { amp.noteOff(); }
    /** The note the grains play, a MIDI note number; fractional, since V/OCT is. */
    void setNote(double newNote) { note = newNote; }
    bool isActive() const { return amp.isActive(); }
    float level() const { return amp.current(); }
    int sounding() const
    {
        return (int) std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.active; });
    }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Source& source)
    {
        if (! isActive())
            return;
        control(s, source, count);

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
        int delay = 0;
        /** Read position and step in the chosen copy's samples, 32.32 fixed point (negative steps backwards). */
        int64_t position = 0;
        int64_t increment = 0;
        int level = 0;
        float phase = 0.0f;
        float phaseStep = 0.0f;
        Window window = Window::bell;
        float gainLeft = 1.0f, gainRight = 1.0f;
    };

    static constexpr double kFixed = 4294967296.0;

    void play(Grain& g, const Source& source, float* l, float* r, int count)
    {
        const int16_t* data = source.level[(std::size_t) g.level];
        const uint32_t size = source.length[(std::size_t) g.level];
        if (size < 2)
        {
            g.active = false;
            return;
        }
        const int64_t length = (int64_t) size << 32;
        if (g.position >= length || g.position < 0)
            g.position = ((g.position % length) + length) % length;
        const auto& windows = Windows::instance();
        const int begin = std::max(0, g.delay);
        g.delay = std::max(0, g.delay - count);
        int64_t position = g.position;
        float phase = g.phase;
        const float gl = g.gainLeft * Sources::kScale, gr = g.gainRight * Sources::kScale;
        for (int i = begin; i < count; ++i)
        {
            const auto at = (uint32_t) (position >> 32);
            const uint32_t next = at + 1 < size ? at + 1 : 0;
            const float frac = (float) (uint32_t) position * (1.0f / 4294967296.0f);
            const float a = (float) data[at];
            const float sample = (a + ((float) data[next] - a) * frac) * windows.at(g.window, phase);
            l[i] += sample * gl;
            r[i] += sample * gr;
            position += g.increment;
            if (position >= length)
                position -= length;
            else if (position < 0)
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

    void control(const Settings& s, const Source& source, int count)
    {
        amp.set(s.env.attack, s.env.decay, s.env.sustain, s.env.release);
        const double seconds = count / sampleRate;

        std::array<float, (std::size_t) Target::count> m {};
        const float smoothing = first ? 0.0f : (float) std::exp(-seconds / 0.002);
        double now = lfoPhase + (double) s.lfoRate * seconds;
        now -= std::floor(now);
        if (now < lfoPhase)
            lfoHeld = (float) random.next();
        lfoPhase = now;
        const float value = s.lfoShape == LfoShape::random ? lfoHeld : 2.0f * lfoShape(s.lfoShape, now, 0.0f) - 1.0f;
        lfoSmooth = value + (lfoSmooth - value) * smoothing;
        if (s.lfoTarget != Target::none)
            m[(std::size_t) s.lfoTarget] += s.lfoAmount * lfoSmooth;
        auto at = [&m](Target t) { return m[(std::size_t) t]; };

        const double length = (double) source.length[0];
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
        const double played = note - source.rootNote + s.fine / 100.0 + 12.0 * at(Target::pitch);

        const auto& windows = Windows::instance();
        const double overlap = std::max(1.0, density * size) * windows.power(s.window);
        cloud.set((float) std::min(2.0, 1.0 / std::sqrt(overlap)) * std::clamp(1.0f + at(Target::amp), 0.0f, 2.0f), first);

        while (untilNext < (double) count)
        {
            const int offset = std::max(0, (int) untilNext);
            if (length > 1.0)
                spawn(s, source, offset, centre, spray, size, played, width, rateRatio);
            const double jitter = std::clamp(s.jitter, 0.0f, 1.0f) * random.next();
            untilNext += std::max(1.0, sampleRate / density * (1.0 + jitter));
        }
        untilNext -= (double) count;

        const double cutoffNote = 12.0 * std::log2(std::max(20.0f, s.cutoff) / 440.0) + 69.0
                                  + s.keytrack * (note - 60.0) + 120.0 * at(Target::cutoff);
        const double cutoff = std::clamp(440.0 * std::exp2((cutoffNote - 69.0) / 12.0), 20.0,
                                         std::min(20000.0, 0.45 * sampleRate));
        const float resonance = std::clamp(s.resonance + at(Target::resonance), 0.0f, 1.0f);
        filterG.set((float) std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
    }

    void spawn(const Settings& s, const Source& source, int offset, double centre, double spray, double size,
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
            const double semis = cents / 100.0;
            if (s.scatterMode == Scatter::octaves)
                cents = 100.0 * 12.0 * std::round(semis / 12.0);
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
        while (rate > 1.0 && level < Source::kLevels - 1)
        {
            rate *= 0.5;
            ++level;
        }
        const auto samples = std::max(16.0, size * sampleRate);
        const bool backwards = random.next() * 0.5 + 0.5 < (double) s.reverse;
        const double levelLength = (double) source.length[(std::size_t) level];
        const double span = samples * rate;
        double place = centre + 0.5 * spray * random.next();
        place -= std::floor(place);
        const double room = std::max(0.0, levelLength - span - 2.0);
        double start = place * room;
        if (backwards)
            start += span;

        g->active = true;
        g->delay = offset;
        g->level = level;
        g->position = (int64_t) std::llround(start * kFixed);
        g->increment = (int64_t) std::llround((backwards ? -rate : rate) * kFixed);
        g->phase = 0.0f;
        g->phaseStep = (float) (1.0 / samples);
        g->window = s.window;
        const float pan = width * (float) random.next();
        g->gainLeft = std::sqrt(1.0f - pan);
        g->gainRight = std::sqrt(1.0f + pan);
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
            default: break;
        }
    }

    double sampleRate = 48000.0;
    double note = 60.0;
    bool first = true;
    Adsr amp;
    std::array<Grain, kMaxGrains> grains {};
    double untilNext = 0.0;
    double playhead = 0.0;
    double lfoPhase = 0.0;
    float lfoHeld = 0.0f;
    float lfoSmooth = 0.0f;
    Random random;
    Ramp cloud;
    std::array<SvfStage, 2> filterLeft {}, filterRight {};
    Ramp filterG, filterK;
};

/** What the module plays from: the sound, the pitch (MIDI note numbers, fractional) and the gate. */
struct PlayParams
{
    Settings settings;
    float note = 60.0f;
    bool gate = false;
};

/**
 * The module's instrument: one voice. A rising gate starts the envelope and reads from the position again; while the
 * gate is on (or no cable is in), a jump at V/OCT of half a semitone does the same, as on the other firmwares. The
 * grains follow V/OCT all the time. Writes left and right, the grains spread between them by `width`.
 */
class Player
{
public:
    static constexpr int kControl = Voice::kControl;
    static constexpr int kJumpBlocks = 6;
    static constexpr float kJumpSemitones = 0.5f;
    static constexpr float kHoldOffSeconds = 0.015f;
    /** The cloud at the plugin's master 0 dB (its default is -6 dB): sources are leveled to -12 dBFS RMS, so even a
     *  dense, wide cloud peaks under full scale (±5 V) and a sparse one is loud enough for a module. */
    static constexpr float kOutput = 1.0f;

    void prepare(float rate)
    {
        sampleRate = rate;
        voice.prepare(rate);
        recent.fill(60.0f);
    }
    /** The sources the grains are cut from; silence until they are there. */
    void use(const Sources* bank) { sources = bank; }

    void render(float* left, float* right, int count, const PlayParams& p)
    {
        const float before = recent[(std::size_t) ((position + 1) % kJumpBlocks)];
        if (p.gate && ! gateWas)
            strike(p);
        else if (! p.gate && gateWas)
            voice.release();
        else if (p.gate && sinceStrike >= kHoldOffSeconds && std::abs(p.note - before) >= kJumpSemitones)
            strike(p);
        gateWas = p.gate;
        sinceStrike += (float) count / sampleRate;
        position = (position + 1) % kJumpBlocks;
        recent[(std::size_t) position] = p.note;

        std::fill(left, left + count, 0.0f);
        std::fill(right, right + count, 0.0f);
        if (sources == nullptr)
            return;
        voice.setNote(p.note);
        const Source& source = sources->source(p.settings.source);
        for (int done = 0; done < count; done += Voice::kControl)
        {
            const int n = std::min(Voice::kControl, count - done);
            voice.render(left + done, right + done, n, p.settings, source);
        }
        for (int i = 0; i < count; ++i)
        {
            left[i] *= kOutput;
            right[i] *= kOutput;
        }
    }

    float envelope() const { return voice.isActive() ? std::min(1.0f, voice.level()) : 0.0f; }
    int strikes() const { return struck; }
    int grains() const { return voice.sounding(); }

private:
    void strike(const PlayParams& p)
    {
        voice.start(p.settings);
        ++struck;
        sinceStrike = 0.0f;
    }

    float sampleRate = 48000.0f;
    const Sources* sources = nullptr;
    Voice voice;
    bool gateWas = false;
    float sinceStrike = 1.0f;
    std::array<float, kJumpBlocks> recent {};
    int position = 0;
    int struck = 0;
};
} // namespace twm::grain
