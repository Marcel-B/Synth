#include "GlitchEditor.h"

namespace chromeglitch
{
namespace
{
const juce::Colour kBackground { 0xff17191c };
const juce::Colour kPanel { 0xff22262b };
const juce::Colour kBorder { 0xff343a41 };
const juce::Colour kText { 0xffe6e8eb };
const juce::Colour kMuted { 0xff9aa3ad };
const juce::Colour kAccent { 0xff22d3ee };

constexpr int kSlot = 92;
constexpr int kRowHeight = 128;
constexpr int kGap = 10;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }
} // namespace

GlitchLookAndFeel::GlitchLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kMuted);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kBorder);
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::ComboBox::backgroundColourId, kPanel.brighter(0.05f));
    setColour(juce::ComboBox::outlineColourId, kBorder);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::arrowColourId, kMuted);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.35f));
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, kMuted);
    setColour(juce::ToggleButton::textColourId, kText);
}

void GlitchLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);
    const float thickness = 4.0f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference(radius - 8.0f, angle);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.drawLine({ centre.getPointOnCircumference(radius * 0.25f, angle), tip }, 2.0f);
}

Control::Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label)
{
    name.setText(label, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(name);

    auto* parameter = state.getParameter(parameterId);
    jassert(parameter != nullptr);
    if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
    {
        toggle = std::make_unique<juce::ToggleButton>(utf8("an"));
        addAndMakeVisible(*toggle);
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, parameterId, *toggle);
    }
    else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter))
    {
        menu = std::make_unique<juce::ComboBox>();
        menu->addItemList(choice->choices, 1);
        addAndMakeVisible(*menu);
        menuAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, parameterId, *menu);
    }
    else
    {
        slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kSlot - 8, 18);
        addAndMakeVisible(*slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, *slider);
    }
}

void Control::resized()
{
    auto area = getLocalBounds();
    name.setBounds(area.removeFromTop(18));
    if (slider)
        slider->setBounds(area);
    else if (menu)
        menu->setBounds(area.withSizeKeepingCentre(area.getWidth() - 10, 26));
    else if (toggle)
        toggle->setBounds(area.withSizeKeepingCentre(60, 26));
}

Section::Section(juce::AudioProcessorValueTreeState& state, const juce::String& sectionTitle,
                 std::initializer_list<std::pair<const char*, const char*>> items)
    : title(sectionTitle)
{
    for (const auto& [id, label] : items)
    {
        controls.push_back(std::make_unique<Control>(state, id, utf8(label)));
        addAndMakeVisible(*controls.back());
    }
}

void Section::paint(juce::Graphics& g)
{
    g.setColour(kPanel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(kMuted);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), getLocalBounds().reduced(10, 6).removeFromTop(14), juce::Justification::left);
}

void Section::resized()
{
    auto area = getLocalBounds().reduced(4, 6);
    area.removeFromTop(18);
    for (auto& control : controls)
        control->setBounds(area.removeFromLeft(kSlot));
}

ChromeGlitchEditor::ChromeGlitchEditor(ChromeGlitchProcessor& p)
    : AudioProcessorEditor(p),
      processor(p),
      main(p.state, utf8("Gesamt"), { { "amount", "Stärke" }, { "freeze", "Einfrieren" }, { "mix", "Mix" } }),
      stutter(p.state, utf8("Stottern"),
              { { "stutterChance", "Chance" }, { "stutterLength", "Länge" }, { "repeats", "Wiederh." },
                { "accelerate", "Beschleun." } }),
      dropout(p.state, utf8("Aussetzer"), { { "dropoutChance", "Chance" }, { "dropoutLength", "Länge" } }),
      damage(p.state, utf8("Defekt"),
             { { "crush", "Bitcrusher" }, { "pitchChance", "Pitch-Chance" }, { "pitchRange", "Pitch-Bereich" } }),
      timing(p.state, utf8("Timing"), { { "sync", "Tempo-Sync" }, { "division", "Raster" }, { "chaos", "Chaos" } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* section : { &main, &stutter, &dropout, &damage, &timing })
        addAndMakeVisible(*section);
    // Two rows: 3 + 4 and 2 + 3 + 3 slots, plus the gaps.
    setSize(kGap * 4 + kSlot * 8 + 8 * 3, 56 + kRowHeight * 2 + kGap * 2);
    startTimerHz(30);
}

ChromeGlitchEditor::~ChromeGlitchEditor()
{
    setLookAndFeel(nullptr);
}

void ChromeGlitchEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    auto header = getLocalBounds().removeFromTop(48).reduced(kGap + 4, 0);
    g.setColour(kText);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("Chrome Glitch", header, juce::Justification::centredLeft);

    const auto lamp = header.removeFromRight(18).withSizeKeepingCentre(12, 12).toFloat();
    g.setColour(kBorder);
    g.fillEllipse(lamp);
    g.setColour(kAccent.withAlpha(light));
    g.fillEllipse(lamp);
}

void ChromeGlitchEditor::resized()
{
    auto area = getLocalBounds().reduced(kGap, 0);
    area.removeFromTop(48);
    auto place = [](juce::Rectangle<int>& row, Section& section) {
        section.setBounds(row.removeFromLeft(kSlot * section.count() + 8));
        row.removeFromLeft(kGap);
    };
    auto first = area.removeFromTop(kRowHeight);
    place(first, main);
    place(first, stutter);
    area.removeFromTop(kGap);
    auto second = area.removeFromTop(kRowHeight);
    place(second, dropout);
    place(second, damage);
    place(second, timing);
}

void ChromeGlitchEditor::timerCallback()
{
    const float next = processor.glitching ? 1.0f : light * 0.85f;
    if (std::abs(next - light) > 0.01f)
    {
        light = next;
        repaint(getLocalBounds().removeFromTop(48));
    }
}
} // namespace chromeglitch
