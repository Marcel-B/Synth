#include "AdditiveEditor.h"

#include "AdditivePresets.h"
#include "PresetCategories.h"

namespace tonwerkadd
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 104;
constexpr int kKeyboard = 56;
constexpr std::array<const char*, (std::size_t) Spectrum::count> kSpectrumNames {
    "SINUS", "SÄGE", "RECHTECK", "DREIECK", "PULS", "ORGEL", "HOHL", "HELL", "OKTAVEN", "GLAS", "PRIMZAHLEN", "ZUFALL"
};
/** The note the display shows the knobs' spectrum for while nothing sounds: C3. */
constexpr double kRestingHz = 130.81;
} // namespace

void SpectrumView::update()
{
    settings = synth.readSettings();
    frequency = synth.shownFrequency.load();
    playing = frequency > 0.0f;
    if (playing)
        for (std::size_t k = 0; k < (std::size_t) kPartials; ++k)
        {
            levels[k] = synth.shownLevels[k].load();
            ratios[k] = synth.shownRatios[k].load();
        }
    else
    {
        const auto shape = shapeOf(settings);
        for (int k = 1; k <= kPartials; ++k)
            ratios[(std::size_t) k - 1] = ratioOf(k, shape.stretch, shape.beating);
        levelsOf(shape, kRestingHz, 48000.0, ratios.data(), levels.data());
    }
    repaint();
}

void SpectrumView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(10.0f, 6.0f);

    const auto& s = settings;
    juce::String caption = utf8(kSpectrumNames[(std::size_t) s.spectrumA]);
    if (s.morph > 0.005f)
        caption << utf8(" ▸ ") << utf8(kSpectrumNames[(std::size_t) s.spectrumB]) << " "
                << juce::roundToInt(s.morph * 100.0f) << " %";
    caption << utf8(" · ") << juce::roundToInt(s.partials) << utf8(" OBERTÖNE");
    if (playing)
        caption << utf8(" · ") << juce::roundToInt(frequency) << " HZ";
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
    g.drawText(caption, inner.toNearestInt(), juce::Justification::topLeft);

    const auto stage = inner.withTrimmedTop(14.0f);
    g.setColour(border.brighter(0.4f));
    g.drawHorizontalLine(juce::roundToInt(stage.getBottom() - 1.0f), stage.getX(), stage.getRight());

    float loudest = 0.0f;
    for (const float level : levels)
        loudest = std::max(loudest, level);
    const float barWidth = stage.getWidth() / (float) kPartials;
    for (std::size_t k = 0; k < (std::size_t) kPartials; ++k)
    {
        // The bars fall back slowly, so a short note still leaves its spectrum on screen for a moment.
        const float level = loudest > 1.0e-6f ? levels[k] / loudest : 0.0f;
        held[k] = std::max(level, held[k] * 0.85f);
        const float db = 20.0f * std::log10(std::max(1.0e-6f, held[k]));
        const float share = std::clamp(1.0f + db / 48.0f, 0.0f, 1.0f);
        if (share <= 0.0f)
            continue;
        const float x = stage.getX() + barWidth * (float) k;
        const float top = stage.getBottom() - share * (stage.getHeight() - 4.0f);
        const auto colour = k == 0 ? yellow : cyan;
        g.setColour(colour.withAlpha(0.18f));
        g.fillRect(x + 0.5f, top, std::max(1.0f, barWidth - 1.0f), stage.getBottom() - top);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRect(x + 0.5f, top, std::max(1.0f, barWidth - 1.0f), 2.0f);
    }

    // The filter's cutoff where it falls among the partials of the note shown.
    if (s.filterOn)
    {
        const double f0 = playing ? frequency : kRestingHz;
        const double partial = s.cutoff / f0;
        if (partial >= 0.5 && partial <= kPartials + 0.5)
        {
            const float x = stage.getX() + barWidth * (float) (partial - 0.5);
            g.setColour(red.withAlpha(0.8f));
            g.drawVerticalLine(juce::roundToInt(x), stage.getY(), stage.getBottom());
        }
    }
}

SpectrumSection::SpectrumSection(AdditiveProcessor& processor)
    : Section(processor.state, "Spektrum",
              { { { "spectrumA", "Spektrum A" }, { "spectrumB", "Spektrum B" }, { "morph", "Morph" },
                  { "partials", utf8("Obertöne") }, { "tilt", "Neigung" } },
                { { "oddEven", "Ger./Unger." }, { "stretch", "Spreizung" }, { "beating", "Schwebung" },
                  { "phases", "Phasen" }, { "width", "Breite" } } },
              {}, { 0, kDisplay }),
      view(processor)
{
    addAndMakeVisible(view);
    view.update();
}

void SpectrumSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

AdditiveEditor::AdditiveEditor(AdditiveProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      spectrum(p),
      colour(p.state, "Formant & Kamm",
             { { { "vowel", "Vokal" }, { "formantDepth", utf8("Stärke") }, { "formantShift", "Verschieb." } },
               { { "combDepth", "Kamm" }, { "combSpacing", "Abstand" }, { "combShift", "Verschieb." } } }),
      filter(p.state, "Filter",
             { { { "filterMode", "Typ" }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" },
                 { "keytrack", "Keytrack" } } },
             "filterOn"),
      spectralEnvelope(p.state, utf8("Obertöne-Hüllkurve"),
                       { { { "bloom", "Einschw." }, { "spectralDecay", "Abklingen" }, { "spectralSustain", "Halten" },
                           { "spectralDamping", utf8("Dämpfung") } } },
                       "spectralOn"),
      amp(p.state, utf8("Lautstärke"),
          { { { "env1Attack", "Attack" }, { "env1Decay", "Decay" }, { "env1Sustain", "Sustain" },
              { "env1Release", "Release" }, { "dynamics", "Dynamik" } } }),
      tone(p.state, utf8("Tonhöhe"), { { { "octave", "Oktave" }, { "semi", "Halbton" }, { "fine", "Fein" } } }),
      // The modulation in two rows each, so the editor stays narrow enough for a laptop's screen.
      env2(p.state, utf8("HÜLLKURVE 2"),
           { { { "env2Attack", "Attack" }, { "env2Decay", "Decay" }, { "env2Sustain", "Sustain" },
               { "env2Release", "Release" } },
             { { "env2Target", "Ziel" }, { "env2Amount", "Menge" } } }),
      lfo1(p.state, "LFO 1",
           { { { "lfo1Shape", "Form" }, { "lfo1Sync", "Sync" }, { "lfo1Rate", "Rate", 0, "lfo1Sync", false },
               { "lfo1Division", "Raster", 0, "lfo1Sync", true } },
             { { "lfo1Target", "Ziel" }, { "lfo1Amount", "Menge" } } }),
      lfo2(p.state, "LFO 2",
           { { { "lfo2Shape", "Form" }, { "lfo2Sync", "Sync" }, { "lfo2Rate", "Rate", 0, "lfo2Sync", false },
               { "lfo2Division", "Raster", 0, "lfo2Sync", true } },
             { { "lfo2Target", "Ziel" }, { "lfo2Amount", "Menge" } } }),
      controllers(p.state, "Spielhilfen",
                  { { { "wheelTarget", "Modrad" }, { "wheelAmount", "Menge" } },
                    { { "velocityTarget", "Anschlag" }, { "velocityAmount", "Menge" } } }),
      voice(p.state, "Stimmen",
            { { { "voiceMode", "Modus" }, { "glide", "Glide" } }, { { "bendRange", "Bend" }, { "master", "Master" } } }),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" } },
                { { "fxDelayTone", "Ton" }, { "fxReverbMix", "Hall" }, { "fxReverbDecay", utf8("Länge") } } }),
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
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &spectrum, &colour, &filter,
                                                            &spectralEnvelope, &amp, &tone, &env2, &lfo1, &lfo2,
                                                            &controllers, &voice, &effects, &keyboard })
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
                      + std::max({ rowWidth({ &spectrum, &colour, &effects }) + 120,
                                   rowWidth({ &filter, &spectralEnvelope, &amp, &tone }),
                                   rowWidth({ &env2, &lfo1, &lfo2, &controllers, &voice }) });
    const int height = kMargin * 2 + kHeader + spectrum.preferredHeight()
                       + std::max({ filter.preferredHeight(), spectralEnvelope.preferredHeight(), amp.preferredHeight() })
                       + lfo1.preferredHeight() + kKeyboard + 4 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

AdditiveEditor::~AdditiveEditor() { setLookAndFeel(nullptr); }

void AdditiveEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void AdditiveEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK ADDITIV");
}

void AdditiveEditor::resized()
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

    layoutRow(area, { &spectrum, &colour, &effects });
    layoutRow(area, { &filter, &spectralEnvelope, &amp, &tone });
    layoutRow(area, { &env2, &lfo1, &lfo2, &controllers, &voice });
}

void AdditiveEditor::timerCallback()
{
    spectrum.update();
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
} // namespace tonwerkadd
