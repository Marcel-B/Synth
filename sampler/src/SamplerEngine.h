#pragma once

#include "SampleData.h"
#include "WaveDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Tonwerk Sampler's sound, plain C++: voices that read a `Sample` at a pitch, in a region, looped or not, forwards or
 * backwards, or one slice of it per key; a filter with its own envelope, an amplitude envelope, one LFO and a lo-fi
 * stage after all voices. Borrows `Adsr`, `SvfStage` and `lfoShape` from Tonwerk Wavetable's `WaveDsp.h`. Pitch,
 * filter and LFO are set every `kControl` samples, the envelopes run per sample.
 */
namespace tonwerksampler
{
using tonwerkwave::Adsr;
using tonwerkwave::EnvSettings;
using tonwerkwave::kPi;
using tonwerkwave::LfoShape;
using tonwerkwave::SvfStage;
using tonwerkwave::VoiceMode;

inline constexpr int kVoices = 16;
/** The key of the first slice: C1 in Logic's names, as in Logic's own Quick Sampler. */
inline constexpr int kFirstSliceKey = 36;
inline constexpr int kMaxSlices = 32;

// Menu orders, saved in projects: append only.
enum class Mode { classic, slices };
enum class LoopMode { off, forward, pingPong };
enum class Playback { gate, oneShot };
enum class SliceMode { grid, transients };
enum class FilterType { off, low12, low24, high12, band12 };
enum class LfoTarget { off, pitch, cutoff, volume, pan };

struct Settings
{
    int source = 0;
    Mode mode = Mode::classic;
    /** The key at which the sample plays at its own speed. */
    int root = 60;
    int tune = 0;
    float fine = 0.0f;
    /** The region played, as shares of the sample. */
    float start = 0.0f;
    float end = 1.0f;
    bool reverse = false;
    Playback playback = Playback::gate;
    LoopMode loop = LoopMode::off;
    float loopStart = 0.3f;
    float loopEnd = 0.8f;
    /** Seconds of the sample over which a forward loop's end blends into its start. */
    float crossfade = 0.05f;
    int slices = 8;
    SliceMode sliceMode = SliceMode::grid;
    /** A new slice cuts the one still sounding, as on an MPC's mute group. */
    bool choke = true;
    EnvSettings amp { 0.002f, 0.3f, 1.0f, 0.15f };
    /** How much the key's velocity sets the level: 0 every note full, 1 the level follows velocity. */
    float velocity = 0.5f;
    FilterType filter = FilterType::off;
    float cutoff = 20000.0f;
    float resonance = 0.0f;
    /** The filter envelope's reach, -1 to 1 for six octaves down or up. */
    float envAmount = 0.0f;
    float keytrack = 0.0f;
    EnvSettings filterEnv { 0.002f, 0.4f, 0.0f, 0.3f };
    LfoShape lfoShape = LfoShape::sine;
    bool lfoSync = false;
    float lfoRate = 5.0f;
    int lfoDivision = tonwerkwave::kEighth;
    LfoTarget lfoTarget = LfoTarget::off;
    float lfoAmount = 0.2f;
    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f;
    int bendRange = 2;
    /** Rate and bits taken down together, as on the old 12-bit samplers. */
    float lofi = 0.0f;
    /** Two keys at full velocity stay under full scale with the hall. */
    float masterDb = -6.0f;
};

struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/** Where slices begin, within the region; `count` of them, the last ending at the region's end. */
struct SliceMap
{
    std::array<double, kMaxSlices + 1> bounds {};
    int count = 0;
};

/**
 * Cuts [from, to) into `count` slices: even ones, or at the strongest onsets of the sample that lie inside, the
 * region's start always the first. Fewer onsets than slices leave the rest even. No allocation.
 */
inline SliceMap sliceRegion(const Sample& sample, double from, double to, int count, SliceMode mode)
{
    SliceMap map;
    count = std::clamp(count, 1, kMaxSlices);
    map.count = count;
    if (mode == SliceMode::transients)
    {
        std::array<double, kMaxSlices> starts {};
        int found = 0;
        starts[(std::size_t) found++] = from;
        // Not closer than 30 ms to a start already taken, so one hit is one slice.
        const double gap = 0.03 * sample.sampleRate;
        for (const auto& onset : sample.onsets)
        {
            if (found >= count)
                break;
            if (onset.first < from + gap || onset.first > to - gap)
                continue;
            bool apart = true;
            for (int i = 0; i < found && apart; ++i)
                apart = std::abs(starts[(std::size_t) i] - onset.first) >= gap;
            if (apart)
                starts[(std::size_t) found++] = onset.first;
        }
        if (found == count)
        {
            std::sort(starts.begin(), starts.begin() + found);
            for (int i = 0; i < count; ++i)
                map.bounds[(std::size_t) i] = starts[(std::size_t) i];
            map.bounds[(std::size_t) count] = to;
            return map;
        }
    }
    for (int i = 0; i <= count; ++i)
        map.bounds[(std::size_t) i] = from + (to - from) * i / count;
    return map;
}

/** One playing note. */
class Voice
{
public:
    static constexpr int kControl = 32;

    void prepare(double rate)
    {
        sampleRate = rate;
        amp.prepare(rate);
        filterEnv.prepare(rate);
        reset();
    }

    void reset()
    {
        amp.reset();
        filterEnv.reset();
        for (auto& s : stages)
            s.reset();
        active = false;
        killing = false;
    }

    /**
     * Starts the note on the region [from, to) of the sample; `slice` mode leaves out the key's pitch and the loop.
     * `legato` keeps the place in the sample and the envelopes, only the pitch moves on.
     */
    void start(int noteNumber, float velocityValue, const Settings& s, double from, double to, bool slice, bool legato, std::uint64_t order)
    {
        note = noteNumber;
        age = order;
        held = true;
        sliced = slice;
        targetPitch = slice ? 0.0 : (double) (noteNumber - s.root);
        if (legato && active && ! killing)
            return;
        currentPitch = targetPitch;
        regionFrom = from;
        regionTo = std::max(from + 1.0, to);
        place = 0.0;
        direction = 1.0;
        gain = (1.0f - s.velocity) + s.velocity * velocityValue;
        keyNote = noteNumber;
        amp.set(s.amp);
        filterEnv.set(s.filterEnv);
        amp.noteOn();
        filterEnv.noteOn();
        active = true;
        killing = false;
        fade = 1.0f;
        for (auto& stage : stages)
            stage.reset();
    }

    void glideTo(int noteNumber, const Settings& s)
    {
        note = noteNumber;
        targetPitch = sliced ? 0.0 : (double) (noteNumber - s.root);
    }

    void release()
    {
        held = false;
        amp.noteOff();
        filterEnv.noteOff();
    }

    /** Out within about 5 ms: a choked slice or a stolen voice. */
    void kill() { killing = true; }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    bool isSliced() const { return sliced; }
    int playingNote() const { return note; }
    std::uint64_t order() const { return age; }
    float level() const { return amp.current() * fade; }
    /** The place in the sample (samples of level 0), for the editor's play heads. */
    double position() const { return sampleIndex(place); }

    /** Adds the voice to `left` and `right`; `pitchMod` and `cutoffMod` in semitones and octaves, `pan` -1 to 1. */
    void render(float* left, float* right, int count, const Settings& s, const Sample& sample, double bend,
                double pitchMod, double cutoffMod, float volumeMod, float pan)
    {
        if (! active)
            return;
        if (sample.empty())
        {
            reset();
            return;
        }
        reverseNow = s.reverse;
        amp.set(s.amp);
        filterEnv.set(s.filterEnv);
        const double length = regionTo - regionFrom;
        // The loop in the voice's own coordinates: from the region's start in the direction it plays.
        const bool looping = ! sliced && s.loop != LoopMode::off;
        double loopFrom = 0.0, loopTo = length;
        if (looping)
        {
            const double a = std::clamp((double) s.loopStart * (double) sample.length(), regionFrom, regionTo);
            const double b = std::clamp((double) s.loopEnd * (double) sample.length(), regionFrom, regionTo);
            const double low = std::min(a, b), high = std::max(a, b);
            loopFrom = s.reverse ? regionTo - high : low - regionFrom;
            loopTo = s.reverse ? regionTo - low : high - regionFrom;
            if (loopTo - loopFrom < 64.0)
                loopTo = std::min(length, loopFrom + 64.0);
        }
        const double loopLength = loopTo - loopFrom;
        const double crossfade = s.loop == LoopMode::forward
                                     ? std::min({ (double) s.crossfade * sample.sampleRate, 0.5 * loopLength, loopFrom })
                                     : 0.0;
        const float leftGain = std::sqrt(0.5f * (1.0f - pan)) * 1.41421356f;
        const float rightGain = std::sqrt(0.5f * (1.0f + pan)) * 1.41421356f;
        const bool stereo = sample.channels > 1;

        int done = 0;
        while (done < count && active)
        {
            const int n = std::min(kControl, count - done);
            // Glide in the pitch, a fixed share per control step for the time constant `glide`.
            if (s.glide > 0.0005f)
                currentPitch += (targetPitch - currentPitch)
                                * (1.0 - std::exp(-(double) n / (std::max(0.001, (double) s.glide) * sampleRate)));
            else
                currentPitch = targetPitch;
            const double semitones = currentPitch + s.tune + s.fine / 100.0 + bend + pitchMod;
            const double step = sample.sampleRate / sampleRate * std::pow(2.0, semitones / 12.0);
            // The copy that keeps the read at most 1.5 samples a step: what folds back then lands above 15 kHz.
            int level = 0;
            while (level < Sample::kLevels - 1 && step / (double) (1 << level) > 1.5)
                ++level;
            setFilter(s, cutoffMod);

            for (int i = 0; i < n; ++i)
            {
                const double index = sampleIndex(place);
                float l = sample.read(0, level, index);
                float r = stereo ? sample.read(1, level, index) : l;
                if (crossfade > 0.0 && direction > 0.0 && place > loopTo - crossfade)
                {
                    // The loop's end blends into what comes before its start, so the seam has no step.
                    const float w = (float) ((place - (loopTo - crossfade)) / crossfade);
                    const double earlier = sampleIndex(place - loopLength);
                    const float el = sample.read(0, level, earlier);
                    const float er = stereo ? sample.read(1, level, earlier) : el;
                    l += (el - l) * w;
                    r += (er - r) * w;
                }
                float env = amp.next();
                const float shape = filterEnv.next();
                if (killing)
                {
                    fade -= 1.0f / (0.005f * (float) sampleRate);
                    if (fade <= 0.0f)
                    {
                        reset();
                        return;
                    }
                }
                if (! looping)
                {
                    // The last 3 ms before the region's end fade out, so a cut sample does not click.
                    const double remaining = (length - place) / step;
                    env *= (float) std::min(1.0, remaining / (0.003 * sampleRate));
                }
                filterShape = shape;
                const float out = env * fade * gain * volumeMod;
                filterSample(l, r, s.filter);
                left[done + i] += l * out * leftGain;
                right[done + i] += r * out * rightGain;

                place += step * direction;
                if (looping && s.loop == LoopMode::forward && place >= loopTo)
                    place -= loopLength;
                else if (looping && s.loop == LoopMode::pingPong)
                {
                    if (direction > 0.0 && place >= loopTo)
                    {
                        place = 2.0 * loopTo - place;
                        direction = -1.0;
                    }
                    else if (direction < 0.0 && place <= loopFrom)
                    {
                        place = 2.0 * loopFrom - place;
                        direction = 1.0;
                    }
                }
                if (! amp.isActive() || (! looping && place >= length))
                {
                    reset();
                    return;
                }
            }
            done += n;
        }
    }

private:
    double sampleIndex(double u) const { return reverseNow ? regionTo - 1.0 - u : regionFrom + u; }

    void setFilter(const Settings& s, double cutoffMod)
    {
        if (s.filter == FilterType::off)
            return;
        const double octaves = 6.0 * s.envAmount * filterShape + s.keytrack * (keyNote - 60) / 12.0 + cutoffMod;
        const double hz = std::clamp((double) s.cutoff * std::pow(2.0, octaves), 20.0, 0.45 * sampleRate);
        g = (float) std::tan(kPi * hz / sampleRate);
        // Resonance 0 to 1: Q from 0.5 to about 12.
        k = 2.0f - 1.92f * s.resonance;
        a1 = 1.0f / (1.0f + g * (g + k));
    }

    void filterSample(float& l, float& r, FilterType type)
    {
        if (type == FilterType::off)
            return;
        auto one = [this, type](float x, SvfStage& first, SvfStage& second) {
            first.process(x, g, k, a1);
            switch (type)
            {
                case FilterType::low12:
                    return first.low;
                case FilterType::low24:
                    // The second stage without resonance, so the peak stays where one stage puts it.
                    second.process(first.low, g, 1.414f, 1.0f / (1.0f + g * (g + 1.414f)));
                    return second.low;
                case FilterType::high12:
                    return first.high;
                case FilterType::band12:
                    return first.band * k;
                case FilterType::off:
                    break;
            }
            return x;
        };
        l = one(l, stages[0], stages[1]);
        r = one(r, stages[2], stages[3]);
    }

    double sampleRate = 48000.0;
    Adsr amp, filterEnv;
    std::array<SvfStage, 4> stages {};
    float g = 1.0f, k = 2.0f, a1 = 0.5f;
    float filterShape = 0.0f;
    double regionFrom = 0.0, regionTo = 1.0;
    double place = 0.0;
    double direction = 1.0;
    double currentPitch = 0.0, targetPitch = 0.0;
    float gain = 1.0f;
    float fade = 1.0f;
    int note = -1;
    int keyNote = 60;
    std::uint64_t age = 0;
    bool active = false, held = false, killing = false, sliced = false, reverseNow = false;
};

/** All voices, the note logic for poly, mono and legato, slices, the LFO and the lo-fi stage. */
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& v : voices)
            v.prepare(rate);
        allNotesOff();
        lfoPhase = 0.0;
        crushHeld = { 0.0f, 0.0f };
        crushPhase = 0.0;
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            v.reset();
        stackSize = 0;
        sustain = false;
        for (auto& d : deferred)
            d = false;
    }

    void setBend(double semitones) { bend = semitones; }
    void setSustain(bool down)
    {
        sustain = down;
        if (! down)
            for (int n = 0; n < 128; ++n)
                if (deferred[(std::size_t) n])
                {
                    deferred[(std::size_t) n] = false;
                    releaseNote(n);
                }
    }

    void noteOn(int note, float velocity, const Settings& s, const Sample& sample)
    {
        deferred[(std::size_t) note] = false;
        if (sample.empty())
            return;
        const double length = (double) sample.length();
        const double from = std::min(s.start, s.end) * length;
        const double to = std::max(std::max(s.start, s.end) * length, from + 64.0);
        if (s.mode == Mode::slices)
        {
            const int slice = note - kFirstSliceKey;
            if (slice < 0 || slice >= s.slices)
                return;
            const auto map = sliceRegion(sample, from, std::min(to, length), s.slices, s.sliceMode);
            for (auto& v : voices)
                if (v.isActive() && (s.choke || v.playingNote() == note))
                    v.kill();
            free().start(note, velocity, s, map.bounds[(std::size_t) slice],
                         map.bounds[(std::size_t) slice + 1], true, false, ++counter);
            return;
        }
        if (s.voiceMode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && v.playingNote() == note)
                    v.kill();
            free().start(note, velocity, s, from, std::min(to, length), false, false, ++counter);
            return;
        }
        // Mono and legato: the newest held key sounds; legato keeps the voice going while a key is held.
        push(note);
        auto& v = voices[0];
        const bool slide = s.voiceMode == VoiceMode::legato && stackSize > 1 && v.isActive() && v.isHeld();
        v.start(note, velocity, s, from, std::min(to, length), false, slide, ++counter);
        if (slide)
            v.glideTo(note, s);
    }

    void noteOff(int note, const Settings& s)
    {
        if (sustain)
        {
            deferred[(std::size_t) note] = true;
            return;
        }
        if (s.mode == Mode::classic && s.voiceMode != VoiceMode::poly)
        {
            remove(note);
            auto& v = voices[0];
            if (v.playingNote() != note)
                return;
            if (stackSize > 0)
            {
                // Back to the key still held, without a new attack.
                v.glideTo(stack[(std::size_t) stackSize - 1], s);
                return;
            }
        }
        if (s.playback == Playback::gate)
            releaseNote(note);
    }

    /** Renders all voices into `left` and `right` (added to, not cleared). */
    void render(float* left, float* right, int count, const Settings& s, const Sample& sample, const Transport& t)
    {
        int done = 0;
        while (done < count)
        {
            const int n = std::min(Voice::kControl, count - done);
            const double seconds = (double) done / sampleRate;
            double phase;
            if (s.lfoSync)
            {
                const double beats = tonwerkwave::divisionBeats(s.lfoDivision);
                if (t.playing && t.hasPosition)
                    phase = (t.ppq + seconds * t.bpm / 60.0) / beats;
                else
                    phase = lfoPhase;
                lfoPhase = phase + (double) n / sampleRate * t.bpm / 60.0 / beats;
            }
            else
            {
                phase = lfoPhase;
                lfoPhase += (double) n / sampleRate * s.lfoRate;
            }
            phase -= std::floor(phase);
            lfoPhase -= std::floor(lfoPhase);
            if (phase < lastPhase)
                held = (float) random.next();
            lastPhase = phase;
            const float value = tonwerkwave::lfoShape(s.lfoShape, phase, held);
            const float bipolar = 2.0f * value - 1.0f;
            double pitchMod = 0.0, cutoffMod = 0.0;
            float volume = 1.0f, pan = 0.0f;
            switch (s.lfoTarget)
            {
                case LfoTarget::pitch:
                    pitchMod = 12.0 * s.lfoAmount * bipolar;
                    break;
                case LfoTarget::cutoff:
                    cutoffMod = 4.0 * s.lfoAmount * bipolar;
                    break;
                case LfoTarget::volume:
                    volume = 1.0f - s.lfoAmount * value;
                    break;
                case LfoTarget::pan:
                    pan = s.lfoAmount * bipolar;
                    break;
                case LfoTarget::off:
                    break;
            }
            for (auto& v : voices)
                v.render(left + done, right + done, n, s, sample, bend, pitchMod, cutoffMod, volume, pan);
            done += n;
        }
        crush(left, right, count, s.lofi);
    }

    /** The newest sounding voices' places as shares of the sample, for the editor; -1 where none. */
    template <std::size_t N>
    void positions(std::array<float, N>& out, double length) const
    {
        out.fill(-1.0f);
        std::array<const Voice*, kVoices> sorted {};
        int count = 0;
        for (const auto& v : voices)
            if (v.isActive())
                sorted[(std::size_t) count++] = &v;
        std::sort(sorted.begin(), sorted.begin() + count, [](auto* a, auto* b) { return a->order() > b->order(); });
        for (int i = 0; i < std::min(count, (int) N); ++i)
            out[(std::size_t) i] = length > 0.0 ? (float) (sorted[(std::size_t) i]->position() / length) : -1.0f;
    }

    int activeVoices() const
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

private:
    Voice& free()
    {
        for (auto& v : voices)
            if (! v.isActive())
                return v;
        // All busy: the oldest released voice, else the oldest; it starts over from its new note.
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (! v.isHeld() && (oldest == nullptr || v.order() < oldest->order()))
                oldest = &v;
        if (oldest == nullptr)
            for (auto& v : voices)
                if (oldest == nullptr || v.order() < oldest->order())
                    oldest = &v;
        oldest->reset();
        return *oldest;
    }

    void releaseNote(int note)
    {
        for (auto& v : voices)
            if (v.isActive() && v.isHeld() && v.playingNote() == note)
                v.release();
    }

    void push(int note)
    {
        remove(note);
        if (stackSize < (int) stack.size())
            stack[(std::size_t) stackSize++] = note;
    }

    void remove(int note)
    {
        int out = 0;
        for (int i = 0; i < stackSize; ++i)
            if (stack[(std::size_t) i] != note)
                stack[(std::size_t) out++] = stack[(std::size_t) i];
        stackSize = out;
    }

    /**
     * The lo-fi stage: the rate held down to as low as an eighth, the bits from 16 to 6. Without dither and without a
     * filter in front, so it aliases the way the old samplers did.
     */
    void crush(float* left, float* right, int count, float amount)
    {
        if (amount <= 0.001f)
            return;
        const double hold = std::pow(2.0, -3.0 * amount);
        const float steps = std::pow(2.0f, 15.0f - 10.0f * amount);
        for (int i = 0; i < count; ++i)
        {
            crushPhase += hold;
            if (crushPhase >= 1.0)
            {
                crushPhase -= 1.0;
                crushHeld[0] = std::round(left[i] * steps) / steps;
                crushHeld[1] = std::round(right[i] * steps) / steps;
            }
            left[i] = crushHeld[0];
            right[i] = crushHeld[1];
        }
    }

    struct Random
    {
        std::uint32_t state = 0x2545f491u;
        double next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (double) state / 4294967296.0;
        }
    };

    std::array<Voice, kVoices> voices;
    std::array<int, 16> stack {};
    int stackSize = 0;
    std::array<bool, 128> deferred {};
    bool sustain = false;
    double sampleRate = 48000.0;
    double bend = 0.0;
    double lfoPhase = 0.0, lastPhase = 0.0;
    float held = 0.5f;
    Random random;
    std::uint64_t counter = 0;
    double crushPhase = 0.0;
    std::array<float, 2> crushHeld {};
};
} // namespace tonwerksampler
