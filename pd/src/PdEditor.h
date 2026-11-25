#pragma once

#include "Compact.h"
#include "NightCity.h"
#include "PdProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkpd
{
using tonwerkui::Section;

/**
 * Each line's wave as it is bent right now, line 1 cyan, line 2 yellow, two cycles so an alternating pair shows both
 * its waves; under them, faint, the plain cosine they are bent from. While nothing sounds it shows the waves at the
 * knobs' DCW.
 */
class PhaseView : public juce::Component
{
public:
    explicit PhaseView(PdProcessor& processor) : synth(processor) {}
    /** Takes the processor's newest note; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;

private:
    struct Line
    {
        bool on = false;
        Wave first = Wave::saw;
        bool alternate = false;
        Wave second = Wave::saw;
        float bend = 0.0f;
    };

    PdProcessor& synth;
    bool playing = false;
    float frequency = 0.0f;
    std::array<Line, kLines> lines {};
};

class LinesSection : public Section
{
public:
    explicit LinesSection(PdProcessor& processor);
    void resized() override;
    void update() { view.update(); }

private:
    PhaseView view;
};

/**
 * The same controls on pages for a phone's screen (`tonwerkui::CompactEditor`), at most ten places a page: the bent
 * waves above the knobs that bend them (Haupt, Linien), each line's wave and its envelopes behind a row of buttons
 * (Linie 1, Linie 2; one choice for both tabs), the two LFOs the same way.
 */
std::vector<tonwerkui::CompactPage> pdPages(PdProcessor& processor);

class PdEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PdEditor(PdProcessor&);
    ~PdEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    PdProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    LinesSection lines;
    Section line1, line2;
    Section lfo1, lfo2, controllers;
    Section voice, chorus, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PdEditor)
};
} // namespace tonwerkpd
