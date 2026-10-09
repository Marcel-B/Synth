#include "WavetableEditor.h"

#include "WavetablePresets.h"

namespace tonwerkwave
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 150;
constexpr int kModLine = 28;
constexpr int kKeyboard = 56;

/** A menu filled with a choice parameter's entries. */
void fill(juce::ComboBox& box, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId)))
        box.addItemList(choice->choices, 1);
    else
        jassertfalse;
}
} // namespace

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
    g.setColour(screen);
    g.fillPath(glass);
    g.setColour(cyan.withAlpha(0.04f));
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
        g.setColour(cyan.withAlpha(0.18f));
        g.strokePath(trace((float) f), juce::PathStrokeType(1.0f));
    }
    const auto lit = trace(position);
    g.setColour(yellow.withAlpha(0.25f));
    g.strokePath(lit, juce::PathStrokeType(5.0f));
    g.setColour(yellow);
    g.strokePath(lit, juce::PathStrokeType(1.8f));

    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
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
              oscPrefix + "On", { kDisplay }),
      state(processor.state),
      prefix(oscPrefix)
{
    addAndMakeVisible(view);
    update(-1.0f);
}

void OscillatorSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
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
    number.setColour(juce::Label::textColourId, yellow);
    addAndMakeVisible(number);

    fill(source, state, base + "Source");
    fill(target, state, base + "Target");
    for (auto* box : { &source, &target })
        addAndMakeVisible(*box);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, base + "Source", source);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, base + "Target", target);

    amount.setSliderStyle(juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    amount.setColour(juce::Slider::textBoxTextColourId, cyan);
    amount.setColour(juce::Slider::textBoxOutlineColourId, cyan.withAlpha(0.25f));
    amount.setColour(juce::Slider::textBoxBackgroundColourId, screen);
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
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
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
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK WAVETABLE");
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

    layoutRow(area, { &oscA, &oscB });
    layoutRow(area, { &sub, &noise, &filter, &distortion, &master });
    layoutRow(area, { &env1, &env2, &env3, &macros });
    layoutRow(area, { &lfo1, &lfo2, &voice });
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
