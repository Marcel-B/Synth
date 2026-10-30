#include "TapeProcessor.h"
#include "Platform.h"

#include "TapeEditor.h"
#include "TapePresets.h"

namespace tonwerktape
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;

/** The seed every transport start begins from: one fixed tape, so every bounce wobbles the same. */
constexpr std::uint32_t kSeed = 1979;

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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TapeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(number("input", "Eingang", { -24.0f, 24.0f }, 0.0f, "dB"));
    layout.add(percent("drive", "Sättigung", 0.4f));
    juce::StringArray types;
    for (const auto* type : { "Spulenband", "Chrom", "Ferro", "Diktiergerät" })
        types.add(juce::String::fromUTF8(type));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "type", 1 }, "Band", types, 2));
    // Skewed so the dark half (where lo-fi lives) gets most of the turn.
    layout.add(number("tone", "Höhen", juce::NormalisableRange<float>(2000.0f, 20000.0f, 0.0f, 0.5f), 12000.0f, "Hz"));
    layout.add(percent("wow", "Leiern", 0.2f));
    layout.add(percent("flutter", "Flattern", 0.2f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "noise", 1 }, "Rauschen", juce::NormalisableRange<float>(kNoiseOff, -30.0f), -66.0f,
        Attributes()
            .withStringFromValueFunction([](float value, int) {
                return value <= kNoiseOff ? juce::String("Aus") : juce::String(juce::roundToInt(value)) + " dB";
            })
            .withValueFromStringFunction([](const juce::String& text) {
                return text.trim().startsWithIgnoreCase("aus") ? kNoiseOff : text.getFloatValue();
            })));
    layout.add(percent("dropouts", "Aussetzer", 0.0f));
    layout.add(percent("mix", "Mix", 1.0f));
    layout.add(number("output", "Ausgang", { -24.0f, 12.0f }, 0.0f, "dB"));
    return layout;
}

TapeProcessor::TapeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkTape", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id); };
    values = { get("input"), get("drive"), get("type"),     get("tone"), get("wow"),
               get("flutter"), get("noise"), get("dropouts"), get("mix"),  get("output") };
}

Settings TapeProcessor::readSettings() const
{
    Settings s;
    s.inputDb = values.input->load();
    s.drive = values.drive->load();
    s.type = (TapeType) juce::roundToInt(values.type->load());
    s.toneHz = values.tone->load();
    s.wow = values.wow->load();
    s.flutter = values.flutter->load();
    s.noiseDb = values.noise->load();
    s.dropouts = values.dropouts->load();
    s.mix = values.mix->load();
    s.outputDb = values.output->load();
    return s;
}

void TapeProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    engine.reset(kSeed);
    engine.update(readSettings());
    wasPlaying = false;
    setLatencySamples(engine.latency());
}

bool TapeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void TapeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    bool playing = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            playing = position->getIsPlaying();
    // The same tape from every start: the hiss, the wobble and the dropouts repeat in a bounce.
    if (playing && ! wasPlaying)
        engine.resetRandom(kSeed);
    wasPlaying = playing;

    const int channels = std::min(buffer.getNumChannels(), TapeEngine::kMaxChannels);
    if (channels > 0)
        scopeIn.write(buffer.getReadPointer(0), buffer.getNumSamples());
    engine.update(readSettings());
    engine.process(buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
    if (channels > 0)
        scopeOut.write(buffer.getReadPointer(0), buffer.getNumSamples());
    speed = (float) engine.speed();
    samplesRun += buffer.getNumSamples();
}

juce::AudioProcessorEditor* TapeProcessor::createEditor() { return tonwerkui::forHost<TapeEditor>(*this); }

void TapeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void TapeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerktape

#ifndef TONWERK_TAPE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerktape::TapeProcessor();
}
#endif
