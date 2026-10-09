#include "GlitchEditor.h"

namespace chromeglitch
{
namespace
{
// Cyberpunk 2077's interface: its yellow, the cyan of the HUD, the red of a warning, on near black.
const juce::Colour kBackground { 0xff0a0b10 };
const juce::Colour kPanel { 0xff14151c };
const juce::Colour kScreen { 0xff04070a };
const juce::Colour kBorder { 0xff2a2d3a };
const juce::Colour kText { 0xffe8e6d9 };
const juce::Colour kYellow { 0xfffcee0a };
const juce::Colour kCyan { 0xff00f0ff };
const juce::Colour kRed { 0xffff003c };

constexpr int kSlot = 92;
constexpr int kRowHeight = 128;
constexpr int kGap = 10;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

juce::Font mono(float size)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
}

/** A panel with its top right corner cut off and a thin cyan edge, the shape of the game's HUD boxes. */
void paintPanel(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto box = area.toFloat().reduced(0.5f);
    const float cut = 10.0f;
    juce::Path shape;
    shape.startNewSubPath(box.getX(), box.getY());
    shape.lineTo(box.getRight() - cut, box.getY());
    shape.lineTo(box.getRight(), box.getY() + cut);
    shape.lineTo(box.getRight(), box.getBottom());
    shape.lineTo(box.getX(), box.getBottom());
    shape.closeSubPath();
    g.setColour(kPanel);
    g.fillPath(shape);
    g.setColour(kCyan.withAlpha(0.3f));
    g.strokePath(shape, juce::PathStrokeType(1.0f));

    g.setColour(kYellow);
    g.fillRect(box.getX() + 1.0f, box.getY() + 7.0f, 3.0f, 12.0f);
    g.setFont(mono(12.0f));
    g.drawText(title.toUpperCase(), area.reduced(10, 6).removeFromTop(14), juce::Justification::left);
}
} // namespace

GlitchLookAndFeel::GlitchLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kCyan);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, kYellow);
    setColour(juce::Slider::rotarySliderOutlineColourId, kBorder);
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::ComboBox::backgroundColourId, kPanel.brighter(0.05f));
    setColour(juce::ComboBox::outlineColourId, kCyan.withAlpha(0.4f));
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::arrowColourId, kYellow);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kYellow.withAlpha(0.35f));
    setColour(juce::ToggleButton::tickColourId, kYellow);
    setColour(juce::ToggleButton::tickDisabledColourId, kCyan.withAlpha(0.6f));
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
        // Set on the slider itself: its value box is made before the editor's look and feel is in place.
        slider->setColour(juce::Slider::textBoxTextColourId, kCyan);
        slider->setColour(juce::Slider::textBoxOutlineColourId, kCyan.withAlpha(0.25f));
        slider->setColour(juce::Slider::textBoxBackgroundColourId, kScreen);
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
    paintPanel(g, getLocalBounds(), title);
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
    addAndMakeVisible(monitor);
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
    const auto header = getLocalBounds().removeFromTop(48).reduced(kGap + 4, 0);
    // The title with the game's colour split: red and cyan ghosts either side of the yellow.
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.setColour(kRed.withAlpha(0.7f));
    g.drawText("CHROME GLITCH", header.translated(-2, 0), juce::Justification::centredLeft);
    g.setColour(kCyan.withAlpha(0.6f));
    g.drawText("CHROME GLITCH", header.translated(2, 0), juce::Justification::centredLeft);
    g.setColour(kYellow);
    g.drawText("CHROME GLITCH", header, juce::Justification::centredLeft);
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
    monitor.setBounds(first);
    area.removeFromTop(kGap);
    auto second = area.removeFromTop(kRowHeight);
    place(second, dropout);
    place(second, damage);
    place(second, timing);
}

void ChromeGlitchEditor::timerCallback()
{
    monitor.update(processor.glitching, processor.glitchStarted.exchange(false), processor.lastEvent);
}

void GlitchMonitor::update(bool glitching, bool started, int latest)
{
    ++frame;
    if (glitching || started)
    {
        intensity = 1.0f;
        event = latest;
    }
    else
    {
        intensity = intensity > 0.02f ? intensity * 0.8f : 0.0f;
    }
    repaint();
}

void GlitchMonitor::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds(), "Monitor");
    const auto screen = getLocalBounds().reduced(8).withTrimmedTop(18);
    const auto area = screen.toFloat();
    g.setColour(kScreen);
    g.fillRect(area);

    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(screen);

        // Scanlines and a slow bright band rolling down, as on an old screen.
        g.setColour(kCyan.withAlpha(0.05f));
        for (int y = screen.getY(); y < screen.getBottom(); y += 3)
            g.drawHorizontalLine(y, area.getX(), area.getRight());
        const float band = area.getY() + (float) ((frame * 2) % (screen.getHeight() + 20)) - 10.0f;
        g.setColour(kCyan.withAlpha(0.06f));
        g.fillRect(area.getX(), band, area.getWidth(), 10.0f);

        g.setFont(mono(10.0f));
        if (intensity <= 0.0f)
        {
            const bool blink = (frame / 15) % 2 == 0;
            g.setColour(kCyan.withAlpha(0.7f));
            g.drawText(blink ? "CHROME OK_" : "CHROME OK ", screen, juce::Justification::centred);
        }
        else
        {
            const auto torn = (int) (3 + intensity * 8);
            const juce::Colour colours[] = { kYellow, kCyan, kRed };
            for (int i = 0; i < torn; ++i)
            {
                const float y = area.getY() + random.nextFloat() * area.getHeight();
                const float height = 1.0f + random.nextFloat() * 8.0f;
                const float shift = (random.nextFloat() - 0.5f) * 30.0f * intensity;
                g.setColour(colours[random.nextInt(3)].withAlpha((0.25f + 0.6f * random.nextFloat()) * intensity));
                g.fillRect(area.getX() + shift, y, area.getWidth(), height);
            }
            for (int i = 0; i < (int) (80 * intensity); ++i)
            {
                g.setColour((random.nextBool() ? kText : kCyan).withAlpha(intensity));
                g.fillRect(area.getX() + random.nextFloat() * area.getWidth(),
                           area.getY() + random.nextFloat() * area.getHeight(), 1.5f, 1.5f);
            }
            if (event == (int) GlitchEngine::Event::dropout)
            {
                g.setColour(juce::Colours::black.withAlpha(0.5f * intensity));
                g.fillRect(area);
            }

            // The label flickers: some frames it drops out, and its colours split further the stronger the glitch.
            if (random.nextFloat() > 0.3f * intensity)
            {
                const char* label = event == (int) GlitchEngine::Event::dropout  ? "KEIN SIGNAL"
                                    : event == (int) GlitchEngine::Event::freeze ? "EINGEFROREN"
                                                                                 : "STÖRUNG";
                const auto text = utf8(label);
                const int split = 1 + (int) (3.0f * intensity * random.nextFloat());
                g.setColour(kRed.withAlpha(intensity));
                g.drawText(text, screen.translated(-split, 0), juce::Justification::centred);
                g.setColour(kCyan.withAlpha(intensity));
                g.drawText(text, screen.translated(split, 0), juce::Justification::centred);
                g.setColour(kYellow.withAlpha(std::max(0.4f, intensity)));
                g.drawText(text, screen, juce::Justification::centred);
            }
        }
    }

    g.setColour((intensity > 0.0f && frame % 2 == 0 ? kRed : kCyan).withAlpha(0.6f));
    g.drawRect(area, 1.0f);
}
} // namespace chromeglitch
