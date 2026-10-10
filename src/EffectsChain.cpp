#include "EffectsChain.h"

#include "dsp/Tempo.h"

namespace tonwerk
{
namespace
{
constexpr float kReverbLevel = 0.15f;
} // namespace

void EffectsChain::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    delayLine.assign((size_t) (kMaxDelaySeconds * sampleRate) + 4, 0.0f);
    // One-pole glides with the browser's time constants: 0.02 s for levels and tone, 0.05 s for the delay time.
    glideFast = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    glideSlow = 1.0 - std::exp(-1.0 / (0.05 * sampleRate));
    reverb.prepare({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    reverbBuffer.setSize(2, maxBlockSize);
    // A room made for another sample rate has to be made again.
    loaded.store(0);
    reset();
}

void EffectsChain::reset()
{
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    writeIndex = 0;
    tone.reset();
    reverb.reset();
    reverbTail = 0;
}

int EffectsChain::roomFor(const Effects& fx)
{
    return fx.reverb.mix > 0.0f ? juce::roundToInt(fx.reverb.decay * 10.0f) : 0;
}

int EffectsChain::impulseLength(double rate, double decay)
{
    return std::max(1, (int) std::ceil(rate * decay));
}

bool EffectsChain::roomPlaying() const
{
    const int tenths = loaded.load();
    return tenths == 0 || reverb.getCurrentIRSize() == impulseLength(sampleRate, tenths / 10.0);
}

juce::AudioBuffer<float> EffectsChain::makeImpulse(double rate, double decay, juce::Random& random)
{
    const int length = impulseLength(rate, decay);
    juce::AudioBuffer<float> impulse(2, length);
    // ln(1000): the level after `decay` seconds is 1/1000, i.e. -60 dB. Different noise per side spreads the tail.
    const double fall = std::log(1000.0) / length;
    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = impulse.getWritePointer(channel);
        for (int i = 0; i < length; ++i)
            data[i] = (float) ((random.nextDouble() * 2.0 - 1.0) * std::exp(-fall * i));
    }
    return impulse;
}

void EffectsChain::loadRoom(int tenths)
{
    if (tenths <= 0 || tenths == loaded.load())
        return;
    // The same noise for the same room every time: a sound plays alike whenever it is loaded, and a bounce repeats.
    juce::Random random(tenths);
    reverb.loadImpulseResponse(makeImpulse(sampleRate, tenths / 10.0, random),
                               sampleRate,
                               juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::no,
                               // Normalised, a long room would be as loud as a short one.
                               juce::dsp::Convolution::Normalise::no);
    loaded.store(tenths);
}

void EffectsChain::process(const Effects& fx, const float* in, juce::AudioBuffer<float>& out, int start, int count)
{
    // Some hosts send more than they announced; the reverb's buffer only holds the announced size.
    const int capacity = reverbBuffer.getNumSamples();
    if (count > capacity && capacity > 0)
    {
        for (int done = 0; done < count; done += capacity)
            process(fx, in + done, out, start + done, std::min(capacity, count - done));
        return;
    }
    auto* left = out.getWritePointer(0, start);
    auto* right = out.getNumChannels() > 1 ? out.getWritePointer(1, start) : nullptr;
    const int size = (int) delayLine.size();

    for (int done = 0; done < count;)
    {
        const int chunk = std::min(16, count - done);
        tone.set(FilterType::lowpass, smoothedTone, 1.0, sampleRate);
        for (int i = 0; i < chunk; ++i)
        {
            smoothedDelayMix += (fx.delay.mix - smoothedDelayMix) * glideFast;
            smoothedFeedback += (fx.delay.feedback - smoothedFeedback) * glideFast;
            smoothedTone += (fx.delay.tone - smoothedTone) * glideFast;
            smoothedReverbMix += (fx.reverb.mix - smoothedReverbMix) * glideFast;
            smoothedTime += (fx.delay.time - smoothedTime) * glideSlow;

            const float dry = in[done + i];
            // A fractional read, so a gliding delay time bends the repeats instead of crackling.
            const double delaySamples = std::clamp(smoothedTime * sampleRate, 1.0, (double) size - 2.0);
            double readPosition = writeIndex - delaySamples;
            if (readPosition < 0.0)
                readPosition += size;
            const int index = (int) readPosition;
            const double fraction = readPosition - index;
            const float a = delayLine[(size_t) index];
            const float b = delayLine[(size_t) ((index + 1) % size)];
            const double delayed = a + (b - a) * fraction;
            const double toned = tone.process(delayed);
            const float wet = (float) toned;
            left[done + i] = dry + wet;
            if (right != nullptr)
                right[done + i] = dry + wet;
            delayLine[(size_t) writeIndex] = (float) (dry * smoothedDelayMix + toned * smoothedFeedback);
            writeIndex = (writeIndex + 1) % size;
            reverbBuffer.setSample(0, done + i, (float) (dry * smoothedReverbMix));
            reverbBuffer.setSample(1, done + i, (float) (dry * smoothedReverbMix));
        }
        done += chunk;
    }

    // The room keeps ringing for its decay after the send is closed; only then is it left out.
    if (smoothedReverbMix > 1.0e-5)
        reverbTail = juce::roundToInt(loaded.load() / 10.0 * sampleRate) + count;
    if (loaded.load() > 0 && reverbTail > 0)
    {
        reverbTail -= count;
        juce::dsp::AudioBlock<float> block(reverbBuffer.getArrayOfWritePointers(), 2, 0, (size_t) count);
        reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
        out.addFrom(0, start, reverbBuffer, 0, 0, count, kReverbLevel);
        if (right != nullptr)
            out.addFrom(1, start, reverbBuffer, 1, 0, count, kReverbLevel);
    }
}
} // namespace tonwerk
