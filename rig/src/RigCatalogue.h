#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace tonwerkrig
{
/**
 * One instrument or effect a rig can hold. `id` goes into saved projects and rigs: never rename one. The order is the
 * menu's and may change.
 */
struct Kind
{
    const char* id;
    juce::String name;
    std::function<std::unique_ptr<juce::AudioProcessor>()> make;
};

/** The instruments, in the order of the website. Tonwerk Synth is here as its two engines, Analog and FM. */
const std::vector<Kind>& instruments();

/**
 * The effects that work on what comes in: not Robovox and Vocoder (they want a voice on the side chain and MIDI), nor
 * the MIDI effects (nothing in a rig plays what they send).
 */
const std::vector<Kind>& effects();

/** The kind with this id, or nullptr. */
const Kind* find(const std::vector<Kind>& kinds, const juce::String& id);

/** The instrument whose separate outputs a rig's effects can take one by one (Tonwerk Groovebox's tracks). */
inline constexpr const char* kTracksInstrument = "groovebox";
} // namespace tonwerkrig
