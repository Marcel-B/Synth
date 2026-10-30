#pragma once

#include "NightCity.h"
#include "SamplerProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerksampler
{
using tonwerkui::Section;

/**
 * The sample as a waveform: the region played lit, the loop in yellow, the slices as numbered lines with their keys,
 * and a line for each sounding voice where it reads.
 */
class WaveView : public juce::Component
{
public:
    explicit WaveView(SamplerProcessor& processor) : synth(processor) {}
    /** Takes the processor's sample, knobs and play heads; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;
    /** The slices as they are drawn, for the tests. */
    const SliceMap& shownSlices() const { return slices; }

private:
    SamplerProcessor& synth;
    std::shared_ptr<const Sample> sample;
    Settings settings;
    SliceMap slices;
    std::array<float, 8> heads {};
};

class SampleSection : public Section, public juce::FileDragAndDropTarget
{
public:
    explicit SampleSection(SamplerProcessor& processor);
    void resized() override;
    void update() { view.update(); }

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

    WaveView view;

private:
    void choose();

    SamplerProcessor& synth;
    juce::TextButton load { tonwerkui::tr("Datei …") };
    std::unique_ptr<juce::FileChooser> chooser;
};

class SamplerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SamplerEditor(SamplerProcessor&);
    ~SamplerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SampleSection sample;

private:
    void timerCallback() override;
    void choosePreset(int index);

    SamplerProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    Section loop, slices, voice;
    Section filter, amp;
    Section lfo, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerEditor)
};
} // namespace tonwerksampler
