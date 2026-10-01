#include "dsp/AnalogVoice.h"
#include "dsp/FmVoice.h"
#include "EffectsChain.h"

#include <juce_core/juce_core.h>

#include <numeric>
#include <vector>

using namespace tonwerk;

namespace
{
constexpr double kRate = 48000.0;

float peakOf(const std::vector<float>& samples, size_t from = 0, size_t to = SIZE_MAX)
{
    float peak = 0.0f;
    for (size_t i = from; i < std::min(to, samples.size()); ++i)
        peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

bool allFinite(const std::vector<float>& samples)
{
    return std::all_of(samples.begin(), samples.end(), [](float s) { return std::isfinite(s); });
}

/** Renders a note held for `held` seconds, then released, for `total` seconds in blocks of 512. */
template <typename Voice, typename VoicePatch>
std::vector<float> renderNote(Voice& voice, const VoicePatch& patch, double pitch, float velocity, double held, double total)
{
    std::vector<float> out((size_t) (total * kRate), 0.0f);
    voice.prepare(kRate);
    voice.start(pitch, velocity, patch);
    const size_t releaseAt = (size_t) (held * kRate);
    bool released = false;
    for (size_t at = 0; at < out.size(); at += 512)
    {
        if (! released && at >= releaseAt)
        {
            voice.release();
            released = true;
        }
        voice.render(patch, out.data() + at, (int) std::min<size_t>(512, out.size() - at), 0.0);
    }
    return out;
}
} // namespace

class EnvelopeTests : public juce::UnitTest
{
public:
    EnvelopeTests() : juce::UnitTest("Envelope", "DSP") {}

    void runTest() override
    {
        beginTest("Linear attack reaches the peak after the attack time");
        EnvelopeGenerator env;
        env.prepare(kRate);
        env.setParameters({ 0.1f, 0.4f, 0.5f, 0.2f });
        env.noteOn();
        float value = 0.0f;
        for (int i = 0; i < (int) (0.05 * kRate); ++i)
            value = env.next();
        expectWithinAbsoluteError(value, 0.5f, 0.01f);
        for (int i = 0; i < (int) (0.05 * kRate); ++i)
            value = env.next();
        expectWithinAbsoluteError(value, 1.0f, 0.01f);

        beginTest("Decay gets about 98 % of the way to sustain within the decay time");
        for (int i = 0; i < (int) (0.4 * kRate); ++i)
            value = env.next();
        // e^-4 of the distance is left: 0.5 + 0.5 * 0.018.
        expectWithinAbsoluteError(value, 0.5f + 0.5f * std::exp(-4.0f), 0.005f);

        beginTest("Release ends the envelope after the release time");
        env.noteOff();
        int samples = 0;
        while (env.isActive() && samples < kRate)
        {
            env.next();
            ++samples;
        }
        expect(! env.isActive());
        expectWithinAbsoluteError(samples / kRate, 0.2 + 0.003, 0.002);
    }
};

class AnalogVoiceTests : public juce::UnitTest
{
public:
    AnalogVoiceTests() : juce::UnitTest("Analog voice", "DSP") {}

    void runTest() override
    {
        beginTest("Every default sound plays and ends after its release");
        for (auto kind : { Kind::melody, Kind::chords, Kind::bass, Kind::guideTones })
        {
            AnalogVoice voice;
            const auto patch = defaultAnalog(kind);
            const auto out = renderNote(voice, patch, 60, 1.0f, 0.5, 2.5);
            expect(allFinite(out));
            expectGreaterThan(peakOf(out, 0, (size_t) (0.5 * kRate)), 0.05f);
            expect(! voice.isActive(), "the voice should have ended");
            expectLessThan(peakOf(out, (size_t) (2.4 * kRate)), 1.0e-6f);
        }

        beginTest("The pulse is high for its width and carries no DC");
        AnalogPatch patch;
        patch.osc1 = { Wave::square, 0, 0.0f, 1.0f, 0.25f };
        patch.osc2.level = 0.0f;
        patch.filter = { FilterType::lowpass, 18000.0f, 0.0f, 0.0f, 0.0f };
        patch.ampEnv = { 0.0f, 0.01f, 1.0f, 0.01f };
        patch.lfo.depth = 0.0f;
        patch.volume = 1.0f;
        AnalogVoice voice;
        // 100 Hz: 480 samples a cycle, so the band-limited edges hardly count.
        const auto out = renderNote(voice, patch, noteFrequencyToPitch(100.0), 1.0f, 1.0, 1.0);
        const size_t from = (size_t) (0.5 * kRate);
        const size_t to = from + 4800;
        size_t high = 0;
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            high += out[i] > 0.0f ? 1u : 0u;
            sum += out[i];
        }
        expectWithinAbsoluteError((double) high / (to - from), 0.25, 0.01);
        expectWithinAbsoluteError(sum / (to - from), 0.0, 0.01);
        // A step of 1 from -0.25 to 0.75, as Tonwerk's sawtooth-minus-delayed-sawtooth builds it; the median of the
        // high part, since the band-limited edges ring a little.
        std::vector<float> highs;
        for (size_t i = from; i < to; ++i)
            if (out[i] > 0.0f)
                highs.push_back(out[i]);
        std::nth_element(highs.begin(), highs.begin() + (long) highs.size() / 2, highs.end());
        expectWithinAbsoluteError(highs[highs.size() / 2], 0.75f, 0.02f);

        beginTest("PWM from the LFO moves the width");
        patch.osc1.pwm = 1.0f;
        patch.lfo = { Wave::square, 1.0f, AnalogLfoTarget::pitch, 0.0f };
        AnalogVoice moving;
        const auto moved = renderNote(moving, patch, noteFrequencyToPitch(100.0), 1.0f, 1.0, 1.0);
        auto dutyBetween = [&moved](double start, double end) {
            size_t above = 0;
            for (size_t i = (size_t) (start * kRate); i < (size_t) (end * kRate); ++i)
                above += moved[i] > 0.0f ? 1u : 0u;
            return (double) above / ((end - start) * kRate);
        };
        // A square LFO at 1 Hz: the first half second wide, the second narrow, by the whole room (0.25 - 0.02).
        expectWithinAbsoluteError(dutyBetween(0.1, 0.4), 0.48, 0.02);
        expectWithinAbsoluteError(dutyBetween(0.6, 0.9), 0.02, 0.02);
    }

    static double noteFrequencyToPitch(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }
};

class FmVoiceTests : public juce::UnitTest
{
public:
    FmVoiceTests() : juce::UnitTest("FM voice", "DSP") {}

    void runTest() override
    {
        beginTest("Every default sound plays and ends after its carriers' release");
        for (auto kind : { Kind::melody, Kind::chords, Kind::bass, Kind::guideTones })
        {
            FmVoice voice;
            const auto patch = defaultFm(kind);
            const auto out = renderNote(voice, patch, 48, 0.8f, 0.5, 2.0);
            expect(allFinite(out));
            expectGreaterThan(peakOf(out, 0, (size_t) (0.5 * kRate)), 0.05f);
            expect(! voice.isActive());
        }

        beginTest("Four carriers without modulation sum to the volume");
        FmPatch patch;
        patch.algorithm = 8;
        patch.feedback = 0.0f;
        for (auto& op : patch.ops)
            op = { 1.0f, 0.0f, 1.0f, 0.0f, { 0.0f, 0.01f, 1.0f, 0.01f } };
        patch.lfo.depth = 0.0f;
        patch.volume = 1.0f;
        FmVoice voice;
        const auto out = renderNote(voice, patch, 69, 1.0f, 0.5, 0.5);
        // Four sines in phase at 1 / sqrt(4) each: a sine of 2.
        expectWithinAbsoluteError(peakOf(out, (size_t) (0.1 * kRate)), 2.0f, 0.01f);

        beginTest("A silent carrier and the modulators only it hears are left out");
        patch.algorithm = 1;
        patch.ops[0].level = 0.0f;
        FmVoice silent;
        const auto none = renderNote(silent, patch, 69, 1.0f, 0.5, 0.5);
        expectEquals(peakOf(none), 0.0f);
        expect(! silent.isActive());

        beginTest("Feedback turns operator 4's sine into a brighter wave");
        patch.algorithm = 8;
        for (auto& op : patch.ops)
            op.level = 0.0f;
        patch.ops[3].level = 1.0f;
        patch.feedback = 1.0f;
        FmVoice bright;
        const auto fed = renderNote(bright, patch, 45, 1.0f, 0.5, 0.5);
        expect(allFinite(fed));
        // A sine's crest factor is sqrt(2); feedback towards a sawtooth flattens it.
        double square = 0.0;
        const size_t from = (size_t) (0.1 * kRate), to = (size_t) (0.4 * kRate);
        for (size_t i = from; i < to; ++i)
            square += (double) fed[i] * fed[i];
        const double rms = std::sqrt(square / (double) (to - from));
        const double crest = peakOf(fed, from, to) / rms;
        expectGreaterThan(crest, 1.5);
    }
};

class EffectsTests : public juce::UnitTest
{
public:
    EffectsTests() : juce::UnitTest("Effects", "DSP") {}

    void runTest() override
    {
        beginTest("Without sends the sound passes unchanged to both sides");
        EffectsChain chain;
        chain.prepare(kRate, 512);
        Effects fx;
        std::vector<float> in(512, 0.0f);
        in[0] = 1.0f;
        in[100] = -0.5f;
        juce::AudioBuffer<float> out(2, 512);
        chain.process(fx, in.data(), out, 0, 512);
        expectEquals(out.getSample(0, 0), 1.0f);
        expectEquals(out.getSample(1, 100), -0.5f);
        expectEquals(out.getSample(0, 50), 0.0f);

        beginTest("The delay repeats after its time, darker and quieter");
        fx.delay.mix = 1.0f;
        fx.delay.time = 0.02f;
        fx.delay.feedback = 0.5f;
        fx.delay.tone = 12000.0f;
        EffectsChain delay;
        delay.prepare(kRate, 512);
        // Let the glides settle on silence first.
        std::vector<float> silence(512, 0.0f);
        for (int i = 0; i < 100; ++i)
            delay.process(fx, silence.data(), out, 0, 512);
        const int blocks = 8;
        juce::AudioBuffer<float> long_(2, 512 * blocks);
        std::vector<float> impulse(512 * blocks, 0.0f);
        impulse[0] = 1.0f;
        delay.process(fx, impulse.data(), long_, 0, 512 * blocks);
        const int echo = (int) (0.02 * kRate);
        float first = 0.0f, second = 0.0f;
        for (int i = echo - 5; i < echo + 10; ++i)
            first = std::max(first, std::abs(long_.getSample(0, i)));
        for (int i = 2 * echo - 5; i < 2 * echo + 10; ++i)
            second = std::max(second, std::abs(long_.getSample(0, i)));
        expectGreaterThan(first, 0.5f);
        expectWithinAbsoluteError(second / first, 0.5f, 0.1f);

        beginTest("The room falls to -60 dB over its decay and differs per side");
        juce::Random random(42);
        const auto room = EffectsChain::makeImpulse(kRate, 1.0, random);
        expectEquals(room.getNumSamples(), (int) kRate);
        expectEquals(room.getNumChannels(), 2);
        float early = 0.0f, late = 0.0f;
        for (int i = 0; i < 1000; ++i)
            early = std::max(early, std::abs(room.getSample(0, i)));
        for (int i = 0; i < 10; ++i)
            late = std::max(late, std::abs(room.getSample(0, room.getNumSamples() - 1 - i)));
        expectGreaterThan(early, 0.9f);
        expectLessThan(late, 0.0011f);
        expectGreaterThan(std::abs(room.getSample(0, 10) - room.getSample(1, 10)), 0.0f);

        beginTest("Reverb adds a tail once its room is loaded");
        fx = {};
        fx.reverb.mix = 1.0f;
        fx.reverb.decay = 0.5f;
        EffectsChain hall;
        hall.prepare(kRate, 512);
        hall.loadRoom(EffectsChain::roomFor(fx));
        expectEquals(hall.loadedRoom(), 5);
        // The convolution swaps its new impulse in on its own thread; give it time and blocks to pick it up.
        float tail = 0.0f;
        for (int attempt = 0; attempt < 200 && tail <= 0.0f; ++attempt)
        {
            juce::Thread::sleep(5);
            std::vector<float> click(512, 0.0f);
            click[0] = 1.0f;
            hall.process(fx, click.data(), out, 0, 512);
            for (int i = 1; i < 512; ++i)
                tail = std::max(tail, std::abs(out.getSample(1, i)));
        }
        expectGreaterThan(tail, 0.0f);
    }
};

static EnvelopeTests envelopeTests;
static AnalogVoiceTests analogVoiceTests;
static FmVoiceTests fmVoiceTests;
static EffectsTests effectsTests;
