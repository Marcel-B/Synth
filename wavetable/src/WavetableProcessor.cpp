#include "WavetableProcessor.h"

#include "WavetableEditor.h"
#include "WavetablePresets.h"

namespace tonwerkwave
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
    const juce::String suffix = utf8(unit).isEmpty() ? juce::String() : " " + utf8(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), range, initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Shares and amounts in whole percent, -100 to 100 when `bipolar`. */
void percent(Layout& layout, const juce::String& id, const char* name, float initial, bool bipolar = false)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, utf8(name), juce::NormalisableRange<float>(bipolar ? -1.0f : 0.0f, 1.0f), initial,
        Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

/** Times in whole milliseconds, skewed so the short ones that shape a bass get most of the turn. */
void milliseconds(Layout& layout, const juce::String& id, const char* name, float max, float initial)
{
    number(layout, id, name, 0.0f, max, initial, "ms", max / 20.0f);
}

void integer(Layout& layout, const juce::String& id, const char* name, int min, int max, int initial,
             const char* unit = "")
{
    const juce::String suffix = utf8(unit).isEmpty() ? juce::String() : " " + utf8(unit);
    auto toText = [suffix](int value, int) { return juce::String(value) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getIntValue(); };
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, utf8(name), min, max, initial,
        juce::AudioParameterIntAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const juce::String& id, const char* name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, utf8(name), items, initial));
}

void toggle(Layout& layout, const juce::String& id, const char* name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, utf8(name), initial));
}

juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (const auto& d : kDivisions)
        names.add(d.name);
    return names;
}

void oscillator(Layout& layout, const juce::String& p, const char* label, bool on)
{
    const juce::String name(label);
    auto n = [&name](const char* what) { return (name + " " + utf8(what)).toStdString(); };
    // juce::String keeps the text; the helpers take UTF-8.
    const auto onName = n("an"), table = n("Wavetable"), position = n("Position"), warpMode = n("Warp-Modus"),
               warp = n("Warp"), octave = n("Oktave"), semi = n("Halbton"), fine = n("Fein"), unison = n("Unison"),
               detune = n("Detune"), blend = n("Blend"), width = n("Breite"), level = n("Pegel");
    toggle(layout, p + "On", onName.c_str(), on);
    juce::StringArray tables;
    for (const auto& t : WavetableBank::names())
        tables.add(utf8(t.c_str()));
    choice(layout, p + "Table", table.c_str(), tables, 0);
    percent(layout, p + "Position", position.c_str(), 0.0f);
    choice(layout, p + "WarpMode", warpMode.c_str(),
           utf8Array({ "Aus", "Sync", "Bend", "PWM", p == "a" ? "FM von B" : "FM von A" }), 0);
    percent(layout, p + "Warp", warp.c_str(), 0.0f);
    integer(layout, p + "Octave", octave.c_str(), -3, 3, 0, "Okt");
    integer(layout, p + "Semi", semi.c_str(), -12, 12, 0, "HT");
    number(layout, p + "Fine", fine.c_str(), -100.0f, 100.0f, 0.0f, "ct");
    integer(layout, p + "Unison", unison.c_str(), 1, kMaxUnison, 1);
    number(layout, p + "Detune", detune.c_str(), 0.0f, 100.0f, 20.0f, "ct", 25.0f);
    percent(layout, p + "Blend", blend.c_str(), 0.75f);
    percent(layout, p + "Width", width.c_str(), 0.8f);
    percent(layout, p + "Level", level.c_str(), 0.75f);
}

juce::String id(const char* prefix, int index, const char* suffix)
{
    return juce::String(prefix) + juce::String(index) + suffix;
}
} // namespace

Layout WavetableProcessor::createParameterLayout()
{
    Layout layout;
    oscillator(layout, "a", "Osz A", true);
    oscillator(layout, "b", "Osz B", false);

    toggle(layout, "subOn", "Sub an", false);
    choice(layout, "subShape", "Sub Form", utf8Array({ "Sinus", "Dreieck", "Säge", "Rechteck" }), 0);
    integer(layout, "subOctave", "Sub Oktave", -3, 0, -1, "Okt");
    percent(layout, "subLevel", "Sub Pegel", 0.6f);
    toggle(layout, "subDirect", "Sub am Filter vorbei", true);
    percent(layout, "noise", "Rauschen", 0.0f);

    toggle(layout, "filterOn", "Filter an", true);
    choice(layout, "filterMode", "Filtertyp",
           utf8Array({ "Tiefpass 12", "Tiefpass 24", "Hochpass 12", "Bandpass", "Kerbfilter", "Kamm" }), 1);
    number(layout, "cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f, "Hz", 1000.0f);
    percent(layout, "resonance", "Resonanz", 0.1f);
    percent(layout, "filterDrive", "Filter-Drive", 0.0f);
    percent(layout, "keytrack", "Keytracking", 0.0f);

    const float sustains[] = { 1.0f, 0.0f, 0.0f };
    const float decays[] = { 400.0f, 300.0f, 300.0f };
    for (int i = 1; i <= kEnvelopes; ++i)
    {
        const auto name = [i](const char* what) { return ("Hüllkurve " + std::to_string(i) + " " + what); };
        milliseconds(layout, id("env", i, "Attack"), name("Attack").c_str(), 10000.0f, 2.0f);
        milliseconds(layout, id("env", i, "Decay"), name("Decay").c_str(), 10000.0f, decays[i - 1]);
        percent(layout, id("env", i, "Sustain"), name("Sustain").c_str(), sustains[i - 1]);
        milliseconds(layout, id("env", i, "Release"), name("Release").c_str(), 10000.0f, 200.0f);
    }

    for (int i = 1; i <= kLfos; ++i)
    {
        const auto name = [i](const char* what) { return ("LFO " + std::to_string(i) + " " + what); };
        choice(layout, id("lfo", i, "Shape"), name("Form").c_str(),
               utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }), 0);
        toggle(layout, id("lfo", i, "Sync"), name("Tempo-Sync").c_str(), true);
        // Below 10 Hz a tenth of a hertz matters, so the rate is the one value shown with a decimal.
        {
            juce::NormalisableRange<float> range(0.1f, 30.0f);
            range.setSkewForCentre(3.0f);
            auto toText = [](float value, int) {
                return (value < 9.95f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " Hz";
            };
            auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { id("lfo", i, "Rate"), 1 }, utf8(name("Rate").c_str()), range, 2.0f,
                Attributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
        }
        choice(layout, id("lfo", i, "Division"), name("Raster").c_str(), divisionNames(), kEighth);
        toggle(layout, id("lfo", i, "Retrigger"), name("Neustart").c_str(), true);
    }

    const auto sources = utf8Array({ "Aus", "LFO 1", "LFO 2", "Hüllkurve 1", "Hüllkurve 2", "Hüllkurve 3", "Anschlag",
                                     "Modrad", "Aftertouch", "Tonhöhe", "Makro 1", "Makro 2", "Makro 3", "Makro 4" });
    const auto targets = utf8Array({ "Aus", "A Position", "A Warp", "A Tonhöhe", "A Pegel", "A Detune", "B Position",
                                     "B Warp", "B Tonhöhe", "B Pegel", "B Detune", "Sub Pegel", "Rauschen", "Cutoff",
                                     "Resonanz", "Filter-Drive", "Lautstärke" });
    for (int i = 1; i <= kModSlots; ++i)
    {
        const auto name = [i](const char* what) { return ("Mod " + std::to_string(i) + " " + what); };
        choice(layout, id("mod", i, "Source"), name("Quelle").c_str(), sources, 0);
        choice(layout, id("mod", i, "Target"), name("Ziel").c_str(), targets, 0);
        percent(layout, id("mod", i, "Amount"), name("Menge").c_str(), 0.0f, true);
        toggle(layout, id("mod", i, "Bipolar"), name("Bipolar").c_str(), false);
    }
    for (int i = 1; i <= kMacros; ++i)
        percent(layout, id("macro", i, ""), ("Makro " + std::to_string(i)).c_str(), 0.0f);

    choice(layout, "voiceMode", "Stimmen-Modus", utf8Array({ "Poly", "Mono", "Legato" }), 0);
    milliseconds(layout, "glide", "Glide", 2000.0f, 0.0f);
    integer(layout, "bendRange", "Pitchbend-Bereich", 0, 24, 2, "HT");

    toggle(layout, "distOn", "Verzerrung an", false);
    choice(layout, "distMode", "Verzerrung Modus", utf8Array({ "Weich", "Hart", "Falten", "Röhre" }), 0);
    percent(layout, "distDrive", "Verzerrung Drive", 0.5f);
    percent(layout, "distMix", "Verzerrung Mix", 1.0f);
    number(layout, "master", "Master", -36.0f, 6.0f, -6.0f, "dB");
    return layout;
}

WavetableProcessor::WavetableProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkWavetable", createParameterLayout())
{
    // Builds the tables now, on the message thread, rather than on the audio thread's first note.
    WavetableBank::instance();
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            values.push_back(state.getRawParameterValue(withId->paramID));
    state.state.setProperty("presetName", "Init", nullptr);
}

Settings WavetableProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };

    Settings s;
    for (auto* osc : { &s.a, &s.b })
    {
        osc->on = flag();
        osc->table = whole();
        osc->position = next();
        osc->warp = (Warp) whole();
        osc->warpAmount = next();
        osc->octave = whole();
        osc->semitones = whole();
        osc->fine = next();
        osc->unison = whole();
        osc->detune = next();
        osc->blend = next();
        osc->width = next();
        osc->level = next();
    }
    s.sub.on = flag();
    s.sub.shape = (tonwerk::Wave) whole();
    s.sub.octave = whole();
    s.sub.level = next();
    s.sub.direct = flag();
    s.noise = next();

    s.filter.on = flag();
    s.filter.mode = (FilterMode) whole();
    s.filter.cutoff = next();
    s.filter.resonance = next();
    s.filter.drive = next();
    s.filter.keytrack = next();

    for (auto& e : s.env)
    {
        e.attack = next() / 1000.0f;
        e.decay = next() / 1000.0f;
        e.sustain = next();
        e.release = next() / 1000.0f;
    }
    for (auto& l : s.lfo)
    {
        l.shape = (LfoShape) whole();
        l.sync = flag();
        l.rate = next();
        l.division = whole();
        l.retrigger = flag();
    }
    for (auto& m : s.mods)
    {
        m.source = (ModSource) whole();
        m.target = (ModTarget) whole();
        m.amount = next();
        m.bipolar = flag();
    }
    for (auto& macro : s.macros)
        macro = next();

    s.voiceMode = (VoiceMode) whole();
    s.glide = next() / 1000.0f;
    s.bendRange = whole();

    s.distortion.on = flag();
    s.distortion.mode = (DistortionMode) whole();
    s.distortion.drive = next();
    s.distortion.mix = next();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

bool WavetableProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void WavetableProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    maxBlock = std::max(1, samplesPerBlock);
    oversampling.initProcessing((std::size_t) maxBlock);
    monoScratch.setSize(1, samplesPerBlock);
    dry.setSize(2, samplesPerBlock);
    oversampling.reset();
    dcIn = {};
    dcOut = {};
    dcPole = (float) (1.0 - 2.0 * kPi * 10.0 / sampleRate);
    keyboardState.reset();
}

void WavetableProcessor::handle(const juce::MidiMessage& message, const Settings& s)
{
    if (message.isNoteOn())
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity(), s);
    else if (message.isNoteOff())
        engine.noteOff(message.getNoteNumber(), s);
    else if (message.isPitchWheel())
        wheel = message.getPitchWheelValue();
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.allNotesOff();
    else if (message.isChannelPressure())
        engine.setAftertouch((float) message.getChannelPressureValue() / 127.0f);
    else if (message.isController())
    {
        if (message.getControllerNumber() == 1)
            engine.setModWheel((float) message.getControllerValue() / 127.0f);
        else if (message.getControllerNumber() == 64)
            engine.setSustain(message.getControllerValue() >= 64);
    }
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
}

void WavetableProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, count, true);
    buffer.clear();

    Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            transport.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0)
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.hasPosition = true;
            }
        }

    const Settings s = readSettings();
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
    // Two channels to render into even on a mono bus; the second is folded in below.
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (right == nullptr)
    {
        monoScratch.setSize(1, count, false, false, true);
        monoScratch.clear();
        right = monoScratch.getWritePointer(0);
    }

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

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < count; ++i)
            left[i] = 0.5f * (left[i] + right[i]);

    if (s.distortion.on)
        applyDistortion(buffer, s.distortion);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));

    if (const auto* voice = engine.newestVoice())
    {
        playedPosition[0] = voice->shownPosition(0);
        playedPosition[1] = voice->shownPosition(1);
    }
    else
    {
        playedPosition[0] = -1.0f;
        playedPosition[1] = -1.0f;
    }
}

void WavetableProcessor::applyDistortion(juce::AudioBuffer<float>& buffer, const DistortionSettings& d)
{
    // Up to 30 dB into the curve, about a third of it taken back after, so turning it up adds dirt more than level.
    const float gain = juce::Decibels::decibelsToGain(30.0f * d.drive);
    const float makeUp = juce::Decibels::decibelsToGain(-10.0f * d.drive);
    const int channels = buffer.getNumChannels();
    dry.setSize(2, buffer.getNumSamples(), false, false, true);
    for (int ch = 0; ch < channels; ++ch)
        dry.copyFrom(ch, 0, buffer, ch, 0, buffer.getNumSamples());

    // Twice the rate for the curve, so its harmonics fold back far less. The oversampler holds only as many samples as
    // prepareToPlay announced, so a longer block goes through in pieces.
    juce::dsp::AudioBlock<float> whole(buffer);
    for (std::size_t start = 0; start < whole.getNumSamples(); start += (std::size_t) maxBlock)
    {
        auto block = whole.getSubBlock(start, std::min((std::size_t) maxBlock, whole.getNumSamples() - start));
        auto high = oversampling.processSamplesUp(block);
        for (std::size_t ch = 0; ch < high.getNumChannels(); ++ch)
        {
            float* x = high.getChannelPointer(ch);
            for (std::size_t i = 0; i < high.getNumSamples(); ++i)
                x[i] = shape(d.mode, x[i] * gain);
        }
        oversampling.processSamplesDown(block);
    }

    // Folding and the lopsided tube leave DC behind; a highpass at 10 Hz takes it out. The dry part rides along
    // unfiltered, so the mix is taken against the input kept in `dry`.
    for (int ch = 0; ch < channels; ++ch)
    {
        float* x = buffer.getWritePointer(ch);
        const float* clean = dry.getReadPointer(ch);
        auto& in = dcIn[(std::size_t) ch];
        auto& out = dcOut[(std::size_t) ch];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float y = x[i] - in + dcPole * out;
            in = x[i];
            out = y;
            x[i] = clean[i] + (y * makeUp - clean[i]) * d.mix;
        }
    }
}

juce::AudioProcessorEditor* WavetableProcessor::createEditor() { return new WavetableEditor(*this); }

int WavetableProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void WavetableProcessor::setCurrentProgram(int index)
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

const juce::String WavetableProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void WavetableProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WavetableProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace tonwerkwave

#ifndef TONWERK_WAVETABLE_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonwerkwave::WavetableProcessor();
}
#endif
