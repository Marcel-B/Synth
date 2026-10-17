#include "GlueProcessor.h"
#include "Platform.h"

#include "GlueEditor.h"
#include "GluePresets.h"

namespace tonwerkglue
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

Layout GlueProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    number(layout, "threshold", "Threshold", -40.0f, 0.0f, d.thresholdDb, "dB");
    juce::StringArray ratios;
    for (const float ratio : kRatios)
        ratios.add(juce::String(ratio, ratio < 2.0f ? 1 : 0) + ":1");
    choice(layout, "ratio", "Ratio", ratios, d.ratio);
    choice(layout, "attack", "Attack", { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, d.attack);
    choice(layout, "release", "Release", { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, d.release);
    number(layout, "knee", "Knie", 0.0f, 12.0f, d.kneeDb, "dB");
    number(layout, "makeup", "Make-up", -6.0f, 24.0f, d.makeupDb, "dB");
    choice(layout, "source", "Quelle", { "Intern", "Sidechain" }, (int) d.source);
    number(layout, "scHighpass", "SC-Hochpass", 20.0f, 500.0f, d.sidechainHighpass, "Hz", 100.0f);
    toggle(layout, "pump", "Pumpen", d.pump);
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.name);
    choice(layout, "pumpDivision", "Pump-Notenwert", divisions, d.pumpDivision);
    number(layout, "pumpDepth", "Pump-Tiefe", 0.0f, 36.0f, d.pumpDepthDb, "dB");
    percent(layout, "pumpLength", "Pump-Länge", d.pumpLength, 0.05f, 1.0f);
    percent(layout, "pumpCurve", "Pump-Kurve", d.pumpCurve);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

GlueProcessor::GlueProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "TonwerkGlue", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings GlueProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.thresholdDb = next();
    s.ratio = whole();
    s.attack = whole();
    s.release = whole();
    s.kneeDb = next();
    s.makeupDb = next();
    s.source = (Source) std::clamp(whole(), 0, 1);
    s.sidechainHighpass = next();
    s.pump = flag();
    s.pumpDivision = whole();
    s.pumpDepthDb = next();
    s.pumpLength = next();
    s.pumpCurve = next();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void GlueProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    keyCopy.assign((std::size_t) std::max(2 * samplesPerBlock, 2), 0.0f);
}

bool GlueProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.inputBuses.size() < 2)
        return true;
    const auto key = layouts.getChannelSet(true, 1);
    return key.isDisabled() || key == juce::AudioChannelSet::mono() || key == juce::AudioChannelSet::stereo();
}

void GlueProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    auto main = getBusBuffer(buffer, false, 0);
    const int inputs = getBus(true, 0) != nullptr ? getBus(true, 0)->getNumberOfChannels() : 0;
    const int channels = main.getNumChannels();
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

    // The side chain, when the host feeds one; read before the main output overwrites anything it shares.
    const float* keyLeft = nullptr;
    const float* keyRight = nullptr;
    if (auto* keyBus = getBus(true, 1); keyBus != nullptr && keyBus->isEnabled() && keyBus->getNumberOfChannels() > 0)
    {
        auto key = getBusBuffer(buffer, true, 1);
        if ((int) keyCopy.size() < 2 * total)
            keyCopy.resize((std::size_t) (2 * total));
        std::copy_n(key.getReadPointer(0), total, keyCopy.data());
        std::copy_n(key.getReadPointer(key.getNumChannels() > 1 ? 1 : 0), total, keyCopy.data() + total);
        keyLeft = keyCopy.data();
        keyRight = keyCopy.data() + total;
    }
    keyConnected.store(keyLeft != nullptr, std::memory_order_relaxed);

    // A mono input plays in the middle; a mono output gets both sides folded together.
    if (channels > 1 && inputs == 1)
        main.copyFrom(1, 0, main, 0, 0, total);
    if ((int) spare.size() < total)
        spare.resize((std::size_t) total);
    float* left = main.getWritePointer(0);
    float* right = channels > 1 ? main.getWritePointer(1) : spare.data();
    if (channels == 1)
        std::copy_n(left, total, right);

    scopeIn.write(left, total);
    const Settings settings = readSettings();
    float deepest = 0.0f, deepestPump = 0.0f, loudest = -100.0f;
    // In slices of 32, so the meter sees the deepest reduction rather than the block's last sample.
    for (int at = 0; at < total; at += 32)
    {
        const int count = std::min(32, total - at);
        const double sliceBeat = beat < 0.0 ? -1.0 : beat + (double) at / getSampleRate() * tempo / 60.0;
        engine.process(left + at, right + at, keyLeft != nullptr ? keyLeft + at : nullptr,
                       keyRight != nullptr ? keyRight + at : nullptr, count, settings, tempo, sliceBeat);
        deepest = std::max(deepest, engine.reductionNow());
        deepestPump = std::max(deepestPump, engine.pumpNow());
        loudest = std::max(loudest, engine.levelNow());
    }
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);
    reduction.store(deepest, std::memory_order_relaxed);
    pumping.store(deepestPump, std::memory_order_relaxed);
    level.store(loudest, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GlueProcessor::createEditor() { return tonwerkui::forHost(new GlueEditor(*this)); }

void GlueProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GlueProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkglue

#ifndef TONWERK_GLUE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkglue::GlueProcessor(); }
#endif
