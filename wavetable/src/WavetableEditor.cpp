#include "WavetableEditor.h"

#include "WavetablePresets.h"

namespace tonwerkwave
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

constexpr int kSlot = 60;
constexpr int kRow = 84;
constexpr int kTitle = 22;
constexpr int kPad = 6;
constexpr int kGap = 6;
constexpr int kMargin = 8;
constexpr int kHeader = 40;
constexpr int kDisplay = 150;
constexpr int kModLine = 28;
constexpr int kKeyboard = 56;

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
    g.fillRect(box.getX() + 1.0f, box.getY() + 5.0f, 3.0f, 12.0f);
    g.setFont(mono(12.0f));
    // juce::String upper-cases ASCII only, so titles with umlauts come in capitals already.
    g.drawText(title.toUpperCase(), area.withHeight(kTitle).reduced(10, 0), juce::Justification::centredLeft);
}

/** A menu filled with a choice parameter's entries. */
void fill(juce::ComboBox& box, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId)))
        box.addItemList(choice->choices, 1);
    else
        jassertfalse;
}
} // namespace

WaveLookAndFeel::WaveLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kCyan);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, kYellow);
    setColour(juce::Slider::rotarySliderOutlineColourId, kBorder);
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::Slider::trackColourId, kYellow);
    setColour(juce::Slider::backgroundColourId, kBorder);
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
    setColour(juce::TextButton::buttonColourId, kPanel.brighter(0.05f));
    setColour(juce::TextButton::textColourOffId, kYellow);
}

void WaveLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                       float start, float end, juce::Slider& slider)
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

void WaveLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float,
                                       float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    // A thin bar lit from the zero point, for the matrix's amounts that go either way.
    const auto bar = juce::Rectangle<float>((float) x, (float) y + (float) height / 2.0f - 3.0f, (float) width, 6.0f);
    g.setColour(kBorder);
    g.fillRect(bar);
    const float zero = (float) x + (float) slider.valueToProportionOfLength(0.0) * (float) width;
    g.setColour(position >= zero ? kYellow : kCyan);
    g.fillRect(juce::Rectangle<float>(std::min(zero, position), bar.getY(), std::abs(position - zero), bar.getHeight()));
    g.setColour(kText);
    g.fillRect(position - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
}

juce::Font WaveLookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::Font(juce::FontOptions(13.0f)); }

juce::Font WaveLookAndFeel::getPopupMenuFont() { return juce::Font(juce::FontOptions(14.0f)); }

Control::Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label)
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
    name.setBounds(area.removeFromTop(16));
    if (slider)
        slider->setBounds(area);
    else if (menu)
        menu->setBounds(area.withSizeKeepingCentre(area.getWidth() - 8, 24));
    else if (toggle)
        toggle->setBounds(area.withSizeKeepingCentre(52, 24));
}

Section::Section(juce::AudioProcessorValueTreeState& state, const juce::String& sectionTitle,
                 std::vector<ControlList> controlRows, const juce::String& powerId, int displayWidth)
    : title(sectionTitle), inset(displayWidth)
{
    for (const auto& row : controlRows)
    {
        rows.emplace_back();
        for (const auto& [id, label] : row)
        {
            rows.back().push_back(std::make_unique<Control>(state, id, utf8(label)));
            addAndMakeVisible(*rows.back().back());
        }
    }
    if (powerId.isNotEmpty())
    {
        power = std::make_unique<juce::ToggleButton>();
        power->setTooltip(utf8("Ein/Aus"));
        addAndMakeVisible(*power);
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, powerId, *power);
    }
}

int Section::preferredWidth() const
{
    int widest = 0;
    for (const auto& row : rows)
    {
        int slots = 0;
        for (const auto& control : row)
            slots += control->slots();
        widest = std::max(widest, slots);
    }
    return inset + widest * kSlot + 2 * kPad;
}

int Section::preferredHeight() const { return kTitle + (int) rows.size() * kRow + kPad; }

juce::Rectangle<int> Section::insetArea() const
{
    return { kPad, kTitle, inset - kPad, getHeight() - kTitle - kPad };
}

void Section::paint(juce::Graphics& g) { paintPanel(g, getLocalBounds(), title); }

void Section::resized()
{
    if (power)
        power->setBounds(getWidth() - 40, 0, 28, kTitle);
    auto area = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitle).withTrimmedLeft(inset);
    for (auto& row : rows)
    {
        auto line = area.removeFromTop(kRow);
        int slots = 0;
        for (const auto& control : row)
            slots += control->slots();
        // A panel wider than its controls keeps them together in the middle.
        line = line.withSizeKeepingCentre(slots * kSlot, line.getHeight());
        for (auto& control : row)
            control->setBounds(line.removeFromLeft(control->slots() * kSlot));
    }
}

void WavetableView::setTable(int index)
{
    if (index == table)
        return;
    table = index;
    const auto& wavetable = WavetableBank::instance().table(index);
    // 64 harmonics are plenty for a picture this size and show no ringing.
    constexpr int level = 3;
    frames.resize((std::size_t) kFrames * kPoints);
    for (int f = 0; f < kFrames; ++f)
        for (int i = 0; i < kPoints; ++i)
            frames[(std::size_t) (f * kPoints + i)] = wavetable.read(level, (float) f, (double) i / (kPoints - 1) * 0.9999);
    repaint();
}

void WavetableView::setPosition(float newPosition)
{
    if (std::abs(newPosition - position) < 0.01f)
        return;
    position = newPosition;
    repaint();
}

void WavetableView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    juce::Path glass;
    glass.addRoundedRectangle(area, 6.0f);
    g.setColour(kScreen);
    g.fillPath(glass);
    g.setColour(kCyan.withAlpha(0.04f));
    for (float y = area.getY(); y < area.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
    if (frames.empty())
        return;

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    // Frames recede up and to the right; the first in front, at the bottom left.
    const float waveWidth = area.getWidth() * 0.68f;
    const float amplitude = area.getHeight() * 0.16f;
    const float depthX = area.getWidth() * 0.24f;
    const float depthY = area.getHeight() * 0.42f;
    const float baseX = area.getX() + 6.0f;
    const float baseY = area.getBottom() - amplitude - 8.0f;
    auto trace = [&](float frame) {
        const int lower = std::min((int) frame, kFrames - 2);
        const float between = frame - (float) lower;
        const float depth = frame / (float) (kFrames - 1);
        juce::Path path;
        for (int i = 0; i < kPoints; ++i)
        {
            const float a = frames[(std::size_t) (lower * kPoints + i)];
            const float b = frames[(std::size_t) ((lower + 1) * kPoints + i)];
            const float x = baseX + depthX * depth + waveWidth * (float) i / (kPoints - 1);
            const float y = baseY - depthY * depth - amplitude * (a + (b - a) * between);
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    };
    for (int f = kFrames - 1; f >= 0; f -= 4)
    {
        g.setColour(kCyan.withAlpha(0.18f));
        g.strokePath(trace((float) f), juce::PathStrokeType(1.0f));
    }
    const auto lit = trace(position);
    g.setColour(kYellow.withAlpha(0.25f));
    g.strokePath(lit, juce::PathStrokeType(5.0f));
    g.setColour(kYellow);
    g.strokePath(lit, juce::PathStrokeType(1.8f));

    g.setFont(mono(10.0f));
    g.setColour(kCyan.withAlpha(0.8f));
    g.drawText(juce::String(juce::roundToInt(position / (kFrames - 1) * 100.0f)) + " %",
               area.reduced(6.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
}

OscillatorSection::OscillatorSection(WavetableProcessor& processor, const juce::String& heading,
                                     const juce::String& oscPrefix)
    : Section(processor.state, heading,
              { { { oscPrefix + "Table", "Wavetable" },
                  { oscPrefix + "Position", "Position" },
                  { oscPrefix + "WarpMode", "Warp" },
                  { oscPrefix + "Warp", "Menge" },
                  { oscPrefix + "Level", "Pegel" } },
                { { oscPrefix + "Octave", "Oktave" },
                  { oscPrefix + "Semi", "Halbton" },
                  { oscPrefix + "Fine", "Fein" },
                  { oscPrefix + "Unison", "Unison" },
                  { oscPrefix + "Detune", "Detune" },
                  { oscPrefix + "Blend", "Blend" },
                  { oscPrefix + "Width", "Breite" } } },
              oscPrefix + "On", kDisplay),
      state(processor.state),
      prefix(oscPrefix)
{
    addAndMakeVisible(view);
    update(-1.0f);
}

void OscillatorSection::resized()
{
    Section::resized();
    view.setBounds(insetArea().reduced(0, 4));
}

void OscillatorSection::update(float playedPosition)
{
    view.setTable(juce::roundToInt(state.getRawParameterValue(prefix + "Table")->load()));
    const float knob = state.getRawParameterValue(prefix + "Position")->load() * (float) (kFrames - 1);
    view.setPosition(playedPosition >= 0.0f ? playedPosition : knob);
}

ModRow::ModRow(juce::AudioProcessorValueTreeState& state, int slot)
{
    const juce::String base = "mod" + juce::String(slot);
    number.setText(juce::String(slot), juce::dontSendNotification);
    number.setFont(mono(12.0f));
    number.setColour(juce::Label::textColourId, kYellow);
    addAndMakeVisible(number);

    fill(source, state, base + "Source");
    fill(target, state, base + "Target");
    for (auto* box : { &source, &target })
        addAndMakeVisible(*box);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, base + "Source", source);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, base + "Target", target);

    amount.setSliderStyle(juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    amount.setColour(juce::Slider::textBoxTextColourId, kCyan);
    amount.setColour(juce::Slider::textBoxOutlineColourId, kCyan.withAlpha(0.25f));
    amount.setColour(juce::Slider::textBoxBackgroundColourId, kScreen);
    addAndMakeVisible(amount);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, base + "Amount", amount);

    bipolar.setButtonText(utf8("±"));
    bipolar.setTooltip(utf8("Bipolar: die Quelle schwingt um die Mitte statt nur nach oben"));
    addAndMakeVisible(bipolar);
    bipolarAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, base + "Bipolar", bipolar);
}

void ModRow::resized()
{
    auto area = getLocalBounds().reduced(0, 2);
    number.setBounds(area.removeFromLeft(18));
    source.setBounds(area.removeFromLeft(118));
    area.removeFromLeft(4);
    target.setBounds(area.removeFromLeft(118));
    area.removeFromLeft(6);
    bipolar.setBounds(area.removeFromRight(46));
    amount.setBounds(area);
}

MatrixSection::MatrixSection(juce::AudioProcessorValueTreeState& state)
{
    for (int i = 1; i <= kModSlots; ++i)
    {
        rows.push_back(std::make_unique<ModRow>(state, i));
        addAndMakeVisible(*rows.back());
    }
}

void MatrixSection::paint(juce::Graphics& g) { paintPanel(g, getLocalBounds(), "Modulation"); }

void MatrixSection::resized()
{
    auto area = getLocalBounds().reduced(10, 0).withTrimmedTop(kTitle);
    const int half = kModSlots / 2;
    auto left = area.removeFromLeft(area.getWidth() / 2 - 10);
    area.removeFromLeft(20);
    for (int i = 0; i < kModSlots; ++i)
        rows[(std::size_t) i]->setBounds((i < half ? left : area).removeFromTop(kModLine));
}

WavetableEditor::WavetableEditor(WavetableProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      oscA(p, "Oszillator A", "a"),
      oscB(p, "Oszillator B", "b"),
      sub(p.state, "Sub", { { { "subShape", "Form" }, { "subOctave", "Oktave" }, { "subLevel", "Pegel" },
                              { "subDirect", "Direkt" } } },
          "subOn"),
      noise(p.state, "Rauschen", { { { "noise", "Pegel" } } }),
      filter(p.state, "Filter",
             { { { "filterMode", "Typ" }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" },
                 { "filterDrive", "Drive" }, { "keytrack", "Keytrack" } } },
             "filterOn"),
      distortion(p.state, "Verzerrung", { { { "distMode", "Modus" }, { "distDrive", "Drive" }, { "distMix", "Mix" } } },
                 "distOn"),
      master(p.state, "Master", { { { "master", "Pegel" } } }),
      env1(p.state, utf8("HÜLLKURVE 1 · AMP"),
           { { { "env1Attack", "Attack" }, { "env1Decay", "Decay" }, { "env1Sustain", "Sustain" },
               { "env1Release", "Release" } } }),
      env2(p.state, utf8("HÜLLKURVE 2"),
           { { { "env2Attack", "Attack" }, { "env2Decay", "Decay" }, { "env2Sustain", "Sustain" },
               { "env2Release", "Release" } } }),
      env3(p.state, utf8("HÜLLKURVE 3"),
           { { { "env3Attack", "Attack" }, { "env3Decay", "Decay" }, { "env3Sustain", "Sustain" },
               { "env3Release", "Release" } } }),
      macros(p.state, "Makros",
             { { { "macro1", "Makro 1" }, { "macro2", "Makro 2" }, { "macro3", "Makro 3" }, { "macro4", "Makro 4" } } }),
      lfo1(p.state, "LFO 1",
           { { { "lfo1Shape", "Form" }, { "lfo1Sync", "Sync" }, { "lfo1Rate", "Rate" }, { "lfo1Division", "Raster" },
               { "lfo1Retrigger", "Neustart" } } }),
      lfo2(p.state, "LFO 2",
           { { { "lfo2Shape", "Form" }, { "lfo2Sync", "Sync" }, { "lfo2Rate", "Rate" }, { "lfo2Division", "Raster" },
               { "lfo2Retrigger", "Neustart" } } }),
      voice(p.state, "Stimmen", { { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" } } }),
      matrix(p.state),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);
    const auto& factory = factoryPresets();
    for (std::size_t i = 0; i < factory.size(); ++i)
        presets.addItem(utf8(factory[i].name), (int) i + 1);
    presets.setTextWhenNothingSelected(utf8("Preset"));
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            choosePreset(presets.getSelectedId() - 1);
    };
    previous.onClick = [this] { choosePreset(synth.getCurrentProgram() - 1); };
    next.onClick = [this] { choosePreset(synth.getCurrentProgram() + 1); };
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &oscA, &oscB, &sub, &noise,
                                                            &filter, &distortion, &master, &env1, &env2, &env3,
                                                            &macros, &lfo1, &lfo2, &voice, &matrix, &keyboard })
        addAndMakeVisible(*c);

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, kPanel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, kBorder);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, kCyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, kYellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setAvailableRange(24, 96);
    keyboard.setLowestVisibleKey(24);

    const int width = kMargin * 2 + oscA.preferredWidth() + kGap + oscB.preferredWidth();
    const int height = kMargin * 2 + kHeader + oscA.preferredHeight() + 3 * sub.preferredHeight()
                       + kTitle + 4 * kModLine + kPad + kKeyboard + 5 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

WavetableEditor::~WavetableEditor() { setLookAndFeel(nullptr); }

void WavetableEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void WavetableEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    // The title with the game's colour split: red and cyan ghosts either side of the yellow.
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    const juce::String title = "TONWERK WAVETABLE";
    g.setColour(kRed.withAlpha(0.7f));
    g.drawText(title, header.translated(-2, 0), juce::Justification::centredLeft);
    g.setColour(kCyan.withAlpha(0.6f));
    g.drawText(title, header.translated(2, 0), juce::Justification::centredLeft);
    g.setColour(kYellow);
    g.drawText(title, header, juce::Justification::centredLeft);
}

void WavetableEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));

    keyboard.setBounds(area.removeFromBottom(kKeyboard));
    keyboard.setKeyWidth((float) keyboard.getWidth() / 42.0f);
    area.removeFromBottom(kGap);

    // Each row's panels share out the width left over, so the rows end flush on the right.
    auto row = [&area](std::initializer_list<Section*> sections) {
        int height = 0, wanted = 0;
        for (auto* s : sections)
        {
            height = std::max(height, s->preferredHeight());
            wanted += s->preferredWidth();
        }
        auto line = area.removeFromTop(height);
        area.removeFromTop(kGap);
        const int spare = line.getWidth() - wanted - kGap * ((int) sections.size() - 1);
        const int share = std::max(0, spare / (int) sections.size());
        for (auto* s : sections)
        {
            s->setBounds(line.removeFromLeft(s->preferredWidth() + share));
            line.removeFromLeft(kGap);
        }
    };
    row({ &oscA, &oscB });
    row({ &sub, &noise, &filter, &distortion, &master });
    row({ &env1, &env2, &env3, &macros });
    row({ &lfo1, &lfo2, &voice });
    matrix.setBounds(area);
}

void WavetableEditor::timerCallback()
{
    oscA.update(synth.playedPosition[0].load());
    oscB.update(synth.playedPosition[1].load());
    if (synth.presetName() != shownPreset)
    {
        shownPreset = synth.presetName();
        presets.setSelectedId(0, juce::dontSendNotification);
        for (int i = 0; i < presets.getNumItems(); ++i)
            if (presets.getItemText(i) == shownPreset)
                presets.setSelectedId(i + 1, juce::dontSendNotification);
        if (presets.getSelectedId() == 0)
            presets.setText(shownPreset, juce::dontSendNotification);
    }
}
} // namespace tonwerkwave
