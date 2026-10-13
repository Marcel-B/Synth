#pragma once

#include "AdditiveProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkadd
{
using tonwerkui::Section;

/**
 * The partials as bars, one per partial from the fundamental (yellow) to the 64th, as high as each sounds (48 dB from
 * the loudest to the floor). While a note plays they move with its envelopes and modulation; while nothing sounds
 * they show the spectrum as the knobs set it, for a C3. With the filter on, a red line marks its cutoff.
 */
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView(AdditiveProcessor& processor) : synth(processor) {}
    /** Takes the processor's newest note or the knobs; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;

private:
    AdditiveProcessor& synth;
    bool playing = false;
    float frequency = 0.0f;
    Settings settings;
    std::array<float, kPartials> levels {}, ratios {}, held {};
};

class SpectrumSection : public Section
{
public:
    explicit SpectrumSection(AdditiveProcessor& processor);
    void resized() override;
    void update() { view.update(); }

private:
    SpectrumView view;
};

class AdditiveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AdditiveEditor(AdditiveProcessor&);
    ~AdditiveEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    AdditiveProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    SpectrumSection spectrum;
    Section colour;
    Section filter, spectralEnvelope, amp, tone;
    Section env2, lfo1, lfo2;
    Section controllers, voice, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveEditor)
};
} // namespace tonwerkadd
