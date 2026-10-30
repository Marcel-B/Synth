#include "BassProcessor.h"
#include "Platform.h"

#include "BassEditor.h"
#include "BassPresets.h"
#include "dsp/Tempo.h"

namespace tonwerkbass
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
void number(Layout& layout, const juce::String& id, const juce::String& name, float min, float max, float initial,
            const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = utf8(unit).isEmpty() ? juce::String() : " " + utf8(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const juce::String& id, const juce::String& name, float initial, float min = 0.0f,
             float max = 1.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void integer(Layout& layout, const juce::String& id, const juce::String& name, int min, int max, int initial,
             const char* unit = "")
{
    const juce::String suffix = utf8(unit).isEmpty() ? juce::String() : " " + utf8(unit);
    auto toText = [suffix](int value, int) { return juce::String(value) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getIntValue(); };
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, name, min, max, initial,
        juce::AudioParameterIntAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, items, initial));
}

void toggle(Layout& layout, const juce::String& id, const juce::String& name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, initial));
}

/** Rates below 10 Hz with a tenth, since there a tenth of a hertz matters; the one value shown with a decimal. */
void hertz(Layout& layout, const juce::String& id, const juce::String& name, float min, float max, float initial)
{
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(1.0f);
    auto toText = [](float value, int) {
        return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
    };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}


bool isFx(const juce::String& id) { return id.startsWith("fx"); }

/** Everything but the fx parameters, which the engine does not read. */
std::vector<std::atomic<float>*> engineValues(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    std::vector<std::atomic<float>*> list;
    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter); withId && ! isFx(withId->paramID))
            list.push_back(state.getRawParameterValue(withId->paramID));
    return list;
}
} // namespace

juce::String BassProcessor::noteName(int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 2);
}

Layout BassProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "wave", "Welle", utf8Array({ "Säge", "Rechteck" }), (int) d.wave);
    integer(layout, "tune", "Tune", -12, 12, d.tune, "HT");
    number(layout, "fine", "Fein", -100.0f, 100.0f, d.fine, "ct");
    percent(layout, "sub", "Sub", d.sub);

    number(layout, "cutoff", "Cutoff", 30.0f, 8000.0f, d.cutoff, "Hz", 500.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    percent(layout, "envMod", "Env Mod", d.envMod);
    number(layout, "decay", "Decay", 30.0f, 3000.0f, d.decay * 1000.0f, "ms", 500.0f);
    percent(layout, "keytrack", "Keytracking", d.keytrack);

    percent(layout, "accent", "Accent", d.accent);
    integer(layout, "accentThreshold", "Accent ab Anschlag", 1, 127, d.accentThreshold);
    percent(layout, "sweep", "Accent-Sweep", d.sweep);

    number(layout, "slide", "Slide", 5.0f, 1000.0f, d.slide * 1000.0f, "ms", 100.0f);
    number(layout, "ampDecay", utf8("Ausklang"), 50.0f, 10000.0f, d.ampDecay * 1000.0f, "ms", 2000.0f);
    number(layout, "release", "Release", 2.0f, 2000.0f, d.release * 1000.0f, "ms", 100.0f);
    percent(layout, "drive", "Drive", d.drive);

    choice(layout, "lfoShape", "LFO Form", utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }),
           (int) d.lfoShape);
    toggle(layout, "lfoSync", "LFO Tempo-Sync", d.lfoSync);
    hertz(layout, "lfoRate", "LFO Rate", 0.05f, 20.0f, d.lfoRate);
    juce::StringArray divisions;
    for (const auto& division : tonwerkwave::kDivisions)
        divisions.add(division.name);
    choice(layout, "lfoDivision", "LFO Raster", divisions, d.lfoDivision);
    choice(layout, "lfoTarget", "LFO Ziel", utf8Array({ "Cutoff", "Tonhöhe", "Lautstärke" }), (int) d.lfoTarget);
    percent(layout, "lfoAmount", "LFO Menge", d.lfoAmount);

    integer(layout, "bendRange", "Pitchbend-Bereich", 0, 24, d.bendRange, "HT");
    number(layout, "master", "Master", -36.0f, 6.0f, d.masterDb, "dB");

    // Tonwerk's delay and hall, with Tonwerk DX's ids.
    const tonwerk::Effects fxDefaults;
    percent(layout, "fxDelayMix", "Delay Mix", fxDefaults.delay.mix);
    toggle(layout, "fxDelaySync", "Delay Sync", fxDefaults.delay.sync);
    number(layout, "fxDelayTime", "Delay Zeit", 20.0f, 1500.0f, fxDefaults.delay.time * 1000.0f, "ms", 350.0f);
    juce::StringArray noteValues;
    for (const auto& division : tonwerk::kDivisions)
        noteValues.add(division.label);
    choice(layout, "fxDelayDivision", "Delay Teilung", noteValues, fxDefaults.delay.division);
    percent(layout, "fxDelayFeedback", "Delay Feedback", fxDefaults.delay.feedback, 0.0f, 0.9f);
    number(layout, "fxDelayTone", "Delay Ton", 500.0f, 12000.0f, fxDefaults.delay.tone, "Hz", 3000.0f);
    percent(layout, "fxReverbMix", "Hall Mix", 0.0f);
    number(layout, "fxReverbDecay", utf8("Hall Länge"), 300.0f, 8000.0f, 1500.0f, "ms", 2000.0f);
    return layout;
}

BassProcessor::BassProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkBass", createParameterLayout())
{
    values = engineValues(*this, state);
    fx = { state.getRawParameterValue("fxDelayMix"),      state.getRawParameterValue("fxDelaySync"),
           state.getRawParameterValue("fxDelayTime"),     state.getRawParameterValue("fxDelayDivision"),
           state.getRawParameterValue("fxDelayFeedback"), state.getRawParameterValue("fxDelayTone"),
           state.getRawParameterValue("fxReverbMix"),     state.getRawParameterValue("fxReverbDecay") };
    state.state.setProperty("presetName", "Init", nullptr);
    startTimerHz(10);
}

BassProcessor::~BassProcessor() { stopTimer(); }

Settings BassProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them, the fx left out.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto seconds = [&next] { return next() / 1000.0f; };

    Settings s;
    s.wave = (Wave) whole();
    s.tune = whole();
    s.fine = next();
    s.sub = next();
    s.cutoff = next();
    s.resonance = next();
    s.envMod = next();
    s.decay = seconds();
    s.keytrack = next();
    s.accent = next();
    s.accentThreshold = whole();
    s.sweep = next();
    s.slide = seconds();
    s.ampDecay = seconds();
    s.release = seconds();
    s.drive = next();
    s.lfoShape = (LfoShape) whole();
    s.lfoSync = flag();
    s.lfoRate = next();
    s.lfoDivision = whole();
    s.lfoTarget = (LfoTarget) whole();
    s.lfoAmount = next();
    s.bendRange = whole();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

tonwerk::Effects BassProcessor::effectsFor() const
{
    tonwerk::Effects e;
    e.delay.mix = fx.delayMix->load();
    e.delay.sync = fx.delaySync->load() >= 0.5f;
    e.delay.division = juce::roundToInt(fx.delayDivision->load());
    e.delay.time = e.delay.sync ? (float) std::clamp(tonwerk::divisionSeconds(e.delay.division, bpm.load()), 0.02,
                                                     tonwerk::kMaxDelaySeconds)
                                : fx.delayTime->load() / 1000.0f;
    e.delay.feedback = fx.delayFeedback->load();
    e.delay.tone = fx.delayTone->load();
    e.reverb.mix = fx.reverbMix->load();
    e.reverb.decay = fx.reverbDecay->load() / 1000.0f;
    return e;
}

bool BassProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void BassProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voices.setSize(2, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void BassProcessor::handle(const juce::MidiMessage& message, const Settings& s)
{
    if (message.isNoteOn())
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity(), s);
    else if (message.isNoteOff())
        engine.noteOff(message.getNoteNumber());
    else if (message.isPitchWheel())
        wheel = message.getPitchWheelValue();
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.allNotesOff();
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
}

void BassProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midiLearn.process(midi);
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, count, true);

    Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            transport.playing = position->getIsPlaying();
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                transport.bpm = *hostBpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.hasPosition = true;
            }
        }
    bpm.store(transport.bpm);

    const Settings s = readSettings();
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
    if (voices.getNumSamples() < count)
        voices.setSize(2, count, false, false, true);
    voices.clear();
    float* mono = voices.getWritePointer(0);

    int done = 0;
    auto renderUpTo = [&](int end) {
        if (end <= done)
            return;
        Transport at = transport;
        at.ppq += (double) done / getSampleRate() * transport.bpm / 60.0;
        engine.render(mono + done, end - done, s, at);
        done = end;
    };
    for (const auto metadata : midi)
    {
        renderUpTo(std::min(metadata.samplePosition, count));
        handle(metadata.getMessage(), s);
    }
    renderUpTo(count);
    midi.clear();

    buffer.clear();
    effects.process(effectsFor(), mono, buffer, 0, count);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));
    scope.write(buffer.getReadPointer(0), count);
    shownNote.store(engine.sounding(), std::memory_order_relaxed);
    shownAccent.store(engine.isAccented(), std::memory_order_relaxed);
    shownSlide.store(engine.isSliding(), std::memory_order_relaxed);
}

void BassProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* BassProcessor::createEditor() { return tonwerkui::forHost<BassEditor>(*this); }

int BassProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void BassProcessor::setCurrentProgram(int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow(index, (int) presets.size()))
        return;
    currentProgram = index;
    const auto& preset = presets[(std::size_t) index];
    // Everything back to its default first, so a preset only names what it changes.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    for (const auto& [parameterId, value] : preset.values)
        if (auto* parameter = state.getParameter(parameterId))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse; // a preset names a parameter that does not exist
    state.state.setProperty("presetName", utf8(preset.name), nullptr);
}

const juce::String BassProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void BassProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void BassProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkbass

#ifndef TONWERK_BASS_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkbass::BassProcessor(); }
#endif
