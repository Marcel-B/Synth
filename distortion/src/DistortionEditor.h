#pragma once

#include "DistortionProcessor.h"
#include "NightCity.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonwerkdistortion
{
/**
 * Night City, as Tonwerk Wavetable: the pedal's three knobs in one panel, the input gain beside it, and above them an
 * oscilloscope with the clipped output over the input, so DIST is seen squaring the wave.
 */
class DistortionEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 440;

    explicit DistortionEditor(DistortionProcessor&);
    ~DistortionEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    tonwerkui::LookAndFeel lookAndFeel;
    tonwerkui::ScopeView scope;
    tonwerkui::Section pedal, input;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionEditor)
};
} // namespace tonwerkdistortion
