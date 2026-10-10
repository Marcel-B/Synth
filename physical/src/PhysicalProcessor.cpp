#include "PhysicalProcessor.h"

#include "PhysicalEditor.h"
#include "PhysicalPresets.h"
#include "dsp/Tempo.h"

namespace tonwerkphys
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
    return utf8Array({ "Aus", "Tonhöhe", "Druck", "Härte", "Position", "Abklingen", "Dämpfung", "Cutoff", "Resonanz",
                       "Lautstärke" });
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

Layout PhysicalProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    choice(layout, "exciter", "Erreger", utf8Array({ "Zupfen", "Schlagen", "Streichen", "Blasen" }), (int) d.exciter);
    percent(layout, "hardness", utf8("Härte"), d.hardness);
    percent(layout, "pressure", "Druck", d.pressure);
    percent(layout, "noise", "Rauschen", d.noise);
    percent(layout, "position", "Position", d.position, 0.02f, 0.5f);
    envelope(layout, "env1", utf8("Hüllkurve 1"), d.env1);

    choice(layout, "resonator", "Resonator",
           utf8Array({ "Saite", "Rohr geschlossen", "Rohr offen", "Stab", "Glocke", "Fell", "Schale" }),
           (int) d.resonator);
    number(layout, "decay", "Abklingen", 10.0f, 20000.0f, d.decay * 1000.0f, "ms", 2000.0f);
    percent(layout, "damping", utf8("Dämpfung"), d.damping);
    percent(layout, "inharmonicity", "Inharmonie", d.inharmonicity);
    number(layout, "release", "Loslassen", 5.0f, 10000.0f, d.release * 1000.0f, "ms", 500.0f);
    choice(layout, "body", "Korpus", utf8Array({ "Aus", "Gitarre", "Geige", "Kiste", "Resonanzboden" }), (int) d.body);
    percent(layout, "bodyMix", "Korpus Mix", d.bodyMix);

    integer(layout, "octave", "Oktave", -3, 3, d.octave, "Okt");
    integer(layout, "semi", "Halbton", -12, 12, d.semitones, "HT");
    number(layout, "fine", "Fein", -100.0f, 100.0f, d.fine, "ct");

    toggle(layout, "filterOn", "Filter an", d.filterOn);
    choice(layout, "filterMode", "Filtertyp", utf8Array({ "Tiefpass 12", "Tiefpass 24", "Hochpass 12", "Bandpass" }),
           (int) d.filterMode);
    number(layout, "cutoff", "Cutoff", 20.0f, 20000.0f, d.cutoff, "Hz", 1000.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    percent(layout, "keytrack", "Keytracking", d.keytrack);

    envelope(layout, "env2", utf8("Hüllkurve 2"), d.env2);
    routing(layout, "env2", utf8("Hüllkurve 2"), (int) d.env2Route.target, d.env2Route.amount);

    for (int i = 1; i <= kLfos; ++i)
    {
        const auto& lfo = d.lfo[(std::size_t) i - 1];
        const juce::String prefix = "lfo" + juce::String(i);
        const juce::String name = "LFO " + juce::String(i);
        choice(layout, prefix + "Shape", name + " Form",
               utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }), (int) lfo.shape);
        toggle(layout, prefix + "Sync", name + " Tempo-Sync", lfo.sync);
        {
            // Below 10 Hz a tenth of a hertz matters, so the rate is the one value shown with a decimal.
            juce::NormalisableRange<float> range(0.05f, 20.0f);
            range.setSkewForCentre(1.0f);
            auto toText = [](float value, int) {
                return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
            };
            auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { prefix + "Rate", 1 }, name + " Rate", range, lfo.rate,
                Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
        }
        juce::StringArray divisions;
        for (const auto& division : tonwerkwave::kDivisions)
            divisions.add(division.name);
        choice(layout, prefix + "Division", name + " Raster", divisions, lfo.division);
        routing(layout, prefix, name, (int) lfo.route.target, lfo.route.amount);
    }
    routing(layout, "wheel", "Modrad", (int) d.wheel.target, d.wheel.amount);
    routing(layout, "velocity", "Anschlag", (int) d.velocity.target, d.velocity.amount);
    percent(layout, "dynamics", "Dynamik", d.dynamics);

    choice(layout, "voiceMode", "Stimmen-Modus", utf8Array({ "Poly", "Mono" }), (int) d.voiceMode);
    milliseconds(layout, "glide", "Glide", 2000.0f, d.glide * 1000.0f);
    integer(layout, "bendRange", "Pitchbend-Bereich", 0, 24, d.bendRange, "HT");
    percent(layout, "width", "Breite", d.width);
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

PhysicalProcessor::PhysicalProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkPhysical", createParameterLayout())
{
    values = engineValues(*this, state);
    fx = { state.getRawParameterValue("fxDelayMix"),      state.getRawParameterValue("fxDelaySync"),
           state.getRawParameterValue("fxDelayTime"),     state.getRawParameterValue("fxDelayDivision"),
           state.getRawParameterValue("fxDelayFeedback"), state.getRawParameterValue("fxDelayTone"),
           state.getRawParameterValue("fxReverbMix"),     state.getRawParameterValue("fxReverbDecay") };
    state.state.setProperty("presetName", "Init", nullptr);
    startTimerHz(10);
}

PhysicalProcessor::~PhysicalProcessor() { stopTimer(); }

Settings PhysicalProcessor::readSettings() const
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
    s.exciter = (Exciter) whole();
    s.hardness = next();
    s.pressure = next();
    s.noise = next();
    s.position = next();
    envelope(s.env1);

    s.resonator = (Resonator) whole();
    s.decay = next() / 1000.0f;
    s.damping = next();
    s.inharmonicity = next();
    s.release = next() / 1000.0f;
    s.body = (Body) whole();
    s.bodyMix = next();

    s.octave = whole();
    s.semitones = whole();
    s.fine = next();

    s.filterOn = flag();
    s.filterMode = (FilterMode) whole();
    s.cutoff = next();
    s.resonance = next();
    s.keytrack = next();

    envelope(s.env2);
    route(s.env2Route);
    for (auto& lfo : s.lfo)
    {
        lfo.shape = (LfoShape) whole();
        lfo.sync = flag();
        lfo.rate = next();
        lfo.division = whole();
        route(lfo.route);
    }
    route(s.wheel);
    route(s.velocity);
    s.dynamics = next();

    s.voiceMode = (VoiceMode) whole();
    s.glide = next() / 1000.0f;
    s.bendRange = whole();
    s.width = next();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

tonwerk::Effects PhysicalProcessor::effectsFor() const
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

bool PhysicalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void PhysicalProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voices.setSize(2, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void PhysicalProcessor::handle(const juce::MidiMessage& message, const Settings& s)
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

void PhysicalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

void PhysicalProcessor::publishPicture()
{
    const auto* voice = engine.newestVoice();
    if (voice == nullptr)
    {
        shownResonator.store(-1, std::memory_order_relaxed);
        return;
    }
    voice->draw(picture);
    for (int i = 0; i < picture.points; ++i)
        shownShape[(std::size_t) i].store(picture.shape[(std::size_t) i], std::memory_order_relaxed);
    for (int k = 0; k < picture.modeCount; ++k)
    {
        shownRatios[(std::size_t) k].store(picture.modeRatio[(std::size_t) k], std::memory_order_relaxed);
        shownLevels[(std::size_t) k].store(picture.modeLevel[(std::size_t) k], std::memory_order_relaxed);
    }
    shownPoints.store(picture.points, std::memory_order_relaxed);
    shownModes.store(picture.modeCount, std::memory_order_relaxed);
    shownFrequency.store(picture.frequency, std::memory_order_relaxed);
    shownPosition.store(picture.position, std::memory_order_relaxed);
    shownResonator.store((int) picture.resonator, std::memory_order_relaxed);
}

void PhysicalProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* PhysicalProcessor::createEditor() { return new PhysicalEditor(*this); }

int PhysicalProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void PhysicalProcessor::setCurrentProgram(int index)
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

const juce::String PhysicalProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void PhysicalProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PhysicalProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkphys

#ifndef TONWERK_PHYSICAL_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkphys::PhysicalProcessor(); }
#endif
