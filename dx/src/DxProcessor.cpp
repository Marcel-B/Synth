#include "DxProcessor.h"

#include "DxEditor.h"
#include "DxPresets.h"
#include "dsp/Tempo.h"

namespace tonwerkdx
{
namespace
{
using Kind = DxParameter::Kind;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

std::vector<DxParameter> build()
{
    std::vector<DxParameter> list;
    auto whole = [](const juce::String& unit) {
        return [unit](float v) { return juce::String(juce::roundToInt(v)) + (unit.isEmpty() ? juce::String() : " " + unit); };
    };
    auto integer = [&](const juce::String& id, const juce::String& name, int min, int max, std::function<int&(DxPatch&)> field,
                       std::function<juce::String(float)> text = {}) {
        list.push_back({ id, name, Kind::integer, (float) min, (float) max, {}, text ? text : whole({}),
                         [field](const DxPatch& p) { return (float) field(const_cast<DxPatch&>(p)); },
                         [field](DxPatch& p, float v) { field(p) = juce::roundToInt(v); } });
    };
    auto number = [&](const juce::String& id, const juce::String& name, float min, float max,
                      std::function<float&(DxPatch&)> field, std::function<juce::String(float)> text) {
        list.push_back({ id, name, Kind::number, min, max, {}, text,
                         [field](const DxPatch& p) { return field(const_cast<DxPatch&>(p)); },
                         [field](DxPatch& p, float v) { field(p) = v; } });
    };
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };

    juce::StringArray algorithmNames;
    for (int i = 1; i <= kAlgorithms; ++i)
        algorithmNames.add(juce::String(i));
    list.push_back({ "algorithm", "Algorithmus", Kind::choice, 0.0f, 31.0f, algorithmNames, {},
                     [](const DxPatch& p) { return (float) p.algorithm; },
                     [](DxPatch& p, float v) { p.algorithm = juce::roundToInt(v); } });
    integer("feedback", "Feedback", 0, 7, [](DxPatch& p) -> int& { return p.feedback; });
    integer("transpose", "Transponieren", -24, 24, [](DxPatch& p) -> int& { return p.transpose; }, whole("HT"));
    number("volume", utf8("Lautstärke"), 0.0f, 1.0f, [](DxPatch& p) -> float& { return p.volume; }, percent);

    list.push_back({ "lfoWave", "LFO Welle", Kind::choice, 0.0f, 5.0f,
                     { "Dreieck", utf8("Sägezahn ab"), utf8("Sägezahn auf"), "Rechteck", "Sinus", "S&H" }, {},
                     [](const DxPatch& p) { return (float) (int) p.lfoWave; },
                     [](DxPatch& p, float v) { p.lfoWave = (LfoWave) juce::roundToInt(v); } });
    integer("lfoSpeed", "LFO Tempo", 0, 99, [](DxPatch& p) -> int& { return p.lfoSpeed; }, [](float v) {
        const double hz = lfoHz(juce::roundToInt(v));
        return (hz < 9.95 ? juce::String(hz, 1) : juce::String(juce::roundToInt(hz))) + " Hz";
    });
    integer("lfoDelay", utf8("LFO Verzögerung"), 0, 99, [](DxPatch& p) -> int& { return p.lfoDelay; });
    integer("lfoPitch", utf8("LFO Tonhöhe"), 0, 99, [](DxPatch& p) -> int& { return p.lfoPitchDepth; });
    integer("lfoAmp", utf8("LFO Lautstärke"), 0, 99, [](DxPatch& p) -> int& { return p.lfoAmpDepth; });
    integer("lfoPitchSens", utf8("LFO Tonhöhe Empf."), 0, 7, [](DxPatch& p) -> int& { return p.lfoPitchSens; });

    for (int i = 0; i < kOperators; ++i)
    {
        const auto prefix = "op" + juce::String(i + 1);
        const auto name = "Op " + juce::String(i + 1) + " ";
        auto op = [i](DxPatch& p) -> OperatorPatch& { return p.ops[(std::size_t) i]; };
        list.push_back({ prefix + "On", name + "an", Kind::toggle, 0.0f, 1.0f, {}, {},
                         [op](const DxPatch& p) { return op(const_cast<DxPatch&>(p)).on ? 1.0f : 0.0f; },
                         [op](DxPatch& p, float v) { op(p).on = v > 0.5f; } });
        integer(prefix + "Coarse", name + "Grob", 0, 31, [op](DxPatch& p) -> int& { return op(p).coarse; },
                [](float v) { return v < 0.5f ? juce::String("0.5") : juce::String(juce::roundToInt(v)); });
        integer(prefix + "Fine", name + "Fein", 0, 99, [op](DxPatch& p) -> int& { return op(p).fine; }, whole("%"));
        integer(prefix + "Detune", name + "Verstimmung", -7, 7, [op](DxPatch& p) -> int& { return op(p).detune; });
        integer(prefix + "Level", name + "Pegel", 0, 99, [op](DxPatch& p) -> int& { return op(p).level; });
        integer(prefix + "Velocity", name + "Anschlag", 0, 7, [op](DxPatch& p) -> int& { return op(p).velocity; });
        integer(prefix + "RateScale", name + "Tastatur-Rate", 0, 7, [op](DxPatch& p) -> int& { return op(p).rateScaling; });
        integer(prefix + "Ams", name + "LFO-Pegel", 0, 3, [op](DxPatch& p) -> int& { return op(p).ampModSens; });
        for (int s = 0; s < 4; ++s)
        {
            const auto stage = juce::String(s + 1);
            integer(prefix + "R" + stage, name + "Rate " + stage, 0, 99,
                    [op, s](DxPatch& p) -> int& { return op(p).rates[(std::size_t) s]; });
            integer(prefix + "L" + stage, name + "Pegel " + stage, 0, 99,
                    [op, s](DxPatch& p) -> int& { return op(p).levels[(std::size_t) s]; });
        }
    }

    juce::StringArray divisions;
    for (const auto& d : tonwerk::kDivisions)
        divisions.add(d.label);
    number("fxDelayMix", "Delay Mix", 0.0f, 1.0f, [](DxPatch& p) -> float& { return p.fx.delayMix; }, percent);
    list.push_back({ "fxDelaySync", "Delay Sync", Kind::toggle, 0.0f, 1.0f, {}, {},
                     [](const DxPatch& p) { return p.fx.delaySync ? 1.0f : 0.0f; },
                     [](DxPatch& p, float v) { p.fx.delaySync = v > 0.5f; } });
    number("fxDelayTime", "Delay Zeit", 0.02f, 1.5f, [](DxPatch& p) -> float& { return p.fx.delayTime; },
           [](float v) { return juce::String(juce::roundToInt(v * 1000.0f)) + " ms"; });
    list.push_back({ "fxDelayDivision", "Delay Teilung", Kind::choice, 0.0f, (float) divisions.size() - 1, divisions, {},
                     [](const DxPatch& p) { return (float) p.fx.delayDivision; },
                     [](DxPatch& p, float v) { p.fx.delayDivision = juce::roundToInt(v); } });
    number("fxDelayFeedback", "Delay Feedback", 0.0f, 0.9f, [](DxPatch& p) -> float& { return p.fx.delayFeedback; }, percent);
    number("fxDelayTone", "Delay Ton", 500.0f, 12000.0f, [](DxPatch& p) -> float& { return p.fx.delayTone; },
           [](float v) { return juce::String(juce::roundToInt(v)) + " Hz"; });
    number("fxReverbMix", "Hall Mix", 0.0f, 1.0f, [](DxPatch& p) -> float& { return p.fx.reverbMix; }, percent);
    number("fxReverbDecay", utf8("Hall Länge"), 0.3f, 8.0f, [](DxPatch& p) -> float& { return p.fx.reverbDecay; },
           [](float v) { return juce::String(juce::roundToInt(v * 1000.0f)) + " ms"; });
    return list;
}

/** Every note may play; the patch decides the sound. */
struct AnySound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};
} // namespace

const std::vector<DxParameter>& parameters()
{
    static const std::vector<DxParameter> list = build();
    return list;
}

void DxSynthVoice::setCurrentPlaybackSampleRate(double rate)
{
    SynthesiserVoice::setCurrentPlaybackSampleRate(rate);
    if (rate > 0.0)
        voice.prepare(rate);
}

void DxSynthVoice::startNote(int midiNote, float velocity, juce::SynthesiserSound*, int)
{
    seed = seed * 1664525u + 1013904223u;
    voice.start(midiNote, velocity, patch, seed);
}

void DxSynthVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        voice.release();
        return;
    }
    voice.kill();
    clearCurrentNote();
}

void DxSynthVoice::renderNextBlock(juce::AudioBuffer<float>& buffer, int start, int count)
{
    if (! isVoiceActive())
        return;
    voice.render(patch, buffer.getWritePointer(0, start), count, bendSemitones);
    if (! voice.isActive())
        clearCurrentNote();
}

juce::AudioProcessorValueTreeState::ParameterLayout DxProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const DxPatch defaults;
    for (const auto& d : parameters())
    {
        const juce::ParameterID id { d.id, 1 };
        const float value = d.get(defaults);
        switch (d.kind)
        {
            case Kind::integer:
            {
                auto text = d.text;
                layout.add(std::make_unique<juce::AudioParameterInt>(
                    id, d.name, (int) d.min, (int) d.max, (int) value,
                    juce::AudioParameterIntAttributes()
                        .withStringFromValueFunction([text](int v, int) { return text((float) v); })
                        .withValueFromStringFunction([](const juce::String& t) { return t.getIntValue(); })));
                break;
            }
            case Kind::number:
            {
                juce::NormalisableRange<float> range(d.min, d.max);
                // Times and the tone get most of the turn in their short and low end.
                if (d.id == "fxDelayTime")
                    range.setSkewForCentre(0.35f);
                else if (d.id == "fxReverbDecay")
                    range.setSkewForCentre(2.0f);
                else if (d.id == "fxDelayTone")
                    range.setSkewForCentre(3000.0f);
                const bool percent = d.max <= 1.0f;
                const bool milliseconds = d.id == "fxDelayTime" || d.id == "fxReverbDecay";
                auto text = d.text;
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, d.name, range, value,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction([text](float v, int) { return text(v); })
                        .withValueFromStringFunction([percent, milliseconds](const juce::String& t) {
                            return t.getFloatValue() / (percent ? 100.0f : milliseconds ? 1000.0f : 1.0f);
                        })));
                break;
            }
            case Kind::choice:
                layout.add(std::make_unique<juce::AudioParameterChoice>(id, d.name, d.choices, (int) value));
                break;
            case Kind::toggle:
                layout.add(std::make_unique<juce::AudioParameterBool>(id, d.name, value > 0.5f));
                break;
        }
    }
    return layout;
}

DxProcessor::DxProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkDX", createParameterLayout())
{
    for (const auto& d : parameters())
    {
        values.push_back(state.getRawParameterValue(d.id));
        jassert(values.back() != nullptr);
    }
    for (int i = 0; i < kVoices; ++i)
        synth.addVoice(new DxSynthVoice(shared, bend));
    synth.addSound(new AnySound());
    // The first factory sound, so a new instance plays something worth hearing.
    setCurrentProgram(0);
    shared = readPatch();
    startTimerHz(10);
}

DxProcessor::~DxProcessor() { stopTimer(); }

DxPatch DxProcessor::readPatch() const
{
    DxPatch patch;
    const auto& list = parameters();
    for (std::size_t i = 0; i < list.size(); ++i)
        list[i].set(patch, values[i]->load(std::memory_order_relaxed));
    return patch;
}

tonwerk::Effects DxProcessor::effectsFor(const DxPatch& p) const
{
    tonwerk::Effects fx;
    fx.delay.mix = p.fx.delayMix;
    fx.delay.time = p.fx.delaySync ? (float) std::clamp(tonwerk::divisionSeconds(p.fx.delayDivision, bpm.load()), 0.02,
                                                        tonwerk::kMaxDelaySeconds)
                                   : p.fx.delayTime;
    fx.delay.feedback = p.fx.delayFeedback;
    fx.delay.tone = p.fx.delayTone;
    fx.reverb.mix = p.fx.reverbMix;
    fx.reverb.decay = p.fx.reverbDecay;
    return fx;
}

void DxProcessor::applyPatch(const DxPatch& patch, const juce::String& name)
{
    for (const auto& d : parameters())
    {
        if (auto* parameter = state.getParameter(d.id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(d.get(patch)));
            parameter->endChangeGesture();
        }
    }
    state.state.setProperty("presetName", name, nullptr);
}

bool DxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void DxProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voiceBuffer.setSize(1, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor(readPatch())));
}

void DxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, count, true);
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm.store(*hostBpm);
    shared = readPatch();
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isPitchWheel())
            bend = (message.getPitchWheelValue() - 8192) / 8192.0 * kBendRange;
    }

    if (voiceBuffer.getNumSamples() < count)
        voiceBuffer.setSize(1, count, false, false, true);
    voiceBuffer.clear();
    synth.renderNextBlock(voiceBuffer, midi, 0, count);

    buffer.clear();
    effects.process(effectsFor(shared), voiceBuffer.getReadPointer(0), buffer, 0, count);
    scope.write(buffer.getReadPointer(0), count);
    midi.clear();
}

void DxProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor(readPatch())));
}

juce::AudioProcessorEditor* DxProcessor::createEditor() { return new DxEditor(*this); }

int DxProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void DxProcessor::setCurrentProgram(int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow(index, (int) presets.size()))
        return;
    currentProgram = index;
    applyPatch(presets[(std::size_t) index].patch, utf8(presets[(std::size_t) index].name.c_str()));
}

const juce::String DxProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name.c_str())
                                                                 : juce::String();
}

void DxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::File DxProcessor::sysexFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile("Tonwerk DX")
        .getChildFile("SysEx");
}

namespace
{
std::vector<NamedDxPatch> readFile(const juce::File& file)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block))
        return {};
    const auto* bytes = static_cast<const std::uint8_t*>(block.getData());
    return readSysex(std::vector<std::uint8_t>(bytes, bytes + block.getSize()));
}
} // namespace

int DxProcessor::importSysex(const juce::File& file, const juce::File& folder)
{
    const auto voices = readFile(file);
    if (voices.empty())
        return 0;
    folder.createDirectory();
    const auto copy = folder.getChildFile(file.getFileName());
    if (copy != file)
        file.copyFileTo(copy);
    return (int) voices.size();
}

std::vector<DxProcessor::Bank> DxProcessor::banks(const juce::File& folder)
{
    std::vector<Bank> result;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "*.syx;*.SYX");
    files.sort();
    for (const auto& file : files)
        if (auto voices = readFile(file); ! voices.empty())
            result.push_back({ file.getFileNameWithoutExtension(), std::move(voices) });
    return result;
}
} // namespace tonwerkdx

#ifndef TONWERK_DX_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkdx::DxProcessor(); }
#endif
