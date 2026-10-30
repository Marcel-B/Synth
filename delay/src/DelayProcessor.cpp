#include "DelayProcessor.h"
#include "Platform.h"

#include "DelayEditor.h"
#include "DelayPresets.h"

namespace tonwerkdelay
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
            float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + utf8(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial, float min = 0.0f, float max = 1.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const char* id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, utf8(name), items, initial));
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, utf8(name), initial));
}
} // namespace

Layout DelayProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "mode", "Modus", { "Stereo", "Ping-Pong", "Mono" }, (int) d.mode);
    toggle(layout, "sync", "Tempo-Sync", d.sync);
    number(layout, "time", "Zeit", 1.0f, 4000.0f, d.time * 1000.0f, "ms", 400.0f);
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.name);
    choice(layout, "division", "Notenwert", divisions, d.division);
    percent(layout, "offset", "Versatz rechts", d.offset, -0.5f, 0.5f);
    percent(layout, "feedback", "Feedback", d.feedback);
    number(layout, "lowCut", "Low Cut", 20.0f, 2000.0f, d.lowCut, "Hz", 200.0f);
    number(layout, "highCut", "High Cut", 500.0f, 20000.0f, d.highCut, "Hz", 4000.0f);
    percent(layout, "drive", "Sättigung", d.drive);
    percent(layout, "wow", "Leiern", d.wow);
    percent(layout, "duck", "Ducking", d.duck);
    number(layout, "duckRelease", "Ducking Release", 20.0f, 2000.0f, d.duckRelease * 1000.0f, "ms", 300.0f);
    toggle(layout, "hold", "Halten", d.hold);
    percent(layout, "width", "Breite", d.width);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

DelayProcessor::DelayProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkDelay", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings DelayProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto seconds = [&next] { return next() / 1000.0f; };

    Settings s;
    s.mode = (Mode) std::clamp(whole(), 0, 2);
    s.sync = flag();
    s.time = seconds();
    s.division = whole();
    s.offset = next();
    s.feedback = next();
    s.lowCut = next();
    s.highCut = next();
    s.drive = next();
    s.wow = next();
    s.duck = next();
    s.duckRelease = seconds();
    s.hold = flag();
    s.width = next();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void DelayProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
}

bool DelayProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void DelayProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels();
    const int channels = buffer.getNumChannels();
    if (channels == 0)
        return;
    double tempo = 120.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                tempo = *hostBpm;
    bpm.store(tempo, std::memory_order_relaxed);

    // A mono input plays in the middle; a mono output gets both sides folded together.
    if (channels > 1 && inputs == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, total);
    if ((int) spare.size() < total)
        spare.resize((std::size_t) total);
    float* left = buffer.getWritePointer(0);
    float* right = channels > 1 ? buffer.getWritePointer(1) : spare.data();
    if (channels == 1)
        std::copy_n(left, total, right);

    scopeIn.write(left, total);
    engine.process(left, right, total, readSettings(), tempo);
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);
    duckGain.store(engine.duckGainNow(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* DelayProcessor::createEditor() { return tonwerkui::forHost<DelayEditor>(*this); }

void DelayProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DelayProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkdelay

#ifndef TONWERK_DELAY_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkdelay::DelayProcessor(); }
#endif
