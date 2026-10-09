#include "GlitchEditor.h"
#include "GlitchEngine.h"
#include "GlitchProcessor.h"

#include <juce_core/juce_core.h>

using namespace chromeglitch;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/** Everything off but what a test turns on, so each test hears one effect. */
Settings quiet()
{
    Settings s;
    s.amount = 1.0f;
    s.stutterChance = 0.0f;
    s.dropoutChance = 0.0f;
    s.crush = 0.0f;
    s.pitchChance = 0.0f;
    s.chaos = 0.0f;
    s.sync = false;
    return s;
}

/** Runs `input` (mono, copied to both sides) through a fresh engine in blocks; returns the left side. */
std::vector<float> run(const std::vector<float>& input, const Settings& s, Transport transport = {})
{
    GlitchEngine engine;
    engine.prepare(kRate);
    std::vector<float> left(input), right(input);
    for (std::size_t start = 0; start < input.size(); start += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, input.size() - start);
        float* channels[] = { left.data() + start, right.data() + start };
        engine.process(channels, 2, count, s, transport);
        transport.ppq += count * transport.bpm / 60.0 / kRate;
    }
    return left;
}

std::vector<float> noise(int length)
{
    juce::Random random(42);
    std::vector<float> out((std::size_t) length);
    for (auto& sample : out)
        sample = random.nextFloat() * 1.6f - 0.8f;
    return out;
}

/** The share of samples (over `from`) where the output repeats itself `period` samples later. */
float periodicShare(const std::vector<float>& out, int period, std::size_t from)
{
    int same = 0, counted = 0;
    for (std::size_t n = from; n < out.size(); ++n)
    {
        ++counted;
        if (std::abs(out[n]) > 1.0e-3f && std::abs(out[n] - out[n - (std::size_t) period]) < 1.0e-6f)
            ++same;
    }
    return (float) same / (float) counted;
}
} // namespace

class GlitchTests : public juce::UnitTest
{
public:
    GlitchTests() : juce::UnitTest("Chrome Glitch", "Glitch") {}

    void runTest() override
    {
        const int length = (int) kRate * 4;

        beginTest("At amount 0 the voice passes untouched");
        {
            auto s = Settings {};
            s.amount = 0.0f;
            const auto input = noise(length);
            const auto out = run(input, s);
            float worst = 0.0f;
            for (std::size_t n = 0; n < input.size(); ++n)
                worst = std::max(worst, std::abs(out[n] - input[n]));
            expectEquals(worst, 0.0f);
        }

        beginTest("A stutter repeats the last slice");
        {
            auto s = quiet();
            s.stutterChance = 1.0f;
            s.stutterMs = 50.0f;
            s.repeats = 4;
            const auto out = run(noise(length), s);
            // Noise never repeats by itself, so every repeated stretch is the stutter: most of the time is spent in one.
            expectGreaterThan(periodicShare(out, (int) (0.05 * kRate), (std::size_t) kRate), 0.25f);
            for (float sample : out)
                expect(std::isfinite(sample) && std::abs(sample) <= 1.0f);
        }

        beginTest("A dropout silences the voice without a click");
        {
            auto s = quiet();
            s.dropoutChance = 1.0f;
            s.dropoutMs = 100.0f;
            const std::vector<float> input((std::size_t) length, 0.5f);
            const auto out = run(input, s);
            int silent = 0;
            float steepest = 0.0f;
            for (std::size_t n = 1; n < out.size(); ++n)
            {
                silent += juce::exactlyEqual(out[n], 0.0f) ? 1 : 0;
                steepest = std::max(steepest, std::abs(out[n] - out[n - 1]));
            }
            expectGreaterThan(silent, length / 5);
            // 0.5 faded over 1.5 ms (72 samples) moves at most 0.007 per sample.
            expectLessThan(steepest, 0.01f);
        }

        beginTest("In sync without chaos, glitches start on the grid");
        {
            auto s = quiet();
            s.dropoutChance = 1.0f;
            s.dropoutMs = 40.0f;
            s.sync = true;
            s.division = 1;
            Transport transport;
            transport.playing = true;
            transport.hasPosition = true;
            transport.bpm = 120.0;
            const std::vector<float> input((std::size_t) length, 0.5f);
            const auto out = run(input, s, transport);
            const int grid = (int) (0.125 * kRate);
            int starts = 0, onGrid = 0;
            for (std::size_t n = 1; n < out.size(); ++n)
                if (juce::exactlyEqual(out[n - 1], 0.5f) && out[n] < 0.5f)
                {
                    ++starts;
                    const int offset = (int) n % grid;
                    onGrid += std::min(offset, grid - offset) <= 1 ? 1 : 0;
                }
            expectGreaterThan(starts, 10);
            expectEquals(onGrid, starts);
        }

        beginTest("Pitched and faster repeats do not click");
        {
            auto s = quiet();
            s.stutterChance = 1.0f;
            s.pitchChance = 1.0f;
            s.pitchRange = 12.0f;
            s.accelerate = 0.5f;
            s.repeats = 8;
            std::vector<float> input((std::size_t) length);
            for (std::size_t n = 0; n < input.size(); ++n)
                input[n] = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * (float) n / (float) kRate);
            const auto out = run(input, s);
            float steepest = 0.0f;
            for (std::size_t n = 1; n < out.size(); ++n)
                steepest = std::max(steepest, std::abs(out[n] - out[n - 1]));
            // The sine moves up to 0.0145 per sample; an octave up twice that. A jump inside a repeat would be far more.
            expectLessThan(steepest, 0.04f);
        }

        beginTest("Freeze holds the slice for as long as it is on");
        {
            auto s = quiet();
            s.amount = 0.0f;
            s.freeze = true;
            s.stutterMs = 60.0f;
            const auto out = run(noise(length), s);
            expectGreaterThan(periodicShare(out, (int) (0.06 * kRate), (std::size_t) kRate), 0.9f);
        }

        beginTest("The crusher holds samples while the chrome breaks");
        {
            auto s = quiet();
            s.crush = 1.0f;
            std::vector<float> input((std::size_t) length);
            for (std::size_t n = 0; n < input.size(); ++n)
                input[n] = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * (float) n / (float) kRate);
            const auto out = run(input, s);
            int held = 0;
            for (std::size_t n = 1; n < out.size(); ++n)
                held += juce::exactlyEqual(out[n], out[n - 1]) ? 1 : 0;
            expectGreaterThan(held, length / 3);
        }

        beginTest("The dice come from a seed, so two runs glitch alike");
        {
            auto s = Settings {};
            s.amount = 1.0f;
            Transport transport;
            transport.playing = true;
            transport.hasPosition = true;
            const auto input = noise(length);
            expect(run(input, s, transport) == run(input, s, transport));
        }

        beginTest("The processor keeps its settings in the project");
        {
            ChromeGlitchProcessor processor;
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            processor.state.getParameter("amount")->setValueNotifyingHost(0.8f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);

            ChromeGlitchProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().amount, 0.8f, 1.0e-4f);

            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            for (int i = 0; i < 200; ++i)
            {
                for (int n = 0; n < kBlock; ++n)
                {
                    buffer.setSample(0, n, std::sin((float) (i * kBlock + n) * 0.05f));
                    buffer.setSample(1, n, 0.0f);
                }
                processor.processBlock(buffer, midi);
                for (int n = 0; n < kBlock; ++n)
                    expect(std::isfinite(buffer.getSample(0, n)));
            }

            // Logic shows these texts in the knobs' value fields.
            auto text = [&](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("stutterLength", 49.9999962f), juce::String("50 ms"));
            expectEquals(text("dropoutLength", 100.0f), juce::String("100 ms"));
            expectEquals(text("pitchRange", 24.0f), juce::String("24 HT"));

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
        }
    }
};

static GlitchTests glitchTests;
