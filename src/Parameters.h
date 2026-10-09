#pragma once

#include "dsp/Patch.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace tonwerk
{
/**
 * Which plugin a build is: Tonwerk Analog and Tonwerk FM each play one engine and have only its parameters; Tonwerk
 * Synth, the first plugin, plays both and stays for the Logic projects made with it.
 */
enum class Edition { combined, analog, fm };

/** What a parameter belongs to: the engine switch (Tonwerk Synth only), one engine, or the effects both have. */
enum class Part { engine, analog, fm, effects };

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
    /** The version hint hosts get: 1 for the first release's parameters, 2 for 0.2.0's, 3 for 0.3.0's. */
    int version = 1;
    Part part = Part::analog;
};

/** Whether `edition` has the parameters of `part`. */
bool hasPart(Edition edition, Part part);

const std::vector<ParameterDescriptor>& descriptors();

/**
 * The edition's parameters, shown as whole numbers with their units (times in ms, levels in %, the filter envelope in
 * semitones); the values themselves keep Tonwerk's units.
 */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(Edition edition = Edition::combined);

/**
 * The parameters' current values, read without locks: one atomic per parameter, looked up once. Fields the edition
 * has no parameter for keep the patch's defaults.
 */
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
