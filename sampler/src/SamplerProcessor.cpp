#include "SamplerProcessor.h"
#include "Language.h"
#include "Platform.h"

#include "SamplerEditor.h"
#include "SamplerPresets.h"
#include "dsp/Tempo.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace tonwerksampler
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
        result.add(tonwerkui::tr(item));
    return result;
}

/** A number shown whole with its unit; `centre` skews the knob so that value sits halfway. */
void number(Layout& layout, const juce::String& id, const juce::String& name, float min, float max, float initial,
            const char* unit, float centre = 0.0f)
{
    juce::NormalisableRange<float> range(min, max);
    if (centre > 0.0f)
        range.setSkewForCentre(centre);
    const juce::String suffix = tonwerkui::tr(unit).isEmpty() ? juce::String() : " " + tonwerkui::tr(unit);
    auto toText = [suffix](float value, int) { return juce::String(juce::roundToInt(value)) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue(); };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
}

/** Shares in whole percent; the value runs from `min` to `max` (1 is 100 %). */
void percent(Layout& layout, const juce::String& id, const juce::String& name, float initial, float min = 0.0f,
             float max = 1.0f)
{
    auto toText = [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };
    auto fromText = [](const juce::String& text) { return text.getFloatValue() / 100.0f; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), juce::NormalisableRange<float>(min, max), initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
}

/** Times in whole milliseconds, skewed so the short ones get most of the turn. */
void milliseconds(Layout& layout, const juce::String& id, const juce::String& name, float max, float initial)
{
    number(layout, id, name, 0.0f, max, initial, "ms", max / 20.0f);
}

void integer(Layout& layout, const juce::String& id, const juce::String& name, int min, int max, int initial,
             const char* unit = "")
{
    const juce::String suffix = tonwerkui::tr(unit).isEmpty() ? juce::String() : " " + tonwerkui::tr(unit);
    auto toText = [suffix](int value, int) { return juce::String(value) + suffix; };
    auto fromText = [](const juce::String& text) { return text.getIntValue(); };
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), min, max, initial,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
}

void choice(Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, tonwerkui::tr(name),
                                                            tonwerkui::tr(items), initial));
}

void toggle(Layout& layout, const juce::String& id, const juce::String& name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, tonwerkui::tr(name), initial));
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
        juce::ParameterID { id, 1 }, tonwerkui::tr(name), range, initial,
        Attributes().withStringFromValueFunction(tonwerkui::translated(toText)).withValueFromStringFunction(fromText)));
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

juce::String noteName(int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 2);
}

Layout SamplerProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    juce::StringArray sources;
    for (const auto& name : SampleBank::names())
        sources.add(tonwerkui::tr(name.c_str()));
    choice(layout, "source", "Quelle", sources, d.source);
    choice(layout, "mode", "Modus", utf8Array({ "Klassisch", "Slices" }), (int) d.mode);
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "root", 1 }, tonwerkui::tr("Grundton"), 0, 127, d.root,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction(tonwerkui::translated([](int value, int) { return noteName(value); }))
            .withValueFromStringFunction([](const juce::String& text) {
                for (int n = 0; n < 128; ++n)
                    if (noteName(n).equalsIgnoreCase(text.trim()))
                        return n;
                return text.getIntValue();
            })));
    integer(layout, "tune", "Transponieren", -24, 24, d.tune, "HT");
    number(layout, "fine", "Fein", -100.0f, 100.0f, d.fine, "ct");
    percent(layout, "start", "Start", d.start);
    percent(layout, "end", "Ende", d.end);
    toggle(layout, "reverse", tonwerkui::tr("Rückwärts"), d.reverse);
    choice(layout, "playback", "Wiedergabe", utf8Array({ "Gate", "One-Shot" }), (int) d.playback);
    choice(layout, "loop", "Loop", utf8Array({ "Aus", "Vorwärts", "Pingpong" }), (int) d.loop);
    percent(layout, "loopStart", "Loop-Start", d.loopStart);
    percent(layout, "loopEnd", "Loop-Ende", d.loopEnd);
    milliseconds(layout, "crossfade", "Loop-Crossfade", 1000.0f, d.crossfade * 1000.0f);
    integer(layout, "slices", "Slices", 2, kMaxSlices, d.slices);
    choice(layout, "sliceMode", "Schnitt", utf8Array({ "Raster", "Transienten" }), (int) d.sliceMode);
    toggle(layout, "choke", "Einzeln", d.choke);
    envelope(layout, "amp", "Amp", d.amp);
    percent(layout, "velocity", "Anschlag", d.velocity);
    choice(layout, "filter", "Filter", utf8Array({ "Aus", "Tiefpass 12", "Tiefpass 24", "Hochpass", "Bandpass" }),
           (int) d.filter);
    number(layout, "cutoff", "Cutoff", 20.0f, 20000.0f, d.cutoff, "Hz", 1000.0f);
    percent(layout, "resonance", "Resonanz", d.resonance);
    percent(layout, "envAmount", tonwerkui::tr("Hüllkurve"), d.envAmount, -1.0f, 1.0f);
    percent(layout, "keytrack", "Keytracking", d.keytrack);
    envelope(layout, "filterEnv", "Filter", d.filterEnv);
    choice(layout, "lfoShape", "LFO Form", utf8Array({ "Sinus", "Dreieck", "Säge ab", "Säge auf", "Rechteck", "Zufall" }),
           (int) d.lfoShape);
    toggle(layout, "lfoSync", "LFO Tempo-Sync", d.lfoSync);
    hertz(layout, "lfoRate", "LFO Rate", 0.05f, 20.0f, d.lfoRate);
    juce::StringArray divisions;
    for (const auto& division : tonwerkwave::kDivisions)
        divisions.add(division.name);
    choice(layout, "lfoDivision", "LFO Raster", divisions, d.lfoDivision);
    choice(layout, "lfoTarget", "LFO Ziel", utf8Array({ "Aus", "Tonhöhe", "Filter", "Lautstärke", "Panorama" }),
           (int) d.lfoTarget);
    percent(layout, "lfoAmount", "LFO Menge", d.lfoAmount);
    choice(layout, "voiceMode", "Stimmen-Modus", utf8Array({ "Poly", "Mono", "Legato" }), (int) d.voiceMode);
    milliseconds(layout, "glide", "Glide", 2000.0f, d.glide * 1000.0f);
    integer(layout, "bendRange", "Pitchbend-Bereich", 0, 24, d.bendRange, "HT");
    percent(layout, "lofi", "Lo-Fi", d.lofi);
    number(layout, "master", "Master", -36.0f, 6.0f, d.masterDb, "dB");

    // Tonwerk's delay and hall, with Tonwerk DX's ids.
    const tonwerk::Effects fxDefaults;
    percent(layout, "fxDelayMix", "Delay Mix", 0.0f);
    toggle(layout, "fxDelaySync", "Delay Sync", fxDefaults.delay.sync);
    number(layout, "fxDelayTime", "Delay Zeit", 20.0f, 1500.0f, fxDefaults.delay.time * 1000.0f, "ms", 350.0f);
    juce::StringArray noteValues;
    for (const auto& division : tonwerk::kDivisions)
        noteValues.add(division.label);
    choice(layout, "fxDelayDivision", "Delay Teilung", noteValues, fxDefaults.delay.division);
    percent(layout, "fxDelayFeedback", "Delay Feedback", fxDefaults.delay.feedback, 0.0f, 0.9f);
    number(layout, "fxDelayTone", "Delay Ton", 500.0f, 12000.0f, fxDefaults.delay.tone, "Hz", 3000.0f);
    percent(layout, "fxReverbMix", "Hall Mix", 0.1f);
    number(layout, "fxReverbDecay", tonwerkui::tr("Hall Länge"), 300.0f, 8000.0f, 2000.0f, "ms", 2000.0f);
    return layout;
}

SamplerProcessor::SamplerProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkSampler", createParameterLayout())
{
    values = engineValues(*this, state);
    fx = { state.getRawParameterValue("fxDelayMix"),      state.getRawParameterValue("fxDelaySync"),
           state.getRawParameterValue("fxDelayTime"),     state.getRawParameterValue("fxDelayDivision"),
           state.getRawParameterValue("fxDelayFeedback"), state.getRawParameterValue("fxDelayTone"),
           state.getRawParameterValue("fxReverbMix"),     state.getRawParameterValue("fxReverbDecay") };
    for (auto& p : playheads)
        p.store(-1.0f);
    state.state.setProperty("presetName", "Init", nullptr);
    // The built-in sounds are computed once, here rather than on the audio thread's first block.
    SampleBank::instance();
    startTimerHz(10);
}

SamplerProcessor::~SamplerProcessor() { stopTimer(); }

Settings SamplerProcessor::readSettings() const
{
    // The values in the order `createParameterLayout` adds them, the fx left out.
    std::size_t at = 0;
    auto next = [this, &at] { return values[at++]->load(std::memory_order_relaxed); };
    auto flag = [&next] { return next() >= 0.5f; };
    auto whole = [&next] { return juce::roundToInt(next()); };
    auto envelope = [&](EnvSettings& e) {
        e.attack = next() / 1000.0f;
        e.decay = next() / 1000.0f;
        e.sustain = next();
        e.release = next() / 1000.0f;
    };

    Settings s;
    s.source = whole();
    s.mode = (Mode) whole();
    s.root = whole();
    s.tune = whole();
    s.fine = next();
    s.start = next();
    s.end = next();
    s.reverse = flag();
    s.playback = (Playback) whole();
    s.loop = (LoopMode) whole();
    s.loopStart = next();
    s.loopEnd = next();
    s.crossfade = next() / 1000.0f;
    s.slices = whole();
    s.sliceMode = (SliceMode) whole();
    s.choke = flag();
    envelope(s.amp);
    s.velocity = next();
    s.filter = (FilterType) whole();
    s.cutoff = next();
    s.resonance = next();
    s.envAmount = next();
    s.keytrack = next();
    envelope(s.filterEnv);
    s.lfoShape = (LfoShape) whole();
    s.lfoSync = flag();
    s.lfoRate = next();
    s.lfoDivision = whole();
    s.lfoTarget = (LfoTarget) whole();
    s.lfoAmount = next();
    s.voiceMode = (VoiceMode) whole();
    s.glide = next() / 1000.0f;
    s.bendRange = whole();
    s.lofi = next();
    s.masterDb = next();
    jassert(at == values.size());
    return s;
}

tonwerk::Effects SamplerProcessor::effectsFor() const
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

bool SamplerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void SamplerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    voices.setSize(2, samplesPerBlock);
    keyboardState.reset();
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void SamplerProcessor::handle(const juce::MidiMessage& message, const Settings& s, const Sample& sample)
{
    if (message.isNoteOn())
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity(), s, sample);
    else if (message.isNoteOff())
        engine.noteOff(message.getNoteNumber(), s);
    else if (message.isPitchWheel())
        wheel = message.getPitchWheelValue();
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine.allNotesOff();
    else if (message.isController() && message.getControllerNumber() == 64)
        engine.setSustain(message.getControllerValue() >= 64);
    engine.setBend((wheel - 8192) / 8192.0 * s.bendRange);
}

void SamplerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    {
        const juce::SpinLock::ScopedTryLockType lock(sampleLock);
        if (lock.isLocked() && audioUserSample != userSample)
            audioUserSample = userSample;
    }
    const Settings s = readSettings();
    // Without a file, "Eigene Datei" is silent rather than another sound in disguise.
    const Sample& sample = s.source != SampleBank::kUserFile ? SampleBank::instance().sample(s.source)
                           : audioUserSample != nullptr      ? *audioUserSample
                                                             : SampleBank::none();

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
        engine.render(left + done, right + done, end - done, s, sample, at);
        done = end;
    };
    for (const auto metadata : midi)
    {
        renderUpTo(std::min(metadata.samplePosition, count));
        handle(metadata.getMessage(), s, sample);
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

    std::array<float, 8> places {};
    engine.positions(places, (double) sample.length());
    for (std::size_t i = 0; i < places.size(); ++i)
        playheads[i].store(places[i], std::memory_order_relaxed);
}

void SamplerProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
    // A replaced file goes once the audio thread has let go of it too.
    retired.erase(std::remove_if(retired.begin(), retired.end(), [](const auto& s) { return s.use_count() == 1; }),
                  retired.end());
}

bool SamplerProcessor::loadFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;
    const auto length = (int) std::min<juce::int64>(reader->lengthInSamples,
                                                    (juce::int64) (Sample::kMaxSeconds * reader->sampleRate));
    const int channels = (int) std::clamp(reader->numChannels, 1u, 2u);
    juce::AudioBuffer<float> audio(channels, length);
    reader->read(&audio, 0, length, 0, true, channels > 1);
    std::vector<std::vector<float>> data((std::size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
        data[(std::size_t) ch].assign(audio.getReadPointer(ch), audio.getReadPointer(ch) + length);
    auto sample = std::make_shared<const Sample>(
        Sample::fromChannels(std::move(data), reader->sampleRate, file.getFileNameWithoutExtension().toStdString()));
    {
        const juce::SpinLock::ScopedLockType lock(sampleLock);
        if (userSample != nullptr)
            retired.push_back(userSample);
        userSample = std::move(sample);
    }
    state.state.setProperty("sampleFile", file.getFullPathName(), nullptr);
    if (auto* parameter = state.getParameter("source"))
        parameter->setValueNotifyingHost(parameter->convertTo0to1((float) SampleBank::kUserFile));
    return true;
}

std::shared_ptr<const Sample> SamplerProcessor::shownSample() const
{
    const int index = juce::roundToInt(state.getRawParameterValue("source")->load());
    if (index == SampleBank::kUserFile)
    {
        const juce::SpinLock::ScopedLockType lock(sampleLock);
        if (userSample != nullptr)
            return userSample;
        return std::shared_ptr<const Sample>(std::shared_ptr<const Sample>(), &SampleBank::none());
    }
    // The bank lives as long as the program; the pointer only borrows it.
    return std::shared_ptr<const Sample>(std::shared_ptr<const Sample>(), &SampleBank::instance().sample(index));
}

juce::AudioProcessorEditor* SamplerProcessor::createEditor() { return tonwerkui::forHost(new SamplerEditor(*this)); }

int SamplerProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void SamplerProcessor::setCurrentProgram(int index)
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

const juce::String SamplerProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? utf8(presets[(std::size_t) index].name)
                                                                 : juce::String();
}

void SamplerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SamplerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(state.state.getType()))
        return;
    state.replaceState(juce::ValueTree::fromXml(*xml));
    // The file is read again from where it was; the source parameter keeps what the project saved.
    const auto saved = state.getRawParameterValue("source")->load();
    const auto file = tonwerkui::findKept(userFile(), tonwerkui::keptFilesFolder("Tonwerk Sampler"));
    if (file.existsAsFile())
        loadFile(file);
    if (auto* parameter = state.getParameter("source"))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(saved));
}
} // namespace tonwerksampler

#ifndef TONWERK_SAMPLER_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerksampler::SamplerProcessor(); }
#endif
