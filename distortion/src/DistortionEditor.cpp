#include "DistortionEditor.h"

namespace tonwerkdistortion
{
namespace
{
const juce::Colour kOrange { 0xfff26a1b };
const juce::Colour kField { 0xff1b1b1d };
const juce::Colour kKnob { 0xff0d0d0e };
const juce::Colour kText { 0xfff2f2f2 };
const juce::Colour kMuted { 0xffa8a8ad };

constexpr int kWidth = 420;
constexpr int kHeight = 300;
constexpr int kFieldHeight = 170;
constexpr int kKnobWidth = 110;
} // namespace

PedalLookAndFeel::PedalLookAndFeel()
{
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kMuted);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void PedalLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                        float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);

    g.setColour(kKnob);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre).reduced(1.0f), 1.5f);
    g.setColour(kText);
    g.drawLine({ centre.getPointOnCircumference(radius * 0.35f, angle),
                 centre.getPointOnCircumference(radius - 4.0f, angle) },
               3.0f);
}

Knob::Knob(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label)
    : attachment(state, parameterId, slider)
{
    name.setText(label, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(name);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kKnobWidth - 20, 18);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);
}

void Knob::setTextColour(juce::Colour colour)
{
    name.setColour(juce::Label::textColourId, colour);
    slider.setColour(juce::Slider::textBoxTextColourId, colour);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    name.setBounds(area.removeFromTop(18));
    slider.setBounds(area);
}

DistortionEditor::DistortionEditor(DistortionProcessor& p)
    : AudioProcessorEditor(p),
      level(p.state, "level", "LEVEL"),
      tone(p.state, "tone", "TONE"),
      distortion(p.state, "distortion", "DIST"),
      input(p.state, "input", "EINGANG")
{
    setLookAndFeel(&lookAndFeel);
    for (auto* knob : { &level, &tone, &distortion, &input })
        addAndMakeVisible(*knob);
    input.setTextColour(kField);
    setSize(kWidth, kHeight);
}

DistortionEditor::~DistortionEditor()
{
    setLookAndFeel(nullptr);
}

void DistortionEditor::paint(juce::Graphics& g)
{
    g.fillAll(kOrange);
    g.setColour(kField);
    g.fillRect(getLocalBounds().removeFromTop(kFieldHeight));

    auto label = getLocalBounds().withTrimmedTop(kFieldHeight).reduced(20, 14);
    g.setColour(kField);
    g.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    g.drawText("Distortion", label.removeFromTop(36), juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText("Tonwerk", label.removeFromTop(18), juce::Justification::centredLeft);
}

void DistortionEditor::resized()
{
    auto field = getLocalBounds().removeFromTop(kFieldHeight).reduced(15, 12);
    const int gap = (field.getWidth() - 3 * kKnobWidth) / 2;
    level.setBounds(field.removeFromLeft(kKnobWidth));
    field.removeFromLeft(gap);
    tone.setBounds(field.removeFromLeft(kKnobWidth));
    field.removeFromLeft(gap);
    distortion.setBounds(field.removeFromLeft(kKnobWidth));

    // The input gain is not on the pedal; it sits small on the orange, where a guitar's cable would plug in.
    input.setBounds(getLocalBounds().withTrimmedTop(kFieldHeight).reduced(12).removeFromRight(kKnobWidth - 10));
}
} // namespace tonwerkdistortion
