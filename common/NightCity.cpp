#include "NightCity.h"

namespace tonwerkui
{
using namespace colours;

juce::Font mono(float size)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
}

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
    g.setColour(panel);
    g.fillPath(shape);
    g.setColour(cyan.withAlpha(0.3f));
    g.strokePath(shape, juce::PathStrokeType(1.0f));

    g.setColour(yellow);
    g.fillRect(box.getX() + 1.0f, box.getY() + 5.0f, 3.0f, 12.0f);
    g.setFont(mono(12.0f));
    // juce::String upper-cases ASCII only, so titles with umlauts come in capitals already.
    g.drawText(title.toUpperCase(), area.withHeight(kTitle).reduced(10, 0), juce::Justification::centredLeft);
}

juce::Path paintScreen(juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::Path glass;
    glass.addRoundedRectangle(area, 6.0f);
    g.setColour(screen);
    g.fillPath(glass);
    g.setColour(cyan.withAlpha(0.04f));
    for (float y = area.getY(); y < area.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
    return glass;
}

void paintTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.setColour(red.withAlpha(0.7f));
    g.drawText(title, area.translated(-2, 0), juce::Justification::centredLeft);
    g.setColour(cyan.withAlpha(0.6f));
    g.drawText(title, area.translated(2, 0), juce::Justification::centredLeft);
    g.setColour(yellow);
    g.drawText(title, area, juce::Justification::centredLeft);
}

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::Slider::textBoxTextColourId, cyan);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, yellow);
    setColour(juce::Slider::rotarySliderOutlineColourId, border);
    setColour(juce::Slider::thumbColourId, text);
    setColour(juce::Slider::trackColourId, yellow);
    setColour(juce::Slider::backgroundColourId, border);
    setColour(juce::ComboBox::backgroundColourId, panel.brighter(0.05f));
    setColour(juce::ComboBox::outlineColourId, cyan.withAlpha(0.4f));
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, yellow);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, cyan);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, yellow.withAlpha(0.35f));
    setColour(juce::ToggleButton::tickColourId, yellow);
    setColour(juce::ToggleButton::tickDisabledColourId, cyan.withAlpha(0.6f));
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::TextButton::buttonColourId, panel.brighter(0.05f));
    setColour(juce::TextButton::buttonOnColourId, yellow);
    setColour(juce::TextButton::textColourOffId, yellow);
    setColour(juce::TextButton::textColourOnId, background);
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, cyan.withAlpha(0.4f));
    setColour(juce::TextEditor::backgroundColourId, screen);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, border);
    setColour(juce::TextEditor::focusedOutlineColourId, cyan);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float start,
                                   float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);
    const float thickness = 3.5f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knobs that go both ways (fine tune, semitones) light from the middle.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) slider.valueToProportionOfLength(0.0) * (end - start) : start;
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference(radius - 6.0f, angle);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.drawLine({ centre.getPointOnCircumference(radius * 0.25f, angle), tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float, float,
                                   juce::Slider::SliderStyle, juce::Slider& slider)
{
    // A thin bar lit from the zero point, for amounts that go either way.
    const auto bar = juce::Rectangle<float>((float) x, (float) y + (float) height / 2.0f - 3.0f, (float) width, 6.0f);
    g.setColour(border);
    g.fillRect(bar);
    const float zero = (float) x + (float) slider.valueToProportionOfLength(0.0) * (float) width;
    g.setColour(position >= zero ? yellow : cyan);
    g.fillRect(juce::Rectangle<float>(std::min(zero, position), bar.getY(), std::abs(position - zero), bar.getHeight()));
    g.setColour(text);
    g.fillRect(position - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::Font(juce::FontOptions(13.0f)); }

juce::Font LookAndFeel::getPopupMenuFont() { return juce::Font(juce::FontOptions(14.0f)); }

Control::Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label,
                 int slotCount)
{
    name.setText(label, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setFont(juce::FontOptions(12.0f));
    name.setMinimumHorizontalScale(0.7f);
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
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kSlot - 4, 16);
        // Set on the slider itself: its value box is made before the editor's look and feel is in place.
        slider->setColour(juce::Slider::textBoxTextColourId, cyan);
        slider->setColour(juce::Slider::textBoxOutlineColourId, cyan.withAlpha(0.25f));
        slider->setColour(juce::Slider::textBoxBackgroundColourId, screen);
        addAndMakeVisible(*slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, *slider);
    }
    width = slotCount > 0 ? slotCount : (menu != nullptr ? 2 : 1);
}

void Control::resized()
{
    auto area = getLocalBounds();
    name.setBounds(area.removeFromTop(16));
    if (slider)
        slider->setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), kSlot), area.getHeight()));
    else if (menu)
        menu->setBounds(area.withSizeKeepingCentre(area.getWidth() - 8, 24));
    else if (toggle)
        toggle->setBounds(area.withSizeKeepingCentre(52, 24));
}

Section::Section(juce::AudioProcessorValueTreeState& state, const juce::String& sectionTitle, std::vector<Row> rows,
                 const juce::String& powerId, Display displayRoom)
    : title(sectionTitle), display(displayRoom), items(std::move(rows))
{
    juce::StringArray switchIds;
    for (const auto& row : items)
    {
        controls.emplace_back();
        for (const auto& item : row)
        {
            controls.back().push_back(std::make_unique<Control>(state, item.id, item.label, item.slots));
            addAndMakeVisible(*controls.back().back());
            if (item.when.isNotEmpty())
                switchIds.addIfNotAlreadyThere(item.when);
        }
    }
    for (const auto& id : switchIds)
    {
        auto* parameter = state.getParameter(id);
        jassert(parameter != nullptr);
        switches.push_back(std::make_unique<juce::ParameterAttachment>(*parameter, [this, id](float value) {
            for (std::size_t r = 0; r < items.size(); ++r)
                for (std::size_t i = 0; i < items[r].size(); ++i)
                    if (items[r][i].when == id)
                        controls[r][i]->setVisible((value > 0.5f) == items[r][i].whenOn);
        }));
        switches.back()->sendInitialUpdate();
    }
    if (powerId.isNotEmpty())
    {
        power = std::make_unique<juce::ToggleButton>();
        power->setTooltip(utf8("Ein/Aus"));
        addAndMakeVisible(*power);
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, powerId, *power);
    }
}

std::vector<std::pair<int, int>> Section::places(std::size_t row) const
{
    std::vector<std::pair<int, int>> result;
    const auto& list = items[row];
    int at = 0;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const int width = controls[row][i]->slots();
        const bool sharesPlace = i > 0 && list[i].when.isNotEmpty() && list[i].when == list[i - 1].when
                                 && list[i].whenOn != list[i - 1].whenOn;
        if (sharesPlace)
        {
            auto& previous = result.back();
            const int wider = std::max(previous.second, width);
            at += wider - previous.second;
            previous.second = wider;
            result.push_back(previous);
        }
        else
        {
            result.push_back({ at, width });
            at += width;
        }
    }
    return result;
}

int Section::rowSlots(std::size_t row) const
{
    int slots = 0;
    for (const auto& [at, width] : places(row))
        slots = std::max(slots, at + width);
    return slots;
}

int Section::preferredWidth() const
{
    int widest = 0;
    for (std::size_t r = 0; r < items.size(); ++r)
        widest = std::max(widest, rowSlots(r));
    return display.inset + widest * kSlot + 2 * kPad;
}

int Section::preferredHeight() const { return kTitle + display.top + (int) items.size() * kRow + kPad; }

juce::Rectangle<int> Section::displayArea() const
{
    if (display.top > 0)
        return { kPad, kTitle, getWidth() - 2 * kPad, display.top };
    // A panel wider than it needs gives the room to its display.
    const int inset = getWidth() - (preferredWidth() - display.inset);
    return { kPad, kTitle, inset - kPad, getHeight() - kTitle - kPad };
}

void Section::paint(juce::Graphics& g) { paintPanel(g, getLocalBounds(), title); }

void Section::resized()
{
    if (power)
        power->setBounds(getWidth() - 40, 0, 28, kTitle);
    auto area = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitle + display.top);
    if (display.inset > 0)
        area = area.withTrimmedLeft(displayArea().getRight());
    for (std::size_t r = 0; r < items.size(); ++r)
    {
        auto line = area.removeFromTop(kRow);
        // A panel wider than its controls keeps them together in the middle.
        line = line.withSizeKeepingCentre(rowSlots(r) * kSlot, line.getHeight());
        const auto where = places(r);
        for (std::size_t i = 0; i < controls[r].size(); ++i)
            controls[r][i]->setBounds(line.getX() + where[i].first * kSlot, line.getY(), where[i].second * kSlot,
                                      line.getHeight());
    }
}

void layoutRow(juce::Rectangle<int>& area, std::initializer_list<Section*> sections)
{
    int height = 0, wanted = 0, displays = 0;
    for (auto* s : sections)
    {
        height = std::max(height, s->preferredHeight());
        wanted += s->preferredWidth();
        displays += s->hasDisplay() ? 1 : 0;
    }
    auto line = area.removeFromTop(height);
    area.removeFromTop(kGap);
    const int spare = std::max(0, line.getWidth() - wanted - kGap * ((int) sections.size() - 1));
    const int sharing = displays > 0 ? displays : (int) sections.size();
    int left = spare;
    int sharers = sharing;
    for (auto* s : sections)
    {
        int extra = 0;
        if (displays == 0 || s->hasDisplay())
        {
            // The last one to share takes what rounding left, so the row ends flush.
            extra = --sharers == 0 ? left : spare / sharing;
            left -= extra;
        }
        s->setBounds(line.removeFromLeft(s->preferredWidth() + extra));
        line.removeFromLeft(kGap);
    }
}

ScopeView::ScopeView(const ScopeBuffer& buffer, const ScopeBuffer* second, std::function<int()> secondLag)
    : source(buffer), behind(second), lag(std::move(secondLag))
{
    startTimerHz(30);
}

void ScopeView::timerCallback()
{
    if (isShowing())
        refresh();
}

void ScopeView::refresh()
{
    source.read(samples.data(), (int) samples.size());
    if (behind != nullptr)
        behind->read(behindSamples.data(), (int) behindSamples.size(), lag ? lag() : 0);
    repaint();
}

void ScopeView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(6.0f);
    g.setColour(cyan.withAlpha(0.15f));
    g.drawHorizontalLine(juce::roundToInt(inner.getCentreY()), inner.getX(), inner.getRight());
    if (caption.isNotEmpty())
    {
        g.setFont(mono(10.0f));
        g.setColour(cyan.withAlpha(0.8f));
        g.drawText(caption, area.reduced(8.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
    }
    if (legend.first.isNotEmpty())
    {
        g.setFont(mono(10.0f));
        auto corner = area.reduced(8.0f, 4.0f).toNearestInt().removeFromTop(14);
        g.setColour(yellow);
        g.drawText(legend.first, corner.removeFromRight(70), juce::Justification::centredRight);
        g.setColour(cyan.withAlpha(0.8f));
        g.drawText(legend.second, corner.removeFromRight(70), juce::Justification::centredRight);
    }

    // Half the samples are shown; the trigger is looked for in the first half, so the view always has enough after it.
    const int length = (int) samples.size();
    const int shown = length / 2;
    int start = 0;
    for (int i = 1; i < length - shown; ++i)
    {
        if (samples[(std::size_t) i - 1] <= 0.0f && samples[(std::size_t) i] > 0.0f)
        {
            start = i;
            break;
        }
    }
    const int width = juce::roundToInt(inner.getWidth());
    auto trace = [&](const std::array<float, 2048>& data) {
        float peak = 0.0f;
        for (float sample : data)
            peak = std::max(peak, std::abs(sample));
        juce::Path path;
        // Quiet sounds are scaled up to be seen, but not a hum of noise to full height.
        if (peak <= 0.02f)
            return path;
        const float scale = 0.9f / peak;
        for (int x = 0; x <= width; ++x)
        {
            const float sample = data[(std::size_t) (start + std::min(shown - 1, x * shown / std::max(1, width)))] * scale;
            const float y = inner.getCentreY() - sample * inner.getHeight() / 2.0f;
            if (x == 0)
                path.startNewSubPath(inner.getX(), y);
            else
                path.lineTo(inner.getX() + (float) x, y);
        }
        return path;
    };
    if (behind != nullptr)
    {
        g.setColour(cyan.withAlpha(0.45f));
        g.strokePath(trace(behindSamples), juce::PathStrokeType(1.2f));
    }
    const auto lit = trace(samples);
    g.setColour(yellow.withAlpha(0.25f));
    g.strokePath(lit, juce::PathStrokeType(5.0f));
    g.setColour(yellow);
    g.strokePath(lit, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace tonwerkui
