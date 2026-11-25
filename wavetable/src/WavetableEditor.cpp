#include "WavetableEditor.h"

#include "PresetCategories.h"
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
        for (int i = 0; i < choice->choices.size(); ++i)
            box.addItem(retranslate(choice->choices[i]), i + 1);
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
    bipolar.setTooltip(tr("Bipolar: die Quelle schwingt um die Mitte statt nur nach oben"));
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

namespace
{
/** A page's drawing of one oscillator's table, refreshed while it shows; the compact editor has no timer of its own. */
struct LiveWavetableView : WavetableView, private juce::Timer
{
    LiveWavetableView(WavetableProcessor& p, int which) : synth(p), osc(which), prefix(which == 0 ? "a" : "b")
    {
        refresh();
        startTimerHz(30);
    }
    void refresh()
    {
        setTable(juce::roundToInt(synth.state.getRawParameterValue(prefix + "Table")->load()));
        const float played = synth.playedPosition[(std::size_t) osc].load();
        const float knob = synth.state.getRawParameterValue(prefix + "Position")->load() * (float) (kFrames - 1);
        setPosition(played >= 0.0f ? played : knob);
    }
    void timerCallback() override
    {
        if (isShowing())
            refresh();
    }

    WavetableProcessor& synth;
    int osc;
    juce::String prefix;
};
} // namespace

std::vector<tonwerkui::CompactPage> wavetablePages(WavetableProcessor& processor)
{
    using Row = Section::Row;
    tonwerkui::CompactPage oscillators { "Oszillator", {} }, tuning { "Stimmung", {} };
    for (int osc = 0; osc < 2; ++osc)
    {
        const juce::String p = osc == 0 ? "a" : "b";
        const auto name = juce::String(osc == 0 ? "A" : "B");
        oscillators.choices.push_back({ name,
                                        { { "Oszillator " + name, Row { { p + "On", "An" },
                                                                        { p + "Table", "Wavetable", 2 },
                                                                        { p + "Position", "Position" },
                                                                        { p + "WarpMode", "Warp" },
                                                                        { p + "Warp", "Menge" },
                                                                        { p + "Level", "Pegel" } } } },
                                        [&processor, osc] { return std::make_unique<LiveWavetableView>(processor, osc); },
                                        84 });
        tuning.choices.push_back({ name,
                                   { { "Oszillator " + name, Row { { p + "Octave", "Oktave" },
                                                                   { p + "Semi", "Halbton" },
                                                                   { p + "Fine", "Fein" },
                                                                   { p + "Unison", "Unison" },
                                                                   { p + "Detune", "Detune" },
                                                                   { p + "Blend", "Blend" },
                                                                   { p + "Width", "Breite" } } } } });
    }
    oscillators.choiceGroup = tuning.choiceGroup = "osc";

    // The three envelopes and the two LFOs behind one tab: each has only four or five controls.
    tonwerkui::CompactPage motion { "Bewegung", {} };
    for (int i = 1; i <= 3; ++i)
    {
        const juce::String p = "env" + juce::String(i);
        motion.choices.push_back({ utf8("Hüllk. ") + juce::String(i),
                                   { { i == 1 ? utf8("Hüllkurve 1 · Amp") : utf8("Hüllkurve ") + juce::String(i),
                                       Row { { p + "Attack", "Attack" },
                                             { p + "Decay", "Decay" },
                                             { p + "Sustain", "Sustain" },
                                             { p + "Release", "Release" } } } } });
    }
    for (int i = 1; i <= 2; ++i)
    {
        const juce::String p = "lfo" + juce::String(i);
        motion.choices.push_back({ "LFO " + juce::String(i),
                                   { { "LFO " + juce::String(i), Row { { p + "Shape", "Form" },
                                                                       { p + "Sync", "Sync" },
                                                                       { p + "Rate", "Rate", 0, p + "Sync", false },
                                                                       { p + "Division", "Raster", 0, p + "Sync", true },
                                                                       { p + "Retrigger", "Neustart" } } } } });
    }

    // The matrix one slot at a time: two side by side left the knobs smaller than on the other pages.
    tonwerkui::CompactPage matrix { "Matrix", {} };
    for (int slot = 1; slot <= kModSlots; ++slot)
    {
        const juce::String p = "mod" + juce::String(slot);
        matrix.choices.push_back({ juce::String(slot),
                                   { { "Slot " + juce::String(slot), Row { { p + "Source", "Quelle", 2 },
                                                                           { p + "Target", "Ziel", 2 },
                                                                           { p + "Amount", "Menge" },
                                                                           { p + "Bipolar", "Bipolar" } } } } });
    }

    return {
        { "Haupt",
          { { "Haupt", Row { { "aPosition", "Position A" },
                             { "bPosition", "Position B" },
                             { "cutoff", "Cutoff" },
                             { "resonance", "Resonanz" },
                             { "distDrive", "Drive" },
                             { "macro1", "Makro 1" },
                             { "macro2", "Makro 2" },
                             { "macro3", "Makro 3" },
                             { "macro4", "Makro 4" },
                             { "master", "Master" } } } } },
        oscillators,
        tuning,
        { "Filter",
          { { "Filter", Row { { "filterOn", "An" },
                              { "filterMode", "Typ" },
                              { "cutoff", "Cutoff" },
                              { "resonance", "Resonanz" },
                              { "filterDrive", "Drive" },
                              { "keytrack", "Keytrack" } } },
            { "Verzerrung", Row { { "distOn", "An" }, { "distMode", "Modus" }, { "distDrive", "Drive" }, { "distMix", "Mix" } } } } },
        { "Sub",
          { { "Sub", Row { { "subOn", "An" },
                           { "subShape", "Form" },
                           { "subOctave", "Oktave" },
                           { "subLevel", "Pegel" },
                           { "subDirect", "Direkt" } } },
            { "Rauschen", Row { { "noise", "Pegel" } } },
            { "Master", Row { { "master", "Pegel" } } } } },
        motion,
        matrix,
        { "Spiel",
          { { "Makros", Row { { "macro1", "Makro 1" }, { "macro2", "Makro 2" }, { "macro3", "Makro 3" }, { "macro4", "Makro 4" } } },
            { "Stimmen", Row { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" } } } } },
    };
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
    // Grouped under headings; an item's id stays its program number + 1.
    const auto& factory = factoryPresets();
    menu = menuOrder((int) factory.size(), [&factory](int i) { return factory[(std::size_t) i].category; });
    int group = -1;
    for (const int i : menu)
    {
        const auto& preset = factory[(std::size_t) i];
        const int category = categoryIndex(preset.category);
        if (category != group && category >= 0)
            presets.addSectionHeading(tr(preset.category));
        group = category;
        presets.addItem(utf8(preset.name), i + 1);
    }
    presets.setTextWhenNothingSelected(tr("Preset"));
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            choosePreset(presets.getSelectedId() - 1);
    };
    // The arrows step through the menu as it reads, group by group.
    auto step = [this](int direction) {
        const auto at = std::find(menu.begin(), menu.end(), synth.getCurrentProgram());
        const auto from = at == menu.end() ? -1 : (int) std::distance(menu.begin(), at);
        const int to = from + direction;
        if (juce::isPositiveAndBelow(to, (int) menu.size()))
            choosePreset(menu[(std::size_t) to]);
    };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
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
    const int height = kMargin * 2 + headerHeight() + oscA.preferredHeight() + 3 * sub.preferredHeight()
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
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + headerHeight());
    if (headerHeight() > 0)
        paintTitle(g, header, "TONWERK WAVETABLE");
}

void WavetableEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(headerHeight()).reduced(0, 7);
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
                presets.setSelectedId(presets.getItemId(i), juce::dontSendNotification);
        if (presets.getSelectedId() == 0)
            presets.setText(shownPreset, juce::dontSendNotification);
    }
}
} // namespace tonwerkwave
