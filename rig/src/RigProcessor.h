#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

namespace tonwerkrig
{
/** Effect places behind the instrument. */
inline constexpr int kSlots = 6;
/** A slot's source: 0 the whole instrument, 1 to `kTrackSources` one of Tonwerk Groovebox's tracks. */
inline constexpr int kTrackSources = 8;

/** A factory rig: the instrument and its effects with the sounds they start with (program names, empty for Init). */
struct RigPreset
{
    struct Effect
    {
        const char* kind;
        int source = 0;
        const char* program = "";
    };
    const char* name;
    const char* category;
    const char* instrument;
    const char* program;
    std::vector<Effect> effects;
};

/** The factory rigs, in program order; new ones go at the end. */
const std::vector<RigPreset>& factoryRigs();
/** The menu's groups. */
inline constexpr std::array<const char*, 4> kRigCategories { "Drums", "Bass", "Flächen", "Leads" };

/**
 * Tonwerk Rig: one of the instruments with up to `kSlots` effects behind it, saved as one sound. The plugins inside are
 * the same processors as on their own, made by `RigCatalogue`, so they sound the same. With Tonwerk Groovebox an effect
 * can take a single track: the groovebox's output of that track is turned on, the track's effects run on it, and it
 * joins the rest before the effects on the whole. Paths of different latency are delayed to meet.
 *
 * Everything that changes what is inside (instrument, effects, sources, switches) happens on the message thread under
 * `lock`; the audio thread only tries the lock and plays silence for the block it misses. The rig has no host
 * parameters of its own (the inner plugins change with the rig); a host reaches the instrument by MIDI learn.
 */
class RigProcessor : public juce::AudioProcessor
{
public:
    /** The editor, told before anything inside is replaced (so it lets go of the inner editors) and after. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void rigWillChange() = 0;
        virtual void rigChanged() = 0;
    };

    RigProcessor();
    ~RigProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonwerk Rig"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return (int) factoryRigs().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // What is inside; message thread.
    juce::String instrumentKind() const { return instrumentId; }
    juce::AudioProcessor* instrument() const { return instrumentProcessor.get(); }
    juce::String effectKind(int slot) const { return slots[(std::size_t) slot].kind; }
    juce::AudioProcessor* effect(int slot) const { return slots[(std::size_t) slot].processor.get(); }
    int sourceOf(int slot) const { return slots[(std::size_t) slot].source; }
    bool isOn(int slot) const { return slots[(std::size_t) slot].on; }
    /** Whether the instrument has tracks an effect can take one by one. */
    bool hasTracks() const;
    juce::String presetName() const { return name; }

    void setInstrument(const juce::String& kind);
    /** An empty kind clears the slot. */
    void setEffect(int slot, const juce::String& kind);
    void setSource(int slot, int source);
    void setOn(int slot, bool on);
    /** Moves a slot's effect one place earlier (-1) or later (+1). */
    void moveEffect(int slot, int by);

    void setListener(Listener* l) { listener = l; }

    /** The name of a track source (1 to `kTrackSources`), as the groovebox names its outputs. */
    static juce::String sourceName(int source);

private:
    struct Slot
    {
        juce::String kind;
        int source = 0;
        bool on = true;
        std::unique_ptr<juce::AudioProcessor> processor;
        bool prepared = false;
    };

    /** A delay for one stereo path, so paths of different latency meet. */
    struct Delay
    {
        std::array<std::vector<float>, 2> line;
        int length = 0, position = 0;
        void setLength(int samples);
        void process(juce::AudioBuffer<float>& buffer, int channel, int count);
    };

    /** Runs `edit` with the editor told before and after, under the lock, then brings everything up to date. */
    template <typename Edit>
    void change(Edit&& edit);
    /** Bus layout, preparing what is new, delays and latency; under the lock. */
    void rebuild();
    void prepare(juce::AudioProcessor& processor);
    /** Runs a slot's effect on the pair of `buffer`'s channels from `channel` on. */
    void runSlot(Slot& slot, juce::AudioBuffer<float>& buffer, int channel, int count);
    /** The instrument's latency, the slowest track's and the effects' on the whole; message thread. */
    void updateLatency();
    void playPreset(const RigPreset& preset);
    static void chooseProgram(juce::AudioProcessor& processor, const juce::String& program);

    juce::SpinLock lock;
    juce::String instrumentId;
    std::unique_ptr<juce::AudioProcessor> instrumentProcessor;
    bool instrumentPrepared = false;
    std::array<Slot, kSlots> slots;
    /** Which of the groovebox's track outputs are on (those with an effect of their own). */
    std::array<bool, kTrackSources> trackOn {};
    /** Per track and for the main output: the delay that brings it to the slowest track's latency. */
    std::array<Delay, kTrackSources + 1> delays;

    double rate = 0.0;
    int blockSize = 512;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer noMidi;
    Listener* listener = nullptr;
    int currentProgram = 0;
    juce::String name;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RigProcessor)
};
} // namespace tonwerkrig
