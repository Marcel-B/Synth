#pragma once

#include "BassProcessor.h"
#include "Compact.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkbass
{
using tonwerkui::Section;

/**
 * The filter's curve: the resonance peak riding the envelope, the accent and the LFO, which is what an acid line is.
 * The faint line is where the cutoff knob has it at rest.
 */
class ResponseView : public juce::Component, private juce::Timer
{
public:
    explicit ResponseView(BassProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The ladder's gain at `hz` with the cutoff at `cutoff`, in dB (the linear prototype; enough for a picture). */
    static float responseDb(float resonance, float cutoff, float hz);

private:
    void timerCallback() override { refresh(); }

    BassProcessor& processor;
    Settings shown;
    float cutoffNow = 500.0f;
    bool accented = false;
};

/** One cycle of what the oscillator sends into the filter: the wave with the sub under it. */
class WaveView : public juce::Component, private juce::Timer
{
public:
    explicit WaveView(BassProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();
    /** The oscillator's value at `phase` 0 to 1 of the sub's cycle, which holds two of the wave's. */
    static float valueAt(const Settings&, float phase);

private:
    void timerCallback() override { refresh(); }

    BassProcessor& processor;
    Settings shown;
    bool firstPaint = true;
};

/** A panel with a display over its knobs. */
template <typename View>
class DisplaySection : public Section
{
public:
    DisplaySection(BassProcessor& p, const juce::String& name, std::initializer_list<Row> rows, int height)
        : Section(p.state, name, rows, {}, { 0, height }), view(p)
    {
        addAndMakeVisible(view);
    }
    void resized() override
    {
        Section::resized();
        view.setBounds(displayArea().reduced(0, 4));
    }
    View view;
};

/** The scope over the panels, its caption naming the note and whether it is accented or sliding. */
class BassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassEditor(BassProcessor&);
    ~BassEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    BassProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    tonwerkui::ScopeView scope;
    DisplaySection<WaveView> oscillator;
    DisplaySection<ResponseView> filter;
    Section accent, envelope, output;
    Section lfo, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset, shownCaption;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEditor)
};

/**
 * The same controls cut into pages for a phone's screen (`tonwerkui::CompactEditor`), at most ten places a page so
 * the knobs come out as large as Tonwerk Analog's. The first holds what a hand is on while an acid line plays.
 */
std::vector<tonwerkui::CompactPage> bassPages();
} // namespace tonwerkbass
