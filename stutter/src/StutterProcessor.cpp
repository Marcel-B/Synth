#include "StutterProcessor.h"
#include "Platform.h"

#include "StutterEditor.h"
#include "StutterPresets.h"

namespace tonwerkstutter
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

void add(Layout& layout, const char* id, const char* name, juce::NormalisableRange<float> range, float initial,
         std::function<juce::String(float)> show)
{
    auto toText = [show](float value, int) { return show(value); };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
            float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + utf8(unit);
    add(layout, id, name, range, initial, [suffix](float value) { return juce::String(juce::roundToInt(value)) + suffix; });
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial, float min = 0.0f)
{
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(min, 1.0f), initial,
        Attributes()
            .withStringFromValueFunction([](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() / 100.0f; })));
}

void choice(Layout& layout, const char* id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, utf8(name), items, initial));
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, utf8(name), initial));
}

template <std::size_t n>
juce::StringArray names(const std::array<const char*, n>& list)
{
    juce::StringArray out;
    for (const auto* name : list)
        out.add(utf8(name));
    return out;
}

/** A whole number with its unit, the knob in steps of one. */
void integer(Layout& layout, const char* id, const char* name, int min, int max, int initial, const char* unit)
{
    const juce::String suffix = utf8(unit);
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, utf8(name), min, max, initial,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([suffix](int value, int) { return juce::String(value) + suffix; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getIntValue(); })));
}

template <std::size_t n>
juce::StringArray lengths(const std::array<Length, n>& list)
{
    juce::StringArray out;
    for (const auto& length : list)
        out.add(utf8(length.name));
    return out;
}
} // namespace

Layout StutterProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "interval", "Intervall", lengths(kIntervals), d.interval);
    integer(layout, "offset", "Versatz", 0, 63, d.offset, "/16");
    integer(layout, "gate", "Länge", 1, 64, d.gate, "/16");
    percent(layout, "chance", "Chance", d.chance);
    choice(layout, "grid", "Raster", lengths(kGrids), d.grid);
    percent(layout, "variation", "Variation", d.variation);
    integer(layout, "seed", "Variante", 1, 99, d.seed, "");
    toggle(layout, "hold", "Halten", d.hold);

    percent(layout, "decay", "Ausklang", d.decay);
    integer(layout, "pitch", "Tonhöhe", -24, 24, (int) d.pitch, " HT");
    percent(layout, "reverse", "Rückwärts", d.reverse);
    percent(layout, "filter", "Filter", d.filter, -1.0f);

    choice(layout, "mode", "Modus", names(kModeNames), (int) d.mode);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

StutterProcessor::StutterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkStutter", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings StutterProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.interval = std::clamp(whole(), 0, (int) kIntervals.size() - 1);
    s.offset = whole();
    s.gate = whole();
    s.chance = next();
    s.grid = std::clamp(whole(), 0, (int) kGrids.size() - 1);
    s.variation = next();
    s.seed = whole();
    s.hold = next() >= 0.5f;
    s.decay = next();
    s.pitch = (float) whole();
    s.reverse = next();
    s.filter = next();
    s.mode = (Mode) std::clamp(whole(), 0, (int) kModeNames.size() - 1);
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void StutterProcessor::prepareToPlay(double rate, int)
{
    sampleRate = rate;
    engine.prepare(rate);
    running = 0.0;
}

bool StutterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void StutterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    const int count = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), 2);
    if (channels == 0)
        return;

    double bpm = 120.0, ppq = 0.0;
    bool locked = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto tempo = position->getBpm())
                bpm = *tempo;
            if (const auto at = position->getPpqPosition(); at && position->getIsPlaying())
            {
                ppq = *at;
                locked = true;
            }
        }

    const auto settings = readSettings();
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    // While the song plays the beat is where the song is: the same repeats from any start or loop, and in a bounce.
    if (locked)
        running = ppq;
    scopeIn.write(buffer.getReadPointer(0), count);
    engine.process(buffer.getArrayOfWritePointers(), channels, count, settings, { running, beatsPerSample });
    scopeOut.write(buffer.getReadPointer(0), count);
    running += beatsPerSample * count;
    positionNow.store(engine.intervalPosition(), std::memory_order_relaxed);
    repeatNow.store(engine.repeatNumber(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* StutterProcessor::createEditor() { return tonwerkui::forHost<StutterEditor>(*this); }

void StutterProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void StutterProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkstutter

#ifndef TONWERK_STUTTER_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkstutter::StutterProcessor(); }
#endif
