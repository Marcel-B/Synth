#include "MultibandProcessor.h"
#include "Platform.h"

#include "MultibandEditor.h"
#include "MultibandPresets.h"

namespace tonwerkmultiband
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

void add(Layout& layout, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
         float initial, std::function<juce::String(float, int)> toText)
{
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        Attributes().withStringFromValueFunction(std::move(toText)).withValueFromStringFunction(fromText)));
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const juce::String& id, const juce::String& name, float min, float max, float initial,
            const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + utf8(unit);
    add(layout, id, name, range, initial, [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; });
}

/** Shares in whole percent (1 is 100 %). */
void percent(Layout& layout, const juce::String& id, const juce::String& name, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void toggle(Layout& layout, const juce::String& id, const juce::String& name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, initial));
}
} // namespace

juce::String ratioText(float ratio)
{
    return juce::String(ratio, ratio < 1.95f ? 1 : 0) + ":1";
}

Layout MultibandProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    number(layout, "input", "Eingang", -24.0f, 24.0f, d.inputDb, "dB");
    number(layout, "lowSplit", "Trennung Tief/Mitte", 40.0f, 1000.0f, d.lowSplit, "Hz", 200.0f);
    number(layout, "highSplit", "Trennung Mitte/Hoch", 1000.0f, 12000.0f, d.highSplit, "Hz", 3000.0f);
    percent(layout, "depth", "Tiefe", d.depth);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    for (std::size_t b = 0; b < kBands; ++b)
    {
        const juce::String id = kBandIds[b], name = utf8(kBandNames[b]) + " ";
        const auto& band = d.bands[b];
        toggle(layout, id + "On", name + "an", band.on);
        number(layout, id + "Threshold", name + "Threshold", -60.0f, 0.0f, band.thresholdDb, "dB");
        juce::NormalisableRange<float> ratios(1.0f, 20.0f);
        ratios.setSkewForCentre(4.0f);
        add(layout, id + "Ratio", name + "Ratio", ratios, band.ratio, [](float value, int) { return ratioText(value); });
        percent(layout, id + "Upward", name + utf8("Aufwärts"), band.upward);
        number(layout, id + "Attack", name + "Attack", 0.5f, 200.0f, band.attackMs, "ms", 20.0f);
        number(layout, id + "Release", name + "Release", 10.0f, 1000.0f, band.releaseMs, "ms", 150.0f);
        number(layout, id + "Gain", name + "Gain", -24.0f, 24.0f, band.gainDb, "dB");
        toggle(layout, id + "Solo", name + "Solo", band.solo);
    }
    return layout;
}

MultibandProcessor::MultibandProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkMultiband", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
    for (std::size_t b = 0; b < kBands; ++b)
    {
        levels[b].store(-100.0f);
        changes[b].store(0.0f);
    }
}

Settings MultibandProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };

    Settings s;
    s.inputDb = next();
    s.lowSplit = next();
    s.highSplit = next();
    s.depth = next();
    s.mix = next();
    s.outputDb = next();
    for (auto& band : s.bands)
    {
        band.on = flag();
        band.thresholdDb = next();
        band.ratio = next();
        band.upward = next();
        band.attackMs = next();
        band.releaseMs = next();
        band.gainDb = next();
        band.solo = flag();
    }
    jassert(at == values.size());
    return s;
}

void MultibandProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
}

bool MultibandProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void MultibandProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels();
    const int channels = std::min(buffer.getNumChannels(), getTotalNumOutputChannels());
    if (channels == 0 || total == 0)
        return;

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
    engine.process(left, right, total, readSettings());
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);

    // Held until the editor takes them, so a short peak between two of its frames still shows.
    for (std::size_t b = 0; b < kBands; ++b)
    {
        levels[b].store(std::max(levels[b].load(std::memory_order_relaxed), engine.levelNow((int) b)),
                        std::memory_order_relaxed);
        const float change = engine.changeNow((int) b);
        if (std::abs(change) > std::abs(changes[b].load(std::memory_order_relaxed)))
            changes[b].store(change, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* MultibandProcessor::createEditor()
{
    return tonwerkui::forHost<MultibandEditor>(*this);
}

void MultibandProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void MultibandProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkmultiband

#ifndef TONWERK_MULTIBAND_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkmultiband::MultibandProcessor(); }
#endif
