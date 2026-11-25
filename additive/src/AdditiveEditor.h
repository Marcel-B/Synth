#pragma once

#include "AdditiveProcessor.h"
#include "Compact.h"
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

/**
 * The same controls on pages for a phone's screen (`tonwerkui::CompactEditor`), at most ten places a page, the
 * partials' bars above the knobs that shape them (Haupt, Spektrum), refreshed by their own timer.
 */
std::vector<tonwerkui::CompactPage> additivePages(AdditiveProcessor& processor);

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
