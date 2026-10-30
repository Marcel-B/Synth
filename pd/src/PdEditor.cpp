#include "PdEditor.h"

#include "PdPresets.h"
#include "PresetCategories.h"

namespace tonwerkpd
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 96;
constexpr int kKeyboard = 56;
constexpr std::array<const char*, (std::size_t) Wave::count> kWaveNames { "SÄGE",        "RECHTECK",     "PULS",
                                                                          "DOPPELSINUS", "SÄGE-PULS",    "RES SÄGE",
                                                                          "RES DREIECK", "RES TRAPEZ" };

Wave waveOf(float value) { return (Wave) std::clamp(juce::roundToInt(value), 0, (int) Wave::count - 1); }
} // namespace

void PhaseView::update()
{
    auto knob = [this](const juce::String& id) { return synth.state.getRawParameterValue(id)->load(); };
    playing = synth.shownActive.load();
    if (playing)
    {
        frequency = synth.shownFrequency.load();
        for (std::size_t k = 0; k < kLines; ++k)
        {
            auto& line = lines[k];
            line.on = synth.shownOn[k].load();
            line.first = (Wave) synth.shownFirst[k].load();
            line.alternate = synth.shownAlternate[k].load();
            line.second = (Wave) synth.shownSecond[k].load();
            line.bend = synth.shownBend[k].load();
        }
    }
    else
    {
        const auto select = (LineSelect) std::clamp(juce::roundToInt(knob("lines")), 0, 3);
        for (std::size_t k = 0; k < kLines; ++k)
        {
            // 1+1' plays line 1's waves twice.
            const auto source = "line" + juce::String(k == 1 && select == LineSelect::line1Doubled ? 1 : (int) k + 1);
            auto& line = lines[k];
            line.on = k == 0 ? select != LineSelect::line2 : select != LineSelect::line1;
            line.first = waveOf(knob(source + "Wave"));
            const int second = juce::roundToInt(knob(source + "Wave2"));
            line.alternate = second > 0;
            line.second = waveOf((float) second - 1.0f);
            line.bend = knob(source + "Depth");
        }
    }
    repaint();
}

void PhaseView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(10.0f, 6.0f);

    juce::String caption;
    for (std::size_t k = 0; k < kLines; ++k)
    {
        const auto& line = lines[k];
        if (! line.on)
            continue;
        if (caption.isNotEmpty())
            caption << "  |  ";
        caption << "L" << (int) k + 1 << " " << tr(kWaveNames[(std::size_t) line.first]);
        if (line.alternate)
            caption << "/" << tr(kWaveNames[(std::size_t) line.second]);
        caption << " DCW " << juce::roundToInt(line.bend * 100.0f) << "%";
    }
    if (playing)
        caption << juce::String::fromUTF8(" · ") << juce::roundToInt(frequency) << " HZ";
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
    g.drawText(caption, inner.toNearestInt(), juce::Justification::topLeft);

    const auto stage = inner.withTrimmedTop(14.0f);
    const float mid = stage.getCentreY();
    const float reach = stage.getHeight() * 0.44f;
    g.setColour(border.brighter(0.2f));
    g.drawHorizontalLine(juce::roundToInt(mid), stage.getX(), stage.getRight());

    // Two cycles across the screen; the plain cosine first, faint, then the lines over it.
    constexpr int kPoints = 240;
    auto trace = [&](auto sample) {
        juce::Path path;
        for (int i = 0; i <= kPoints; ++i)
        {
            const double along = 2.0 * i / kPoints;
            const float x = stage.getX() + (float) (along * 0.5) * stage.getWidth();
            const float y = mid - sample(along) * reach;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    };
    g.setColour(muted.withAlpha(0.35f));
    g.strokePath(trace([](double along) { return cosine(along); }), juce::PathStrokeType(1.0f));

    for (std::size_t k = kLines; k-- > 0;)
    {
        const auto& line = lines[k];
        if (! line.on)
            continue;
        const auto path = trace([&line](double along) {
            const bool odd = along >= 1.0 && line.alternate;
            const double phase = along - std::floor(along);
            // A cycle on screen is about a hundred points: the fall may take one, as on a low note.
            return pdWave(odd ? line.second : line.first, std::min(phase, 0.999999), line.bend, 0.004);
        });
        const auto colour = k == 0 ? cyan : yellow;
        g.setColour(colour.withAlpha(0.22f));
        g.strokePath(path, juce::PathStrokeType(4.0f));
        g.setColour(colour.withAlpha(0.9f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
}

LinesSection::LinesSection(PdProcessor& processor)
    : Section(processor.state, "Linien",
              { { { "lines", "Linien" }, { "modulation", "Modulation" }, { "detuneSemi", "Halbton 2" },
                  { "detuneFine", "Fein 2" }, { "pitchEnvAmount", tr("Tonhöhe") }, { "pitchEnvTime", "Tonh. Zeit" },
                  { "width", "Breite" } } },
              {}, { 0, kDisplay }),
      view(processor)
{
    addAndMakeVisible(view);
    view.update();
}

void LinesSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

namespace
{
Section::Row waveRow(const juce::String& prefix)
{
    return { { prefix + "Wave", "Welle" },    { prefix + "Wave2", "Welle 2" }, { prefix + "Depth", "DCW" },
             { prefix + "KeyFollow", "Keytrack" }, { prefix + "Level", "Pegel" },  { prefix + "Octave", "Oktave" } };
}

Section::Row envelopeRow(const juce::String& prefix)
{
    return { { prefix + "DcwAttack", "DCW A" }, { prefix + "DcwDecay", "DCW D" },
             { prefix + "DcwSustain", "DCW S" }, { prefix + "DcwRelease", "DCW R" },
             { prefix + "AmpAttack", "DCA A" }, { prefix + "AmpDecay", "DCA D" },
             { prefix + "AmpSustain", "DCA S" }, { prefix + "AmpRelease", "DCA R" } };
}
} // namespace

PdEditor::PdEditor(PdProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      lines(p),
      line1(p.state, "Linie 1", { waveRow("line1"), envelopeRow("line1") }),
      line2(p.state, "Linie 2", { waveRow("line2"), envelopeRow("line2") }),
      lfo1(p.state, "LFO 1",
           { { { "lfo1Shape", "Form" }, { "lfo1Sync", "Sync" }, { "lfo1Rate", "Rate", 0, "lfo1Sync", false },
               { "lfo1Division", "Raster", 0, "lfo1Sync", true }, { "lfo1Fade", tr("Verzög.") },
               { "lfo1Target", "Ziel" }, { "lfo1Amount", "Menge" } } }),
      lfo2(p.state, "LFO 2",
           { { { "lfo2Shape", "Form" }, { "lfo2Sync", "Sync" }, { "lfo2Rate", "Rate", 0, "lfo2Sync", false },
               { "lfo2Division", "Raster", 0, "lfo2Sync", true }, { "lfo2Target", "Ziel" }, { "lfo2Amount", "Menge" } } }),
      controllers(p.state, "Spielhilfen",
                  { { { "wheelTarget", "Modrad" }, { "wheelAmount", "Menge" }, { "velocityTarget", "Anschlag" },
                      { "velocityAmount", "Menge" }, { "dynamics", "Dynamik" } } }),
      voice(p.state, "Stimmen",
            { { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" }, { "master", "Master" } } }),
      chorus(p.state, "Chorus",
             { { { "chorusMix", "Mix" }, { "chorusRate", "Rate" }, { "chorusDepth", "Tiefe" } } }),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", tr("Länge") } } }),
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
    auto step = [this](int direction) {
        const auto at = std::find(menu.begin(), menu.end(), synth.getCurrentProgram());
        const auto from = at == menu.end() ? -1 : (int) std::distance(menu.begin(), at);
        const int to = from + direction;
        if (juce::isPositiveAndBelow(to, (int) menu.size()))
            choosePreset(menu[(std::size_t) to]);
    };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &lines, &line1, &line2, &lfo1,
                                                            &lfo2, &controllers, &voice, &chorus, &effects, &keyboard })
        addAndMakeVisible(*c);

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setAvailableRange(24, 96);
    keyboard.setLowestVisibleKey(24);

    // As wide as the widest row needs.
    auto rowWidth = [](std::initializer_list<const Section*> row) {
        int width = kGap * ((int) row.size() - 1);
        for (const auto* s : row)
            width += s->preferredWidth();
        return width;
    };
    const int width = 2 * kMargin
                      + std::max({ rowWidth({ &lines, &controllers }) + 120, rowWidth({ &line1, &line2 }),
                                   rowWidth({ &lfo1, &lfo2 }), rowWidth({ &voice, &chorus, &effects }) });
    const int height = kMargin * 2 + kHeader + std::max(lines.preferredHeight(), controllers.preferredHeight())
                       + line1.preferredHeight()
                       + lfo1.preferredHeight() + voice.preferredHeight() + kKeyboard + 5 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

PdEditor::~PdEditor() { setLookAndFeel(nullptr); }

void PdEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void PdEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK PD");
}

void PdEditor::resized()
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

    layoutRow(area, { &lines, &controllers });
    layoutRow(area, { &line1, &line2 });
    layoutRow(area, { &lfo1, &lfo2 });
    layoutRow(area, { &voice, &chorus, &effects });
}

void PdEditor::timerCallback()
{
    lines.update();
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
} // namespace tonwerkpd
