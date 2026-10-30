#include "NeuroProcessor.h"
#include "Platform.h"

#include "NeuroEditor.h"
#include "NeuroPresets.h"
#include "dsp/Tempo.h"

namespace tonwerkneuro
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


template <std::size_t n>
juce::StringArray names(const std::array<const char*, n>& list)
{
    juce::StringArray out;
    for (const auto* name : list)
        out.add(utf8(name));
    return out;
}

/** An ADSR: times in whole ms, sustain in percent. */
void envelope(Layout& layout, const juce::String& prefix, const juce::String& name, const EnvSettings& d)
{
    number(layout, prefix + "Attack", name + " Attack", 0.0f, 5000.0f, d.attack * 1000.0f, "ms", 100.0f);
    number(layout, prefix + "Decay", name + " Decay", 1.0f, 5000.0f, d.decay * 1000.0f, "ms", 300.0f);
    percent(layout, prefix + "Sustain", name + " Sustain", d.sustain);
    number(layout, prefix + "Release", name + " Release", 1.0f, 5000.0f, d.release * 1000.0f, "ms", 300.0f);
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

juce::String NeuroProcessor::noteName(int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 2);
}

Layout NeuroProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    percent(layout, "reese", "Reese", d.reese);
    number(layout, "detune", "Verstimmung", 0.0f, 60.0f, d.detune, "ct");
    percent(layout, "width", "Breite", d.width);
    percent(layout, "fm", "FM", d.fm);
    juce::StringArray ratios;
    for (const auto& ratio : kRatios)
        ratios.add(ratio.name);
    choice(layout, "ratio", utf8("FM-Verhältnis"), ratios, d.ratio);
    percent(layout, "fmIndex", "FM-Index", d.fmIndex);
    percent(layout, "feedback", "FM-Feedback", d.feedback);
    percent(layout, "sub", "Sub", d.sub);
    choice(layout, "subOctave", "Sub-Lage", utf8Array({ "Oktave tiefer", "Grundton" }), d.subOctave < 0 ? 0 : 1);
    integer(layout, "tune", "Tune", -24, 24, d.tune, "HT");

    choice(layout, "driveMode", "Drive-Kurve", names(kDriveNames), (int) d.driveMode);
    percent(layout, "drive", "Drive", d.drive);

    choice(layout, "filterType", "Filtertyp", names(kFilterNames), (int) d.filterType);
    number(layout, "cutoff", "Cutoff", 20.0f, 18000.0f, d.cutoff, "Hz", 600.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    percent(layout, "envCutoff", utf8("Hüllkurve Cutoff"), d.envCutoff, -1.0f);
    percent(layout, "envFm", utf8("Hüllkurve FM"), d.envFm);

    envelope(layout, "amp", "Amp", d.amp);
    envelope(layout, "mod", "Mod", d.mod);

    choice(layout, "lfoShape", "LFO Form", utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }),
           (int) d.lfoShape);
    toggle(layout, "lfoSync", "LFO Tempo-Sync", d.lfoSync);
    hertz(layout, "lfoRate", "LFO Rate", 0.05f, 20.0f, d.lfoRate);
    juce::StringArray divisions;
    for (const auto& division : tonwerkwave::kDivisions)
        divisions.add(division.name);
    choice(layout, "lfoDivision", "LFO Raster", divisions, d.lfoDivision);
    toggle(layout, "lfoRetrigger", "LFO Neustart", d.lfoRetrigger);
    percent(layout, "lfoCutoff", "LFO Cutoff", d.lfoCutoff);
    percent(layout, "lfoFm", "LFO FM", d.lfoFm);

    number(layout, "glide", "Glide", 0.0f, 1000.0f, d.glide * 1000.0f, "ms", 100.0f);
    toggle(layout, "legato", "Legato", d.legato);
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

NeuroProcessor::NeuroProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkNeuro", createParameterLayout())
{
    values = engineValues(*this, state);
    fx = { state.getRawParameterValue("fxDelayMix"),      state.getRawParameterValue("fxDelaySync"),
           state.getRawParameterValue("fxDelayTime"),     state.getRawParameterValue("fxDelayDivision"),
           state.getRawParameterValue("fxDelayFeedback"), state.getRawParameterValue("fxDelayTone"),
           state.getRawParameterValue("fxReverbMix"),     state.getRawParameterValue("fxReverbDecay") };
    state.state.setProperty("presetName", "Init", nullptr);
    startTimerHz(10);
}

NeuroProcessor::~NeuroProcessor() { stopTimer(); }

Settings NeuroProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them, the fx left out.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto seconds = [&next] { return next() / 1000.0f; };

    Settings s;
    s.reese = next();
    s.detune = next();
    s.width = next();
    s.fm = next();
    s.ratio = whole();
    s.fmIndex = next();
    s.feedback = next();
    s.sub = next();
    s.subOctave = whole() == 0 ? -1 : 0;
    s.tune = whole();
    s.driveMode = (DistortionMode) std::clamp(whole(), 0, (int) kDriveNames.size() - 1);
    s.drive = next();
    s.filterType = (FilterType) std::clamp(whole(), 0, (int) kFilterNames.size() - 1);
    s.cutoff = next();
    s.resonance = next();
    s.envCutoff = next();
    s.envFm = next();
    for (auto* env : { &s.amp, &s.mod })
    {
        env->attack = seconds();
        env->decay = seconds();
        env->sustain = next();
        env->release = seconds();
    }
    s.lfoShape = (LfoShape) whole();
    s.lfoSync = flag();
    s.lfoRate = next();
    s.lfoDivision = whole();
    s.lfoRetrigger = flag();
    s.lfoCutoff = next();
    s.lfoFm = next();
    s.glide = seconds();
    s.legato = flag();
    s.bendRange = whole();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

tonwerk::Effects NeuroProcessor::effectsFor() const
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

bool NeuroProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void NeuroProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voices.setSize(2, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void NeuroProcessor::handle(const juce::MidiMessage& message, const Settings& s)
{
    if (message.isNoteOn())
        engine.noteOn(message.getNoteNumber(), s);
    else if (message.isNoteOff())
        engine.noteOff(message.getNoteNumber());
    else if (message.isPitchWheel())
        wheel = message.getPitchWheelValue();
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.allNotesOff();
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
}

void NeuroProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    float* left = voices.getWritePointer(0);
    float* right = voices.getWritePointer(1);

    int done = 0;
    auto renderUpTo = [&](int end) {
        if (end <= done)
            return;
        Transport at = transport;
        at.ppq += (double) done / getSampleRate() * transport.bpm / 60.0;
        engine.render(left + done, right + done, end - done, s, at);
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
    effects.process(effectsFor(), left, right, buffer, 0, count);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));
    scope.write(buffer.getReadPointer(0), count);
    shownNote.store(engine.sounding(), std::memory_order_relaxed);
    shownLfo.store(engine.lfoNow(), std::memory_order_relaxed);
    shownCutoff.store(engine.cutoffNow(), std::memory_order_relaxed);
}

void NeuroProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* NeuroProcessor::createEditor() { return tonwerkui::forHost<NeuroEditor>(*this); }

int NeuroProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void NeuroProcessor::setCurrentProgram(int index)
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

const juce::String NeuroProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void NeuroProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void NeuroProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkneuro

#ifndef TONWERK_NEURO_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkneuro::NeuroProcessor(); }
#endif
