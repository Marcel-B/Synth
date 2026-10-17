#include "VocoderProcessor.h"
#include "Platform.h"

#include "VocoderEditor.h"
#include "VocoderPresets.h"

namespace tonwerkvocoder
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

juce::StringArray utf8Array(std::initializer_list<const char*> items)
{
    juce::StringArray result;
    for (const auto* item : items)
        result.add(utf8(item));
    return result;
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const juce::String& id, const char* name, float min, float max, float initial,
            const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + utf8(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void percent(Layout& layout, const juce::String& id, const char* name, float initial)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(0.0f, 1.0f), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const juce::String& id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, utf8(name), items, initial));
}

void toggle(Layout& layout, const juce::String& id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, utf8(name), initial));
}

int bandIndex(int bands)
{
    for (std::size_t i = 0; i < kBandCounts.size(); ++i)
        if (kBandCounts[i] == bands)
            return (int) i;
    return 2;
}
} // namespace

juce::String VocoderProcessor::noteName(int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 2);
}

Layout VocoderProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    juce::StringArray bandNames;
    for (const int bands : kBandCounts)
        bandNames.add(juce::String(bands));
    choice(layout, "bands", "Bänder", bandNames, bandIndex(d.bands));
    number(layout, "formant", "Formant", -12.0f, 12.0f, d.formant, "HT");
    percent(layout, "sharpness", "Schärfe", d.sharpness);
    number(layout, "attack", "Attack", 1.0f, 100.0f, d.attack * 1000.0f, "ms", 10.0f);
    number(layout, "release", "Release", 5.0f, 1000.0f, d.release * 1000.0f, "ms", 100.0f);
    percent(layout, "sibilance", "Zischlaute", d.sibilance);
    toggle(layout, "freeze", "Einfrieren", d.freeze);

    choice(layout, "wave", "Welle", utf8Array({ "Säge", "Rechteck", "Puls", "Super-Säge" }), (int) d.wave);
    number(layout, "detune", "Verstimmung", 0.0f, 50.0f, d.detune, "ct");
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "octave", 1 }, "Oktave", -2, 2, d.octave,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int value, int) { return juce::String(value) + " Okt"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getIntValue(); })));
    number(layout, "carrierAttack", "Träger Attack", 1.0f, 2000.0f, d.carrierAttack * 1000.0f, "ms", 100.0f);
    number(layout, "carrierRelease", "Träger Release", 5.0f, 5000.0f, d.carrierRelease * 1000.0f, "ms", 400.0f);
    percent(layout, "noise", "Rauschen", d.noise);
    toggle(layout, "latch", "Akkord halten", d.latch);

    percent(layout, "width", "Breite", d.width);
    percent(layout, "mix", "Mix", d.mix);
    number(layout, "output", "Ausgang", -24.0f, 12.0f, d.outputDb, "dB");
    return layout;
}

VocoderProcessor::VocoderProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkVocoder", createParameterLayout()),
      programs(state, factoryPresets())
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
    for (auto& note : carrierNotes)
        note = -1;
}

Settings VocoderProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto seconds = [&next] { return next() / 1000.0f; };

    Settings s;
    s.bands = kBandCounts[(std::size_t) std::clamp(whole(), 0, (int) kBandCounts.size() - 1)];
    s.formant = next();
    s.sharpness = next();
    s.attack = seconds();
    s.release = seconds();
    s.sibilance = next();
    s.freeze = flag();
    s.wave = (CarrierWave) std::clamp(whole(), 0, (int) CarrierWave::count - 1);
    s.detune = next();
    s.octave = whole();
    s.carrierAttack = seconds();
    s.carrierRelease = seconds();
    s.noise = next();
    s.latch = flag();
    s.width = next();
    s.mix = next();
    s.outputDb = next();
    jassert(at == values.size());
    return s;
}

void VocoderProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    mono.assign((std::size_t) std::max(samplesPerBlock, 1), 0.0f);
    side.assign(mono.size(), 0.0f);
    latched = false;
}

bool VocoderProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void VocoderProcessor::handleMidi(const juce::MidiMessage& message, const Settings& s)
{
    if (message.isNoteOn())
        engine.carrier.noteOn(message.getNoteNumber(), s);
    else if (message.isNoteOff())
        engine.carrier.noteOff(message.getNoteNumber(), s);
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.carrier.allNotesOff();
    else if (message.isPitchWheel())
    {
        wheel = message.getPitchWheelValue();
        engine.carrier.setBend((wheel - 8192) / 8192.0 * 2.0);
    }
}

void VocoderProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midiLearn.process(midi);
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inputs = std::min(getTotalNumInputChannels(), buffer.getNumChannels());
    const Settings s = readSettings();
    if (latched && ! s.latch)
        engine.carrier.unlatch();
    latched = s.latch;
    if ((int) mono.size() < total)
        mono.resize((std::size_t) total);

    // The voice in mono: a stereo take summed, so a voice panned to one side keeps its level.
    std::fill(mono.begin(), mono.begin() + total, 0.0f);
    for (int ch = 0; ch < inputs; ++ch)
        juce::FloatVectorOperations::addWithMultiply(mono.data(), buffer.getReadPointer(ch), 1.0f / (float) inputs,
                                                     total);
    scopeIn.write(mono.data(), total);

    float* left = buffer.getWritePointer(0);
    // A mono output gets the right side in a buffer of its own, folded in below.
    const bool stereo = buffer.getNumChannels() > 1;
    if ((int) side.size() < total)
        side.resize((std::size_t) total);
    float* right = stereo ? buffer.getWritePointer(1) : side.data();

    // In pieces between MIDI events, so a note starts the carrier at its sample.
    int done = 0;
    auto renderUpTo = [&](int end) {
        if (end > done)
            engine.process(mono.data() + done, left + done, right + done, end - done, s);
        done = std::max(done, end);
    };
    for (const auto event : midi)
    {
        renderUpTo(std::clamp(event.samplePosition, done, total));
        handleMidi(event.getMessage(), s);
    }
    renderUpTo(total);
    midi.clear();
    if (! stereo)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + right[i]);

    scopeOut.write(left, total);
    const auto& levels = engine.levels();
    for (std::size_t b = 0; b < kMaxBands; ++b)
        bandLevels[b].store(levels[b], std::memory_order_relaxed);
    bandCount.store(engine.bands(), std::memory_order_relaxed);
    std::size_t shown = 0;
    engine.carrier.forEachNote([&](int note) {
        if (shown < carrierNotes.size())
            carrierNotes[shown++].store(note, std::memory_order_relaxed);
    });
    for (; shown < carrierNotes.size(); ++shown)
        carrierNotes[shown].store(-1, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VocoderProcessor::createEditor() { return tonwerkui::forHost(new VocoderEditor(*this)); }

void VocoderProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void VocoderProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkvocoder

#ifndef TONWERK_VOCODER_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkvocoder::VocoderProcessor(); }
#endif
