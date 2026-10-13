#include "PdProcessor.h"

#include "PdEditor.h"
#include "PdPresets.h"
#include "dsp/Tempo.h"

namespace tonwerkpd
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

/** Times in whole milliseconds, skewed so the short ones get most of the turn. */
void milliseconds(Layout& layout, const juce::String& id, const juce::String& name, float max, float initial)
{
    number(layout, id, name, 0.0f, max, initial, "ms", max / 20.0f);
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

juce::StringArray targets()
{
    return utf8Array({ "Aus", "Tonhöhe", "DCW", "DCW 1", "DCW 2", "Tonhöhe 2", "Lautstärke", "Balance" });
}

juce::StringArray waveNames()
{
    return utf8Array({ "Säge", "Rechteck", "Puls", "Doppelsinus", "Säge-Puls", "Resonanz Säge", "Resonanz Dreieck",
                       "Resonanz Trapez" });
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


void routing(Layout& layout, const juce::String& prefix, const juce::String& name, int target, float amount)
{
    choice(layout, prefix + "Target", name + " " + utf8("Ziel"), targets(), target);
    percent(layout, prefix + "Amount", name + " " + utf8("Menge"), amount, -1.0f, 1.0f);
}

void envelope(Layout& layout, const juce::String& prefix, const juce::String& name, const EnvSettings& e)
{
    milliseconds(layout, prefix + "Attack", name + " Attack", 10000.0f, e.attack * 1000.0f);
    milliseconds(layout, prefix + "Decay", name + " Decay", 10000.0f, e.decay * 1000.0f);
    percent(layout, prefix + "Sustain", name + " Sustain", e.sustain);
    milliseconds(layout, prefix + "Release", name + " Release", 10000.0f, e.release * 1000.0f);
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

Layout PdProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "lines", "Linien", utf8Array({ "1", "2", "1 + 1'", "1 + 2'" }), (int) d.lines);
    choice(layout, "modulation", "Modulation", utf8Array({ "Aus", "Ring", "Rauschen" }), (int) d.modulation);
    for (int n = 1; n <= kLines; ++n)
    {
        const auto& line = d.line[(std::size_t) n - 1];
        const juce::String prefix = "line" + juce::String(n);
        const juce::String name = "Linie " + juce::String(n);
        choice(layout, prefix + "Wave", name + " Welle", waveNames(), (int) line.first);
        auto second = waveNames();
        second.insert(0, "Aus");
        choice(layout, prefix + "Wave2", name + " Welle 2", second, line.alternate ? (int) line.second + 1 : 0);
        percent(layout, prefix + "Depth", name + " DCW", line.depth);
        percent(layout, prefix + "KeyFollow", name + " DCW Keytracking", line.keyFollow);
        percent(layout, prefix + "Level", name + " Pegel", line.level);
        integer(layout, prefix + "Octave", name + " Oktave", -3, 3, line.octave, "Okt");
        envelope(layout, prefix + "Dcw", name + " DCW", line.dcw);
        envelope(layout, prefix + "Amp", name + " DCA", line.amp);
    }
    // The CZ's detune: line 2 against line 1.
    integer(layout, "detuneSemi", "Verstimmung Halbton", -24, 24, d.line[1].semitones, "HT");
    number(layout, "detuneFine", "Verstimmung Fein", -100.0f, 100.0f, d.line[1].fine, "ct");
    number(layout, "pitchEnvAmount", utf8("Tonhöhen-Hüllkurve"), -24.0f, 24.0f, d.pitchEnvAmount, "HT");
    milliseconds(layout, "pitchEnvTime", utf8("Tonhöhen-Hüllkurve Zeit"), 5000.0f, d.pitchEnvTime * 1000.0f);

    for (int i = 1; i <= kLfos; ++i)
    {
        const auto& lfo = d.lfo[(std::size_t) i - 1];
        const juce::String prefix = "lfo" + juce::String(i);
        const juce::String name = "LFO " + juce::String(i);
        choice(layout, prefix + "Shape", name + " Form",
               utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }), (int) lfo.shape);
        toggle(layout, prefix + "Sync", name + " Tempo-Sync", lfo.sync);
        hertz(layout, prefix + "Rate", name + " Rate", 0.05f, 20.0f, lfo.rate);
        juce::StringArray divisions;
        for (const auto& division : tonwerkwave::kDivisions)
            divisions.add(division.name);
        choice(layout, prefix + "Division", name + " Raster", divisions, lfo.division);
        routing(layout, prefix, name, (int) lfo.route.target, lfo.route.amount);
    }
    milliseconds(layout, "lfo1Fade", utf8("LFO 1 Verzögerung"), 5000.0f, d.lfo[0].fade * 1000.0f);
    routing(layout, "wheel", "Modrad", (int) d.wheel.target, d.wheel.amount);
    routing(layout, "velocity", "Anschlag", (int) d.velocity.target, d.velocity.amount);
    percent(layout, "dynamics", "Dynamik", d.dynamics);

    choice(layout, "voiceMode", "Stimmen-Modus", utf8Array({ "Poly", "Mono", "Legato" }), (int) d.voiceMode);
    milliseconds(layout, "glide", "Glide", 2000.0f, d.glide * 1000.0f);
    integer(layout, "bendRange", "Pitchbend-Bereich", 0, 24, d.bendRange, "HT");
    percent(layout, "width", "Breite", d.width);
    percent(layout, "chorusMix", "Chorus Mix", d.chorusMix);
    hertz(layout, "chorusRate", "Chorus Rate", 0.05f, 5.0f, d.chorusRate);
    percent(layout, "chorusDepth", "Chorus Tiefe", d.chorusDepth);
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
    percent(layout, "fxReverbMix", "Hall Mix", 0.15f);
    number(layout, "fxReverbDecay", utf8("Hall Länge"), 300.0f, 8000.0f, 2500.0f, "ms", 2000.0f);
    return layout;
}

PdProcessor::PdProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkPD", createParameterLayout())
{
    values = engineValues(*this, state);
    fx = { state.getRawParameterValue("fxDelayMix"),      state.getRawParameterValue("fxDelaySync"),
           state.getRawParameterValue("fxDelayTime"),     state.getRawParameterValue("fxDelayDivision"),
           state.getRawParameterValue("fxDelayFeedback"), state.getRawParameterValue("fxDelayTone"),
           state.getRawParameterValue("fxReverbMix"),     state.getRawParameterValue("fxReverbDecay") };
    state.state.setProperty("presetName", "Init", nullptr);
    startTimerHz(10);
}

PdProcessor::~PdProcessor() { stopTimer(); }

Settings PdProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them, the fx left out.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto route = [&](Route& r) {
        r.target = (Target) whole();
        r.amount = next();
    };
    auto envelope = [&](EnvSettings& e) {
        e.attack = next() / 1000.0f;
        e.decay = next() / 1000.0f;
        e.sustain = next();
        e.release = next() / 1000.0f;
    };

    Settings s;
    s.lines = (LineSelect) whole();
    s.modulation = (Modulation) whole();
    for (auto& line : s.line)
    {
        line.first = (Wave) whole();
        const int second = whole();
        line.alternate = second > 0;
        line.second = (Wave) std::max(0, second - 1);
        line.depth = next();
        line.keyFollow = next();
        line.level = next();
        line.octave = whole();
        envelope(line.dcw);
        envelope(line.amp);
    }
    s.line[1].semitones = whole();
    s.line[1].fine = next();
    s.pitchEnvAmount = next();
    s.pitchEnvTime = next() / 1000.0f;

    for (auto& lfo : s.lfo)
    {
        lfo.shape = (LfoShape) whole();
        lfo.sync = flag();
        lfo.rate = next();
        lfo.division = whole();
        route(lfo.route);
    }
    s.lfo[0].fade = next() / 1000.0f;
    route(s.wheel);
    route(s.velocity);
    s.dynamics = next();

    s.voiceMode = (VoiceMode) whole();
    s.glide = next() / 1000.0f;
    s.bendRange = whole();
    s.width = next();
    s.chorusMix = next();
    s.chorusRate = next();
    s.chorusDepth = next();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

tonwerk::Effects PdProcessor::effectsFor() const
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

bool PdProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void PdProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voices.setSize(2, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void PdProcessor::handle(const juce::MidiMessage& message, const Settings& s)
{
    if (message.isNoteOn())
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity(), s);
    else if (message.isNoteOff())
        engine.noteOff(message.getNoteNumber(), s);
    else if (message.isPitchWheel())
        wheel = message.getPitchWheelValue();
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.allNotesOff();
    else if (message.isController())
    {
        if (message.getControllerNumber() == 1)
            engine.setModWheel((float) message.getControllerValue() / 127.0f);
        else if (message.getControllerNumber() == 64)
            engine.setSustain(message.getControllerValue() >= 64);
    }
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
}

void PdProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
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
    if (buffer.getNumChannels() == 1)
    {
        // A mono bus: both sides folded into one before the effects.
        for (int i = 0; i < count; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
        right = left;
    }
    effects.process(effectsFor(), left, right, buffer, 0, count);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));
    publishPicture();
}

void PdProcessor::publishPicture()
{
    const auto* voice = engine.newestVoice();
    if (voice == nullptr)
    {
        shownActive.store(false, std::memory_order_relaxed);
        return;
    }
    voice->draw(picture);
    for (std::size_t k = 0; k < kLines; ++k)
    {
        shownOn[k].store(picture.on[k], std::memory_order_relaxed);
        shownFirst[k].store((int) picture.first[k], std::memory_order_relaxed);
        shownAlternate[k].store(picture.alternate[k], std::memory_order_relaxed);
        shownSecond[k].store((int) picture.second[k], std::memory_order_relaxed);
        shownBend[k].store(picture.bend[k], std::memory_order_relaxed);
    }
    shownFrequency.store(picture.frequency, std::memory_order_relaxed);
    shownActive.store(true, std::memory_order_relaxed);
}

void PdProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* PdProcessor::createEditor() { return new PdEditor(*this); }

int PdProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void PdProcessor::setCurrentProgram(int index)
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

const juce::String PdProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void PdProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkpd

#ifndef TONWERK_PD_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkpd::PdProcessor(); }
#endif
