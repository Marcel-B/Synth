#include "PhysicalEditor.h"

#include "PhysicalPresets.h"
#include "PresetCategories.h"

namespace tonwerkphys
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 84;
constexpr int kKeyboard = 56;
constexpr std::array<const char*, 7> kResonatorNames { "SAITE", "ROHR GESCHLOSSEN", "ROHR OFFEN", "STAB",
                                                       "GLOCKE", "FELL", "SCHALE" };
constexpr std::array<const char*, 4> kExciterNames { "ZUPFEN", "SCHLAGEN", "STREICHEN", "BLASEN" };
} // namespace

void ModelView::update()
{
    auto knob = [this](const char* id) { return synth.state.getRawParameterValue(id)->load(); };
    knobResonator = std::clamp(juce::roundToInt(knob("resonator")), 0, (int) Resonator::count - 1);
    knobExciter = std::clamp(juce::roundToInt(knob("exciter")), 0, 3);
    knobPosition = knob("position");
    resonator = synth.shownResonator.load();
    if (resonator >= 0)
    {
        frequency = synth.shownFrequency.load();
        position = synth.shownPosition.load();
        points = synth.shownPoints.load();
        for (int i = 0; i < points; ++i)
            shape[(std::size_t) i] = synth.shownShape[(std::size_t) i].load();
        modes = synth.shownModes.load();
        for (int k = 0; k < modes; ++k)
        {
            ratios[(std::size_t) k] = synth.shownRatios[(std::size_t) k].load();
            levels[(std::size_t) k] = synth.shownLevels[(std::size_t) k].load();
        }
    }
    repaint();
}

void ModelView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(10.0f, 6.0f);
    const bool playing = resonator >= 0;
    const auto kind = (Resonator) (playing ? resonator : knobResonator);

    juce::String caption = utf8(kResonatorNames[(std::size_t) kind]) + utf8(" · ")
                           + kExciterNames[(std::size_t) knobExciter];
    if (playing)
        caption << utf8(" · ") << juce::roundToInt(frequency) << " HZ";
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
    g.drawText(caption, inner.toNearestInt(), juce::Justification::topLeft);

    const auto stage = inner.withTrimmedTop(14.0f);
    if (isWaveguide(kind))
    {
        const float mid = stage.getCentreY();
        const float reach = stage.getHeight() * 0.42f;
        const bool string = kind == Resonator::string;
        if (! string)
        {
            // The tube's walls, open on the right, closed on the left where the reed sits if it is closed.
            g.setColour(border.brighter(0.4f));
            g.drawHorizontalLine(juce::roundToInt(mid - reach - 2.0f), stage.getX(), stage.getRight());
            g.drawHorizontalLine(juce::roundToInt(mid + reach + 2.0f), stage.getX(), stage.getRight());
            if (kind == Resonator::closedTube)
                g.drawVerticalLine(juce::roundToInt(stage.getX()), mid - reach - 2.0f, mid + reach + 2.0f);
        }
        // The wave scaled to the view, the scale easing down as it dies so a quiet ring still shows.
        float loudest = 0.0f;
        if (playing)
            for (int i = 0; i < points; ++i)
                loudest = std::max(loudest, std::abs(shape[(std::size_t) i]));
        scale = std::max(loudest, scale * 0.92f);
        juce::Path wave;
        const int n = playing && points > 1 ? points : 2;
        for (int i = 0; i < n; ++i)
        {
            const float along = (float) i / (float) (n - 1);
            // A string is held at both ends; a tube's air moves freely at an open end.
            const float pinned = string ? std::sin(juce::MathConstants<float>::pi * along) : 1.0f;
            const float value = playing && points > 1 && scale > 1.0e-6f ? shape[(std::size_t) i] / scale : 0.0f;
            const float x = stage.getX() + along * stage.getWidth();
            const float y = mid - value * pinned * reach;
            if (i == 0)
                wave.startNewSubPath(x, y);
            else
                wave.lineTo(x, y);
        }
        g.setColour(cyan.withAlpha(0.25f));
        g.strokePath(wave, juce::PathStrokeType(4.0f));
        g.setColour(cyan.withAlpha(0.9f));
        g.strokePath(wave, juce::PathStrokeType(1.5f));

        // Where it is set going: the pluck, hammer or bow on the string, the mouth of a tube.
        const float at = string ? (playing ? position : knobPosition) : 0.0f;
        const float x = stage.getX() + at * stage.getWidth();
        g.setColour(yellow.withAlpha(0.3f));
        g.fillEllipse(x - 6.0f, mid - 6.0f, 12.0f, 12.0f);
        g.setColour(yellow);
        g.fillEllipse(x - 2.5f, mid - 2.5f, 5.0f, 5.0f);
        return;
    }

    // A struck body's resonances, each a bar at its ratio on a scale of octaves.
    const auto& table = modeTable(kind);
    int count = playing ? modes : table.count;
    std::array<float, kModes> r {}, height {};
    float strongest = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        r[(std::size_t) k] = playing ? ratios[(std::size_t) k] : table.ratio[(std::size_t) k];
        height[(std::size_t) k] = playing ? levels[(std::size_t) k] : table.gain[(std::size_t) k];
        strongest = std::max(strongest, height[(std::size_t) k]);
    }
    const float lowest = std::log2(std::min(0.5f, table.ratio[0]) * 0.8f);
    const float highest = std::log2(table.ratio[(std::size_t) table.count - 1] * 1.25f);
    g.setColour(border.brighter(0.4f));
    g.drawHorizontalLine(juce::roundToInt(stage.getBottom() - 1.0f), stage.getX(), stage.getRight());
    for (int k = 0; k < count; ++k)
    {
        const float x = stage.getX() + (std::log2(r[(std::size_t) k]) - lowest) / (highest - lowest) * stage.getWidth();
        // 48 dB from the strongest resonance to the floor.
        const float db = 20.0f * std::log10(std::max(1.0e-6f, height[(std::size_t) k] / std::max(1.0e-6f, strongest)));
        const float share = std::clamp(1.0f + db / 48.0f, 0.0f, 1.0f);
        const float top = stage.getBottom() - share * (stage.getHeight() - 4.0f);
        const bool fundamental = std::abs(r[(std::size_t) k] - 1.0f) < 0.02f;
        const auto colour = fundamental ? yellow : cyan;
        g.setColour(colour.withAlpha(0.2f));
        g.fillRect(x - 3.0f, top, 6.0f, stage.getBottom() - top);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRect(x - 1.0f, top, 2.0f, stage.getBottom() - top);
    }
}

ResonatorSection::ResonatorSection(PhysicalProcessor& processor)
    : Section(processor.state, "Resonator",
              { { { "resonator", "Typ" }, { "decay", "Abklingen" }, { "damping", utf8("Dämpfung") },
                  { "inharmonicity", "Inharmonie" }, { "release", "Loslassen" } } },
              {}, { 0, kDisplay }),
      view(processor)
{
    addAndMakeVisible(view);
    view.update();
}

void ResonatorSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

PhysicalEditor::PhysicalEditor(PhysicalProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      resonator(p),
      exciter(p.state, "Erreger",
              { { { "exciter", "Typ" }, { "hardness", utf8("Härte") }, { "pressure", "Druck" },
                  { "noise", "Rauschen" }, { "position", "Position" } },
                { { "env1Attack", "Attack" }, { "env1Decay", "Decay" }, { "env1Sustain", "Sustain" },
                  { "env1Release", "Release" }, { "dynamics", "Dynamik" } } }),
      body(p.state, "Korpus", { { { "body", "Typ" }, { "bodyMix", "Mix" } } }),
      tone(p.state, utf8("Tonhöhe"), { { { "octave", "Oktave" }, { "semi", "Halbton" }, { "fine", "Fein" } } }),
      filter(p.state, "Filter",
             { { { "filterMode", "Typ" }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" },
                 { "keytrack", "Keytrack" } } },
             "filterOn"),
      env2(p.state, utf8("HÜLLKURVE 2"),
           { { { "env2Attack", "Attack" }, { "env2Decay", "Decay" }, { "env2Sustain", "Sustain" },
               { "env2Release", "Release" }, { "env2Target", "Ziel" }, { "env2Amount", "Menge" } } }),
      lfo1(p.state, "LFO 1",
           { { { "lfo1Shape", "Form" }, { "lfo1Sync", "Sync" }, { "lfo1Rate", "Rate", 0, "lfo1Sync", false },
               { "lfo1Division", "Raster", 0, "lfo1Sync", true }, { "lfo1Target", "Ziel" }, { "lfo1Amount", "Menge" } } }),
      lfo2(p.state, "LFO 2",
           { { { "lfo2Shape", "Form" }, { "lfo2Sync", "Sync" }, { "lfo2Rate", "Rate", 0, "lfo2Sync", false },
               { "lfo2Division", "Raster", 0, "lfo2Sync", true }, { "lfo2Target", "Ziel" }, { "lfo2Amount", "Menge" } } }),
      controllers(p.state, "Spielhilfen",
                  { { { "wheelTarget", "Modrad" }, { "wheelAmount", "Menge" }, { "velocityTarget", "Anschlag" },
                      { "velocityAmount", "Menge" } } }),
      voice(p.state, "Stimmen",
            { { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" }, { "width", "Breite" },
                { "master", "Master" } } }),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", utf8("Länge") } } }),
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
            presets.addSectionHeading(utf8(preset.category));
        group = category;
        presets.addItem(utf8(preset.name), i + 1);
    }
    presets.setTextWhenNothingSelected(utf8("Preset"));
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
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &resonator, &exciter, &body,
                                                            &tone, &filter, &env2, &lfo1, &lfo2, &controllers, &voice,
                                                            &effects, &keyboard })
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
                      + std::max({ rowWidth({ &resonator, &exciter }) + 240, rowWidth({ &body, &tone, &filter, &env2 }),
                                   rowWidth({ &lfo1, &lfo2 }), rowWidth({ &controllers, &voice, &effects }) });
    const int height = kMargin * 2 + kHeader + std::max(resonator.preferredHeight(), exciter.preferredHeight())
                       + std::max({ body.preferredHeight(), filter.preferredHeight(), env2.preferredHeight() })
                       + lfo1.preferredHeight() + voice.preferredHeight() + kKeyboard + 5 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

PhysicalEditor::~PhysicalEditor() { setLookAndFeel(nullptr); }

void PhysicalEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void PhysicalEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK PHYSICAL");
}

void PhysicalEditor::resized()
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

    layoutRow(area, { &resonator, &exciter });
    layoutRow(area, { &body, &tone, &filter, &env2 });
    layoutRow(area, { &lfo1, &lfo2 });
    layoutRow(area, { &controllers, &voice, &effects });
}

void PhysicalEditor::timerCallback()
{
    resonator.update();
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
} // namespace tonwerkphys
