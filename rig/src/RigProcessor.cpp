#include "RigProcessor.h"

#include "GrooveProcessor.h"
#include "Platform.h"
#include "RigCatalogue.h"
#include "RigEditor.h"

namespace tonwerkrig
{
void RigProcessor::Delay::setLength(int samples)
{
    length = std::max(0, samples);
    position = 0;
    for (auto& channel : line)
        channel.assign((std::size_t) length, 0.0f);
}

void RigProcessor::Delay::process(juce::AudioBuffer<float>& buffer, int channel, int count)
{
    if (length == 0)
        return;
    float* samples[2] = { buffer.getWritePointer(channel), buffer.getWritePointer(channel + 1) };
    int at = position;
    for (int c = 0; c < 2; ++c)
    {
        auto& ring = line[(std::size_t) c];
        at = position;
        for (int i = 0; i < count; ++i)
        {
            std::swap(samples[c][i], ring[(std::size_t) at]);
            if (++at == length)
                at = 0;
        }
    }
    position = at;
}

RigProcessor::RigProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    work.setSize(2 + 2 * kTrackSources, blockSize);
    setCurrentProgram(0);
}

RigProcessor::~RigProcessor()
{
    // The inner editors belong to the rig's editor, which the host deletes first.
    listener = nullptr;
}

bool RigProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

double RigProcessor::getTailLengthSeconds() const
{
    // The longest of the effects' tails (Shimmer's and the halls), whatever the rig holds; a host asks from anywhere.
    return 20.0;
}

bool RigProcessor::hasTracks() const { return instrumentId == kTracksInstrument; }

juce::String RigProcessor::sourceName(int source)
{
    if (source < 1 || source > kTrackSources)
        return juce::String::fromUTF8("Alles");
    return juce::String::fromUTF8(tonwerkgroove::kTrackNames[(std::size_t) (source - 1)]);
}

void RigProcessor::prepare(juce::AudioProcessor& processor)
{
    processor.setRateAndBufferSizeDetails(rate, blockSize);
    processor.prepareToPlay(rate, blockSize);
}

void RigProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    {
        const juce::SpinLock::ScopedLockType hold(lock);
        rate = sampleRate;
        blockSize = std::max(1, samplesPerBlock);
        work.setSize(2 + 2 * kTrackSources, blockSize);
        instrumentPrepared = false;
        for (auto& slot : slots)
            slot.prepared = false;
        rebuild();
    }
    updateLatency();
}

void RigProcessor::rebuild()
{
    const bool tracks = hasTracks();
    std::array<bool, kTrackSources> wanted {};
    for (auto& slot : slots)
    {
        if (! tracks || slot.processor == nullptr)
            slot.source = 0;
        if (slot.processor != nullptr && slot.on && slot.source > 0)
            wanted[(std::size_t) (slot.source - 1)] = true;
    }

    if (auto* inst = instrumentProcessor.get())
    {
        if (tracks && wanted != trackOn)
        {
            // A track with an effect of its own leaves the groovebox's main output for its own.
            auto layout = inst->getBusesLayout();
            for (int bus = 1; bus < layout.outputBuses.size() && bus <= kTrackSources; ++bus)
                layout.outputBuses.getReference(bus) =
                    wanted[(std::size_t) (bus - 1)] ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
            inst->releaseResources();
            [[maybe_unused]] const bool laidOut = inst->setBusesLayout(layout);
            jassert(laidOut);
            trackOn = wanted;
            instrumentPrepared = false;
        }
        if (! instrumentPrepared && rate > 0.0)
        {
            prepare(*inst);
            instrumentPrepared = true;
        }
    }
    else
        trackOn = {};

    for (auto& slot : slots)
        if (slot.processor != nullptr && ! slot.prepared && rate > 0.0)
        {
            prepare(*slot.processor);
            slot.prepared = true;
        }

    // Every track meets the slowest one, and the main output with them.
    std::array<int, kTrackSources> path {};
    int slowest = 0;
    for (int t = 0; t < kTrackSources; ++t)
    {
        if (! trackOn[(std::size_t) t])
            continue;
        for (const auto& slot : slots)
            if (slot.processor != nullptr && slot.on && slot.source == t + 1)
                path[(std::size_t) t] += slot.processor->getLatencySamples();
        slowest = std::max(slowest, path[(std::size_t) t]);
    }
    if (delays[0].length != slowest)
        delays[0].setLength(slowest);
    for (int t = 0; t < kTrackSources; ++t)
    {
        const int length = trackOn[(std::size_t) t] ? slowest - path[(std::size_t) t] : 0;
        if (delays[(std::size_t) (t + 1)].length != length)
            delays[(std::size_t) (t + 1)].setLength(length);
    }
}

template <typename Edit>
void RigProcessor::change(Edit&& edit)
{
    if (listener != nullptr)
        listener->rigWillChange();
    // Replaced processors are deleted after the lock is given back.
    std::vector<std::unique_ptr<juce::AudioProcessor>> retired;
    {
        const juce::SpinLock::ScopedLockType hold(lock);
        edit(retired);
        rebuild();
    }
    retired.clear();
    updateLatency();
    if (listener != nullptr)
        listener->rigChanged();
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
}

void RigProcessor::updateLatency()
{
    int latency = instrumentProcessor != nullptr ? instrumentProcessor->getLatencySamples() : 0;
    latency += delays[0].length;
    for (const auto& slot : slots)
        if (slot.processor != nullptr && slot.on && slot.source == 0)
            latency += slot.processor->getLatencySamples();
    setLatencySamples(latency);
}

void RigProcessor::runSlot(Slot& slot, juce::AudioBuffer<float>& buffer, int channel, int count)
{
    auto& processor = *slot.processor;
    processor.setPlayHead(getPlayHead());
    juce::AudioBuffer<float> pair(buffer.getArrayOfWritePointers() + channel, 2, count);
    noMidi.clear();
    processor.processBlock(pair, noMidi);
}

void RigProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    const juce::SpinLock::ScopedTryLockType hold(lock);
    auto* inst = instrumentProcessor.get();
    if (! hold.isLocked() || inst == nullptr || ! instrumentPrepared)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    const int channels = inst->getTotalNumOutputChannels();
    if (work.getNumChannels() < channels || work.getNumSamples() < count)
        work.setSize(std::max(channels, work.getNumChannels()), std::max(count, work.getNumSamples()), false, false, true);
    juce::AudioBuffer<float> view(work.getArrayOfWritePointers(), channels, count);
    view.clear();
    inst->setPlayHead(getPlayHead());
    inst->setNonRealtime(isNonRealtime());
    inst->processBlock(view, midi);
    midi.clear();

    // The main output first, then each track on its own with its effects, added to it.
    const int mainChannels = inst->getChannelCountOfBus(false, 0);
    if (mainChannels == 1)
        view.copyFrom(1, 0, view, 0, 0, count);
    delays[0].process(view, 0, count);
    for (int t = 0; t < kTrackSources; ++t)
    {
        if (! trackOn[(std::size_t) t] || t + 1 >= inst->getBusCount(false))
            continue;
        const int first = inst->getChannelIndexInProcessBlockBuffer(false, t + 1, 0);
        for (auto& slot : slots)
            if (slot.processor != nullptr && slot.on && slot.source == t + 1)
                runSlot(slot, view, first, count);
        delays[(std::size_t) (t + 1)].process(view, first, count);
        view.addFrom(0, 0, view, first, 0, count);
        view.addFrom(1, 0, view, first + 1, 0, count);
    }
    for (auto& slot : slots)
        if (slot.processor != nullptr && slot.on && slot.source == 0)
            runSlot(slot, view, 0, count);

    if (buffer.getNumChannels() == 1)
    {
        buffer.copyFrom(0, 0, view, 0, 0, count);
        buffer.addFrom(0, 0, view, 1, 0, count);
        buffer.applyGain(0.5f);
    }
    else
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom(channel, 0, view, std::min(channel, 1), 0, count);
}

void RigProcessor::setInstrument(const juce::String& kind)
{
    const auto* found = find(instruments(), kind);
    if (found == nullptr || kind == instrumentId)
        return;
    auto made = found->make();
    change([&](auto& retired) {
        retired.push_back(std::move(instrumentProcessor));
        instrumentProcessor = std::move(made);
        instrumentId = kind;
        instrumentPrepared = false;
        trackOn = {};
    });
}

void RigProcessor::setEffect(int index, const juce::String& kind)
{
    if (index < 0 || index >= kSlots)
        return;
    const auto* found = find(effects(), kind);
    std::unique_ptr<juce::AudioProcessor> made = found != nullptr ? found->make() : nullptr;
    change([&](auto& retired) {
        auto& slot = slots[(std::size_t) index];
        retired.push_back(std::move(slot.processor));
        slot.processor = std::move(made);
        slot.kind = found != nullptr ? juce::String(found->id) : juce::String();
        slot.prepared = false;
        slot.on = true;
    });
}

void RigProcessor::setSource(int index, int source)
{
    if (index < 0 || index >= kSlots)
        return;
    change([&](auto&) { slots[(std::size_t) index].source = std::clamp(source, 0, kTrackSources); });
}

void RigProcessor::setOn(int index, bool on)
{
    if (index < 0 || index >= kSlots)
        return;
    change([&](auto&) { slots[(std::size_t) index].on = on; });
}

void RigProcessor::moveEffect(int index, int by)
{
    const int other = index + by;
    if (index < 0 || index >= kSlots || other < 0 || other >= kSlots)
        return;
    change([&](auto&) { std::swap(slots[(std::size_t) index], slots[(std::size_t) other]); });
}

void RigProcessor::chooseProgram(juce::AudioProcessor& processor, const juce::String& program)
{
    if (program.isEmpty())
        return;
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        if (processor.getProgramName(i) == program)
        {
            processor.setCurrentProgram(i);
            return;
        }
    jassertfalse; // a factory rig names a sound its plugin does not have
}

void RigProcessor::playPreset(const RigPreset& preset)
{
    const auto* kind = find(instruments(), preset.instrument);
    jassert(kind != nullptr);
    if (kind == nullptr)
        return;
    auto made = kind->make();
    chooseProgram(*made, juce::String::fromUTF8(preset.program));
    std::array<Slot, kSlots> fresh;
    for (std::size_t i = 0; i < preset.effects.size() && i < fresh.size(); ++i)
    {
        const auto& e = preset.effects[i];
        const auto* effectKind = find(effects(), e.kind);
        jassert(effectKind != nullptr);
        if (effectKind == nullptr)
            continue;
        fresh[i].kind = e.kind;
        fresh[i].source = e.source;
        fresh[i].processor = effectKind->make();
        chooseProgram(*fresh[i].processor, juce::String::fromUTF8(e.program));
    }
    change([&](auto& retired) {
        retired.push_back(std::move(instrumentProcessor));
        for (auto& slot : slots)
            retired.push_back(std::move(slot.processor));
        instrumentProcessor = std::move(made);
        instrumentId = preset.instrument;
        instrumentPrepared = false;
        trackOn = {};
        slots = std::move(fresh);
        name = juce::String::fromUTF8(preset.name);
    });
}

void RigProcessor::setCurrentProgram(int index)
{
    const auto& rigs = factoryRigs();
    if (index < 0 || index >= (int) rigs.size())
        return;
    currentProgram = index;
    playPreset(rigs[(std::size_t) index]);
}

const juce::String RigProcessor::getProgramName(int index)
{
    const auto& rigs = factoryRigs();
    if (index < 0 || index >= (int) rigs.size())
        return {};
    return juce::String::fromUTF8(rigs[(std::size_t) index].name);
}

void RigProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement root("TonwerkRig");
    root.setAttribute("instrument", instrumentId);
    root.setAttribute("presetName", name);
    root.setAttribute("program", currentProgram);
    auto stateOf = [](juce::AudioProcessor& processor) {
        juce::MemoryBlock block;
        processor.getStateInformation(block);
        return block.toBase64Encoding();
    };
    if (instrumentProcessor != nullptr)
        root.createNewChildElement("Instrument")->addTextElement(stateOf(*instrumentProcessor));
    for (int i = 0; i < kSlots; ++i)
    {
        auto& slot = slots[(std::size_t) i];
        if (slot.processor == nullptr)
            continue;
        auto* element = root.createNewChildElement("Slot");
        element->setAttribute("index", i);
        element->setAttribute("kind", slot.kind);
        element->setAttribute("source", slot.source);
        element->setAttribute("on", slot.on);
        element->addTextElement(stateOf(*slot.processor));
    }
    copyXmlToBinary(root, destData);
}

void RigProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName("TonwerkRig"))
        return;
    auto restore = [](juce::AudioProcessor& processor, const juce::String& text) {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding(text) && block.getSize() > 0)
            processor.setStateInformation(block.getData(), (int) block.getSize());
    };

    const auto kindId = xml->getStringAttribute("instrument");
    const auto* kind = find(instruments(), kindId);
    if (kind == nullptr)
        return;
    auto made = kind->make();
    if (auto* element = xml->getChildByName("Instrument"))
        restore(*made, element->getAllSubText());
    std::array<Slot, kSlots> fresh;
    for (auto* element : xml->getChildWithTagNameIterator("Slot"))
    {
        const int index = element->getIntAttribute("index", -1);
        const auto* effectKind = find(effects(), element->getStringAttribute("kind"));
        if (index < 0 || index >= kSlots || effectKind == nullptr)
            continue;
        auto& slot = fresh[(std::size_t) index];
        slot.kind = effectKind->id;
        slot.source = std::clamp(element->getIntAttribute("source"), 0, kTrackSources);
        slot.on = element->getBoolAttribute("on", true);
        slot.processor = effectKind->make();
        restore(*slot.processor, element->getAllSubText());
    }
    change([&](auto& retired) {
        retired.push_back(std::move(instrumentProcessor));
        for (auto& slot : slots)
            retired.push_back(std::move(slot.processor));
        instrumentProcessor = std::move(made);
        instrumentId = kindId;
        instrumentPrepared = false;
        trackOn = {};
        slots = std::move(fresh);
        name = xml->getStringAttribute("presetName");
        currentProgram = std::clamp(xml->getIntAttribute("program"), 0, std::max(0, getNumPrograms() - 1));
    });
}

juce::AudioProcessorEditor* RigProcessor::createEditor() { return tonwerkui::forHost(new RigEditor(*this)); }
} // namespace tonwerkrig

#ifndef TONWERK_RIG_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tonwerkrig::RigProcessor(); }
#endif
