#pragma once

#include "dsp/Oscillators.h"
#include "dsp/Patch.h"

#include <juce_dsp/juce_dsp.h>

namespace tonwerk
{
/**
 * Delay and reverb after all voices, as Tonwerk's effects.ts has them: both are sends, the dry sound passes unchanged,
 * `mix` sets how much of each is added. The delay has a lowpass in its loop, so every repeat is a little darker; the
 * reverb convolves with stereo noise falling to -60 dB over `decay`, at 0.15 so a full mix is about as loud as the dry
 * sound. Settings glide over a few hundredths of a second, as the browser's `setTargetAtTime` does, so turning a knob
 * while a tail rings does not click.
 */
class EffectsChain
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    /** Takes a mono block of voices and writes dry plus effects into both channels of `out`. */
    void process(const Effects& fx, const float* in, juce::AudioBuffer<float>& out, int start, int count);

    /**
     * The room the reverb needs for `fx`, in tenths of a second, or 0 when it is off. The impulse is computed and loaded
     * off the audio thread (`loadRoom`), since it allocates; the processor asks for it on its timer.
     */
    static int roomFor(const Effects& fx);
    int loadedRoom() const { return loaded.load(); }
    void loadRoom(int tenths);

    /** The impulse itself, for the tests: stereo noise falling to 1/1000 over `decay` seconds. */
    static juce::AudioBuffer<float> makeImpulse(double sampleRate, double decay, juce::Random& random);

private:
    double sampleRate = 48000.0;
    std::vector<float> delayLine;
    int writeIndex = 0;
    Biquad tone;
    double smoothedTime = 0.35;
    double smoothedDelayMix = 0.0;
    double smoothedFeedback = 0.35;
    double smoothedTone = 4000.0;
    double smoothedReverbMix = 0.0;
    double glideFast = 0.0;
    double glideSlow = 0.0;

    juce::dsp::Convolution reverb;
    juce::AudioBuffer<float> reverbBuffer;
    std::atomic<int> loaded { 0 };
    int reverbTail = 0;
};
} // namespace tonwerk
