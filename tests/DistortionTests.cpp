#include "DistortionEditor.h"
#include "DistortionProcessor.h"
#include "Ds1Circuit.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>

using namespace tonwerkdistortion;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

std::vector<float> sine(float hz, float amplitude, int length, double rate = kRate)
{
    std::vector<float> out((std::size_t) length);
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi * hz * (double) n / rate);
    return out;
}

/** The amplitude of `hz` in `signal` from `from` on (Goertzel). */
float amplitudeAt(const std::vector<float>& signal, float hz, std::size_t from, double rate = kRate)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = from; n < signal.size(); ++n)
    {
        re += signal[n] * std::cos(w * (double) n);
        im += signal[n] * std::sin(w * (double) n);
    }
    return (float) (2.0 * std::sqrt(re * re + im * im) / (double) (signal.size() - from));
}

float peak(const std::vector<float>& signal, std::size_t from)
{
    float worst = 0.0f;
    for (std::size_t n = from; n < signal.size(); ++n)
        worst = std::max(worst, std::abs(signal[n]));
    return worst;
}

float decibels(float ratio) { return 20.0f * std::log10(ratio); }

/** The drive alone, at a rate high enough that its harmonics stay below Nyquist without the processor's oversampler. */
std::vector<float> drive(const std::vector<float>& input, const Settings& s, double rate)
{
    Drive circuit;
    circuit.prepare((float) rate, s);
    std::vector<float> out(input);
    for (auto& sample : out)
        sample = circuit.process(sample);
    return out;
}

/** Tone and level alone, with the level set so the stack's own gain is measured. */
float toneGain(float tone, float hz)
{
    Settings s;
    s.tone = tone;
    s.levelDb = -20.0f * std::log10(1.5f);
    Voice voice;
    voice.prepare((float) kRate, s);
    auto signal = sine(hz, 0.1f, (int) kRate);
    for (auto& sample : signal)
        sample = voice.process(sample);
    return amplitudeAt(signal, hz, signal.size() / 2) / 0.1f;
}

struct Rendered
{
    std::vector<float> left, right;
    double seconds = 0.0;
};

/** Runs `input` (on both sides) through a fresh processor in host-sized blocks. */
Rendered process(const std::vector<float>& input, std::initializer_list<std::pair<const char*, float>> knobs,
                 int channels = 2)
{
    DistortionProcessor processor;
    processor.setPlayConfigDetails(channels, channels, kRate, kBlock);
    for (const auto& [id, value] : knobs)
    {
        auto* parameter = processor.state.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    processor.prepareToPlay(kRate, kBlock);

    Rendered out { input, input };
    juce::AudioBuffer<float> buffer(channels, kBlock);
    juce::MidiBuffer midi;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < input.size(); at += kBlock)
    {
        const int count = (int) std::min<std::size_t>(kBlock, input.size() - at);
        buffer.setSize(channels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, input.data() + at, count);
        processor.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), count, out.left.data() + at);
        std::copy_n(buffer.getReadPointer(channels - 1), count, out.right.data() + at);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}
} // namespace

class DistortionTests : public juce::UnitTest
{
public:
    DistortionTests() : juce::UnitTest("Tonwerk Distortion", "Distortion") {}

    void runTest() override
    {
        beginTest("The diodes pass small signals and clip hard at about 0.6 V");
        {
            const auto& clip = DiodeClipper::instance();
            expectWithinAbsoluteError(clip(0.1f), 0.1f, 0.001f);
            expectWithinAbsoluteError(clip(-0.1f), -0.1f, 0.001f);
            // From 1 V to 4.5 V in, the output grows by less than 0.1 V: a hard clip, not a soft one.
            expect(clip(1.0f) > 0.5f && clip(4.5f) < 0.7f);
            expectLessThan(clip(4.5f) - clip(1.0f), 0.1f);
            expectWithinAbsoluteError(clip(-4.5f), -clip(4.5f), 1.0e-6f);
            float worst = 0.0f;
            for (float in = -5.0f; in <= 5.0f; in += 0.0123f)
                worst = std::max(worst, std::abs(clip(in) - (float) DiodeClipper::solve(in)));
            expectLessThan(worst, 1.0e-4f);
        }

        beginTest("Centred, the tone knob leaves the DS-1's dip around 500 Hz");
        {
            const float lows = toneGain(0.5f, 100.0f);
            const float mids = toneGain(0.5f, 500.0f);
            const float highs = toneGain(0.5f, 3000.0f);
            logMessage("Tone centred: 100 Hz " + juce::String(decibels(lows), 1) + " dB, 500 Hz "
                       + juce::String(decibels(mids), 1) + " dB, 3 kHz " + juce::String(decibels(highs), 1) + " dB");
            expectGreaterThan(decibels(lows / mids), 6.0f);
            expectGreaterThan(decibels(highs / mids), 6.0f);
            // Turned right, the highs come up by far; turned left, the bass.
            expectGreaterThan(decibels(toneGain(1.0f, 3000.0f) / toneGain(0.0f, 3000.0f)), 20.0f);
            expectGreaterThan(decibels(toneGain(0.0f, 100.0f) / toneGain(1.0f, 100.0f)), 15.0f);
        }

        beginTest("DIST adds harmonics to a quiet note");
        {
            const double rate = kRate * 8.0;
            const auto input = sine(220.0f, 0.01f, (int) rate, rate);
            auto thirdOverFirst = [&](float dist) {
                Settings s;
                s.distortion = dist;
                const auto out = drive(input, s, rate);
                return amplitudeAt(out, 660.0f, out.size() / 2, rate) / amplitudeAt(out, 220.0f, out.size() / 2, rate);
            };
            const float low = thirdOverFirst(0.0f), high = thirdOverFirst(1.0f);
            logMessage("3rd harmonic at DIST 0: " + juce::String(decibels(low), 1) + " dB, at DIST 100: "
                       + juce::String(decibels(high), 1) + " dB");
            // A square wave's third is at -9.5 dB; full DIST comes close to it.
            expectGreaterThan(decibels(high), -14.0f);
            expectGreaterThan(decibels(high / low), 10.0f);
        }

        beginTest("The clip's level does not follow the input");
        {
            const auto quiet = process(sine(220.0f, 0.05f, (int) kRate), {});
            const auto loud = process(sine(220.0f, 0.5f, (int) kRate), {});
            const float quietPeak = peak(quiet.left, quiet.left.size() / 2);
            const float loudPeak = peak(loud.left, loud.left.size() / 2);
            logMessage("Peak at -26 dBFS in: " + juce::String(decibels(quietPeak), 1) + " dBFS, at -6 dBFS in: "
                       + juce::String(decibels(loudPeak), 1) + " dBFS");
            // 20 dB more in, at most 3 dB more out; and level 0 dB puts it near -6 dBFS, as the knob promises.
            expectLessThan(decibels(loudPeak / quietPeak), 3.0f);
            expect(decibels(loudPeak) > -10.0f && decibels(loudPeak) < -2.0f);
        }

        beginTest("Oversampling keeps what the clip folds back far below the note");
        {
            // 2511 Hz does not divide 48 kHz, so folded harmonics fall between the true ones. Full DIST and TONE and a
            // note higher than a guitar's highest are the worst case; a guitar's notes fold back far less.
            const float f0 = 2511.0f;
            const auto out = process(sine(f0, 0.5f, 1 << 17), { { "distortion", 1.0f }, { "tone", 1.0f } });
            constexpr int order = 15;
            constexpr int size = 1 << order;
            std::vector<float> spectrum((std::size_t) size * 2, 0.0f);
            for (int n = 0; n < size; ++n)
            {
                const float window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) n / (float) size);
                spectrum[(std::size_t) n] = out.left[out.left.size() - (std::size_t) size + (std::size_t) n] * window;
            }
            juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(spectrum.data());
            const float binHz = (float) kRate / (float) size;
            float harmonic = 0.0f, folded = 0.0f, foldedHz = 0.0f;
            for (int bin = (int) (40.0f / binHz); bin < (int) (20000.0f / binHz); ++bin)
            {
                const float hz = (float) bin * binHz;
                const float distance = std::abs(hz - f0 * std::round(hz / f0));
                const float magnitude = spectrum[(std::size_t) bin];
                if (distance < 6.0f * binHz)
                    harmonic = std::max(harmonic, magnitude);
                else if (distance > 12.0f * binHz && magnitude > folded)
                {
                    folded = magnitude;
                    foldedHz = hz;
                }
            }
            logMessage("Strongest folded component: " + juce::String(decibels(folded / harmonic), 1)
                       + " dB below the strongest harmonic, at " + juce::String(foldedHz, 0) + " Hz");
            expectLessThan(decibels(folded / harmonic), -60.0f);
        }

        beginTest("Silence after a note stays silence");
        {
            auto input = sine(110.0f, 0.5f, (int) kRate);
            input.resize((std::size_t) kRate * 3, 0.0f);
            const auto out = process(input, { { "distortion", 1.0f }, { "tone", 0.0f } });
            expectLessThan(peak(out.left, (std::size_t) kRate * 2), 1.0e-3f);
            for (float sample : out.left)
                expect(std::isfinite(sample));
        }

        beginTest("Mono and stereo, and the CPU it costs");
        {
            const auto input = sine(196.0f, 0.3f, (int) kRate * 10);
            const auto mono = process(input, {}, 1);
            const auto stereo = process(input, {}, 2);
            expect(stereo.left == stereo.right);
            expect(mono.left == stereo.left);
            const double realtime = 10.0 / stereo.seconds;
            logMessage("Stereo runs " + juce::String(realtime, 0) + " times faster than real time");
            // Generous, for debug builds and busy CI machines; the log shows the real figure.
            expectGreaterThan(realtime, 5.0);
        }

        beginTest("The processor keeps its settings and shows whole numbers");
        {
            DistortionProcessor processor;
            processor.setPlayConfigDetails(2, 2, kRate, kBlock);
            processor.prepareToPlay(kRate, kBlock);
            expectGreaterThan(processor.getLatencySamples(), 0);
            logMessage("Latency: " + juce::String(processor.getLatencySamples()) + " samples");
            processor.state.getParameter("distortion")->setValueNotifyingHost(0.8f);
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);

            DistortionProcessor loaded;
            loaded.setStateInformation(saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError(loaded.readSettings().distortion, 0.8f, 1.0e-4f);

            // Logic shows these texts in the knobs' value fields.
            auto text = [&](const char* id, float value) {
                auto* parameter = processor.state.getParameter(id);
                return parameter->getText(parameter->convertTo0to1(value), 32);
            };
            expectEquals(text("distortion", 0.4999999f), juce::String("50 %"));
            expectEquals(text("tone", 1.0f), juce::String("100 %"));
            expectEquals(text("level", -6.0000002f), juce::String("-6 dB"));
            expectEquals(text("input", 12.0f), juce::String("12 dB"));

            beginTest("The oscilloscope sees the input and the clipped output");
            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            const auto wave = sine(220.0f, 0.3f, kBlock * 20);
            for (int b = 0; b < 20; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, 0, wave.data() + b * kBlock, kBlock);
                processor.processBlock(buffer, midi);
            }
            std::array<float, 2048> in {}, out {};
            processor.scopeIn.read(in.data(), (int) in.size());
            processor.scopeOut.read(out.data(), (int) out.size());
            auto peak = [](const std::array<float, 2048>& samples) {
                float result = 0.0f;
                for (float sample : samples)
                    result = std::max(result, std::abs(sample));
                return result;
            };
            expectWithinAbsoluteError(peak(in), 0.3f, 0.01f);
            expectGreaterThan(peak(out), 0.05f);

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            expect(editor != nullptr && editor->getWidth() > 0);
            for (auto* child : editor->getChildren())
                expect(editor->getLocalBounds().contains(child->getBounds()));
            // For a look at it: TONWERK_EDITOR_PNG_DIR=/path
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("TONWERK_EDITOR_PNG_DIR", {}); dir.isNotEmpty())
            {
                for (auto* child : editor->getChildren())
                    if (auto* view = dynamic_cast<tonwerkui::ScopeView*>(child))
                        view->refresh();
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream file { juce::File(dir).getChildFile("Tonwerk Distortion.png") };
                file.setPosition(0);
                file.truncate();
                juce::PNGImageFormat().writeImageToStream(image, file);
            }
        }
    }
};

static DistortionTests distortionTests;
