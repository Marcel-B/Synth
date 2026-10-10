#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

/**
 * Tonwerk DX: six-operator FM in the manner of Yamaha's DX7. Plain C++, no JUCE: the patch in the DX7's own units
 * (0 to 99 for levels and rates, the 32 algorithms, coarse and fine ratios), the operators' four-rate envelopes in
 * the log domain, the LFO and a voice. Nothing of Yamaha's is copied but the published structure of the instrument;
 * the sounds are this plugin's own, and `DxSysex.h` reads DX7 voice files the user owns.
 */
namespace tonwerkdx
{
inline constexpr int kOperators = 6;
inline constexpr int kAlgorithms = 32;
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/**
 * One algorithm: who modulates whom (0 is operator 1), which operators are heard and which one feeds back into
 * itself. Modulators always have a higher number than their targets, so computing operator 6 down to 1 has every
 * input ready. Algorithms 4 and 6 feed back over two or three operators on the DX7; here the loop is on operator 6
 * alone, which sounds close.
 */
struct Algorithm
{
    std::array<std::uint8_t, kOperators> modulators {}; // bit m set: operator m modulates this one
    std::uint8_t carriers = 0;                          // bit n set: operator n is heard
    int feedback = 5;                                   // the operator with feedback, 0-based

    bool isCarrier(int op) const { return (carriers >> op) & 1; }
    bool modulates(int from, int to) const { return (modulators[(std::size_t) to] >> from) & 1; }
    int carrierCount() const
    {
        int n = 0;
        for (int op = 0; op < kOperators; ++op)
            n += isCarrier(op) ? 1 : 0;
        return n;
    }
};

namespace detail
{
/** An algorithm from its links, written as on the DX7's chart: {from, to} with operators counted from 1. */
inline Algorithm make(std::initializer_list<std::array<int, 2>> links, std::initializer_list<int> heard, int feedback)
{
    Algorithm a;
    for (const auto& [from, to] : links)
        a.modulators[(std::size_t) (to - 1)] |= (std::uint8_t) (1 << (from - 1));
    for (int op : heard)
        a.carriers |= (std::uint8_t) (1 << (op - 1));
    a.feedback = feedback - 1;
    return a;
}
} // namespace detail

/** The DX7's 32 algorithms, after the chart printed on the instrument. */
inline const std::array<Algorithm, kAlgorithms>& algorithms()
{
    using detail::make;
    static const std::array<Algorithm, kAlgorithms> table { {
        make({ { 2, 1 }, { 6, 5 }, { 5, 4 }, { 4, 3 } }, { 1, 3 }, 6),
        make({ { 2, 1 }, { 6, 5 }, { 5, 4 }, { 4, 3 } }, { 1, 3 }, 2),
        make({ { 3, 2 }, { 2, 1 }, { 6, 5 }, { 5, 4 } }, { 1, 4 }, 6),
        make({ { 3, 2 }, { 2, 1 }, { 6, 5 }, { 5, 4 } }, { 1, 4 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 6, 5 } }, { 1, 3, 5 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 6, 5 } }, { 1, 3, 5 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 5, 3 }, { 6, 5 } }, { 1, 3 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 5, 3 }, { 6, 5 } }, { 1, 3 }, 4),
        make({ { 2, 1 }, { 4, 3 }, { 5, 3 }, { 6, 5 } }, { 1, 3 }, 2),
        make({ { 3, 2 }, { 2, 1 }, { 5, 4 }, { 6, 4 } }, { 1, 4 }, 3),
        make({ { 3, 2 }, { 2, 1 }, { 5, 4 }, { 6, 4 } }, { 1, 4 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 5, 3 }, { 6, 3 } }, { 1, 3 }, 2),
        make({ { 2, 1 }, { 4, 3 }, { 5, 3 }, { 6, 3 } }, { 1, 3 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 5, 4 }, { 6, 4 } }, { 1, 3 }, 6),
        make({ { 2, 1 }, { 4, 3 }, { 5, 4 }, { 6, 4 } }, { 1, 3 }, 2),
        make({ { 2, 1 }, { 3, 1 }, { 5, 1 }, { 4, 3 }, { 6, 5 } }, { 1 }, 6),
        make({ { 2, 1 }, { 3, 1 }, { 5, 1 }, { 4, 3 }, { 6, 5 } }, { 1 }, 2),
        make({ { 2, 1 }, { 3, 1 }, { 4, 1 }, { 5, 4 }, { 6, 5 } }, { 1 }, 3),
        make({ { 2, 1 }, { 3, 2 }, { 6, 4 }, { 6, 5 } }, { 1, 4, 5 }, 6),
        make({ { 3, 1 }, { 3, 2 }, { 5, 4 }, { 6, 4 } }, { 1, 2, 4 }, 3),
        make({ { 3, 1 }, { 3, 2 }, { 6, 4 }, { 6, 5 } }, { 1, 2, 4, 5 }, 3),
        make({ { 2, 1 }, { 6, 3 }, { 6, 4 }, { 6, 5 } }, { 1, 3, 4, 5 }, 6),
        make({ { 3, 2 }, { 6, 4 }, { 6, 5 } }, { 1, 2, 4, 5 }, 6),
        make({ { 6, 3 }, { 6, 4 }, { 6, 5 } }, { 1, 2, 3, 4, 5 }, 6),
        make({ { 6, 4 }, { 6, 5 } }, { 1, 2, 3, 4, 5 }, 6),
        make({ { 3, 2 }, { 5, 4 }, { 6, 4 } }, { 1, 2, 4 }, 6),
        make({ { 3, 2 }, { 5, 4 }, { 6, 4 } }, { 1, 2, 4 }, 3),
        make({ { 2, 1 }, { 5, 4 }, { 4, 3 } }, { 1, 3, 6 }, 5),
        make({ { 4, 3 }, { 6, 5 } }, { 1, 2, 3, 5 }, 6),
        make({ { 5, 4 }, { 4, 3 } }, { 1, 2, 3, 6 }, 5),
        make({ { 6, 5 } }, { 1, 2, 3, 4, 5 }, 6),
        make({}, { 1, 2, 3, 4, 5, 6 }, 6),
    } };
    return table;
}

/** One operator, in the DX7's units. */
struct OperatorPatch
{
    bool on = true;
    /** 0 is half the note's frequency, 1 to 31 its multiples. */
    int coarse = 1;
    /** Adds 0 to 99 % of the coarse ratio. */
    int fine = 0;
    /** -7 to 7, small steps either way, to beat against another operator. */
    int detune = 0;
    /** 0 to 99, about 0.75 dB per step; 0 is silent. */
    int level = 99;
    /** 0 to 7: how much a soft note takes off. */
    int velocity = 0;
    /** 0 to 7: how much faster the envelope runs on high notes. */
    int rateScaling = 0;
    /** 0 to 3: how much the LFO's amplitude modulation reaches this operator. */
    int ampModSens = 0;
    /** Rates and levels of the four stages: attack to L1, decay to L2, to L3 (held), release to L4. */
    std::array<int, 4> rates { 99, 99, 99, 99 };
    std::array<int, 4> levels { 99, 99, 99, 0 };
};

enum class LfoWave { triangle, sawDown, sawUp, square, sine, sampleHold };

/** Delay and hall after the voices, as the other Tonwerk instruments have them. */
struct DxEffects
{
    float delayMix = 0.0f;
    float delayTime = 0.35f;
    bool delaySync = false;
    int delayDivision = 9; // 1/8. in Tonwerk's divisions
    float delayFeedback = 0.35f;
    float delayTone = 4000.0f;
    float reverbMix = 0.0f;
    float reverbDecay = 2.0f;
};

struct DxPatch
{
    std::array<OperatorPatch, kOperators> ops {};
    /** 0 to 31, as the menu counts; the DX7 prints 1 to 32. */
    int algorithm = 31;
    /** 0 to 7. */
    int feedback = 0;
    LfoWave lfoWave = LfoWave::triangle;
    int lfoSpeed = 35;
    int lfoDelay = 0;
    int lfoPitchDepth = 0;
    int lfoAmpDepth = 0;
    /** 0 to 7: how far the LFO's pitch depth reaches, up to an octave. */
    int lfoPitchSens = 3;
    /** Semitones, -24 to 24. */
    int transpose = 0;
    /** 0 to 1. */
    float volume = 0.8f;
    DxEffects fx;
};

/** The DX7's ratio for a coarse and fine setting. */
inline double ratioOf(int coarse, int fine)
{
    const double base = coarse <= 0 ? 0.5 : (double) coarse;
    return base * (1.0 + std::clamp(fine, 0, 99) / 100.0);
}

/** A level step (0 to 99) as gain: 0.75 dB a step down from 99, 0 silent. */
inline double levelGain(double level)
{
    return level <= 0.0 ? 0.0 : std::exp2((level - 99.0) / 8.0);
}

/** Seconds a stage at `rate` takes to cross all 99 steps: about 3 ms at 99, 40 s at 0. */
inline double stageSeconds(double rate) { return 38.0 * std::exp2(-std::clamp(rate, 0.0, 99.0) / 7.3); }

/**
 * LFO speed 0 to 99 as Hz, fitted to measurements of the DX7 and TX7 (within a few percent from speed 4 up): about
 * 0.16 Hz a step up to 63, so the init voice's 35 is a 5.6 Hz vibrato, then steeper to about 49 Hz at 99.
 */
inline double lfoHz(int speed)
{
    const double s = (double) std::clamp(speed, 0, 99);
    if (s <= 63.0)
        return std::max(0.161 * s - 0.03, 0.0625);
    const double above = s - 63.0;
    return 0.161 * 63.0 - 0.03 + 0.77 * above + 0.009 * above * above;
}

/**
 * The four-stage envelope of one operator, in level steps (0 to 99), so its curves are exponential in loudness as on
 * the DX7. Advanced in chunks; the voice ramps the gain within a chunk.
 */
class DxEnvelope
{
public:
    void start(const OperatorPatch& op, double rateBoost)
    {
        boost = rateBoost;
        stage = 0;
        holding = released = false;
        // Retriggered from where it is, so a stolen voice does not click; a fresh one starts at L4.
        if (! active)
            level = op.levels[3];
        active = true;
    }

    void release()
    {
        stage = 3;
        holding = false;
        released = false;
    }

    void kill()
    {
        active = false;
        level = 0.0;
    }

    /** Moves `seconds` on; returns the level after. */
    double advance(const OperatorPatch& op, double seconds)
    {
        if (! active)
            return 0.0;
        // Stage 2 holds at L3 until the key is let go.
        if (holding || released)
            return level;
        const double target = op.levels[(std::size_t) stage];
        const double speed = 99.0 / stageSeconds(op.rates[(std::size_t) stage] + boost);
        const double step = speed * seconds;
        if (std::abs(target - level) > step)
        {
            level += target > level ? step : -step;
            return level;
        }
        level = target;
        if (stage < 2)
            ++stage;
        else if (stage == 2)
            holding = true;
        else
        {
            released = true;
            active = level > 0.0;
        }
        return level;
    }

    bool isActive() const { return active; }
    /** Done with its release, whatever L4 is. */
    bool isReleased() const { return released; }
    double current() const { return active ? level : 0.0; }

private:
    double level = 0.0;
    double boost = 0.0;
    int stage = 0;
    bool holding = false;
    bool released = false;
    bool active = false;
};

/** A cosine-free sine from a table with linear interpolation: cheap enough for six operators on sixteen voices. */
class SineTable
{
public:
    static constexpr int kSize = 4096;

    static const SineTable& instance()
    {
        static const SineTable table;
        return table;
    }

    /** `phase` in cycles, any value. */
    float at(double phase) const
    {
        const double wrapped = phase - std::floor(phase);
        const double position = wrapped * kSize;
        const int index = std::min((int) position, kSize - 1);
        const float between = (float) (position - index);
        const float* v = values.data() + index;
        return v[0] + between * (v[1] - v[0]);
    }

private:
    SineTable()
    {
        for (int i = 0; i <= kSize; ++i)
            values[(std::size_t) i] = (float) std::sin(kTwoPi * i / kSize);
    }
    std::array<float, kSize + 1> values {};
};

/** The shared LFO's value for a voice: -1 to 1, with the patch's delay fading it in after the key goes down. */
class DxLfo
{
public:
    void start(std::uint32_t seed)
    {
        phase = 0.0;
        sinceStart = 0.0;
        random = seed | 1u;
        fresh = true;
    }

    double advance(const DxPatch& p, double seconds)
    {
        const double previous = phase;
        phase += lfoHz(p.lfoSpeed) * seconds;
        phase -= std::floor(phase);
        sinceStart += seconds;
        if (phase < previous || fresh)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            held = (double) (random & 0xffff) / 32767.5 - 1.0;
            fresh = false;
        }
        double value = 0.0;
        switch (p.lfoWave)
        {
            case LfoWave::triangle: value = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase; break;
            case LfoWave::sawDown: value = 1.0 - 2.0 * phase; break;
            case LfoWave::sawUp: value = 2.0 * phase - 1.0; break;
            case LfoWave::square: value = phase < 0.5 ? 1.0 : -1.0; break;
            case LfoWave::sine: value = std::sin(kTwoPi * phase); break;
            case LfoWave::sampleHold: value = held; break;
        }
        // The delay: silent for its first half, then fading in.
        const double delay = std::pow(p.lfoDelay / 99.0, 2.0) * 5.0;
        const double fade = delay <= 0.0 ? 1.0 : std::clamp((sinceStart - delay * 0.5) / (delay * 0.5), 0.0, 1.0);
        return value * fade;
    }

private:
    double phase = 0.0;
    double sinceStart = 0.0;
    double held = 0.0;
    bool fresh = true;
    std::uint32_t random = 1;
};

/** The LFO's pitch reach in cents at full depth, per sensitivity 0 to 7. */
inline constexpr std::array<double, 8> kPitchSensCents { 0.0, 8.0, 16.0, 27.0, 50.0, 90.0, 200.0, 1200.0 };
/** The LFO's amplitude reach in level steps at full depth, per sensitivity 0 to 3. */
inline constexpr std::array<double, 4> kAmpSensSteps { 0.0, 8.0, 26.0, 64.0 };

/**
 * One note: six operators in the patch's algorithm. The envelopes and the LFO move every 32 samples and the gains
 * ramp between, as in Tonwerk Wavetable, so the inner loop is six table lookups and multiplies.
 */
class DxVoice
{
public:
    static constexpr int kChunk = 32;
    /** Phase modulation in cycles from a modulator at full level: about 13 radians, the DX7's brightest. */
    static constexpr double kModDepth = 2.0;

    void prepare(double rate) { sampleRate = rate; }

    void start(int midiNote, float velocity, const DxPatch& p, std::uint32_t seed)
    {
        note = midiNote;
        softness = 1.0 - std::clamp((double) velocity, 0.0, 1.0);
        // The DX7 speeds envelopes up by about a third of a step per semitone above the lowest A, times the scaling.
        const double keyAbove = std::max(0, midiNote - 21) / 3.0;
        for (int i = 0; i < kOperators; ++i)
        {
            const auto& op = p.ops[(std::size_t) i];
            if (! envelopes[(std::size_t) i].isActive())
                phases[(std::size_t) i] = 0.0;
            envelopes[(std::size_t) i].start(op, keyAbove * op.rateScaling / 8.0);
        }
        lfo.start(seed);
        feedbackLast = feedbackBefore = 0.0;
        if (! playing)
            gains.fill(0.0f);
        playing = true;
    }

    void release()
    {
        for (auto& env : envelopes)
            env.release();
    }

    void kill()
    {
        for (auto& env : envelopes)
            env.kill();
        playing = false;
    }

    bool isActive() const { return playing; }

    /** Adds the voice to `out`; `bend` in semitones. */
    void render(const DxPatch& p, float* out, int count, double bend)
    {
        if (! playing)
            return;
        const auto& algorithm = algorithms()[(std::size_t) std::clamp(p.algorithm, 0, kAlgorithms - 1)];
        const auto& sine = SineTable::instance();
        const double feedbackAmount = p.feedback <= 0 ? 0.0 : std::exp2(p.feedback - 7.0) / 2.0;
        // Carriers summed and scaled so a full chord of six stays under full scale.
        const float outGain = (float) (p.volume * 0.5 / std::sqrt((double) std::max(1, algorithm.carrierCount())));

        for (int done = 0; done < count;)
        {
            const int n = std::min(kChunk, count - done);
            const double seconds = n / sampleRate;
            const double lfoValue = lfo.advance(p, seconds);
            const double cents = 100.0 * (note + p.transpose + bend - 69.0)
                                 + lfoValue * (p.lfoPitchDepth / 99.0) * kPitchSensCents[(std::size_t) std::clamp(p.lfoPitchSens, 0, 7)];
            const double noteHz = 440.0 * std::exp2(cents / 1200.0);
            const double ampMod = (1.0 - lfoValue) / 2.0 * (p.lfoAmpDepth / 99.0);

            std::array<float, kOperators> target {};
            std::array<double, kOperators> increment {};
            bool anyCarrier = false;
            for (int i = 0; i < kOperators; ++i)
            {
                const auto& op = p.ops[(std::size_t) i];
                const double env = envelopes[(std::size_t) i].advance(op, seconds);
                const double steps = env + op.level - 99.0 - softness * op.velocity * 6.0
                                     - ampMod * kAmpSensSteps[(std::size_t) std::clamp(op.ampModSens, 0, 3)];
                target[(std::size_t) i] = op.on && env > 0.0 && op.level > 0 ? (float) levelGain(steps) : 0.0f;
                const double ratio = ratioOf(op.coarse, op.fine);
                increment[(std::size_t) i] = noteHz * ratio * std::exp2(op.detune * 1.5 / 1200.0) / sampleRate;
                // A carrier is done once its release has reached L4; one held above zero there would hold the
                // voice for ever, so it ends too.
                if (algorithm.isCarrier(i) && op.on && envelopes[(std::size_t) i].isActive()
                    && ! envelopes[(std::size_t) i].isReleased())
                    anyCarrier = true;
            }

            std::array<float, kOperators> step {};
            for (int i = 0; i < kOperators; ++i)
                step[(std::size_t) i] = (target[(std::size_t) i] - gains[(std::size_t) i]) / (float) n;

            // Plain arrays in the loop: it runs six times a sample on every voice.
            float* gain = gains.data();
            double* phase = phases.data();
            const float* gainStep = step.data();
            const double* phaseStep = increment.data();
            const std::uint8_t* modulators = algorithm.modulators.data();
            const int feedbackOp = algorithm.feedback;
            const int heard = algorithm.carriers;
            for (int s = 0; s < n; ++s)
            {
                float output[kOperators] {};
                float sum = 0.0f;
                for (int i = kOperators - 1; i >= 0; --i)
                {
                    gain[i] += gainStep[i];
                    double modulation = 0.0;
                    for (int mask = modulators[i] >> (i + 1), m = i + 1; mask != 0; mask >>= 1, ++m)
                        if (mask & 1)
                            modulation += output[m];
                    if (i == feedbackOp)
                        modulation += feedbackAmount * (feedbackLast + feedbackBefore) / 2.0;
                    const float value = gain[i] <= 0.0f ? 0.0f : gain[i] * sine.at(phase[i] + modulation * kModDepth);
                    phase[i] += phaseStep[i];
                    if (phase[i] >= 1.0)
                        phase[i] -= 1.0;
                    if (i == feedbackOp)
                    {
                        feedbackBefore = feedbackLast;
                        feedbackLast = value;
                    }
                    output[i] = value;
                    if ((heard >> i) & 1)
                        sum += value;
                }
                out[done + s] += sum * outGain;
            }
            done += n;
            if (! anyCarrier)
            {
                kill();
                return;
            }
        }
    }

    int currentNote() const { return note; }

private:
    double sampleRate = 48000.0;
    int note = 60;
    double softness = 0.0;
    bool playing = false;
    std::array<DxEnvelope, kOperators> envelopes {};
    std::array<double, kOperators> phases {};
    std::array<float, kOperators> gains {};
    DxLfo lfo;
    double feedbackLast = 0.0;
    double feedbackBefore = 0.0;
};
} // namespace tonwerkdx
