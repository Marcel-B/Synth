#include "FreezeProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "FreezeEditor.h"
#include "FreezePresets.h"

namespace tonwerkfreeze
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;

std::unique_ptr<juce::AudioParameterFloat> number(const char* id, const char* name, juce::NormalisableRange<float> range,
                                                  float initial, const char* unit, float shown = 1.0f)
{
    const juce::String suffix = juce::String(" ") + tonwerkui::tr(unit);
    auto toText = [suffix, shown](float value, int) { return juce::String(juce::roundToInt(value * shown)) + suffix; };
    auto fromText = [shown](const juce::String& text) { return text.getFloatValue() / shown; };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText));
}

std::unique_ptr<juce::AudioParameterFloat> percent(const char* id, const char* name, float initial)
{
    return number(id, name, { 0.0f, 1.0f }, initial, "%", 100.0f);
}

std::unique_ptr<juce::AudioParameterChoice> menu(const char* id, const char* name, juce::StringArray items, int initial)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), tonwerkui::tr(items),
                                                        initial);
}

/** Hertz up to 999, then kilohertz, whole numbers both. */
std::unique_ptr<juce::AudioParameterFloat> frequency(const char* id, const char* name, float low, float high, float initial)
{
    auto toText = [](float hz, int) {
        return hz < 999.5f ? juce::String(juce::roundToInt(hz)) + " Hz" : juce::String(juce::roundToInt(hz / 1000.0f)) + " kHz";
    };
    auto fromText = [](const juce::String& text) {
        const float value = text.getFloatValue();
        return text.containsIgnoreCase("k") ? value * 1000.0f : value;
    };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name),
        juce::NormalisableRange<float>(low, high, 0.0f, 0.25f), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText));
}
} // namespace

double FreezeProcessor::intervalBeats(int index)
{
    static constexpr std::array<double, 5> beats { 1.0, 2.0, 4.0, 8.0, 16.0 };
    return beats[(std::size_t) std::clamp(index, 0, (int) beats.size() - 1)];
}

juce::AudioProcessorValueTreeState::ParameterLayout FreezeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "freeze", 1 }, tonwerkui::tr("Freeze"), false));
    layout.add(menu("trigger", "Auslöser", tonwerkui::tr({ "Manuell", "Tempo" }), 0));
    layout.add(menu("interval", "Intervall", tonwerkui::tr({ "1/4", "1/2", "1 Takt", "2 Takte", "4 Takte" }), 2));
    layout.add(number("capture", "Aufnahme", juce::NormalisableRange<float>(20.0f, 2000.0f, 0.0f, 0.4f), 300.0f, "ms"));
    layout.add(number("fade", "Überblenden", juce::NormalisableRange<float>(10.0f, 10000.0f, 0.0f, 0.3f), 800.0f, "ms"));
    layout.add(percent("stack", "Stapeln", 0.0f));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "pitch", 1 }, tonwerkui::tr("Tonhöhe"), -24, 24, 0,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int value, int) { return juce::String(value) + " " + tonwerkui::tr("HT"); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getIntValue(); })));
    layout.add(percent("shimmer", "Schimmer", 0.0f));
    layout.add(percent("motion", "Bewegung", 0.3f));
    layout.add(number("drift", "Drift", juce::NormalisableRange<float>(1.0f, 30.0f, 0.0f, 0.5f), 6.0f, "s"));
    layout.add(percent("blur", "Unschärfe", 0.1f));
    layout.add(percent("width", "Breite", 0.6f));
    layout.add(frequency("lowCut", "Low Cut", 20.0f, 2000.0f, 20.0f));
    layout.add(frequency("highCut", "High Cut", 1000.0f, 20000.0f, 20000.0f));
    layout.add(percent("mix", "Mix", 0.5f));
    layout.add(number("level", "Pegel", { -24.0f, 12.0f }, 0.0f, "dB"));
    return layout;
}

FreezeProcessor::FreezeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkFreeze", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id); };
    values = { get("freeze"), get("trigger"), get("interval"), get("capture"), get("fade"),   get("stack"),
               get("pitch"),  get("shimmer"), get("motion"),   get("drift"),   get("blur"),   get("width"),
               get("lowCut"), get("highCut"), get("mix"),      get("level") };
    for (auto& band : liveBands)
        band = -120.0f;
    for (auto& band : frozenBands)
        band = -120.0f;
}

Settings FreezeProcessor::readSettings() const
{
    Settings s;
    s.captureMs = values.capture->load();
    s.fadeMs = values.fade->load();
    s.stack = values.stack->load();
    s.pitch = values.pitch->load();
    s.shimmer = values.shimmer->load();
    s.motion = values.motion->load();
    s.driftSeconds = values.drift->load();
    s.blur = values.blur->load();
    s.width = values.width->load();
    s.lowCutHz = values.lowCut->load();
    s.highCutHz = values.highCut->load();
    s.mix = values.mix->load();
    s.levelDb = values.level->load();
    return s;
}

void FreezeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.update(readSettings());
    engine.prepare(sampleRate);
    spareRight.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    lastGridLine = -1.0;
    wasFrozen = false;
    setLatencySamples(0);
}

bool FreezeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void FreezeProcessor::run(const float* inL, const float* inR, float* outL, float* outR, int from, int to)
{
    if (to > from)
        engine.process(inL + from, inR + from, outL + from, outR + from, to - from);
}

void FreezeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int total = buffer.getNumSamples();
    const int inputs = std::min(getTotalNumInputChannels(), buffer.getNumChannels());
    const int outputs = std::min(getTotalNumOutputChannels(), buffer.getNumChannels());
    if ((int) spareRight.size() < total)
        spareRight.resize((std::size_t) total);
    engine.update(readSettings());

    const bool frozen = values.freeze->load() > 0.5f;
    engine.setFrozen(frozen);
    if (frozen && ! wasFrozen)
        lastGridLine = -1.0;
    wasFrozen = frozen;

    // A mono track: the one channel for both sides in, the left side out.
    float* left = buffer.getWritePointer(0);
    if (inputs < 2 && outputs >= 2)
        buffer.copyFrom(1, 0, left, total);
    float* right = outputs >= 2 ? buffer.getWritePointer(1) : spareRight.data();
    if (outputs < 2)
        std::copy_n(left, total, right);

    // On tempo, a new freeze at each grid line the song position crosses in this block, at its sample.
    int splitAt = -1;
    if (frozen && juce::roundToInt(values.trigger->load()) == 1)
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition(); position && position->getIsPlaying())
                if (const auto ppq = position->getPpqPosition())
                {
                    const double bpm = position->getBpm().orFallback(120.0);
                    const double grid = intervalBeats(juce::roundToInt(values.interval->load()));
                    const double beatsPerSample = bpm / 60.0 / getSampleRate();
                    const double line = std::ceil(*ppq / grid - 1.0e-9) * grid;
                    const double at = (line - *ppq) / beatsPerSample;
                    if (at < total && std::abs(line - lastGridLine) > 1.0e-6)
                    {
                        splitAt = std::clamp((int) std::floor(at), 0, total);
                        lastGridLine = line;
                    }
                }

    if (splitAt >= 0)
    {
        run(left, right, left, right, 0, splitAt);
        engine.capture();
        run(left, right, left, right, splitAt, total);
    }
    else
        run(left, right, left, right, 0, total);

    const auto& live = engine.liveSpectrum();
    const auto& held = engine.frozenSpectrum();
    for (std::size_t b = 0; b < liveBands.size(); ++b)
    {
        liveBands[b].store(live[b], std::memory_order_relaxed);
        frozenBands[b].store(held[b], std::memory_order_relaxed);
    }
    frozenLevel = engine.frozenLevel();
    freezes = engine.freezesTaken();
}

juce::AudioProcessorEditor* FreezeProcessor::createEditor() { return tonwerkui::forHost<FreezeEditor>(*this); }

void FreezeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void FreezeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkfreeze

#ifndef TONWERK_FREEZE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkfreeze::FreezeProcessor();
}
#endif
