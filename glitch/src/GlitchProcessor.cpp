#include "GlitchProcessor.h"

#include "GlitchEditor.h"
#include "GlitchPresets.h"

namespace chromeglitch
{
namespace
{
juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

std::unique_ptr<juce::AudioParameterFloat> number(const char* id, const char* name, float min, float max, float initial,
                                                  const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    // Whole milliseconds and semitones are fine enough; the raw float shows as "49.9999962".
    const juce::String suffix = juce::String(" ") + unit;
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText));
}

/** Chances and amounts show as percent, as Logic's own effects do. */
std::unique_ptr<juce::AudioParameterFloat> percent(const char* id, const char* name, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ChromeGlitchProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(percent("amount", "Glitch-Stärke", 0.5f));
    layout.add(percent("stutterChance", "Stotter-Chance", 0.5f));
    layout.add(number("stutterLength", "Stotter-Länge", 10.0f, 200.0f, 50.0f, "ms", 50.0f));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "repeats", 1 }, "Wiederholungen", 2, 16, 4));
    layout.add(percent("accelerate", "Beschleunigen", 0.0f));
    layout.add(percent("dropoutChance", "Aussetzer-Chance", 0.3f));
    layout.add(number("dropoutLength", "Aussetzer-Länge", 20.0f, 300.0f, 100.0f, "ms", 100.0f));
    layout.add(percent("crush", "Bitcrusher", 0.5f));
    layout.add(percent("pitchChance", "Pitch-Chance", 0.3f));
    layout.add(number("pitchRange", "Pitch-Bereich", 0.0f, 24.0f, 7.0f, "HT"));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "sync", 1 }, "Tempo-Sync", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "division", 1 }, "Raster",
                                                            juce::StringArray { "1/8", "1/16", "1/32" }, 1));
    layout.add(percent("chaos", "Chaos", 0.3f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "freeze", 1 }, "Einfrieren", false));
    layout.add(percent("mix", "Mix", 1.0f));
    // New in the release after 0.3.0, hence version hint 2.
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "seed", 2 }, "Seed", 0, 999, 0));
    return layout;
}

ChromeGlitchProcessor::ChromeGlitchProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ChromeGlitch", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id); };
    values = { get("amount"),        get("stutterChance"), get("stutterLength"), get("repeats"),
               get("accelerate"),    get("dropoutChance"), get("dropoutLength"), get("crush"),
               get("pitchChance"),   get("pitchRange"),    get("sync"),          get("division"),
               get("chaos"),         get("freeze"),        get("mix"),           get("seed") };
}

Settings ChromeGlitchProcessor::readSettings() const
{
    Settings s;
    s.amount = values.amount->load();
    s.stutterChance = values.stutterChance->load();
    s.stutterMs = values.stutterLength->load();
    s.repeats = juce::roundToInt(values.repeats->load());
    s.accelerate = values.accelerate->load();
    s.dropoutChance = values.dropoutChance->load();
    s.dropoutMs = values.dropoutLength->load();
    s.crush = values.crush->load();
    s.pitchChance = values.pitchChance->load();
    s.pitchRange = values.pitchRange->load();
    s.sync = values.sync->load() >= 0.5f;
    s.division = juce::roundToInt(values.division->load());
    s.chaos = values.chaos->load();
    s.freeze = values.freeze->load() >= 0.5f;
    s.mix = values.mix->load();
    s.seed = juce::roundToInt(values.seed->load());
    return s;
}

void ChromeGlitchProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
}

bool ChromeGlitchProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void ChromeGlitchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            transport.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.hasPosition = true;
            }
        }

    engine.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), readSettings(),
                   transport);
    glitching = engine.isGlitching();
    if (engine.eventsStarted() != eventsSeen)
    {
        eventsSeen = engine.eventsStarted();
        lastEvent = (int) engine.lastEvent();
        glitchStarted = true;
    }
}

juce::AudioProcessorEditor* ChromeGlitchProcessor::createEditor()
{
    return new ChromeGlitchEditor(*this);
}

void ChromeGlitchProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ChromeGlitchProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace chromeglitch

#ifndef CHROME_GLITCH_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chromeglitch::ChromeGlitchProcessor();
}
#endif
