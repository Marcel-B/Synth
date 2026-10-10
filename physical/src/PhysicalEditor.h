#pragma once

#include "NightCity.h"
#include "PhysicalProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkphys
{
using tonwerkui::Section;

/**
 * The resonator as it rings: a string or tube drawn as the wave running along it, the point where it is plucked,
 * struck or bowed lit yellow; a struck body as its resonances, one bar each at its ratio to the note, as high as it
 * still rings. While nothing sounds it shows the body at rest, as the knobs set it.
 */
class ModelView : public juce::Component
{
public:
    explicit ModelView(PhysicalProcessor& processor) : synth(processor) {}
    /** Takes the processor's newest note; called at the editor's frame rate. */
    void update();
    void paint(juce::Graphics&) override;

private:
    PhysicalProcessor& synth;
    int resonator = -1;
    int knobResonator = 0;
    int knobExciter = 0;
    float knobPosition = 0.2f;
    float frequency = 0.0f;
    float position = 0.2f;
    int points = 0;
    std::array<float, Voice::kShape> shape {};
    int modes = 0;
    std::array<float, kModes> ratios {}, levels {};
    float scale = 0.0f;
};

class ResonatorSection : public Section
{
public:
    explicit ResonatorSection(PhysicalProcessor& processor);
    void resized() override;
    void update() { view.update(); }

private:
    ModelView view;
};

class PhysicalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhysicalEditor(PhysicalProcessor&);
    ~PhysicalEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePreset(int index);

    PhysicalProcessor& synth;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    ResonatorSection resonator;
    Section exciter;
    Section body, tone, filter, env2;
    Section lfo1, lfo2;
    Section controllers, voice, effects;
    juce::MidiKeyboardComponent keyboard;
    juce::String shownPreset;
    std::vector<int> menu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicalEditor)
};
} // namespace tonwerkphys
