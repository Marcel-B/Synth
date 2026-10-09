#pragma once

#include "WaveDsp.h"
#include "Wavetables.h"

#include <cstdint>

namespace tonwerkwave
{
/** What the host says about time, at the start of the stretch being rendered. */
struct Transport
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;
};

/** What all voices share during one control step: performance controls and the free-running LFO phases. */
struct Shared
{
    double sampleRate = 48000.0;
    double bend = 0.0;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    std::array<double, kLfos> lfoRate {};
    std::array<double, kLfos> globalPhase {};
};

/**
 * One note: two wavetable oscillators with unison, a sub, noise, the filter and three envelopes, two LFOs and the
 * modulation matrix. Modulation runs at a control rate (every kControl samples or less); positions, warp, levels and
 * the filter's coefficients ramp across each step, so a fast wobble does not step audibly.
 */
class Voice
{
public:
    /** Samples per control step. 32 is under a millisecond at 44.1 kHz, fine for any LFO. */
    static constexpr int kControl = 32;

    Voice() : noiseLeft(nextSeed()), noiseRight(nextSeed()), random(nextSeed()) {}

    void prepare(double rate)
    {
        sampleRate = rate;
        for (auto& e : env)
            e.prepare(rate);
        combLeft.prepare(rate);
        combRight.prepare(rate);
        reset();
    }

    void reset()
    {
        for (auto& e : env)
            e.reset();
        filterLeft[0].reset();
        filterLeft[1].reset();
        filterRight[0].reset();
        filterRight[1].reset();
        combLeft.reset();
        combRight.reset();
        sustained = false;
        held = false;
    }

    /**
     * Starts `note`. A voice already sounding keeps its phases and filter, and its envelopes climb from where they
     * are, so stealing it or playing legato does not click. `retrigger` false (legato) leaves the envelopes running.
     */
    void start(int newNote, float newVelocity, const Settings& s, bool retrigger, bool glide)
    {
        const bool wasActive = isActive();
        note = newNote;
        velocity = newVelocity;
        held = true;
        sustained = false;
        if (! glide || ! wasActive)
            pitch = note;
        if (retrigger || ! wasActive)
        {
            for (int i = 0; i < kEnvelopes; ++i)
            {
                env[(std::size_t) i].set(s.env[(std::size_t) i]);
                env[(std::size_t) i].noteOn();
            }
            for (int i = 0; i < kLfos; ++i)
                if (s.lfo[(std::size_t) i].retrigger || ! wasActive)
                {
                    lfoPhase[(std::size_t) i] = 0.0;
                    lfoHeld[(std::size_t) i] = 0.5f * (float) (random.next() + 1.0);
                }
        }
        if (! wasActive)
        {
            // One voice starts at phase 0, so a single oscillator hits the same way every time; unison voices start
            // at random phases, or they would all sound like one at the attack.
            for (auto* osc : { &oscA, &oscB })
                for (int u = 0; u < kMaxUnison; ++u)
                    osc->phase[(std::size_t) u] = u == 0 ? 0.0 : 0.5 * (random.next() + 1.0);
            sub.reset();
            lfoSmooth = {};
            first = true;
            lastA = 0.0f;
        }
        ++age;
    }

    void release()
    {
        held = false;
        for (auto& e : env)
            e.noteOff();
    }

    bool isActive() const { return env[0].isActive(); }
    bool isHeld() const { return held; }
    bool isReleasing() const { return env[0].isReleasing(); }
    int currentNote() const { return note; }

    /** Set when the sustain pedal keeps a let-go note sounding. */
    bool sustained = false;
    /** When the voice last started, for stealing the oldest. */
    std::uint64_t age = 0;

    /** The table positions the last step played, in frames, for the editor's display. */
    float shownPosition(int osc) const { return osc == 0 ? oscA.position : oscB.position; }

    /** Adds `count` (at most kControl) samples to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Shared& shared)
    {
        if (! isActive())
            return;
        control(s, shared, count);
        const auto& bank = WavetableBank::instance();
        const Wavetable& tableA = bank.table(s.a.table);
        const Wavetable& tableB = bank.table(s.b.table);
        const bool needA = s.a.on || s.b.warp == Warp::fm;
        const bool needB = s.b.on || s.a.warp == Warp::fm;
        const float step = 1.0f / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const float t = (float) (i + 1) * step;
            float l = 0.0f, r = 0.0f;

            float monoB = 0.0f;
            if (needB)
            {
                float bl = 0.0f, br = 0.0f;
                monoB = oscB.tick(tableB, s.b.warp, t, lastA, bl, br);
                if (s.b.on)
                {
                    l += bl;
                    r += br;
                }
            }
            if (needA)
            {
                float al = 0.0f, ar = 0.0f;
                lastA = oscA.tick(tableA, s.a.warp, t, monoB, al, ar);
                if (s.a.on)
                {
                    l += al;
                    r += ar;
                }
            }
            const float noiseLevel = noise.at(t);
            if (noiseLevel > 0.0f)
            {
                l += noiseLevel * (float) noiseLeft.next();
                r += noiseLevel * (float) noiseRight.next();
            }
            float subSample = 0.0f;
            if (s.sub.on)
                subSample = subLevel.at(t) * (float) sub.next(s.sub.shape, subFrequency, sampleRate);
            if (! s.sub.direct)
            {
                l += subSample;
                r += subSample;
            }

            if (s.filter.on)
                filter(s.filter.mode, l, r, t);

            if (s.sub.direct)
            {
                l += subSample;
                r += subSample;
            }
            const float gain = env[0].next() * amp.at(t);
            left[i] += l * gain;
            right[i] += r * gain;
        }
        for (int e = 1; e < kEnvelopes; ++e)
            for (int i = 0; i < count; ++i)
                env[(std::size_t) e].next();
        first = false;
        if (! isActive())
            reset();
    }

private:
    /** A value that ramps from the last step's to this step's over the step; jumps on a note's first step. */
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

    /** One wavetable oscillator with its unison voices. */
    struct Oscillator
    {
        std::array<double, kMaxUnison> phase {};
        std::array<double, kMaxUnison> increment {};
        std::array<float, kMaxUnison> gainLeft {}, gainRight {};
        int voices = 1;
        int level = 0;
        float position = 0.0f;
        Ramp frame, warp, volume;

        /** The oscillator's next sample into `l` and `r` (with its level); returns the raw mono sample for FM. */
        float tick(const Wavetable& table, Warp mode, float t, float fmSource, float& l, float& r)
        {
            const float pos = frame.at(t);
            const float w = warp.at(t);
            float mono = 0.0f;
            // What the warp does with w, worked out once for all unison voices.
            double sync = 1.0, bend = 1.0, width = 1.0, fm = 0.0;
            switch (mode)
            {
                case Warp::off: break;
                case Warp::sync: sync = 1.0 + 7.0 * w; break;
                case Warp::bend: bend = std::exp2(-4.0 * w); break;
                case Warp::pwm: width = 1.0 - 0.95 * w; break;
                case Warp::fm: fm = 1.5 * w * fmSource; break;
            }
            const double inverseWidth = 1.0 / width;
            for (int u = 0; u < voices; ++u)
            {
                double p = phase[(std::size_t) u];
                phase[(std::size_t) u] = p + increment[(std::size_t) u] >= 1.0 ? p + increment[(std::size_t) u] - 1.0
                                                                            : p + increment[(std::size_t) u];
                switch (mode)
                {
                    case Warp::off: break;
                    case Warp::sync: p *= sync; p -= std::floor(p); break;
                    // Squeezes the cycle towards its start: the wave's first part plays faster, the rest slower.
                    case Warp::bend: p = p / (p + bend * (1.0 - p)); break;
                    // The cycle plays in the first part of the period; the rest holds its start, silence in most
                    // tables: Serum's PWM on any wave.
                    case Warp::pwm: p = p < width ? p * inverseWidth : 0.0; break;
                    case Warp::fm: p += fm; p -= std::floor(p); break;
                }
                const float sample = table.read(level, pos, p);
                mono += sample;
                l += sample * gainLeft[(std::size_t) u];
                r += sample * gainRight[(std::size_t) u];
            }
            const float v = volume.at(t);
            l *= v;
            r *= v;
            return mono / (float) voices;
        }
    };

    /** The modulation sources' values for this step, 0 to 1. */
    float sourceValue(ModSource source, const Shared& shared, const Settings& s) const
    {
        switch (source)
        {
            case ModSource::none: return 0.0f;
            case ModSource::lfo1: return lfoSmooth[0];
            case ModSource::lfo2: return lfoSmooth[1];
            case ModSource::env1: return env[0].current();
            case ModSource::env2: return env[1].current();
            case ModSource::env3: return env[2].current();
            case ModSource::velocity: return velocity;
            case ModSource::modWheel: return shared.modWheel;
            case ModSource::aftertouch: return shared.aftertouch;
            case ModSource::note: return (float) note / 127.0f;
            case ModSource::macro1: return s.macros[0];
            case ModSource::macro2: return s.macros[1];
            case ModSource::macro3: return s.macros[2];
            case ModSource::macro4: return s.macros[3];
        }
        return 0.0f;
    }

    /** Runs the LFOs, the matrix and the glide for the next `count` samples and sets the ramps. */
    void control(const Settings& s, const Shared& shared, int count)
    {
        for (int i = 0; i < kEnvelopes; ++i)
            env[(std::size_t) i].set(s.env[(std::size_t) i]);

        const double seconds = count / sampleRate;
        // About 2 ms of smoothing takes the click out of a square or random LFO's edges.
        const float smoothing = first ? 0.0f : (float) std::exp(-seconds / 0.002);
        for (int i = 0; i < kLfos; ++i)
        {
            const auto& lfo = s.lfo[(std::size_t) i];
            auto& phase = lfoPhase[(std::size_t) i];
            double now = phase;
            if (lfo.retrigger)
            {
                phase += shared.lfoRate[(std::size_t) i] * seconds;
                phase -= std::floor(phase);
            }
            else
            {
                now = shared.globalPhase[(std::size_t) i];
                phase = now;
            }
            // A new random value whenever the phase wraps.
            if (now < lastLfoPhase[(std::size_t) i])
                lfoHeld[(std::size_t) i] = 0.5f * (float) (random.next() + 1.0);
            lastLfoPhase[(std::size_t) i] = now;
            const float value = lfoShape(lfo.shape, now, lfoHeld[(std::size_t) i]);
            lfoSmooth[(std::size_t) i] = value + (lfoSmooth[(std::size_t) i] - value) * smoothing;
        }

        std::array<float, (std::size_t) ModTarget::count> mod {};
        for (const auto& slot : s.mods)
        {
            if (slot.source == ModSource::none || slot.target == ModTarget::none || std::abs(slot.amount) < 1.0e-6f)
                continue;
            float v = sourceValue(slot.source, shared, s);
            if (slot.bipolar)
                v = 2.0f * v - 1.0f;
            mod[(std::size_t) slot.target] += slot.amount * v;
        }
        auto m = [&mod](ModTarget target) { return mod[(std::size_t) target]; };

        if (s.voiceMode != VoiceMode::poly && s.glide > 0.0f && ! first)
            pitch += (note - pitch) * (1.0 - std::exp(-seconds / (s.glide / 4.0)));
        else
            pitch = note;
        const double played = pitch + shared.bend;

        setOscillator(oscA, s.a, played, m(ModTarget::aPosition), m(ModTarget::aWarp), m(ModTarget::aPitch),
                      m(ModTarget::aLevel), m(ModTarget::aDetune));
        setOscillator(oscB, s.b, played, m(ModTarget::bPosition), m(ModTarget::bWarp), m(ModTarget::bPitch),
                      m(ModTarget::bLevel), m(ModTarget::bDetune));

        subFrequency = 440.0 * std::exp2((played + 12.0 * s.sub.octave - 69.0) / 12.0);
        subLevel.set(std::clamp(s.sub.level + m(ModTarget::subLevel), 0.0f, 1.0f), first);
        noise.set(0.5f * std::clamp(s.noise + m(ModTarget::noise), 0.0f, 1.0f), first);
        amp.set(std::clamp(1.0f + m(ModTarget::amp), 0.0f, 2.0f), first);

        // The cutoff moves in semitones: the matrix reaches ten octaves at 100 %, the key tracking follows the note.
        const double cutoffNote = 12.0 * std::log2(std::max(20.0f, s.filter.cutoff) / 440.0) + 69.0
                                  + s.filter.keytrack * (pitch - 60.0) + 120.0 * m(ModTarget::cutoff);
        const double cutoff = std::clamp(440.0 * std::exp2((cutoffNote - 69.0) / 12.0), 20.0,
                                         std::min(20000.0, 0.45 * sampleRate));
        const float resonance = std::clamp(s.filter.resonance + m(ModTarget::resonance), 0.0f, 1.0f);
        filterG.set((float) std::tan(kPi * cutoff / sampleRate), first);
        filterK.set(2.0f - 1.96f * resonance, first);
        combDelay.set((float) (sampleRate / cutoff), first);
        combFeedback = 0.97f * resonance;
        const float drive = std::clamp(s.filter.drive + m(ModTarget::filterDrive), 0.0f, 1.0f);
        driveGain = std::pow(10.0f, 1.2f * drive);
        // Up to 24 dB into the clipper, about half of it taken back after.
        driveMakeUp = std::pow(driveGain, -0.6f);
        driveOn = drive > 0.0f;
    }

    void setOscillator(Oscillator& osc, const OscSettings& o, double played, float position, float warp, float pitchMod,
                       float levelMod, float detuneMod)
    {
        osc.voices = std::clamp(o.unison, 1, kMaxUnison);
        const float w = std::clamp(o.warpAmount + warp, 0.0f, 1.0f);
        osc.position = std::clamp(o.position + position, 0.0f, 1.0f) * (float) (kFrames - 1);
        osc.frame.set(osc.position, first);
        osc.warp.set(w, first);
        osc.volume.set(std::clamp(o.level + levelMod, 0.0f, 1.0f), first);

        const double base = played + 12.0 * o.octave + o.semitones + o.fine / 100.0 + 24.0 * pitchMod;
        const double detune = std::max(0.0f, o.detune + 100.0f * detuneMod) / 100.0;
        double total = 0.0;
        std::array<float, kMaxUnison> weight {};
        for (int u = 0; u < osc.voices; ++u)
        {
            // -1 to 1 across the voices: their place in pitch and in the stereo field.
            const double spread = osc.voices == 1 ? 0.0 : 2.0 * u / (osc.voices - 1) - 1.0;
            const double hz = 440.0 * std::exp2((base + 0.5 * detune * spread - 69.0) / 12.0);
            osc.increment[(std::size_t) u] = std::min(0.49, hz / sampleRate);
            weight[(std::size_t) u] = 1.0f - (float) std::abs(spread) * (1.0f - o.blend);
            total += weight[(std::size_t) u] * weight[(std::size_t) u];
            // A pan law that keeps a single centred voice at full level on both sides.
            const float pan = (float) spread * o.width;
            osc.gainLeft[(std::size_t) u] = std::sqrt(1.0f - pan);
            osc.gainRight[(std::size_t) u] = std::sqrt(1.0f + pan);
        }
        // Voices at random phases add up in power, so the sum is scaled to keep one voice's loudness.
        const float norm = 1.0f / (float) std::sqrt(std::max(1.0e-6, total));
        double fastest = 0.0;
        for (int u = 0; u < osc.voices; ++u)
        {
            osc.gainLeft[(std::size_t) u] *= weight[(std::size_t) u] * norm;
            osc.gainRight[(std::size_t) u] *= weight[(std::size_t) u] * norm;
            fastest = std::max(fastest, osc.increment[(std::size_t) u]);
        }
        // Warping raises the frequencies the table is read at; the band-limited level allows for it.
        switch (o.warp)
        {
            case Warp::off: break;
            case Warp::sync: fastest *= 1.0 + 7.0 * w; break;
            // Bend plays the cycle's start up to 16 times as fast; allowing for all of it would dull the rest.
            case Warp::bend: fastest *= std::exp2(2.0 * w); break;
            case Warp::pwm: fastest /= 1.0 - 0.95 * w; break;
            case Warp::fm: fastest *= 1.0 + 3.0 * w; break;
        }
        osc.level = Wavetable::levelFor(fastest);
    }

    void filter(FilterMode mode, float& l, float& r, float t)
    {
        if (driveOn)
        {
            l = softClip(l * driveGain) * driveMakeUp;
            r = softClip(r * driveGain) * driveMakeUp;
        }
        if (mode == FilterMode::comb)
        {
            const float delay = combDelay.at(t);
            const float makeUp = std::sqrt(1.0f - combFeedback * combFeedback);
            l = combLeft.process(l, delay, combFeedback) * makeUp;
            r = combRight.process(r, delay, combFeedback) * makeUp;
            return;
        }
        const float g = filterG.at(t);
        const float k = filterK.at(t);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        if (mode == FilterMode::low24)
        {
            // Two stages; only the second resonates, so 24 dB keeps one clear peak instead of a doubled one.
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
            // Scaled by k, so the peak stays at unity however narrow the band.
            case FilterMode::band12: l = k * filterLeft[0].band; r = k * filterRight[0].band; break;
            case FilterMode::notch:
                l = filterLeft[0].low + filterLeft[0].high;
                r = filterRight[0].low + filterRight[0].high;
                break;
            case FilterMode::low24:
            case FilterMode::comb: break;
        }
    }

    static std::uint32_t nextSeed()
    {
        static std::uint32_t count = 0;
        return 0x2545f491u * (++count * 2u + 1u);
    }

    double sampleRate = 48000.0;
    int note = 60;
    double pitch = 60.0;
    float velocity = 1.0f;
    bool held = false;
    bool first = true;
    float lastA = 0.0f;

    std::array<Adsr, kEnvelopes> env {};
    std::array<double, kLfos> lfoPhase {};
    std::array<double, kLfos> lastLfoPhase {};
    std::array<float, kLfos> lfoHeld {};
    std::array<float, kLfos> lfoSmooth {};

    Oscillator oscA, oscB;
    tonwerk::BlepOscillator sub;
    double subFrequency = 55.0;
    Ramp subLevel, noise, amp;
    tonwerk::Noise noiseLeft, noiseRight, random;

    std::array<SvfStage, 2> filterLeft {}, filterRight {};
    Comb combLeft, combRight;
    Ramp filterG, filterK, combDelay;
    float combFeedback = 0.0f;
    float driveGain = 1.0f;
    float driveMakeUp = 1.0f;
    bool driveOn = false;
};

/**
 * The instrument without a host: voices, note handling (poly, mono, legato with glide), sustain pedal and the
 * performance controls. Rendering adds nothing global; the processor does the distortion and the master level.
 */
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
                    v.start(note, velocity, s, true, false);
                    v.age = ++clock;
                    return;
                }
            auto& v = freeVoice();
            v.start(note, velocity, s, true, false);
            v.age = ++clock;
            return;
        }
        // Mono and legato: one voice and a stack of held keys; the newest key sounds.
        lastVelocity = velocity;
        const bool overlapping = ! held.empty();
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        held.push_back(note);
        for (std::size_t i = 1; i < voices.size(); ++i)
            if (voices[i].isActive() && ! voices[i].isReleasing())
                voices[i].release();
        const bool legato = s.voiceMode == VoiceMode::legato && overlapping && voices[0].isActive();
        // Mono slides into every note, legato only into one played before the last was let go.
        const bool glide = s.voiceMode == VoiceMode::mono || legato;
        voices[0].start(note, velocity, s, ! legato, glide);
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
            // Back to the key still held, as a legato step in either mode.
            voices[0].start(held.back(), lastVelocity, s, s.voiceMode == VoiceMode::mono, true);
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

    /** `semitones` is the wheel already scaled by the bend range. */
    void setBend(double semitones) { shared.bend = semitones; }
    void setModWheel(float value) { shared.modWheel = value; }
    void setAftertouch(float value) { shared.aftertouch = value; }

    /** Adds `count` samples of all voices to `left` and `right`. */
    void render(float* left, float* right, int count, const Settings& s, const Transport& transport)
    {
        int done = 0;
        while (done < count)
        {
            const int n = std::min(Voice::kControl, count - done);
            const double seconds = (double) done / shared.sampleRate;
            for (int i = 0; i < kLfos; ++i)
            {
                const auto& lfo = s.lfo[(std::size_t) i];
                const double rate = lfo.sync ? transport.bpm / 60.0 / divisionBeats(lfo.division)
                                             : (double) lfo.rate;
                shared.lfoRate[(std::size_t) i] = rate;
                auto& phase = shared.globalPhase[(std::size_t) i];
                if (lfo.sync && transport.playing && transport.hasPosition)
                {
                    // Locked to the song position: the wobble lands on the beat wherever playback starts.
                    const double beats = transport.ppq + seconds * transport.bpm / 60.0;
                    phase = beats / divisionBeats(lfo.division);
                    phase -= std::floor(phase);
                }
                else
                {
                    phase += rate * n / shared.sampleRate;
                    phase -= std::floor(phase);
                }
            }
            for (auto& v : voices)
                v.render(left + done, right + done, n, s, shared);
            done += n;
        }
    }

    /** The voice started last that is still sounding, for the display; null when all are quiet. */
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
    /** An idle voice, else the oldest one let go, else the oldest of all. */
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
} // namespace tonwerkwave
