#include "RumbleProcessor.h"
#include "Platform.h"

#include "RumbleEditor.h"
#include "RumblePresets.h"

namespace tonwerkrumble
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

/** Shares in whole percent; the value runs from 0 to `max` (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial, float max = 1.0f)
{
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(0.0f, max), initial,
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

juce::StringArray divisionNames()
{
    juce::StringArray out;
    for (const auto& division : kDivisions)
        out.add(division.name);
    return out;
}
} // namespace

Layout RumbleProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    number(layout, "tone", "Vorfilter", 100.0f, 5000.0f, d.toneHz, "Hz", 800.0f);
    choice(layout, "space", "Raum", names(kSpaceNames), (int) d.space);
    juce::NormalisableRange<float> decay(0.3f, 10.0f);
    decay.setSkewForCentre(2.5f);
    // Under 10 s a tenth of a second matters, so the decay is shown with a decimal there (as Tonwerk Shimmer's).
    add(layout, "decay", "Hallzeit", decay, d.decaySeconds, [](float value) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " s";
    });
    percent(layout, "size", "Größe", d.size);
    number(layout, "predelay", "Predelay", 0.0f, kMaxPredelayMs, d.predelayMs, "ms", 50.0f);
    choice(layout, "echoDivision", "Echo-Raster", divisionNames(), d.echoDivision);
    percent(layout, "feedback", "Feedback", d.feedback, 0.95f);

    number(layout, "drive", "Drive", 0.0f, 36.0f, d.driveDb, "dB");
    choice(layout, "curve", "Kurve", names(kCurveNames), (int) d.curve);

    number(layout, "cutoff", "Tiefpass", 30.0f, 500.0f, d.cutoff, "Hz", 120.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    number(layout, "lowCut", "Hochpass", 20.0f, 150.0f, d.lowCut, "Hz", 50.0f);

    percent(layout, "duck", "Ducking", d.duck);
    number(layout, "duckRelease", "Duck-Release", 20.0f, 1000.0f, d.duckReleaseMs, "ms", 200.0f);
    toggle(layout, "duckSync", "Duck-Takt", d.duckSync);
    choice(layout, "duckDivision", "Duck-Raster", divisionNames(), d.duckDivision);
    percent(layout, "duckFilter", "Filter-Duck", d.duckFilter);

    number(layout, "rumble", "Rumble", -36.0f, 12.0f, d.rumbleDb, "dB");
    add(layout, "kick", "Kick", { kKickOffDb, 6.0f }, d.kickDb, [](float value) {
        return value <= kKickOffDb + 0.5f ? juce::String("aus") : juce::String(juce::roundToInt(value)) + " dB";
    });
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

RumbleProcessor::RumbleProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkRumble", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
    for (auto& v : duckHistory)
        v.store(1.0f);
}

Settings RumbleProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.toneHz = next();
    s.space = (Space) std::clamp(whole(), 0, (int) kSpaceNames.size() - 1);
    s.decaySeconds = next();
    s.size = next();
    s.predelayMs = next();
    s.echoDivision = whole();
    s.feedback = next();
    s.driveDb = next();
    s.curve = (Curve) std::clamp(whole(), 0, (int) kCurveNames.size() - 1);
    s.cutoff = next();
    s.resonance = next();
    s.lowCut = next();
    s.duck = next();
    s.duckReleaseMs = next();
    s.duckSync = next() >= 0.5f;
    s.duckDivision = whole();
    s.duckFilter = next();
    s.rumbleDb = next();
    s.kickDb = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void RumbleProcessor::prepareToPlay(double rate, int)
{
    sampleRate = rate;
    engine.prepare((float) rate);
    running = 0.0;
    columnSamples = std::max(1, juce::roundToInt(rate * kColumnSeconds));
    columnFilled = 0;
}

bool RumbleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void RumbleProcessor::addColumn()
{
    const int at = written.load(std::memory_order_relaxed) % kColumns;
    kickHistory[(std::size_t) at].store(columnKick, std::memory_order_relaxed);
    rumbleHistory[(std::size_t) at].store(columnRumble, std::memory_order_relaxed);
    duckHistory[(std::size_t) at].store(columnDuck, std::memory_order_relaxed);
    written.store((at + 1) % kColumns, std::memory_order_release);
    columnKick = columnRumble = 0.0f;
    columnDuck = 1.0f;
    columnFilled = 0;
}

void RumbleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    const int count = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), 2);
    if (channels == 0)
        return;

    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto tempo = position->getBpm())
                bpm = *tempo;
            // While the song plays, the grid ducking sits where the song position says: on the kick from any start.
            if (const auto at = position->getPpqPosition(); at && position->getIsPlaying())
                running = *at;
        }
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const auto settings = readSettings();

    // In slices that end on the editor's columns, so each column holds exactly its own samples.
    float* const* audio = buffer.getArrayOfWritePointers();
    for (int done = 0; done < count;)
    {
        const int slice = std::min(count - done, columnSamples - columnFilled);
        float* pointers[] = { audio[0] + done, channels > 1 ? audio[1] + done : nullptr };
        engine.process(pointers, channels, slice, settings, { running, beatsPerSample, bpm });
        running += beatsPerSample * slice;
        columnKick = std::max(columnKick, engine.kickLevel());
        columnRumble = std::max(columnRumble, engine.rumbleLevel());
        columnDuck = std::min(columnDuck, engine.duckGain());
        columnFilled += slice;
        done += slice;
        if (columnFilled >= columnSamples)
            addColumn();
    }
    cutoffNow.store(engine.cutoff(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* RumbleProcessor::createEditor() { return tonwerkui::forHost<RumbleEditor>(*this); }

void RumbleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void RumbleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkrumble

#ifndef TONWERK_RUMBLE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkrumble::RumbleProcessor(); }
#endif
