#include "SpreadProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "SpreadEditor.h"
#include "SpreadPresets.h"

namespace tonwerkspread
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

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

void toggle(Layout& layout, const char* id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
}

/** The bass's crossover in hertz, "Aus" at the bottom, where the lows keep their side. */
void bassMono(Layout& layout, float initial)
{
    juce::NormalisableRange<float> range(20.0f, 500.0f);
    range.setSkewForCentre(120.0f);
    const juce::String off = tonwerkui::tr("Aus");
    auto toText = [off](float value, int) {
        return value <= kBassOff ? off : juce::String(juce::roundToInt(value)) + " Hz";
    };
    auto fromText = [off](const juce::String& text) {
        return text.trim().equalsIgnoreCase(off) ? 20.0f : text.getFloatValue();
    };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bassMono", 1 }, tonwerkui::tr("Bass Mono"), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}
} // namespace

Layout SpreadProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    percent(layout, "width", "Breite", d.width, 0.0f, 2.0f);
    juce::StringArray modes;
    for (const auto* mode : kModeNames)
        modes.add(utf8(mode));
    choice(layout, "mode", "Modus", modes, d.mode);
    percent(layout, "amount", "Verbreiterung", d.amount);
    number(layout, "time", "Zeit", 1.0f, kMaxTime * 1000.0f, d.time * 1000.0f, "ms", 10.0f);
    percent(layout, "motion", "Bewegung", d.motion);
    bassMono(layout, d.bassMono);
    number(layout, "highCut", "High Cut", 1000.0f, 20000.0f, d.highCut, "Hz", 6000.0f);
    toggle(layout, "monoCheck", "Mono-Check", d.monoCheck);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

SpreadProcessor::SpreadProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkSpread", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
}

Settings SpreadProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    s.width = next();
    s.mode = whole();
    s.amount = next();
    s.time = next() / 1000.0f;
    s.motion = next();
    s.bassMono = next();
    s.highCut = next();
    s.monoCheck = flag();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void SpreadProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    spare.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    smoothing = (float) (0.3 * sampleRate);
    inLR = inLL = inRR = outLR = outLL = outRR = outMM = outSS = 0.0;
}

bool SpreadProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SpreadProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels();
    const int channels = buffer.getNumChannels();
    if (channels == 0 || total == 0)
        return;

    // A mono input plays in the middle; a mono output gets both sides folded together, which is the input again.
    if (channels > 1 && inputs == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, total);
    if ((int) spare.size() < total)
        spare.resize((std::size_t) total);
    float* left = buffer.getWritePointer(0);
    float* right = channels > 1 ? buffer.getWritePointer(1) : spare.data();
    if (channels == 1)
        std::copy_n(left, total, right);

    inLeft.write(left, total);
    inRight.write(right, total);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 0; i < total; ++i)
    {
        lr += (double) left[i] * right[i];
        ll += (double) left[i] * left[i];
        rr += (double) right[i] * right[i];
    }

    engine.process(left, right, total, readSettings());

    double olr = 0.0, oll = 0.0, orr = 0.0, mm = 0.0, ss = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const double l = left[i], r = right[i];
        olr += l * r;
        oll += l * l;
        orr += r * r;
        mm += 0.25 * (l + r) * (l + r);
        ss += 0.25 * (l - r) * (l - r);
    }
    outLeft.write(left, total);
    outRight.write(right, total);

    // Sums smoothed over about 300 ms; a silent stretch keeps the last reading rather than jumping to mono.
    const double k = 1.0 - std::exp(-(double) total / std::max(1.0f, smoothing));
    auto follow = [k](double& held, double now) { held += k * (now - held); };
    follow(inLR, lr);
    follow(inLL, ll);
    follow(inRR, rr);
    follow(outLR, olr);
    follow(outLL, oll);
    follow(outRR, orr);
    follow(outMM, mm);
    follow(outSS, ss);
    auto correlation = [](double xy, double xx, double yy) {
        return xx * yy > 1.0e-18 ? (float) std::clamp(xy / std::sqrt(xx * yy), -1.0, 1.0) : 1.0f;
    };
    if (inLL + inRR > 1.0e-12)
        correlationIn.store(correlation(inLR, inLL, inRR), std::memory_order_relaxed);
    if (outLL + outRR > 1.0e-12)
    {
        correlationOut.store(correlation(outLR, outLL, outRR), std::memory_order_relaxed);
        sideShare.store((float) std::sqrt(outSS / (outMM + outSS)), std::memory_order_relaxed);
    }

    if (channels == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
}

juce::AudioProcessorEditor* SpreadProcessor::createEditor() { return tonwerkui::forHost<SpreadEditor>(*this); }

void SpreadProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SpreadProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkspread

#ifndef TONWERK_SPREAD_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkspread::SpreadProcessor(); }
#endif
