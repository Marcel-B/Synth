#include "DistortionProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "DistortionEditor.h"
#include "DistortionPresets.h"

namespace tonwerkdistortion
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;

/** Levels in whole decibels, as Logic's own effects show them. */
std::unique_ptr<juce::AudioParameterFloat> decibels(const char* id, const char* name, float min, float max, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value)) + " dB"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(juce::String::fromUTF8(name)), juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText));
}

/** The pedal's pots, in whole percent of their turn. */
std::unique_ptr<juce::AudioParameterFloat> percent(const char* id, const char* name, float initial, int version = 1)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, version }, tonwerkui::tr(juce::String::fromUTF8(name)), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText));
}

/** A rate in whole hertz on a logarithmic turn, so the low rates, where a crusher is heard, get most of it. */
std::unique_ptr<juce::AudioParameterFloat> hertz(const char* id, const char* name, float min, float max, float initial,
                                                 int version)
{
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(std::sqrt(min * max));
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value)) + " Hz"; };
    auto fromText = [](const juce::String& text) {
        return text.getFloatValue() * (text.containsIgnoreCase("k") ? 1000.0f : 1.0f);
    };
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, version }, tonwerkui::tr(juce::String::fromUTF8(name)), range, initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DistortionProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(decibels("input", "Eingang", -24.0f, 24.0f, 0.0f));
    layout.add(percent("distortion", "Distortion", 0.5f));
    layout.add(percent("tone", "Tone", 0.5f));
    layout.add(decibels("level", "Level", -30.0f, 12.0f, 0.0f));
    // Version 2: the modes came after the pedal. The order is `Mode`'s, saved in projects: append only.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mode", 2 }, tonwerkui::tr("Modus"),
        tonwerkui::tr(juce::StringArray { "Pedal", juce::String::fromUTF8("Röhre"), "Clipper", "Falter",
                                          "Gleichrichter",
                            "Bitcrusher" }),
        0));
    // Also version 2 (0.5.0 is the first release with the modes): the crusher's rate and the dry/wet mix.
    layout.add(hertz("crushRate", "Samplerate", 500.0f, 48000.0f, 48000.0f, 2));
    layout.add(percent("mix", "Mix", 1.0f, 2));
    return layout;
}

DistortionProcessor::DistortionProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkDistortion", createParameterLayout()),
      programs(state, factoryPresets())
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id); };
    values = { get("input"), get("distortion"), get("tone"), get("level"), get("mode"), get("crushRate"), get("mix") };
    // Builds the diode table here, on the message thread, not on the audio thread's first block.
    DiodeClipper::instance();
}

Settings DistortionProcessor::readSettings() const
{
    Settings s;
    s.inputDb = values.input->load();
    s.distortion = values.distortion->load();
    s.tone = values.tone->load();
    s.levelDb = values.level->load();
    s.mode = (Mode) std::clamp(juce::roundToInt(values.mode->load()), 0, kModes - 1);
    s.crushRate = values.crushRate->load();
    s.mix = values.mix->load();
    return s;
}

void DistortionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int channels = std::clamp(getTotalNumOutputChannels(), 1, 2);
    maxBlock = std::max(samplesPerBlock, 1);
    // Not the maximum quality: its steeper filters cost more, and with the clippers' own anti-aliasing what folds back
    // stays more than 60 dB below even a 2.5 kHz note at full DIST (see the tests).
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) channels, (size_t) kOversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false, false);
    oversampling->initProcessing((size_t) maxBlock);
    dryOversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) channels, (size_t) kOversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        false, false);
    dryOversampling->initProcessing((size_t) maxBlock);
    dry.setSize(channels, maxBlock, false, false, true);
    setLatencySamples(juce::roundToInt(oversampling->getLatencyInSamples()));

    const auto settings = readSettings();
    const auto factor = (float) oversampling->getOversamplingFactor();
    for (auto& drive : drives)
        drive.prepare((float) sampleRate * factor, settings);
    for (auto& voice : voices)
        voice.prepare((float) sampleRate, settings);
    mix.prepare((float) sampleRate, settings.mix);
}

bool DistortionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void DistortionProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    if (oversampling == nullptr)
        return;

    const int channels = std::min(buffer.getNumChannels(), (int) oversampling->numChannels);
    const auto settings = readSettings();
    for (int ch = 0; ch < channels; ++ch)
    {
        drives[(size_t) ch].update(settings);
        voices[(size_t) ch].update(settings);
    }
    mix.set(std::clamp(settings.mix, 0.0f, 1.0f));

    if (channels > 0)
        scopeIn.write(buffer.getReadPointer(0), buffer.getNumSamples());
    juce::dsp::AudioBlock<float> whole(buffer.getArrayOfWritePointers(), (size_t) channels,
                                       (size_t) buffer.getNumSamples());
    // A host may send more than it announced; the oversampler only has room for the announced block.
    for (int start = 0; start < buffer.getNumSamples(); start += maxBlock)
    {
        const int count = std::min(maxBlock, buffer.getNumSamples() - start);
        auto block = whole.getSubBlock((size_t) start, (size_t) count);
        // The dry side goes up and down through an oversampler of its own, so it is delayed and phase-shifted exactly
        // as the distorted side and the two add up without combing. Cheap next to the drive, so it always runs.
        juce::dsp::AudioBlock<float> dryBlock(dry.getArrayOfWritePointers(), (size_t) channels, (size_t) count);
        dryBlock.copyFrom(block);
        dryOversampling->processSamplesUp(dryBlock);
        dryOversampling->processSamplesDown(dryBlock);
        auto up = oversampling->processSamplesUp(block);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& drive = drives[(size_t) ch];
            auto* samples = up.getChannelPointer((size_t) ch);
            for (size_t n = 0; n < up.getNumSamples(); ++n)
                samples[n] = drive.process(samples[n]);
        }
        oversampling->processSamplesDown(block);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& voice = voices[(size_t) ch];
            auto* samples = block.getChannelPointer((size_t) ch);
            for (size_t n = 0; n < block.getNumSamples(); ++n)
                samples[n] = voice.process(samples[n]);
        }
        // The same ramp for both channels, so they stay alike.
        const auto ramp = mix;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto fade = ramp;
            auto* samples = block.getChannelPointer((size_t) ch);
            const auto* clean = dryBlock.getChannelPointer((size_t) ch);
            for (size_t n = 0; n < block.getNumSamples(); ++n)
            {
                const float wet = fade.next();
                samples[n] = clean[n] + wet * (samples[n] - clean[n]);
            }
            mix = fade;
        }
    }
    if (channels > 0)
        scopeOut.write(buffer.getReadPointer(0), buffer.getNumSamples());
}

juce::AudioProcessorEditor* DistortionProcessor::createEditor()
{
    return tonwerkui::forHost(new DistortionEditor(*this));
}

void DistortionProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DistortionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkdistortion

#ifndef TONWERK_DISTORTION_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkdistortion::DistortionProcessor();
}
#endif
