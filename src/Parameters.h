#pragma once

#include "dsp/Patch.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace tonwerk
{
/**
 * Every field of a `Patch` as a host parameter, so Logic can automate it and saves it with the project. One table
 * (`descriptors()`) is the source for the parameter layout, for reading a patch out of the parameters on every audio
 * block, and for writing an imported patch into them; a field added to the patch needs one entry there.
 */
struct ParameterDescriptor
{
    enum class Type { number, integer, choice, toggle };

    juce::String id;
    juce::String name;
    Type type;
    float min = 0.0f;
    float max = 1.0f;
    /** For numbers: the value in the middle of the knob, for ranges like Hz and seconds; 0 for a linear knob. */
    float centre = 0.0f;
    juce::StringArray choices;
    juce::String unit;
    std::function<float(const Patch&)> get;
    std::function<void(Patch&, float)> set;
    /** The version hint hosts get; parameters added after the first release have 2. */
    int version = 1;
};

const std::vector<ParameterDescriptor>& descriptors();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** The parameters' current values, read without locks: one atomic per parameter, looked up once. */
class ParameterReader
{
public:
    explicit ParameterReader(juce::AudioProcessorValueTreeState& state);
    Patch read() const;

private:
    std::vector<std::atomic<float>*> values;
};

/** Sets every parameter to the patch's values and tells the host; call on the message thread. */
void writePatch(juce::AudioProcessorValueTreeState& state, const Patch& patch);
} // namespace tonwerk
