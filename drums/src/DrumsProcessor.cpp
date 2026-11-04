#include "DrumsProcessor.h"
#include "Platform.h"

#include "DrumsEditor.h"
#include "DrumsPresets.h"
#include "GrooveProcessor.h"
#include "dsp/Tempo.h"

namespace tonwerkdrums
{
namespace
{
using Attributes = juce::AudioParameterFloatAttributes;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using tonwerkui::Localised;
using tonwerkui::tr;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const juce::String& id, const Localised& name, float min, float max, float initial,
            const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = tr(unit).isEmpty() ? juce::String() : " " + tr(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Like `number`, but the top of the range leaves the sound untouched and reads "Aus". */
void numberOrOff(Layout& layout, const juce::String& id, const Localised& name, float min, float max, float initial,
                 const char* unit, float centre)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = " " + tr(unit);
    auto toText = [suffix, max](float value, int) {
        return value >= max - 0.5f ? tr("Aus") : juce::String(juce::roundToInt(value)) + suffix;
    };
    auto fromText = [max](const juce::String& text) {
        return text.trim().equalsIgnoreCase(tr("Aus")) ? max : text.getFloatValue();
    };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const juce::String& id, const Localised& name, float initial, float min = 0.0f,
             float max = 1.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const juce::String& id, const Localised& name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, items, initial));
}

void toggle(Layout& layout, const juce::String& id, const Localised& name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { id, 1 }, name, initial,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction(
            [](bool on, int) { return on ? tr("An") : tr("Aus"); })));
}

/** A MIDI note as Logic names it (C1 is 36), its number beside it. */
void note(Layout& layout, const juce::String& id, const Localised& name, int initial)
{
    auto toText = [](int value, int) {
        return tonwerkui::noteNames()[value % 12] + juce::String(octaveOf(value)) + " (" + juce::String(value) + ")";
    };
    auto fromText = [](const juce::String& text) {
        // A number as it is, else a name in the current language such as "D1", "Fis2" or "F#2"; the longer names
        // first, so "Cis1" is not read as C.
        const auto trimmed = text.trim();
        if (trimmed.containsOnly("0123456789"))
            return std::clamp(trimmed.getIntValue(), 0, 127);
        const auto names = tonwerkui::noteNames();
        for (int n = 11; n >= 0; --n)
            if (trimmed.startsWithIgnoreCase(names[n]))
            {
                const auto rest = trimmed.substring(names[n].length()).upToFirstOccurrenceOf(" ", false, false);
                if (rest.isNotEmpty() && rest.containsOnly("-0123456789"))
                    return std::clamp((rest.getIntValue() + 2) * 12 + n, 0, 127);
            }
        return std::clamp(trimmed.getIntValue(), 0, 127);
    };
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, name, 0, 127, initial,
        juce::AudioParameterIntAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** How long each pad may ring, in ms: a closed hat is a tick, a cymbal washes for seconds. */
std::pair<float, float> decayRange(Pad pad)
{
    switch (pad)
    {
        case Pad::kick: return { 50.0f, 4000.0f };
        case Pad::snare: return { 30.0f, 2000.0f };
        case Pad::clap: return { 50.0f, 2000.0f };
        case Pad::tomLow:
        case Pad::tomMid:
        case Pad::tomHigh: return { 50.0f, 3000.0f };
        case Pad::hatClosed:
        case Pad::hatPedal: return { 10.0f, 1000.0f };
        case Pad::hatOpen: return { 50.0f, 3000.0f };
        case Pad::cymbal:
        case Pad::ride: return { 200.0f, 8000.0f };
        case Pad::rim:
        case Pad::clave:
        case Pad::shaker: return { 10.0f, 500.0f };
        case Pad::cowbell:
        case Pad::conga: return { 30.0f, 2000.0f };
    }
    return { 10.0f, 8000.0f };
}

juce::String id(int pad, const char* name) { return juce::String(kPadIds[(std::size_t) pad]) + name; }
} // namespace

Layout DrumsProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    juce::StringArray machines;
    for (const auto* machine : tonwerkgroove::kMachineNames)
        machines.add(utf8(machine));
    for (int p = 0; p < kPads; ++p)
    {
        const auto& pad = d.pad[(std::size_t) p];
        const juce::String name = tr(kPadNames[(std::size_t) p]) + " ";
        const auto [shortest, longest] = decayRange((Pad) p);
        number(layout, id(p, "Tune"), name + tr("Stimmung"), -24.0f, 24.0f, pad.tune, "HT");
        number(layout, id(p, "Decay"), name + tr("Abklingen"), shortest, longest, pad.decay * 1000.0f, "ms",
               std::sqrt(shortest * longest));
        percent(layout, id(p, "Tone"), name + tr(kSoundKnobs[(std::size_t) p].first), pad.tone);
        percent(layout, id(p, "Character"), name + tr(kSoundKnobs[(std::size_t) p].second), pad.character);
        number(layout, id(p, "Level"), name + tr("Pegel"), -36.0f, 6.0f, pad.levelDb, "dB");
        percent(layout, id(p, "Pan"), name + tr("Panorama"), pad.pan, -1.0f, 1.0f);
        percent(layout, id(p, "Delay"), name + tr("Delay"), pad.delaySend);
        percent(layout, id(p, "Reverb"), name + tr("Hall"), pad.reverbSend);
        toggle(layout, id(p, "Mute"), name + tr("Stumm"), pad.mute);
        number(layout, id(p, "Attack"), name + tr("Attack"), 0.0f, 100.0f, pad.attack * 1000.0f, "ms", 10.0f);
        number(layout, id(p, "Hold"), name + tr("Halten"), 0.0f, 2000.0f, pad.hold * 1000.0f, "ms", 200.0f);
        number(layout, id(p, "PitchMod"), name + tr("Pitch-Mod"), -24.0f, 48.0f, pad.pitchMod, "HT");
        number(layout, id(p, "ModDecay"), name + tr("Mod-Zeit"), 5.0f, 2000.0f, pad.modDecay * 1000.0f, "ms", 100.0f);
        choice(layout, id(p, "Filter"), name + tr("Filter"), tr({ "Aus", "Tiefpass", "Hochpass", "Bandpass" }),
               (int) pad.filter);
        number(layout, id(p, "Cutoff"), name + tr("Cutoff"), 20.0f, 20000.0f, pad.cutoff, "Hz", 1000.0f);
        percent(layout, id(p, "Resonance"), name + tr("Resonanz"), pad.resonance);
        percent(layout, id(p, "FilterMod"), name + tr("Filter-Mod"), pad.filterMod, -1.0f, 1.0f);
        percent(layout, id(p, "Drive"), name + tr("Zerrung"), pad.drive);
        numberOrOff(layout, id(p, "Bits"), name + tr("Bitcrush"), 2.0f, 16.0f, pad.bits, "Bit", 0.0f);
        numberOrOff(layout, id(p, "Crush"), name + tr("Downsample"), 500.0f, 48000.0f, pad.crush, "Hz", 6000.0f);
        percent(layout, id(p, "Velocity"), name + tr("Dynamik"), pad.velocity);
        percent(layout, id(p, "Random"), name + tr("Zufall"), pad.random);
        choice(layout, id(p, "Machine"), name + tr("Maschine"), machines, (int) pad.machine);
        note(layout, id(p, "Note"), name + tr("Note"), d.note[(std::size_t) p]);
    }

    percent(layout, "drive", "Drive", d.drive);
    number(layout, "master", "Master", -36.0f, 6.0f, d.masterDb, "dB");

    // Tonwerk's delay and hall, with Tonwerk DX's ids (and the Groovebox's, so its kits carry over); the pads' sends
    // feed them.
    const tonwerk::Effects fxDefaults;
    percent(layout, "fxDelayMix", "Delay Mix", 0.5f);
    toggle(layout, "fxDelaySync", "Delay Sync", true);
    number(layout, "fxDelayTime", "Delay Zeit", 20.0f, 1500.0f, fxDefaults.delay.time * 1000.0f, "ms", 350.0f);
    juce::StringArray noteValues;
    for (const auto& division : tonwerk::kDivisions)
        noteValues.add(division.label);
    choice(layout, "fxDelayDivision", "Delay Teilung", noteValues, fxDefaults.delay.division);
    percent(layout, "fxDelayFeedback", "Delay Feedback", fxDefaults.delay.feedback, 0.0f, 0.9f);
    number(layout, "fxDelayTone", "Delay Ton", 500.0f, 12000.0f, fxDefaults.delay.tone, "Hz", 3000.0f);
    percent(layout, "fxReverbMix", "Hall Mix", 0.5f);
    number(layout, "fxReverbDecay", tr("Hall Länge"), 300.0f, 8000.0f, 1500.0f, "ms", 2000.0f);
    toggle(layout, "fxOn", "Effekte", true);
    return layout;
}

DrumsProcessor::DrumsProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkDrums", createParameterLayout())
{
    auto value = [this](const juce::String& parameterId) {
        auto* v = state.getRawParameterValue(parameterId);
        jassert(v != nullptr);
        return v;
    };
    for (int p = 0; p < kPads; ++p)
        padValues[(std::size_t) p] = { value(id(p, "Tune")),      value(id(p, "Decay")),    value(id(p, "Tone")),
                                       value(id(p, "Character")), value(id(p, "Level")),    value(id(p, "Pan")),
                                       value(id(p, "Delay")),     value(id(p, "Reverb")),   value(id(p, "Mute")),
                                       value(id(p, "Attack")),    value(id(p, "Hold")),     value(id(p, "PitchMod")),
                                       value(id(p, "ModDecay")),  value(id(p, "Filter")),   value(id(p, "Cutoff")),
                                       value(id(p, "Resonance")), value(id(p, "FilterMod")), value(id(p, "Drive")),
                                       value(id(p, "Bits")),      value(id(p, "Crush")),    value(id(p, "Velocity")),
                                       value(id(p, "Random")),    value(id(p, "Machine")),  value(id(p, "Note")) };
    drive = value("drive");
    master = value("master");
    fx = { value("fxDelayMix"),      value("fxDelaySync"), value("fxDelayTime"), value("fxDelayDivision"),
           value("fxDelayFeedback"), value("fxDelayTone"), value("fxReverbMix"), value("fxReverbDecay"), value("fxOn") };
    state.state.setProperty("presetName", "Init", nullptr);
    startTimerHz(10);
}

DrumsProcessor::~DrumsProcessor() { stopTimer(); }

Settings DrumsProcessor::readSettings() const
{
    auto get = [](const std::atomic<float>* v) { return v->load(std::memory_order_relaxed); };
    Settings s;
    for (std::size_t p = 0; p < (std::size_t) kPads; ++p)
    {
        const auto& v = padValues[p];
        auto& pad = s.pad[p];
        pad.tune = get(v.tune);
        pad.decay = get(v.decay) / 1000.0f;
        pad.tone = get(v.tone);
        pad.character = get(v.character);
        pad.levelDb = get(v.level);
        pad.pan = get(v.pan);
        pad.delaySend = get(v.delay);
        pad.reverbSend = get(v.reverb);
        pad.mute = get(v.mute) >= 0.5f;
        pad.attack = get(v.attack) / 1000.0f;
        pad.hold = get(v.hold) / 1000.0f;
        pad.pitchMod = get(v.pitchMod);
        pad.modDecay = get(v.modDecay) / 1000.0f;
        pad.filter = (TrackFilter) std::clamp(juce::roundToInt(get(v.filter)), 0, 3);
        pad.cutoff = get(v.cutoff);
        pad.resonance = get(v.resonance);
        pad.filterMod = get(v.filterMod);
        pad.drive = get(v.drive);
        pad.bits = get(v.bits);
        pad.crush = get(v.crush);
        pad.velocity = get(v.velocity);
        pad.random = get(v.random);
        pad.machine = (Machine) std::clamp(juce::roundToInt(get(v.machine)), 0, kMachines - 1);
        s.note[p] = std::clamp(juce::roundToInt(get(v.note)), 0, 127);
    }
    s.drive = get(drive);
    s.masterDb = get(master);
    return s;
}

tonwerk::Effects DrumsProcessor::effectsFor() const
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

void DrumsProcessor::audition(int pad, float velocity)
{
    auditionVelocity.store(juce::roundToInt(std::clamp(velocity, 0.0f, 1.0f) * 127.0f));
    auditions.fetch_or(1u << pad);
}

bool DrumsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.outputBuses.size() == 1
           && (output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono());
}

void DrumsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    work.setSize(4, samplesPerBlock);
    wetGain.reset(sampleRate, kBypassSeconds);
    wetGain.setCurrentAndTargetValue(fx.on->load() >= 0.5f ? 1.0f : 0.0f);
    effectsCleared = false;
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void DrumsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midiLearn.process(midi);
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    const Settings s = readSettings();

    // The tempo only sets a synced delay's time.
    double tempo = 120.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                tempo = *hostBpm;
    bpm.store(tempo);

    if (work.getNumSamples() < count)
        work.setSize(4, count, false, false, true);
    work.clear();
    float* left = work.getWritePointer(0);
    float* right = work.getWritePointer(1);
    float* delaySend = work.getWritePointer(2);
    float* reverbSend = work.getWritePointer(3);

    if (const auto clicked = auditions.exchange(0); clicked != 0)
        for (int p = 0; p < kPads; ++p)
            if ((clicked & (1u << p)) != 0)
                engine.trigger(p, (float) auditionVelocity.load() / 127.0f, s);

    int done = 0;
    auto renderUpTo = [&](int end) {
        if (end <= done)
            return;
        engine.render(left + done, right + done, delaySend + done, reverbSend + done, end - done, s);
        done = end;
    };
    // Every note on plays its pad from its own sample; note offs end nothing, a drum rings out.
    for (const auto metadata : midi)
    {
        const int offset = std::clamp(metadata.samplePosition, 0, std::max(0, count - 1));
        const auto message = metadata.getMessage();
        renderUpTo(offset);
        if (message.isNoteOn())
            engine.trigger(padForNote(message.getNoteNumber(), s.note), message.getFloatVelocity(), s);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.reset();
    }
    renderUpTo(count);
    midi.clear();

    for (int p = 0; p < kPads; ++p)
    {
        const float peak = engine.takePeak(p);
        auto& shown = padPeaks[(std::size_t) p];
        if (peak > shown.load(std::memory_order_relaxed))
            shown.store(peak, std::memory_order_relaxed);
    }

    buffer.clear();
    if (buffer.getNumChannels() == 1)
    {
        for (int i = 0; i < count; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
        right = left;
    }
    // Off, what the effects add fades out over kBypassSeconds and the dry pads pass untouched; once silent the lines are
    // cleared, so switching back on brings no old echoes or hall, and the effects cost nothing until then.
    const bool fxOn = fx.on->load() >= 0.5f;
    wetGain.setTargetValue(fxOn ? 1.0f : 0.0f);
    if (! fxOn && ! wetGain.isSmoothing())
    {
        if (! effectsCleared)
            effects.reset();
        effectsCleared = true;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom(channel, 0, channel == 0 ? left : right, count);
    }
    else
    {
        effectsCleared = false;
        auto e = effectsFor();
        if (! fxOn)
            e.delay.mix = e.reverb.mix = 0.0f;
        effects.process(e, left, right, delaySend, reverbSend, buffer, 0, count);
        if (wetGain.isSmoothing() || ! fxOn) // the fade may end inside this block
        {
            float* out[2] = { buffer.getWritePointer(0),
                              buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr };
            for (int i = 0; i < count; ++i)
            {
                const float g = wetGain.getNextValue();
                out[0][i] = left[i] + g * (out[0][i] - left[i]);
                if (out[1] != nullptr)
                    out[1][i] = right[i] + g * (out[1][i] - right[i]);
            }
        }
    }
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        tonwerkgroove::Engine::saturate(buffer.getWritePointer(channel), count, s.drive);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));
}

void DrumsProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* DrumsProcessor::createEditor() { return tonwerkui::forHost<DrumsEditor>(*this); }

int DrumsProcessor::getNumPrograms() { return (int) factoryKits().size(); }

void DrumsProcessor::setCurrentProgram(int index)
{
    const auto& kits = factoryKits();
    if (! juce::isPositiveAndBelow(index, (int) kits.size()))
        return;
    currentProgram = index;
    const auto& kit = kits[(std::size_t) index];
    // Everything back to its default first, so a kit only names what it changes. The notes are the player's setup, not
    // the kit's, and stay; so does the effects' switch: a kit chosen while listening dry stays dry.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged && ranged->paramID != "fxOn" && ! ranged->paramID.endsWith("Note"))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    for (const auto& [parameterId, value] : kit.values)
        if (auto* parameter = state.getParameter(parameterId))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse; // a kit names a parameter that does not exist
    state.state.setProperty("presetName", kit.name, nullptr);
}

const juce::String DrumsProcessor::getProgramName(int index)
{
    const auto& kits = factoryKits();
    return juce::isPositiveAndBelow(index, (int) kits.size()) ? kits[(std::size_t) index].name : juce::String();
}

void DrumsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DrumsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace tonwerkdrums

#ifndef TONWERK_DRUMS_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkdrums::DrumsProcessor(); }
#endif
