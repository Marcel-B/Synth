#include "ZoneProcessor.h"
#include "Platform.h"

#include "ZoneEditor.h"
#include "ZonePresets.h"

namespace tonwerkzone
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

void add(Layout& layout, const char* id, const char* name, juce::NormalisableRange<float> range, float initial,
         std::function<juce::String(float, int)> toText)
{
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        Attributes().withStringFromValueFunction(std::move(toText)).withValueFromStringFunction(fromText)));
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const char* id, const char* name, float min, float max, float initial, const char* unit,
            float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + utf8(unit);
    add(layout, id, name, range, initial, [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; });
}

/** Shares in whole percent (1 is 100 %). */
void percent(Layout& layout, const char* id, const char* name, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
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

juce::String ratioText(float ratio) { return juce::String(ratio, ratio < 1.95f ? 1 : 0) + ":1"; }

Layout ZoneProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "shape", "Form", { "Glocke", "Tiefen", utf8("Höhen") }, (int) d.shape);
    number(layout, "frequency", "Frequenz", 20.0f, 20000.0f, d.frequency, "Hz", 1000.0f);
    number(layout, "width", "Breite", 2.0f, 48.0f, d.widthSemitones, "HT");
    choice(layout, "mode", "Modus", { "Band", "Breitband" }, (int) d.mode);
    number(layout, "threshold", "Threshold", -60.0f, 0.0f, d.thresholdDb, "dB");
    juce::NormalisableRange<float> ratios(1.0f, 20.0f);
    ratios.setSkewForCentre(4.0f);
    add(layout, "ratio", "Ratio", ratios, d.ratio, [](float value, int) { return ratioText(value); });
    number(layout, "attack", "Attack", 0.5f, 200.0f, d.attackMs, "ms", 20.0f);
    number(layout, "release", "Release", 10.0f, 1000.0f, d.releaseMs, "ms", 150.0f);
    number(layout, "range", "Bereich", 0.0f, 36.0f, d.rangeDb, "dB");
    choice(layout, "source", "Quelle", { "Intern", "Sidechain" }, (int) d.source);
    toggle(layout, "listen", "Hören", d.listen);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");

    // The LFO on the frequency, appended so projects from before keep their values (depth 0: it does nothing).
    juce::StringArray shapes;
    for (const auto* name : kShapeNames)
        shapes.add(utf8(name));
    choice(layout, "lfoShape", "LFO-Form", shapes, (int) d.lfoShape);
    toggle(layout, "lfoSync", "LFO-Sync", d.lfoSync);
    juce::NormalisableRange<float> rates(0.05f, 20.0f);
    rates.setSkewForCentre(2.0f);
    // Below 10 Hz a tenth of a hertz matters, so the free rate is shown with a decimal there, as in Tonwerk Filter.
    add(layout, "lfoRate", "LFO-Rate", rates, d.lfoRate, [](float value, int) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
    });
    juce::StringArray divisions;
    for (const auto& division : tonwerkwobble::kDivisions)
        divisions.add(division.name);
    choice(layout, "lfoDivision", "LFO-Raster", divisions, d.lfoDivision);
    number(layout, "lfoDepth", "LFO-Tiefe", 0.0f, kMaxLfoSemitones, d.lfoSemitones, "HT");
    return layout;
}

ZoneProcessor::ZoneProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "TonwerkZone", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings ZoneProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.shape = (Shape) std::clamp(whole(), 0, 2);
    s.frequency = next();
    s.widthSemitones = next();
    s.mode = (Mode) std::clamp(whole(), 0, 1);
    s.thresholdDb = next();
    s.ratio = next();
    s.attackMs = next();
    s.releaseMs = next();
    s.rangeDb = next();
    s.source = (Source) std::clamp(whole(), 0, 1);
    s.listen = flag();
    s.mix = next();
    s.outputDb = next();
    s.lfoShape = (LfoShape) std::clamp(whole(), 0, (int) kShapeNames.size() - 1);
    s.lfoSync = flag();
    s.lfoRate = next();
    s.lfoDivision = whole();
    s.lfoSemitones = next();
    jassert(at == values.size());
    return s;
}

void ZoneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    running = 0.0;
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    keyCopy.assign((std::size_t) std::max(2 * samplesPerBlock, 2), 0.0f);
}

bool ZoneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void ZoneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    auto main = getBusBuffer(buffer, false, 0);
    const int inputs = getBus(true, 0) != nullptr ? getBus(true, 0)->getNumberOfChannels() : 0;
    const int channels = main.getNumChannels();
    if (channels == 0 || total == 0)
        return;

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
    const double beats = tonwerkwobble::divisionBeats(settings.lfoDivision);
    const double increment = settings.lfoSync ? bpm / 60.0 / beats / rate : settings.lfoRate / rate;
    // While the song plays, a synced LFO is where the song position says: on the beat from any start or loop.
    if (settings.lfoSync && locked)
        running = ppq / beats;
    running -= std::floor(running);
    const Clock clock { running, increment };
    running += increment * total;

    scopeIn.write(left, total);
    engine.process(left, right, keyLeft, keyRight, total, settings, clock);
    frequency.store(engine.frequencyNow(), std::memory_order_relaxed);
    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    scopeOut.write(left, total);
    // Held until the editor takes them, so a short peak between two of its frames still shows.
    reduction.store(std::max(reduction.load(std::memory_order_relaxed), engine.reductionNow()), std::memory_order_relaxed);
    level.store(std::max(level.load(std::memory_order_relaxed), engine.levelNow()), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ZoneProcessor::createEditor() { return tonwerkui::forHost(new ZoneEditor(*this)); }

void ZoneProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ZoneProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkzone

#ifndef TONWERK_ZONE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkzone::ZoneProcessor(); }
#endif
