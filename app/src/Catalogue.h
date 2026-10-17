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
    bool instrument;
    std::function<std::unique_ptr<juce::AudioProcessor>()> make;
};

/**
 * The plugins the app plays, instruments first, in the order of the website. The MIDI effects (Tonwerk Arp, Tonwerk
 * LFO) are left out, since on their own nothing hears what they send, and Tonwerk Synth too, whose two engines are
 * here as Tonwerk Analog and Tonwerk FM; all of them are in the app as AUv3s.
 */
const std::vector<Entry>& catalogue();
} // namespace tonwerkapp
