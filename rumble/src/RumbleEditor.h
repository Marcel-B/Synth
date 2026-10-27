#pragma once

#include "EffectPresets.h"
#include "NightCity.h"
#include "RumbleProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkrumble
{
/**
 * The last two and a half seconds as they ran past: each kick (cyan), the rumble between them (yellow) and how far the
 * ducking held the rumble down (red, from the top). Shows at a glance whether the rumble breathes with the kick.
 */
class RumbleView : public juce::Component, private juce::Timer
{
public:
    explicit RumbleView(RumbleProcessor&);
    void paint(juce::Graphics&) override;
    void refresh();

private:
    void timerCallback() override { refresh(); }

    RumbleProcessor& processor;
    std::array<float, RumbleProcessor::kColumns> kick {}, rumble {}, duck {};
    float cutoff = 140.0f;
};

class RumbleEditor : public juce::AudioProcessorEditor
{
public:
    explicit RumbleEditor(RumbleProcessor&);
    ~RumbleEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    RumbleView view;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::PresetBar presets;
    tonwerkui::Section room, distortion, filter, ducking, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleEditor)
};
} // namespace tonwerkrumble
