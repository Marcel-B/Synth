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

namespace
{
/** The saved state's root; Tonwerk Synth keeps its first one, so its projects load. */
juce::Identifier stateType(Edition edition)
{
    switch (edition)
    {
        case Edition::analog:
            return "TonwerkAnalog";
        case Edition::fm:
            return "TonwerkFM";
        case Edition::combined:
            break;
    }
    return "TonwerkSynth";
}
} // namespace

TonwerkSynthProcessor::TonwerkSynthProcessor(Edition pluginEdition)
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      edition(pluginEdition),
      state(*this, nullptr, stateType(pluginEdition), createParameterLayout(pluginEdition)),
      reader(state)
{
    for (int i = 0; i < kVoices; ++i)
        synth.addVoice(new SynthVoice(shared));
    synth.addSound(new AnySound());
    shared.patch = readPatch();
    state.state.setProperty("presetName", PresetLibrary::factoryPresets(edition)[0].name, nullptr);
    startTimerHz(10);
}

juce::String TonwerkSynthProcessor::editionName(Edition edition)
{
    switch (edition)
    {
        case Edition::analog:
            return "Tonwerk Analog";
        case Edition::fm:
            return "Tonwerk FM";
        case Edition::combined:
            break;
    }
    return "Tonwerk Synth";
}

std::optional<Engine> TonwerkSynthProcessor::fixedEngine() const
{
    switch (edition)
    {
        case Edition::analog:
            return Engine::analog;
        case Edition::fm:
            return Engine::fm;
        case Edition::combined:
            break;
    }
    return std::nullopt;
}

bool TonwerkSynthProcessor::plays(const juce::var& json) const
{
    const auto engine = fixedEngine();
    return ! engine || patchFromJson(json).engine == *engine;
}

Patch TonwerkSynthProcessor::readPatch() const
{
    auto patch = reader.read();
    if (const auto engine = fixedEngine())
        patch.engine = *engine;
    return patch;
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
    effects.loadRoom(EffectsChain::roomFor(readPatch().fx));
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
    shared.patch = withTempo(readPatch(), bpm.load());
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
    effects.loadRoom(EffectsChain::roomFor(readPatch().fx));
}

juce::AudioProcessorEditor* TonwerkSynthProcessor::createEditor() { return new TonwerkSynthEditor(*this); }

int TonwerkSynthProcessor::getNumPrograms() { return PresetLibrary::factoryPresets(edition).size(); }

void TonwerkSynthProcessor::setCurrentProgram(int index)
{
    const auto presets = PresetLibrary::factoryPresets(edition);
    if (! juce::isPositiveAndBelow(index, presets.size()))
        return;
    currentProgram = index;
    applyPatch(presets[index].patch, presets[index].name);
}

const juce::String TonwerkSynthProcessor::getProgramName(int index)
{
    const auto presets = PresetLibrary::factoryPresets(edition);
    return juce::isPositiveAndBelow(index, presets.size()) ? presets[index].name : juce::String();
}

bool TonwerkSynthProcessor::applyPatch(const juce::var& json, const juce::String& name)
{
    if (! plays(json))
        return false;
    writePatch(state, patchFromJson(json, readPatch()));
    state.state.setProperty("presetName", name, nullptr);
    return true;
}

juce::var TonwerkSynthProcessor::currentPatchJson() const { return patchToJson(readPatch()); }

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

#ifndef TONWERK_EDITION
    #define TONWERK_EDITION combined
#endif

// Each of the three plugins is this file built with its own edition (CMakeLists.txt).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerk::TonwerkSynthProcessor(tonwerk::Edition::TONWERK_EDITION);
}
