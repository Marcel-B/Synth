#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace tonwerk
{
namespace
{
/** Every note may play every sound; the patch decides what it sounds like. */
struct AnySound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};
} // namespace

void SynthVoice::setCurrentPlaybackSampleRate(double rate)
{
    SynthesiserVoice::setCurrentPlaybackSampleRate(rate);
    if (rate > 0.0)
    {
        analog.prepare(rate);
        fm.prepare(rate);
    }
}

void SynthVoice::startNote(int midiNote, float velocity, juce::SynthesiserSound*, int)
{
    engine = shared.patch.engine;
    if (engine == Engine::fm)
        fm.start(midiNote, velocity, shared.patch.fm);
    else
        analog.start(midiNote, velocity, shared.patch.analog);
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        analog.release();
        fm.release();
        return;
    }
    // Taken for another note: JUCE starts that one right away, so this one has to be gone now.
    analog.kill();
    fm.kill();
    clearCurrentNote();
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count)
{
    if (! isVoiceActive())
        return;
    float* out = buffer.getWritePointer(0, start);
    bool active;
    if (engine == Engine::fm)
    {
        fm.render(shared.patch.fm, out, count, shared.bend);
        active = fm.isActive();
    }
    else
    {
        analog.render(shared.patch.analog, out, count, shared.bend);
        active = analog.isActive();
    }
    if (! active)
        clearCurrentNote();
}

TonwerkSynthProcessor::TonwerkSynthProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkSynth", createParameterLayout()),
      reader(state)
{
    for (int i = 0; i < kVoices; ++i)
        synth.addVoice(new SynthVoice(shared));
    synth.addSound(new AnySound());
    shared.patch = reader.read();
    state.state.setProperty("presetName", PresetLibrary::factoryPresets()[0].name, nullptr);
    startTimerHz(10);
}

TonwerkSynthProcessor::~TonwerkSynthProcessor() { stopTimer(); }

bool TonwerkSynthProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void TonwerkSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voiceBuffer.setSize(1, samplesPerBlock);
    keyboardState.reset();
    // The room belongs to the sample rate; load it now rather than on the timer, so the first note already has it.
    effects.loadRoom(EffectsChain::roomFor(reader.read().fx));
}

void TonwerkSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, count, true);

    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm.store(*hostBpm);
    shared.patch = withTempo(reader.read(), bpm.load());
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isPitchWheel())
            shared.bend = (message.getPitchWheelValue() - 8192) / 8192.0 * kBendRange;
    }

    if (voiceBuffer.getNumSamples() < count)
        voiceBuffer.setSize(1, count, false, false, true);
    voiceBuffer.clear();
    synth.renderNextBlock(voiceBuffer, midi, 0, count);

    buffer.clear();
    effects.process(shared.patch.fx, voiceBuffer.getReadPointer(0), buffer, 0, count);
    scope.write(buffer.getReadPointer(0), count);
    midi.clear();
}

void TonwerkSynthProcessor::timerCallback()
{
    // The reverb's room allocates, so it is computed here and never on the audio thread.
    effects.loadRoom(EffectsChain::roomFor(reader.read().fx));
}

juce::AudioProcessorEditor* TonwerkSynthProcessor::createEditor() { return new TonwerkSynthEditor(*this); }

int TonwerkSynthProcessor::getNumPrograms() { return PresetLibrary::factoryPresets().size(); }

void TonwerkSynthProcessor::setCurrentProgram(int index)
{
    const auto presets = PresetLibrary::factoryPresets();
    if (! juce::isPositiveAndBelow(index, presets.size()))
        return;
    currentProgram = index;
    applyPatch(presets[index].patch, presets[index].name);
}

const juce::String TonwerkSynthProcessor::getProgramName(int index)
{
    const auto presets = PresetLibrary::factoryPresets();
    return juce::isPositiveAndBelow(index, presets.size()) ? presets[index].name : juce::String();
}

void TonwerkSynthProcessor::applyPatch(const juce::var& json, const juce::String& name)
{
    writePatch(state, patchFromJson(json, reader.read()));
    state.state.setProperty("presetName", name, nullptr);
}

juce::var TonwerkSynthProcessor::currentPatchJson() const { return patchToJson(reader.read()); }

void TonwerkSynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void TonwerkSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerk

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerk::TonwerkSynthProcessor(); }
