#include "PumpProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "PumpEditor.h"
#include "PumpPresets.h"

namespace tonwerkpump
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::NormalisableRange<float> rangeOf(float min, float max, float centre)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    return range;
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
            float centre = 0.0f)
{
    const juce::String suffix = " " + tonwerkui::tr(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), rangeOf(min, max, centre), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** A time kept in seconds, shown in whole milliseconds. */
void milliseconds(Layout& layout, const char* id, const char* name, float min, float max, float initial, float centre)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 1000.0f)) + " ms"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 1000.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), rangeOf(min, max, centre), initial,
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
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name),
                                                            tonwerkui::tr(items), initial));
}

/** The same with German literals (UTF-8), which a juce::StringArray would read as ASCII. */
void choice(Layout& layout, const char* id, const char* name, std::initializer_list<const char*> items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name),
                                                            tonwerkui::tr(items), initial));
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
}
} // namespace

Layout PumpProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "mode", "Modus", { "Auslöser", "Kompressor", "Tempo" }, (int) d.mode);
    choice(layout, "source", "Quelle", { "Sidechain", "Eingang" }, (int) d.source);
    number(layout, "threshold", "Threshold", -60.0f, 0.0f, d.thresholdDb, "dB");
    {
        auto toText = [](float value, int) { return juce::String(juce::roundToInt(value)) + ":1"; };
        auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { "ratio", 1 }, tonwerkui::tr("Ratio"), rangeOf(1.0f, 20.0f, 4.0f), d.ratio,
            Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
    }
    number(layout, "depth", "Tiefe", 0.0f, 48.0f, d.depthDb, "dB");
    milliseconds(layout, "attack", "Attack", 0.0f, 0.05f, d.attack, 0.005f);
    milliseconds(layout, "hold", "Hold", 0.0f, 0.25f, d.hold, 0.03f);
    milliseconds(layout, "release", "Release", 0.01f, 2.0f, d.release, 0.25f);
    percent(layout, "shape", "Kurve", d.shape, -1.0f, 1.0f);
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.name);
    choice(layout, "division", "Notenwert", divisions, d.division);
    percent(layout, "dynamics", "Dynamik", d.dynamics);
    number(layout, "keyLowpass", "SC-Tiefpass", 40.0f, 20000.0f, d.keyLowpass, "Hz", 400.0f);
    choice(layout, "band", "Bereich", { "Voll", "Tiefen", "Höhen" }, (int) d.band);
    number(layout, "split", "Trennung", 40.0f, 2000.0f, d.split, "Hz", 200.0f);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    toggle(layout, "listen", "Sidechain hören", d.listen);
    return layout;
}

PumpProcessor::PumpProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "TonwerkPump", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings PumpProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.mode = (Mode) std::clamp(whole(), 0, 2);
    s.source = (Source) std::clamp(whole(), 0, 1);
    s.thresholdDb = next();
    s.ratio = next();
    s.depthDb = next();
    s.attack = next();
    s.hold = next();
    s.release = next();
    s.shape = next();
    s.division = whole();
    s.dynamics = next();
    s.keyLowpass = next();
    s.band = (Band) std::clamp(whole(), 0, 2);
    s.split = next();
    s.mix = next();
    s.outputDb = next();
    s.listen = next() >= 0.5f;
    jassert(at == values.size());
    return s;
}

void PumpProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    hitsBefore = 0;
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    keyCopy.assign((std::size_t) std::max(2 * samplesPerBlock, 2), 0.0f);
}

bool PumpProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void PumpProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    // The side chain, when the host feeds one; copied before the main output overwrites anything it shares.
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
    engine.process(left, right, keyLeft, keyRight, total, settings, tempo, beat);
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);

    // The meter's values stay at their extremes until the editor takes them.
    auto deepen = [](std::atomic<float>& value, float now) {
        float was = value.load(std::memory_order_relaxed);
        while (now > was && ! value.compare_exchange_weak(was, now, std::memory_order_relaxed))
        {
        }
    };
    deepen(reduction, engine.reductionNow());
    deepen(level, engine.levelNow());
    hits.fetch_add(engine.hitsHeard() - hitsBefore, std::memory_order_relaxed);
    hitsBefore = engine.hitsHeard();
}

juce::AudioProcessorEditor* PumpProcessor::createEditor() { return tonwerkui::forHost<PumpEditor>(*this); }

void PumpProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PumpProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkpump

#ifndef TONWERK_PUMP_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkpump::PumpProcessor(); }
#endif
