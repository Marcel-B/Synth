#include "PatchJson.h"
#include "PluginProcessor.h"

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

        processor.releaseResources();
    }
};

static ProcessorTests processorTests;
