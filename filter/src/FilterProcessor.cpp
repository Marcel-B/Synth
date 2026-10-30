#include "FilterProcessor.h"
#include "Platform.h"

#include "FilterEditor.h"
#include "FilterPresets.h"

namespace tonwerkfilter
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
} // namespace

Layout FilterProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "type", "Typ", names(kTypeNames), (int) d.type);
    number(layout, "cutoff", "Cutoff", 20.0f, 20000.0f, d.cutoff, "Hz", 1000.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    number(layout, "drive", "Drive", 0.0f, 24.0f, d.driveDb, "dB");

    percent(layout, "envAmount", "Hüllkurve", d.envelopeAmount, -1.0f);
    number(layout, "sensitivity", "Empfindlichkeit", -24.0f, 24.0f, d.sensitivityDb, "dB");
    number(layout, "attack", "Attack", 1.0f, 200.0f, d.attackMs, "ms", 20.0f);
    number(layout, "release", "Release", 10.0f, 2000.0f, d.releaseMs, "ms", 200.0f);

    choice(layout, "lfoShape", "LFO-Form", names(kShapeNames), (int) d.shape);
    toggle(layout, "lfoSync", "LFO-Sync", d.sync);
    juce::NormalisableRange<float> rate(0.05f, 20.0f);
    rate.setSkewForCentre(2.0f);
    // Below 10 Hz a tenth of a hertz matters, so the free rate is the one value shown with a decimal.
    add(layout, "lfoRate", "LFO-Rate", rate, d.rate, [](float value) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
    });
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.name);
    choice(layout, "lfoDivision", "LFO-Raster", divisions, d.division);
    percent(layout, "lfoAmount", "LFO-Tiefe", d.lfoAmount);
    number(layout, "spread", "Stereo", 0.0f, 180.0f, d.spreadDegrees, "°");

    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

FilterProcessor::FilterProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkFilter", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings FilterProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.type = (Type) std::clamp(whole(), 0, (int) kTypeNames.size() - 1);
    s.cutoff = next();
    s.resonance = next();
    s.driveDb = next();
    s.envelopeAmount = next();
    s.sensitivityDb = next();
    s.attackMs = next();
    s.releaseMs = next();
    s.shape = (Shape) std::clamp(whole(), 0, (int) kShapeNames.size() - 1);
    s.sync = next() >= 0.5f;
    s.rate = next();
    s.division = whole();
    s.lfoAmount = next();
    s.spreadDegrees = next();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void FilterProcessor::prepareToPlay(double rate, int)
{
    sampleRate = rate;
    engine.prepare((float) rate);
    running = 0.0;
}

bool FilterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void FilterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    const double beats = divisionBeats(settings.division);
    const double increment = settings.sync ? bpm / 60.0 / beats / sampleRate : settings.rate / sampleRate;
    // While the song plays, a synced LFO is where the song position says: on the beat from any start or loop.
    if (settings.sync && locked)
        running = ppq / beats;
    running -= std::floor(running);
    const Clock clock { running, increment };
    running += increment * count;

    scopeIn.write(buffer.getReadPointer(0), count);
    engine.process(buffer.getArrayOfWritePointers(), channels, count, settings, clock);
    scopeOut.write(buffer.getReadPointer(0), count);
    cutoffNow.store(engine.channel(0).cutoffNow(), std::memory_order_relaxed);
    envelopeNow.store(engine.envelope(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* FilterProcessor::createEditor() { return tonwerkui::forHost<FilterEditor>(*this); }

void FilterProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void FilterProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkfilter

#ifndef TONWERK_FILTER_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkfilter::FilterProcessor(); }
#endif
