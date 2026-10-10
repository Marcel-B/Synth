#pragma once

#include "GranularProcessor.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkgrain
{
using tonwerkui::Section;

/**
 * The source drawn as a waveform, the band the grains are cut from lit around its centre, and the newest note's grains
 * as sparks where they read, brightest at the middle of their window. A wandering scan is seen moving.
 */
class GrainView : public juce::Component
{
public:
    explicit GrainView(GranularProcessor& processor) : synth(processor) {}
    /** Takes the processor's newest grains; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;

private:
    GranularProcessor& synth;
    std::shared_ptr<const GrainSource> source;
    float centre = -1.0f, spray = 0.0f, knob = 0.0f, knobSpray = 0.0f;
    std::array<float, kMaxGrains> places {}, phases {};
};

class SourceSection : public Section, public juce::FileDragAndDropTarget
{
public:
    explicit SourceSection(GranularProcessor& processor);
    void resized() override;
    void update() { view.update(); }

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void choose();

    GranularProcessor& synth;
    GrainView view;
    juce::TextButton load { juce::String::fromUTF8("Datei …") };
    std::unique_ptr<juce::FileChooser> chooser;
};

class GranularEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 1204;

    explicit GranularEditor(GranularProcessor&);
    ~GranularEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    GranularProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    SourceSection source;
    Section grains;
    Section filter, env1, env2;
    Section lfo1, lfo2;
    Section controllers, voice, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularEditor)
};
} // namespace tonwerkgrain
