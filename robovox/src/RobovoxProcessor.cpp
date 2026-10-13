#include "RobovoxProcessor.h"

#include "RobovoxEditor.h"
#include "RobovoxPresets.h"

namespace tonwerkrobovox
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;

const juce::StringArray kNoteNames { "C", "Cis", "D", "Dis", "E", "F", "Fis", "G", "Gis", "A", "B", "H" };

std::unique_ptr<juce::AudioParameterFloat> number(const char* id, const char* name, juce::NormalisableRange<float> range,
                                                  float initial, const char* unit, float shown = 1.0f)
{
    const juce::String suffix = juce::String(" ") + juce::String::fromUTF8(unit);
    auto toText = [suffix, shown](float value, int) { return juce::String(juce::roundToInt(value * shown)) + suffix; };
    auto fromText = [shown](const juce::String& text) { return text.getFloatValue() / shown; };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, juce::String::fromUTF8(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText));
}

std::unique_ptr<juce::AudioParameterFloat> percent(const char* id, const char* name, float initial)
{
    return number(id, name, { 0.0f, 1.0f }, initial, "%", 100.0f);
}

std::unique_ptr<juce::AudioParameterChoice> menu(const char* id, const char* name, juce::StringArray items, int initial)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, juce::String::fromUTF8(name), items,
                                                        initial);
}

juce::StringArray utf8Items(std::initializer_list<const char*> items)
{
    juce::StringArray out;
    for (const auto* item : items)
        out.add(juce::String::fromUTF8(item));
    return out;
}
} // namespace

juce::String noteName(int midi)
{
    if (midi < 0)
        return "-";
    return kNoteNames[midi % 12] + juce::String(midi / 12 - 2);
}

juce::AudioProcessorValueTreeState::ParameterLayout RobovoxProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(menu("mode", "Tonhöhe aus", utf8Items({ "Stimme", "MIDI" }), 0));
    layout.add(menu("key", "Tonart", kNoteNames, 2));
    layout.add(menu("scale", "Skala", utf8Items({ "Chromatisch", "Dur", "Moll", "Pentatonik" }), 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "octave", 1 }, "Oktave", -4, 1, -2,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int value, int) { return juce::String(value) + " Okt"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getIntValue(); })));
    layout.add(number("glide", "Gleiten", juce::NormalisableRange<float>(0.0f, 500.0f, 0.0f, 0.4f), 0.0f, "ms"));
    layout.add(menu("wave", "Welle", utf8Items({ "Sägezahn", "Rechteck", "Puls" }), 0));
    layout.add(number("formant", "Formant", { -12.0f, 12.0f }, 0.0f, "HT"));
    layout.add(number("bandwidth", "Bandbreite", juce::NormalisableRange<float>(80.0f, 800.0f, 0.0f, 0.5f), 250.0f, "Hz"));
    layout.add(percent("sibilance", "Zischlaute", 0.6f));
    layout.add(percent("bass", "Fundament", 0.4f));
    layout.add(percent("mix", "Mix", 1.0f));
    layout.add(number("output", "Ausgang", { -24.0f, 12.0f }, 0.0f, "dB"));
    return layout;
}

RobovoxProcessor::RobovoxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkRobovox", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id); };
    values = { get("mode"),    get("key"),       get("scale"),     get("octave"), get("glide"), get("wave"),
               get("formant"), get("bandwidth"), get("sibilance"), get("bass"),   get("mix"),   get("output") };
    held.reserve(128);
}

Settings RobovoxProcessor::readSettings() const
{
    Settings s;
    s.mode = (Mode) juce::roundToInt(values.mode->load());
    s.key = juce::roundToInt(values.key->load());
    s.scale = (Scale) juce::roundToInt(values.scale->load());
    s.octave = juce::roundToInt(values.octave->load());
    s.glideMs = values.glide->load();
    s.wave = (CarrierWave) juce::roundToInt(values.wave->load());
    s.formant = values.formant->load();
    s.bandwidthHz = values.bandwidth->load();
    s.sibilance = values.sibilance->load();
    s.bass = values.bass->load();
    s.mix = values.mix->load();
    s.outputDb = values.output->load();
    return s;
}

void RobovoxProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    engine.update(readSettings());
    mono.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    held.clear();
    setLatencySamples(engine.latency());
}

bool RobovoxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void RobovoxProcessor::handleMidi(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        if (held.size() < held.capacity())
            held.push_back(note);
        engine.setMidiNote(note);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        const bool wasNewest = ! held.empty() && held.back() == note;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        // Back to the key still held, as a mono synth does; with none the carrier keeps its note.
        if (wasNewest && ! held.empty())
            engine.setMidiNote(held.back());
    }
}

void RobovoxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = std::min(getTotalNumInputChannels(), buffer.getNumChannels());
    engine.update(readSettings());
    if ((int) mono.size() < total)
        mono.resize((std::size_t) total);

    // The voice in mono: a stereo take summed, so a voice panned to one side keeps its level.
    std::fill(mono.begin(), mono.begin() + total, 0.0f);
    for (int ch = 0; ch < inputs; ++ch)
        juce::FloatVectorOperations::addWithMultiply(mono.data(), buffer.getReadPointer(ch), 1.0f / (float) inputs,
                                                     total);
    scopeIn.write(mono.data(), total);

    // In pieces between MIDI events, so a note changes the carrier at its sample.
    int done = 0;
    for (const auto event : midi)
    {
        const int at = std::clamp(event.samplePosition, done, total);
        engine.process(mono.data() + done, mono.data() + done, at - done);
        done = at;
        handleMidi(event.getMessage());
    }
    engine.process(mono.data() + done, mono.data() + done, total - done);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, mono.data(), total);
    scopeOut.write(mono.data(), total);
    carrierNote = engine.carrierNote();
    sungPitch = (float) engine.sungPitch();
}

juce::AudioProcessorEditor* RobovoxProcessor::createEditor() { return new RobovoxEditor(*this); }

void RobovoxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void RobovoxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkrobovox

#ifndef TONWERK_ROBOVOX_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkrobovox::RobovoxProcessor();
}
#endif
