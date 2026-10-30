#include "ScopeProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "ScopeEditor.h"
#include "ScopePresets.h"

namespace tonwerkscope
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
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to 1 (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial, float min = 0.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), juce::NormalisableRange<float>(min, 1.0f), initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const char* id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name),
                                                            tonwerkui::tr(items), initial));
}

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
}
} // namespace

Layout ScopeProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "mode", "Modus", { "Welle", "Takt", "XY", tonwerkui::tr("Stimmgerät") }, (int) d.mode);
    number(layout, "time", "Zeit", 1.0f, 500.0f, d.timeMs, "ms", 20.0f);
    juce::StringArray windows;
    for (const auto* name : kWindowNames)
        windows.add(name);
    choice(layout, "window", "Fenster", windows, d.window);
    choice(layout, "trigger", "Trigger", { "Steigend", "Fallend", "Frei" }, (int) d.trigger);
    percent(layout, "triggerLevel", "Trigger-Pegel", d.triggerLevel, -1.0f);
    choice(layout, "channel", "Kanal", { "Summe", "Links", "Rechts", "Beide", "Seite" }, (int) d.channel);
    number(layout, "gain", "Verstärkung", 0.0f, 36.0f, d.gainDb, "dB");
    toggle(layout, "autoGain", "Auto-Zoom", d.autoGain);
    percent(layout, "persistence", "Nachleuchten", d.persistence);
    choice(layout, "colour", "Farbe", { "Cyan", "Gelb", "Rot", tonwerkui::tr("Grün") }, d.colour);
    toggle(layout, "hold", "Halten", d.hold);
    number(layout, "reference", "Kammerton", 430.0f, 450.0f, d.reference, "Hz");
    return layout;
}

ScopeProcessor::ScopeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkScope", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings ScopeProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.mode = (Mode) std::clamp(whole(), 0, 3);
    s.timeMs = next();
    s.window = std::clamp(whole(), 0, (int) kWindowBeats.size() - 1);
    s.trigger = (Trigger) std::clamp(whole(), 0, 2);
    s.triggerLevel = next();
    s.channel = (Channel) std::clamp(whole(), 0, 4);
    s.gainDb = next();
    s.autoGain = flag();
    s.persistence = next();
    s.colour = std::clamp(whole(), 0, 3);
    s.hold = flag();
    s.reference = (float) whole();
    jassert(at == values.size());
    return s;
}

void ScopeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    rate.store(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
}

bool ScopeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void ScopeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The audio is not touched: everything below only reads it.
    const int total = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), getTotalNumInputChannels());
    if (channels == 0 || total == 0)
        return;
    const float* left = buffer.getReadPointer(0);
    const float* right = channels > 1 ? buffer.getReadPointer(1) : left;
    ring.write(left, right, total);

    double bpm = 120.0, beat = -1.0;
    bool running = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;
            if (const auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying())
            {
                beat = *ppq;
                running = true;
            }
        }
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const double perSample = bpm / 60.0 / sampleRate;
    // Stopped, the window runs on at the tempo from where it was, so the picture keeps moving.
    if (! running)
        beat = freeBeat;
    freeBeat = beat + perSample * total;
    // The window is the third parameter `createParameterLayout` adds.
    const int window = juce::roundToInt(values[2]->load(std::memory_order_relaxed));
    const auto windowIndex = (std::size_t) std::clamp(window, 0, (int) kWindowBeats.size() - 1);
    if (window != lastWindow)
    {
        bars.clear();
        lastWindow = window;
    }
    bars.write(left, right, total, beat, perSample, kWindowBeats[windowIndex]);
    tempo.store(bpm, std::memory_order_relaxed);
    playing.store(running, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ScopeProcessor::createEditor() { return tonwerkui::forHost(new ScopeEditor(*this)); }

void ScopeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ScopeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkscope

#ifndef TONWERK_SCOPE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkscope::ScopeProcessor(); }
#endif
