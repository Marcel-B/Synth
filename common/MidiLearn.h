#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace tonwerkui
{
/**
 * MIDI learn for a plugin that takes MIDI: a controller (CC) moves a knob. Tonwerk LFO, Logic's Modulator or a
 * hardware controller in front of the instrument then modulates any of its parameters.
 *
 * The mapping lives in the parameters' own tree, in a child `MidiMap` with one property per controller (`cc20` =
 * `cutoff`), so it is saved with the project and comes back with it; a factory sound leaves it alone. One controller
 * moves one parameter and a parameter listens to one controller. Controllers 0 to 31 take an optional fine half on
 * 32 to 63 (14 bits, as the MIDI standard pairs them): once a fine half has come, the coarse one is read as its upper
 * seven bits, so 16384 steps instead of 128 keep a slow sweep of a cutoff from stepping. Controllers 120 to 127 are
 * channel mode messages and are never learnt; 32 to 63 are only learnt as the fine halves of 0 to 31.
 *
 * The audio thread (`process`) reads the mapping from atomics and sets the parameter as the host would, so the knob
 * moves with it; a controller passes on to the instrument as well (a wheel mapped to a knob still bends its vibrato).
 * The editor's knobs find the plugin's instance by its state (`of`) and listen for changes.
 */
class MidiLearn : public juce::ChangeBroadcaster, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit MidiLearn(juce::AudioProcessorValueTreeState& state);
    ~MidiLearn() override;

    /** The instance working on `state`, or null for a plugin without MIDI learn. Message thread. */
    static MidiLearn* of(const juce::AudioProcessorValueTreeState& state);

    /** Audio thread: moves the mapped parameters by the controllers in `midi`, and takes one to learn. */
    void process(const juce::MidiBuffer& midi);

    /** Message thread: the next controller that comes in is mapped to `parameterId`; an empty id stops learning. */
    void learn(const juce::String& parameterId);
    juce::String learning() const;
    void forget(const juce::String& parameterId);
    /** The controller mapped to `parameterId`, -1 for none. */
    int controllerFor(const juce::String& parameterId) const;
    /** Message thread: maps `controller` to `parameterId` directly (as a learn would), for tests and presets. */
    void assign(int controller, const juce::String& parameterId);

    /** Message thread: writes a controller the audio thread has learnt into the tree. The timer calls it. */
    void commitLearnt();

    static constexpr int kLastLearnable = 119;

private:
    void rebuild();
    juce::ValueTree map();
    void timerCallback() override { commitLearnt(); }
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeRedirected(juce::ValueTree&) override { rebuild(); }

    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::RangedAudioParameter*> parameters;
    juce::StringArray ids;
    /** Per controller, the index of its parameter in `parameters`, -1 for none. */
    std::array<std::atomic<int>, 128> target {};
    std::atomic<int> learnIndex { -1 }, learntController { -1 }, learntIndex { -1 };
    /** Audio thread only: the last coarse half of controllers 0 to 31 and whether a fine half has come for them. */
    std::array<int, 32> coarse {};
    std::array<bool, 32> fine {};
    juce::CriticalSection rebuilding;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearn)
};
} // namespace tonwerkui
