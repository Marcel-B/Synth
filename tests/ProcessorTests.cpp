#include "PatchJson.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/Tempo.h"

#include <juce_core/juce_core.h>

using namespace tonwerk;

class ProcessorTests : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest("Processor", "Plugin") {}

    void runTest() override
    {
        TonwerkSynthProcessor processor;
        const double rate = 48000.0;
        const int block = 256;
        processor.setPlayConfigDetails(0, 2, rate, block);
        processor.prepareToPlay(rate, block);

        auto render = [&](juce::MidiBuffer midi, int blocks) {
            juce::AudioBuffer<float> buffer(2, block);
            float peak = 0.0f;
            bool finite = true;
            for (int i = 0; i < blocks; ++i)
            {
                processor.processBlock(buffer, midi);
                midi.clear();
                peak = std::max(peak, buffer.getMagnitude(0, block));
                for (int s = 0; s < block; ++s)
                    finite = finite && std::isfinite(buffer.getSample(0, s)) && std::isfinite(buffer.getSample(1, s));
            }
            expect(finite);
            return peak;
        };

        beginTest("Silent without notes");
        expectEquals(render({}, 10), 0.0f);

        beginTest("A note sounds on both sides and ends after note off");
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        expectGreaterThan(render(on, 40), 0.05f);
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        render(off, 1);
        // The lead's release is 0.15 s; 60 blocks are 0.32 s.
        render({}, 60);
        expectEquals(render({}, 20), 0.0f);

        beginTest("Parameters hold every field of a patch");
        Patch patch;
        patch.engine = Engine::fm;
        patch.fm = defaultFm(Kind::chords);
        patch.analog = defaultAnalog(Kind::bass);
        patch.analog.osc1.pwmLfo = false;
        patch.analog.filter.type = FilterType::bandpass;
        patch.fx.delay.mix = 0.3f;
        patch.fx.reverb.decay = 4.2f;
        writePatch(processor.state, patch);
        const auto read = ParameterReader(processor.state).read();
        for (const auto& d : descriptors())
            expectWithinAbsoluteError(d.get(read), d.get(patch), 1.0e-3f * std::max(1.0f, std::abs(d.get(patch))), d.id);

        beginTest("The FM engine plays after switching");
        juce::MidiBuffer chord;
        for (int note : { 60, 64, 67 })
            chord.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 90), 0);
        expectGreaterThan(render(chord, 20), 0.05f);
        juce::MidiBuffer chordOff;
        for (int note : { 60, 64, 67 })
            chordOff.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        render(chordOff, 1);

        beginTest("A Tonwerk sound applied by name is what the state saves and restores");
        processor.applyPatch(juce::JSON::parse(R"({ "engine": "analog", "volume": 0.33, "filter": { "cutoff": 900 } })"),
                             "Mein Klang");
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        TonwerkSynthProcessor restored;
        restored.setStateInformation(saved.getData(), (int) saved.getSize());
        expectEquals(restored.presetName(), juce::String("Mein Klang"));
        const auto json = restored.currentPatchJson();
        expectEquals(json.getProperty("engine", {}).toString(), juce::String("analog"));
        expectWithinAbsoluteError((float) json.getProperty("volume", {}), 0.33f, 1.0e-3f);
        expectWithinAbsoluteError((float) json["filter"].getProperty("cutoff", {}), 900.0f, 0.5f);

        beginTest("Sixteen voices, more notes steal without breaking");
        juce::MidiBuffer many;
        for (int note = 40; note < 70; ++note)
            many.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 64), 0);
        expectGreaterThan(render(many, 10), 0.0f);
        juce::MidiBuffer allOff;
        allOff.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        render(allOff, 1);

        beginTest("Factory programs switch the sound");
        expectEquals(processor.getNumPrograms(), 8);
        processor.setCurrentProgram(4);
        expectEquals(processor.getProgramName(4), juce::String("FM Blech"));
        expectEquals(processor.presetName(), juce::String("FM Blech"));
        expectEquals(processor.currentPatchJson().getProperty("engine", {}).toString(), juce::String("fm"));

        beginTest("The oscilloscope sees what is played and draws it");
        processor.setCurrentProgram(0);
        juce::MidiBuffer note;
        note.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8) 110), 0);
        render(note, 30);
        std::array<float, 1024> seen {};
        processor.scope.read(seen.data(), (int) seen.size());
        float loudest = 0.0f;
        for (float sample : seen)
            loudest = std::max(loudest, std::abs(sample));
        expectGreaterThan(loudest, 0.05f);
        ScopeView view(processor.scope);
        view.setSize(300, 72);
        view.refresh();
        const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f);
        // The trace is drawn in the accent green; count the columns it reaches.
        int columns = 0;
        for (int x = 0; x < image.getWidth(); ++x)
        {
            bool green = false;
            for (int y = 0; y < image.getHeight(); ++y)
            {
                const auto pixel = image.getPixelAt(x, y);
                green = green || (pixel.getGreen() > 120 && pixel.getRed() < 100);
            }
            columns += green ? 1 : 0;
        }
        expectGreaterThan(columns, 250);
        // For a look at it: TONWERK_SCOPE_PNG=/path/scope.png
        if (const auto path = juce::SystemStats::getEnvironmentVariable("TONWERK_SCOPE_PNG", {}); path.isNotEmpty())
        {
            juce::FileOutputStream out { juce::File(path) };
            out.setPosition(0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
        juce::MidiBuffer noteOff;
        noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
        render(noteOff, 1);

        processor.releaseResources();
    }
};

class TempoTests : public juce::UnitTest
{
public:
    TempoTests() : juce::UnitTest("Tempo sync", "Plugin") {}

    void runTest() override
    {
        beginTest("Synced LFOs take the rate of their note value");
        Patch patch;
        patch.analog.lfo.sync = true;
        patch.analog.lfo.division = kQuarter;
        patch.fm.lfo.sync = true;
        patch.fm.lfo.division = 10; // 1/8
        const auto at120 = withTempo(patch, 120.0);
        expectWithinAbsoluteError(at120.analog.lfo.rate, 2.0f, 1.0e-4f);
        expectWithinAbsoluteError(at120.fm.lfo.rate, 4.0f, 1.0e-4f);

        beginTest("A synced sample and hold steps on its note value");
        patch.analog.sampleHold.sync = true;
        patch.analog.sampleHold.division = kSixteenth;
        expectWithinAbsoluteError(withTempo(patch, 120.0).analog.sampleHold.rate, 8.0f, 1.0e-4f);

        beginTest("A synced delay repeats on its note value, up to the line's length");
        patch.fx.delay.sync = true;
        patch.fx.delay.division = kDottedEighth;
        expectWithinAbsoluteError(withTempo(patch, 100.0).fx.delay.time, 0.45f, 1.0e-4f);
        patch.fx.delay.division = 0; // 4/1 at 60 bpm: 16 s, more than the line holds.
        expectWithinAbsoluteError(withTempo(patch, 60.0).fx.delay.time, (float) kMaxDelaySeconds, 1.0e-4f);

        beginTest("Free LFOs and delays keep their own values");
        Patch free;
        free.analog.lfo.rate = 3.3f;
        free.fx.delay.time = 0.7f;
        const auto same = withTempo(free, 90.0);
        expectWithinAbsoluteError(same.analog.lfo.rate, 3.3f, 1.0e-6f);
        expectWithinAbsoluteError(same.fx.delay.time, 0.7f, 1.0e-6f);

        beginTest("The host's tempo reaches the delay");
        struct FixedTempo : juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm(90.0);
                return info;
            }
        } host;
        TonwerkSynthProcessor processor;
        processor.setPlayHead(&host);
        processor.setPlayConfigDetails(0, 2, 48000.0, 256);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        expectWithinAbsoluteError(processor.tempo(), 90.0, 1.0e-9);
        processor.setPlayHead(nullptr);
    }
};

static TempoTests tempoTests;

static ProcessorTests processorTests;
