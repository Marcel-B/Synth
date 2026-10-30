#include "WobbleProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "WobbleEditor.h"
#include "WobblePresets.h"

namespace tonwerkwobble
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

const std::array<juce::Identifier, 2> kCurveProperties { juce::Identifier("filterCurve"), juce::Identifier("volumeCurve") };


void number(Layout& layout, const char* id, const char* name, juce::NormalisableRange<float> range, float initial,
            std::function<juce::String(float)> toText)
{
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes()
            .withStringFromValueFunction([toText](float value, int) { return toText(value); })
            .withValueFromStringFunction(fromText)));
}

/** Amounts in whole percent. */
void percent(Layout& layout, const char* id, const char* name, float initial)
{
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        Attributes()
            .withStringFromValueFunction([](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; })
            .withValueFromStringFunction(fromText)));
}

/** Whole units, as Logic's own effects show them. */
void whole(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
           float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + tonwerkui::tr(unit);
    number(layout, id, name, range, initial, [suffix](float value) { return juce::String(juce::roundToInt(value)) + suffix; });
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
}

void choice(Layout& layout, const char* id, const char* name, const juce::StringArray& entries, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), tonwerkui::tr(entries), initial));
}

juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (const auto& d : kDivisions)
        names.add(d.name);
    return names;
}

void lane(Layout& layout, const char* prefix, const char* name, int division)
{
    const auto id = [prefix](const char* what) { return std::string(prefix) + what; };
    const auto label = [name](const char* what) { return std::string(name) + " " + what; };
    toggle(layout, id("Sync").c_str(), label("Tempo-Sync").c_str(), true);
    choice(layout, id("Division").c_str(), label("Raster").c_str(), divisionNames(), division);
    // Below 10 Hz a tenth of a hertz matters, so the free rate is the one value shown with a decimal.
    juce::NormalisableRange<float> range(0.05f, 20.0f);
    range.setSkewForCentre(2.0f);
    number(layout, id("Rate").c_str(), label("Rate").c_str(), range, 2.0f, [](float value) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
    });
    whole(layout, id("Phase").c_str(), label("Phase").c_str(), 0.0f, 360.0f, 0.0f, "°");
}
} // namespace

Layout WobbleProcessor::createParameterLayout()
{
    Layout layout;
    toggle(layout, "filterOn", "Filter an", true);
    lane(layout, "filter", "Filter-LFO", kQuarter);
    choice(layout, "filterType", "Filtertyp", { "Tiefpass", "Bandpass", "Hochpass", "Vokal" }, 0);
    choice(layout, "filterSlope", "Flanke", { "12 dB", "24 dB" }, 1);
    whole(layout, "filterLow", "Cutoff unten", 20.0f, 20000.0f, 150.0f, "Hz", 1000.0f);
    whole(layout, "filterHigh", "Cutoff oben", 20.0f, 20000.0f, 3000.0f, "Hz", 1000.0f);
    percent(layout, "resonance", "Resonanz", 0.35f);
    whole(layout, "drive", "Drive", 0.0f, 24.0f, 0.0f, "dB");

    toggle(layout, "volumeOn", "Volume an", false);
    lane(layout, "volume", "Volume-LFO", kQuarter);
    percent(layout, "volumeDepth", "Volume-Tiefe", 1.0f);

    whole(layout, "smooth", "Glätten", 0.0f, 50.0f, 2.0f, "ms", 10.0f);
    whole(layout, "spread", "Stereo", 0.0f, 180.0f, 0.0f, "°");
    percent(layout, "mix", "Mix", 1.0f);
    whole(layout, "output", "Pegel", -24.0f, 12.0f, 0.0f, "dB");
    return layout;
}

WobbleProcessor::WobbleProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkWobble", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const juce::String& id) { return state.getRawParameterValue(id); };
    for (std::size_t i = 0; i < 2; ++i)
    {
        const juce::String prefix = i == 0 ? "filter" : "volume";
        lanes[i] = { get(prefix + "Sync"), get(prefix + "Division"), get(prefix + "Rate"), get(prefix + "Phase") };
    }
    values = { get("filterOn"),  get("filterType"), get("filterSlope"), get("filterLow"), get("filterHigh"),
               get("resonance"), get("drive"),      get("volumeOn"),    get("volumeDepth"), get("smooth"),
               get("spread"),    get("mix"),        get("output") };
    setCurve(Lane::filter, defaultCurve(Lane::filter));
    setCurve(Lane::volume, defaultCurve(Lane::volume));
}

Settings WobbleProcessor::readSettings() const
{
    Settings s;
    s.filterOn = values.filterOn->load() > 0.5f;
    s.filterType = (FilterType) std::clamp((int) values.filterType->load(), 0, 3);
    s.steep = values.filterSlope->load() > 0.5f;
    s.low = values.filterLow->load();
    s.high = values.filterHigh->load();
    s.resonance = values.resonance->load();
    s.driveDb = values.drive->load();
    s.volumeOn = values.volumeOn->load() > 0.5f;
    s.volumeDepth = values.volumeDepth->load();
    s.smoothMs = values.smooth->load();
    s.spreadDegrees = values.spread->load();
    s.mix = values.mix->load();
    s.outputDb = values.output->load();
    return s;
}

void WobbleProcessor::setCurve(Lane lane, const Curve& drawn)
{
    const auto i = index(lane);
    curves[i] = drawn;
    curves[i].tidy();
    tables[i].fill(curves[i]);
    const juce::String text(curves[i].toString());
    if (state.state.getProperty(kCurveProperties[i]).toString() != text)
    {
        state.state.setProperty(kCurveProperties[i], text, nullptr);
        // Not a parameter, so the host would not know the project changed.
        updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }
}

void WobbleProcessor::loadCurves()
{
    for (const auto lane : { Lane::filter, Lane::volume })
    {
        auto loaded = Curve::fromString(state.state.getProperty(kCurveProperties[index(lane)]).toString().toStdString());
        setCurve(lane, loaded.points.empty() ? defaultCurve(lane) : loaded);
    }
}

void WobbleProcessor::setCurrentProgram(int index)
{
    if (! juce::isPositiveAndBelow(index, programs.count()))
        return;
    programs.choose(index);
    const auto& drawn = factoryCurves()[(std::size_t) index];
    setCurve(Lane::filter, drawn.filter.points.empty() ? defaultCurve(Lane::filter) : drawn.filter);
    setCurve(Lane::volume, drawn.volume.points.empty() ? defaultCurve(Lane::volume) : drawn.volume);
}

void WobbleProcessor::prepareToPlay(double rate, int)
{
    sampleRate = rate;
    engine.prepare((float) rate);
    running = {};
}

bool WobbleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void WobbleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    const int count = buffer.getNumSamples();

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

    std::array<Clock, 2> clocks;
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& lane = lanes[i];
        const bool sync = lane.sync->load() > 0.5f;
        const double beats = divisionBeats((int) lane.division->load());
        const double increment = sync ? bpm / 60.0 / beats / sampleRate : lane.rate->load() / sampleRate;
        // While the song plays, a synced LFO is where the song position says: on the beat from any start or loop.
        if (sync && locked)
            running[i] = ppq / beats;
        running[i] -= std::floor(running[i]);
        clocks[i] = { running[i] + lane.phase->load() / 360.0, increment };
        running[i] += increment * count;
    }

    const auto settings = readSettings();
    engine.process(buffer.getArrayOfWritePointers(), std::min(buffer.getNumChannels(), 2), count, settings, tables[0],
                   tables[1], clocks[0], clocks[1]);

    for (std::size_t i = 0; i < 2; ++i)
    {
        const double at = clocks[i].phase + clocks[i].increment * count;
        phase[i].store((float) (at - std::floor(at)), std::memory_order_relaxed);
    }
    value[0].store(engine.channel(0).filterValue(), std::memory_order_relaxed);
    value[1].store(engine.channel(0).volumeValue(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* WobbleProcessor::createEditor() { return tonwerkui::forHost<WobbleEditor>(*this); }

void WobbleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WobbleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
        {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            loadCurves();
        }
}
} // namespace tonwerkwobble

#ifndef TONWERK_WOBBLE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkwobble::WobbleProcessor();
}
#endif
