#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace tonwerkapp
{
/** One plugin the app plays on its own. */
struct Entry
{
    juce::String name;
    std::function<std::unique_ptr<juce::AudioProcessor>()> make;
    /** Hears the device's input and only shows it (Tonwerk Scope): no keyboard, nothing to the speakers. */
    bool listens = false;
};

/**
 * The instruments the app plays, in the order of the website, Tonwerk Rig (an instrument with effects behind it, the way
 * the effects play in the app) and Tonwerk Scope last, to look at or tune what comes in. The other effects are left out
 * on their own (they want a sound to work on, which a host or the rig gives them), the MIDI effects too (on their
 * own nothing hears what they send), and Tonwerk Synth, whose two engines are here as Tonwerk Analog and Tonwerk FM; all
 * of them are in the app as AUv3s.
 */
const std::vector<Entry>& catalogue();
} // namespace tonwerkapp
