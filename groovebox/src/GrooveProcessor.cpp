#include "GrooveProcessor.h"

#include "GrooveEditor.h"
#include "GroovePresets.h"
#include "dsp/Tempo.h"

namespace tonwerkgroove
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

void choice(Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& items, int initial)
{
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, items, initial));
}

void toggle(Layout& layout, const juce::String& id, const juce::String& name, bool initial)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, initial));
}

/** How long each instrument may ring, in ms: a closed hat is a tick, a cymbal washes for seconds. */
std::pair<float, float> decayRange(Instrument instrument)
{
    switch (instrument)
    {
        case Instrument::kick: return { 50.0f, 4000.0f };
        case Instrument::snare: return { 30.0f, 2000.0f };
        case Instrument::clap: return { 50.0f, 2000.0f };
        case Instrument::lowTom:
        case Instrument::highTom: return { 50.0f, 3000.0f };
        case Instrument::closedHat: return { 10.0f, 1000.0f };
        case Instrument::openHat: return { 50.0f, 3000.0f };
        case Instrument::cymbal: return { 200.0f, 8000.0f };
    }
    return { 10.0f, 8000.0f };
}

juce::String id(int track, const char* name) { return juce::String(kTrackIds[(std::size_t) track]) + name; }

/** A pattern's bits as text for the state: on and accent of each track in hex, separated by spaces. */
juce::String patternText(const Pattern& p)
{
    juce::StringArray tracks;
    for (const auto& t : p)
        tracks.add(juce::String::toHexString((juce::int64) t.on) + ":" + juce::String::toHexString((juce::int64) t.accent));
    return tracks.joinIntoString(" ");
}

Pattern parsePatternText(const juce::String& text)
{
    Pattern p {};
    const auto tracks = juce::StringArray::fromTokens(text, " ", "");
    for (int i = 0; i < std::min(kTracks, tracks.size()); ++i)
    {
        p[(std::size_t) i].on = (std::uint32_t) tracks[i].upToFirstOccurrenceOf(":", false, false).getHexValue64();
        p[(std::size_t) i].accent = (std::uint32_t) tracks[i].fromFirstOccurrenceOf(":", false, false).getHexValue64();
        p[(std::size_t) i].accent &= p[(std::size_t) i].on;
    }
    return p;
}

std::uint64_t pack(const TrackPattern& t) { return (std::uint64_t) t.on | ((std::uint64_t) t.accent << 32); }
TrackPattern unpack(std::uint64_t bits) { return { (std::uint32_t) bits, (std::uint32_t) (bits >> 32) }; }
} // namespace

Layout GrooveProcessor::createParameterLayout()
{
    const Settings d;
    Layout layout;
    for (int t = 0; t < kTracks; ++t)
    {
        const auto& track = d.track[(std::size_t) t];
        const juce::String name = utf8(kTrackNames[(std::size_t) t]) + " ";
        const auto [shortest, longest] = decayRange((Instrument) t);
        number(layout, id(t, "Tune"), name + "Stimmung", -24.0f, 24.0f, track.tune, "HT");
        number(layout, id(t, "Decay"), name + "Abklingen", shortest, longest, track.decay * 1000.0f, "ms",
               std::sqrt(shortest * longest));
        percent(layout, id(t, "Tone"), name + utf8(kSoundKnobs[(std::size_t) t].first), track.tone);
        percent(layout, id(t, "Character"), name + utf8(kSoundKnobs[(std::size_t) t].second), track.character);
        number(layout, id(t, "Level"), name + "Pegel", -36.0f, 6.0f, track.levelDb, "dB");
        percent(layout, id(t, "Pan"), name + "Panorama", track.pan, -1.0f, 1.0f);
        percent(layout, id(t, "Delay"), name + "Delay", track.delaySend);
        percent(layout, id(t, "Reverb"), name + "Hall", track.reverbSend);
        toggle(layout, id(t, "Mute"), name + "Stumm", track.mute);
    }

    choice(layout, "seqMode", "Sequencer", utf8Array({ "Aus", "Mit Host", "Frei" }), (int) d.seqMode);
    choice(layout, "pattern", "Pattern", { "A", "B", "C", "D", "E", "F", "G", "H" }, d.pattern);
    {
        auto toText = [](int value, int) { return juce::String(value) + " Schritte"; };
        auto fromText = [](const juce::String& text) { return text.getIntValue(); };
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { "length", 1 }, utf8("Länge"), 1, kSteps, d.length,
            juce::AudioParameterIntAttributes().withStringFromValueFunction(toText).withValueFromStringFunction(fromText)));
    }
    choice(layout, "rate", "Raster", { "1/16", "1/8", "1/16 T", "1/32" }, (int) d.rate);
    percent(layout, "swing", "Swing", d.swing, 0.5f, 0.75f);
    percent(layout, "accent", "Akzent", d.accent);
    percent(layout, "drive", "Drive", d.drive);
    number(layout, "master", "Master", -36.0f, 6.0f, d.masterDb, "dB");

    // Tonwerk's delay and hall, with Tonwerk DX's ids; the tracks' sends feed them.
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
    number(layout, "fxReverbDecay", utf8("Hall Länge"), 300.0f, 8000.0f, 1500.0f, "ms", 2000.0f);
    return layout;
}

GrooveProcessor::GrooveProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TonwerkGroovebox", createParameterLayout())
{
    auto value = [this](const juce::String& parameterId) {
        auto* v = state.getRawParameterValue(parameterId);
        jassert(v != nullptr);
        return v;
    };
    for (int t = 0; t < kTracks; ++t)
        trackValues[(std::size_t) t] = { value(id(t, "Tune")),  value(id(t, "Decay")), value(id(t, "Tone")),
                                         value(id(t, "Character")), value(id(t, "Level")), value(id(t, "Pan")),
                                         value(id(t, "Delay")), value(id(t, "Reverb")), value(id(t, "Mute")) };
    global = { value("seqMode"), value("pattern"), value("length"), value("rate"),
               value("swing"),   value("accent"),  value("drive"),  value("master") };
    fx = { value("fxDelayMix"),      value("fxDelaySync"), value("fxDelayTime"), value("fxDelayDivision"),
           value("fxDelayFeedback"), value("fxDelayTone"), value("fxReverbMix"), value("fxReverbDecay") };
    // A new instance starts with Init's beat, so pressing play gives a groove at once.
    setCurrentProgram(0);
    startTimerHz(10);
}

GrooveProcessor::~GrooveProcessor() { stopTimer(); }

Settings GrooveProcessor::readSettings() const
{
    auto get = [](const std::atomic<float>* v) { return v->load(std::memory_order_relaxed); };
    Settings s;
    for (std::size_t t = 0; t < (std::size_t) kTracks; ++t)
    {
        const auto& v = trackValues[t];
        auto& track = s.track[t];
        track.tune = get(v.tune);
        track.decay = get(v.decay) / 1000.0f;
        track.tone = get(v.tone);
        track.character = get(v.character);
        track.levelDb = get(v.level);
        track.pan = get(v.pan);
        track.delaySend = get(v.delay);
        track.reverbSend = get(v.reverb);
        track.mute = get(v.mute) >= 0.5f;
    }
    s.seqMode = (SeqMode) juce::roundToInt(get(global.seqMode));
    s.pattern = std::clamp(juce::roundToInt(get(global.pattern)), 0, kPatterns - 1);
    s.length = std::clamp(juce::roundToInt(get(global.length)), 1, kSteps);
    s.rate = (StepRate) juce::roundToInt(get(global.rate));
    s.swing = get(global.swing);
    s.accent = get(global.accent);
    s.drive = get(global.drive);
    s.masterDb = get(global.master);
    return s;
}

tonwerk::Effects GrooveProcessor::effectsFor() const
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

TrackPattern GrooveProcessor::trackPattern(int index, int track) const
{
    return unpack(patterns[(std::size_t) index][(std::size_t) track].load(std::memory_order_relaxed));
}

void GrooveProcessor::setTrackPattern(int index, int track, const TrackPattern& value)
{
    TrackPattern clean = value;
    clean.accent &= clean.on;
    patterns[(std::size_t) index][(std::size_t) track].store(pack(clean), std::memory_order_relaxed);
}

int GrooveProcessor::step(int index, int track, int at) const
{
    const auto t = trackPattern(index, track);
    const auto bit = 1u << at;
    return (t.on & bit) == 0 ? 0 : (t.accent & bit) != 0 ? 2 : 1;
}

void GrooveProcessor::setStep(int index, int track, int at, int value)
{
    auto t = trackPattern(index, track);
    const auto bit = 1u << at;
    t.on = value > 0 ? t.on | bit : t.on & ~bit;
    t.accent = value > 1 ? t.accent | bit : t.accent & ~bit;
    setTrackPattern(index, track, t);
    patternsChanged();
}

Pattern GrooveProcessor::pattern(int index) const
{
    Pattern p;
    for (int t = 0; t < kTracks; ++t)
        p[(std::size_t) t] = trackPattern(index, t);
    return p;
}

void GrooveProcessor::setPattern(int index, const Pattern& value)
{
    for (int t = 0; t < kTracks; ++t)
        setTrackPattern(index, t, value[(std::size_t) t]);
}

void GrooveProcessor::clearPattern(int index)
{
    setPattern(index, {});
    patternsChanged();
}

void GrooveProcessor::patternsChanged()
{
    // The patterns are not parameters; this tells the host the project has changed all the same.
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
}

bool GrooveProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void GrooveProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate, samplesPerBlock);
    work.setSize(4, samplesPerBlock);
    running = false;
    lastMode = SeqMode::off;
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

void GrooveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const Settings s = readSettings();

    bool hostPlaying = false, hasPosition = false;
    double ppq = 0.0, tempo = 120.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            hostPlaying = position->getIsPlaying();
            if (const auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                tempo = *hostBpm;
            if (const auto hostPpq = position->getPpqPosition())
            {
                ppq = *hostPpq;
                hasPosition = true;
            }
        }
    bpm.store(tempo);
    const double samplesPerBeat = rate * 60.0 / tempo;

    // The steps of this block: where the sequencer stands, and whether it joins the last block.
    struct Hit
    {
        int offset;
        int track;
        float velocity;
    };
    std::array<Hit, 256> hits {};
    int hitCount = 0;
    bool runs = false;
    double from = 0.0;
    if (s.seqMode == SeqMode::free)
    {
        if (lastMode != SeqMode::free)
            freeBeats = 0.0;
        runs = true;
        from = freeBeats;
    }
    else if (s.seqMode == SeqMode::host && hostPlaying && hasPosition)
    {
        runs = true;
        from = ppq;
    }
    // Hosts round the position; a block that starts within half a sample of where the last ended continues it, so a
    // step on the seam plays once.
    if (runs && running && std::abs(from - lastEnd) < 0.5 / samplesPerBeat)
        from = lastEnd;
    const double to = from + count / samplesPerBeat;
    if (runs)
    {
        const auto& p = patterns[(std::size_t) s.pattern];
        int shown = playingStep.load(std::memory_order_relaxed);
        Clock::steps(from, to, samplesPerBeat, count, s.length, s.rate, s.swing, [&](int offset, int step) {
            shown = step;
            for (int t = 0; t < kTracks; ++t)
            {
                const auto bits = unpack(p[(std::size_t) t].load(std::memory_order_relaxed));
                if ((bits.on & (1u << step)) != 0 && hitCount < (int) hits.size())
                    hits[(std::size_t) hitCount++] = { offset, t, stepVelocity((bits.accent & (1u << step)) != 0, s.accent) };
            }
        });
        playingStep.store(shown, std::memory_order_relaxed);
    }
    else
        playingStep.store(-1, std::memory_order_relaxed);
    running = runs;
    lastEnd = to;
    lastMode = s.seqMode;
    if (s.seqMode == SeqMode::free)
        freeBeats = to;

    if (const auto clicked = auditions.exchange(0); clicked != 0)
        for (int t = 0; t < kTracks; ++t)
            if ((clicked & (1u << t)) != 0 && hitCount < (int) hits.size())
                hits[(std::size_t) hitCount++] = { 0, t, 0.9f };

    if (work.getNumSamples() < count)
        work.setSize(4, count, false, false, true);
    work.clear();
    float* left = work.getWritePointer(0);
    float* right = work.getWritePointer(1);
    float* delaySend = work.getWritePointer(2);
    float* reverbSend = work.getWritePointer(3);

    int done = 0;
    auto renderUpTo = [&](int end) {
        if (end <= done)
            return;
        engine.render(left + done, right + done, delaySend + done, reverbSend + done, end - done, s);
        done = end;
    };
    // The sequencer's hits and the MIDI notes in time order; the hits came in order already.
    int nextHit = 0;
    auto playHitsBefore = [&](int offset) {
        while (nextHit < hitCount && hits[(std::size_t) nextHit].offset <= offset)
        {
            const auto& hit = hits[(std::size_t) nextHit++];
            renderUpTo(hit.offset);
            engine.trigger(hit.track, hit.velocity, s);
        }
    };
    std::stable_sort(hits.begin(), hits.begin() + hitCount, [](const Hit& a, const Hit& b) { return a.offset < b.offset; });
    for (const auto metadata : midi)
    {
        const int offset = std::clamp(metadata.samplePosition, 0, std::max(0, count - 1));
        playHitsBefore(offset);
        const auto message = metadata.getMessage();
        renderUpTo(offset);
        if (message.isNoteOn())
            engine.trigger(trackForNote(message.getNoteNumber()), message.getFloatVelocity(), s);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.reset();
    }
    playHitsBefore(count);
    renderUpTo(count);
    midi.clear();

    for (int t = 0; t < kTracks; ++t)
    {
        const float peak = engine.takePeak(t);
        auto& shown = trackPeaks[(std::size_t) t];
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
    effects.process(effectsFor(), left, right, delaySend, reverbSend, buffer, 0, count);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        Engine::saturate(buffer.getWritePointer(channel), count, s.drive);
    buffer.applyGain(juce::Decibels::decibelsToGain(s.masterDb));
}

void GrooveProcessor::timerCallback()
{
    // The reverb's room allocates, so it is made here, never on the audio thread.
    effects.loadRoom(tonwerk::EffectsChain::roomFor(effectsFor()));
}

juce::AudioProcessorEditor* GrooveProcessor::createEditor() { return new GrooveEditor(*this); }

int GrooveProcessor::getNumPrograms() { return (int) factoryKits().size(); }

void GrooveProcessor::setCurrentProgram(int index)
{
    const auto& kits = factoryKits();
    if (! juce::isPositiveAndBelow(index, (int) kits.size()))
        return;
    currentProgram = index;
    const auto& kit = kits[(std::size_t) index];
    // Everything back to its default first, so a kit only names what it changes; how the sequencer runs is the
    // user's setup, not the kit's, and stays.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged && ranged->paramID != "seqMode")
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    for (const auto& [parameterId, value] : kit.values)
        if (auto* parameter = state.getParameter(parameterId))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse; // a kit names a parameter that does not exist
    for (int p = 0; p < kPatterns; ++p)
    {
        Pattern pattern {};
        if (p < (int) kit.patterns.size())
            for (int t = 0; t < kTracks; ++t)
                pattern[(std::size_t) t] = parseTrack(kit.patterns[(std::size_t) p][(std::size_t) t]);
        setPattern(p, pattern);
    }
    state.state.setProperty("presetName", utf8(kit.name), nullptr);
}

const juce::String GrooveProcessor::getProgramName(int index)
{
    const auto& kits = factoryKits();
    return juce::isPositiveAndBelow(index, (int) kits.size()) ? utf8(kits[(std::size_t) index].name) : juce::String();
}

void GrooveProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    juce::ValueTree saved("Patterns");
    for (int p = 0; p < kPatterns; ++p)
        saved.setProperty("p" + juce::String(p), patternText(pattern(p)), nullptr);
    copy.removeChild(copy.getChildWithName("Patterns"), nullptr);
    copy.appendChild(saved, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary(*xml, destData);
}

void GrooveProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(state.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml(*xml);
    const auto saved = tree.getChildWithName("Patterns");
    for (int p = 0; p < kPatterns; ++p)
        setPattern(p, parsePatternText(saved.getProperty("p" + juce::String(p), {}).toString()));
    tree.removeChild(saved, nullptr);
    state.replaceState(tree);
}

} // namespace tonwerkgroove

#ifndef TONWERK_GROOVEBOX_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkgroove::GrooveProcessor(); }
#endif
