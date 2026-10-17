#include "LfoProcessor.h"

#include "LfoEditor.h"
#include "LfoPresets.h"

namespace tonwerklfo
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

/** Shares in whole percent (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void whole(Layout& layout, const char* id, const char* name, int low, int high, int initial, const char* prefix,
           const char* suffix)
{
    const juce::String before = utf8(prefix), after = utf8(suffix);
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, utf8(name), low, high, initial,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([before, after](int value, int) { return before + juce::String(value) + after; })
            .withValueFromStringFunction([](const juce::String& text) {
                return text.retainCharacters("0123456789").getIntValue();
            })));
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

Layout LfoProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    juce::StringArray shapes;
    for (const auto* name : kShapeNames)
        shapes.add(utf8(name));
    choice(layout, "shape", "Form", shapes, (int) d.shape);
    toggle(layout, "sync", "Sync", d.sync);
    juce::NormalisableRange<float> rates(0.05f, 20.0f);
    rates.setSkewForCentre(2.0f);
    // Below 10 Hz a tenth of a hertz matters, so the free rate is shown with a decimal there, as in Tonwerk Filter.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "rate", 1 }, "Rate", rates, d.rate,
        Attributes()
            .withStringFromValueFunction([](float value, int) {
                return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
            })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));
    juce::StringArray divisions;
    for (const auto& division : tonwerkwobble::kDivisions)
        divisions.add(division.name);
    choice(layout, "division", "Takt", divisions, d.division);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "phase", 1 }, "Phase", juce::NormalisableRange<float>(0.0f, 360.0f), d.phaseDegrees,
        Attributes()
            .withStringFromValueFunction([](float value, int) { return juce::String(juce::roundToInt(value)) + utf8(" °"); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));
    percent(layout, "depth", "Tiefe", d.depth);
    percent(layout, "centre", "Mitte", d.centre);
    whole(layout, "controller", "Controller", 0, kLastController, d.controller, "CC ", "");
    whole(layout, "channel", "Kanal", 1, 16, d.channel, "", "");
    toggle(layout, "fine", "14 Bit", d.fine);
    choice(layout, "start", "Start", { "Song", "Note" }, (int) d.start);
    return layout;
}

LfoProcessor::LfoProcessor()
    : AudioProcessor(BusesProperties()), // a MIDI effect: no audio in or out
      state(*this, nullptr, "TonwerkLFO", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
    restarts.reserve(256);
}

Settings LfoProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.shape = (Shape) std::clamp(whole(), 0, (int) kShapeNames.size() - 1);
    s.sync = flag();
    s.rate = next();
    s.division = whole();
    s.phaseDegrees = next();
    s.depth = next();
    s.centre = next();
    s.controller = std::clamp(whole(), 0, kLastController);
    s.channel = std::clamp(whole(), 1, 16);
    s.fine = flag();
    s.start = (Start) std::clamp(whole(), 0, 1);
    jassert(at == values.size());
    return s;
}

void LfoProcessor::prepareToPlay(double sampleRate, int)
{
    rate = sampleRate;
    running = 0.0;
    held = 0;
    engine.reset();
    out.ensureSize(8192);
}

bool LfoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
}

void LfoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int count = buffer.getNumSamples();
    buffer.clear();
    if (count <= 0)
        return;
    const auto s = readSettings();
    // A new controller, channel or resolution gets the current value at once.
    if (s.controller != lastController || s.channel != lastChannel || sendsFine(s) != lastFine)
        engine.resend();
    lastController = s.controller;
    lastChannel = s.channel;
    lastFine = sendsFine(s);

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
    const double beats = tonwerkwobble::divisionBeats(s.division);
    const double increment = s.sync ? bpm / 60.0 / beats / rate : s.rate / rate;
    // While the song plays, a synced LFO started by the song is where the song position says: on the beat from any
    // start or loop. Started by a note, it runs from that note.
    if (s.sync && locked && s.start == Start::song)
        running = ppq / beats;
    running -= std::floor(running);

    // Everything passes; the first key of a phrase restarts the cycle (Start note).
    out.clear();
    restarts.clear();
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        if (message.isNoteOn())
        {
            if (held == 0 && s.start == Start::note && restarts.size() < restarts.capacity())
                restarts.push_back(std::clamp(event.samplePosition, 0, count - 1));
            ++held;
        }
        else if (message.isNoteOff())
            held = std::max(0, held - 1);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            held = 0;
        out.addEvent(message, event.samplePosition);
    }

    const Clock clock { running, increment };
    running = engine.process(count, s, clock, restarts.data(), (int) restarts.size(),
                             [this](int offset, int controller, int value, int channel) {
                                 out.addEvent(juce::MidiMessage::controllerEvent(channel, controller, value), offset);
                             });
    midi.swapWith(out);

    phaseNow.store((float) (running - std::floor(running)), std::memory_order_relaxed);
    if (const float value = engine.lastValue(); value >= 0.0f)
        valueNow.store(value, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* LfoProcessor::createEditor() { return new LfoEditor(*this); }

void LfoProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void LfoProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerklfo

#ifndef TONWERK_LFO_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerklfo::LfoProcessor(); }
#endif
