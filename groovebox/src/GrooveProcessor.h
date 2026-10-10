#pragma once

#include "EffectsChain.h"
#include "GrooveEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace tonwerkgroove
{
/** The tracks' parameter id prefixes, in grid order. Part of saved projects: never rename one. */
inline constexpr std::array<const char*, kTracks> kTrackIds { "kick",    "snare",     "clap",    "tomLow",
                                                              "tomHigh", "hatClosed", "hatOpen", "cymbal" };
/** The tracks' names in the editor and the host. */
inline constexpr std::array<const char*, kTracks> kTrackNames { "Bassdrum", "Snare",     "Clap",         "Tom tief",
                                                                "Tom hoch", "Hi-Hat zu", "Hi-Hat offen", "Becken" };
/** What `tone` and `character` do on each track, as the knobs are labelled. */
inline constexpr std::array<std::pair<const char*, const char*>, kTracks> kSoundKnobs { {
    { "Punch", "Klick" },
    { "Ton", "Snappy" },
    { "Schärfe", "Streuung" },
    { "Sweep", "Fell" },
    { "Sweep", "Fell" },
    { "Ton", "Rauschen" },
    { "Ton", "Rauschen" },
    { "Ton", "Rauschen" },
} };

/**
 * Tonwerk Groovebox: a drum machine of eight synthesised instruments with a step sequencer that follows the host's
 * transport (or runs by itself), and every instrument on a MIDI note as well. The patterns are not parameters; they
 * live in the state (`Patterns`) and in atomics the audio thread reads. Parameter ids are part of saved Logic projects:
 * never rename or remove one, and append new ones with the version hint of the release they first appear in.
 */
class GrooveProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GrooveProcessor();
    ~GrooveProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Groovebox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    /** The longest cymbal and the longest hall. */
    double getTailLengthSeconds() const override { return 8.0 + 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** The knobs as the engine reads them; lock-free, for the audio thread. */
    Settings readSettings() const;
    juce::String presetName() const { return state.state.getProperty("presetName", {}).toString(); }
    bool roomPlaying() const { return effects.roomPlaying(); }

    /** A pattern's track, for the editor and the tests. */
    TrackPattern trackPattern(int pattern, int track) const;
    void setTrackPattern(int pattern, int track, const TrackPattern& value);
    /** Steps: 0 off, 1 on, 2 accented. */
    int step(int pattern, int track, int step) const;
    void setStep(int pattern, int track, int step, int value);
    void clearPattern(int pattern);
    Pattern pattern(int index) const;
    /** Sets a pattern without telling the host; an edit by the user calls `patternsChanged` afterwards. */
    void setPattern(int index, const Pattern& value);
    /** Tells the host the patterns have changed, so the project counts as modified. */
    void patternsChanged();

    /** Plays a track at once, as a click on its name does. */
    void audition(int track) { auditions.fetch_or(1u << track); }

    juce::AudioProcessorValueTreeState state;

    /** The step that sounded last, -1 while the sequencer stands; for the editor's running light. */
    std::atomic<int> playingStep { -1 };
    /** Each track's loudest sample since the editor last looked; the editor swaps in 0. */
    std::array<std::atomic<float>, kTracks> trackPeaks {};
    /** The track whose knobs the editor shows; kept here so it survives closing the window. */
    std::atomic<int> selectedTrack { 0 };

    /** Hits per track since `prepareToPlay`, for the tests. */
    int hitCount(int track) const { return engine.hitCount(track); }

private:
    void timerCallback() override;
    tonwerk::Effects effectsFor() const;

    Engine engine;
    tonwerk::EffectsChain effects;
    juce::AudioBuffer<float> work;
    int currentProgram = 0;
    std::atomic<double> bpm { 120.0 };

    /** Per pattern and track: the on bits low, the accent bits high. */
    std::array<std::array<std::atomic<std::uint64_t>, kTracks>, kPatterns> patterns {};
    std::atomic<std::uint32_t> auditions { 0 };

    /** Where the last block ended, in beats, while the sequencer ran; the next one starts there if they join. */
    double lastEnd = 0.0;
    bool running = false;
    double freeBeats = 0.0;
    SeqMode lastMode = SeqMode::off;

    struct TrackValues
    {
        std::atomic<float>*tune, *decay, *tone, *character, *level, *pan, *delay, *reverb, *mute;
    };
    std::array<TrackValues, kTracks> trackValues {};
    struct GlobalValues
    {
        std::atomic<float>*seqMode, *pattern, *length, *rate, *swing, *accent, *drive, *master;
    } global {};
    struct FxValues
    {
        std::atomic<float>*delayMix, *delaySync, *delayTime, *delayDivision, *delayFeedback, *delayTone, *reverbMix,
            *reverbDecay;
    } fx {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveProcessor)
};
} // namespace tonwerkgroove
