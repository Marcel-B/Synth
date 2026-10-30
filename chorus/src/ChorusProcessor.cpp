#include "ChorusProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "ChorusEditor.h"
#include "ChorusPresets.h"

namespace tonwerkchorus
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;


/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
            float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + tonwerkui::tr(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial, float min = 0.0f, float max = 1.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const char* id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), tonwerkui::tr(items), initial));
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
}
/** A rate with a decimal below 10 Hz, where a tenth of a hertz matters. */
void hertz(Layout& layout, const char* id, const char* name, float min, float max, float initial)
{
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(1.0f);
    auto toText = [](float value, int) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
    };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** A delay with a decimal below 10 ms, where a flanger lives and a tenth of a millisecond is a different comb. */
void milliseconds(Layout& layout, const char* id, const char* name, float min, float max, float initial)
{
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(6.0f);
    auto toText = [](float value, int) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " ms";
    };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}
} // namespace

Layout ChorusProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "mode", "Modus", { "Chorus", "Ensemble", "Flanger", "Phaser" }, (int) d.mode);
    choice(layout, "shape", "LFO-Form", { "Sinus", "Dreieck" }, (int) d.shape);
    toggle(layout, "sync", "Tempo-Sync", d.sync);
    hertz(layout, "rate", "Rate", 0.02f, 10.0f, d.rate);
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.name);
    choice(layout, "division", "Notenwert", divisions, d.division);
    percent(layout, "depth", "Tiefe", d.depth);
    number(layout, "spread", "Stereo-Versatz", 0.0f, 180.0f, d.spread, "°");
    milliseconds(layout, "delay", "Verzögerung", 0.5f, 30.0f, d.delay * 1000.0f);
    choice(layout, "voices", "Stimmen", { "1", "2", "3", "4" }, d.voices - 1);
    percent(layout, "feedback", "Feedback", d.feedback, -0.95f, 0.95f);
    number(layout, "frequency", "Phaser-Frequenz", 50.0f, 5000.0f, d.frequency, "Hz", 600.0f);
    juce::StringArray stages;
    for (const int count : kStages)
        stages.add(juce::String(count));
    choice(layout, "stages", "Stufen", stages, d.stages);
    number(layout, "lowCut", "Low Cut", 20.0f, 1000.0f, d.lowCut, "Hz", 150.0f);
    number(layout, "highCut", "High Cut", 1000.0f, 20000.0f, d.highCut, "Hz", 6000.0f);
    percent(layout, "width", "Breite", d.width);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

ChorusProcessor::ChorusProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkChorus", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings ChorusProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto seconds = [&next] { return next() / 1000.0f; };

    Settings s;
    s.mode = (Mode) std::clamp(whole(), 0, 3);
    s.shape = (Shape) std::clamp(whole(), 0, 1);
    s.sync = flag();
    s.rate = next();
    s.division = whole();
    s.depth = next();
    s.spread = next();
    s.delay = seconds();
    s.voices = whole() + 1;
    s.feedback = next();
    s.frequency = next();
    s.stages = whole();
    s.lowCut = next();
    s.highCut = next();
    s.width = next();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void ChorusProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
}

bool ChorusProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ChorusProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels();
    const int channels = buffer.getNumChannels();
    if (channels == 0)
        return;
    double tempo = 120.0, beat = -1.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                tempo = *hostBpm;
            if (const auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying() && *ppq >= 0.0)
                beat = *ppq;
        }
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
    engine.process(left, right, total, readSettings(), tempo, beat);
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);
    phase.store(engine.phaseNow(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ChorusProcessor::createEditor() { return tonwerkui::forHost<ChorusEditor>(*this); }

void ChorusProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ChorusProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkchorus

#ifndef TONWERK_CHORUS_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkchorus::ChorusProcessor(); }
#endif
