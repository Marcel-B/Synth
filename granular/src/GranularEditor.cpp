#include "GranularEditor.h"

#include "GranularPresets.h"
#include "PresetCategories.h"

namespace tonwerkgrain
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 84;
constexpr int kKeyboard = 56;
const juce::String kAudioFiles = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf";
} // namespace

void GrainView::update()
{
    source = synth.shownSource();
    centre = synth.shownCentre.load();
    spray = synth.shownSpray.load();
    knob = synth.state.getRawParameterValue("position")->load();
    knobSpray = synth.state.getRawParameterValue("spray")->load();
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        places[i] = synth.grainPlaces[i].load();
        phases[i] = synth.grainPhases[i].load();
    }
    repaint();
}

void GrainView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(6.0f, 4.0f);

    g.setFont(mono(10.0f));
    if (source == nullptr || source->empty())
    {
        g.setColour(cyan.withAlpha(0.8f));
        g.drawText(utf8("KEINE DATEI · HIERHER ZIEHEN ODER LADEN"), inner.toNearestInt(), juce::Justification::centred);
        return;
    }

    // Where the grains come from: the knobs while nothing plays, the newest note's (scan included) while it sounds.
    const bool playing = centre >= 0.0f;
    const float middle = playing ? centre : knob;
    const float reach = (playing ? spray : knobSpray) * 0.5f;
    g.setColour(yellow.withAlpha(0.1f));
    for (const float offset : { -1.0f, 0.0f, 1.0f })
    {
        const float from = middle - reach + offset, to = middle + reach + offset;
        g.fillRect(juce::Rectangle<float>(inner.getX() + std::max(0.0f, from) * inner.getWidth(), inner.getY(),
                                          (std::min(1.0f, to) - std::max(0.0f, from)) * inner.getWidth(),
                                          inner.getHeight())
                       .getIntersection(inner));
    }

    const auto& overview = source->overview;
    const float mid = inner.getCentreY();
    const float half = inner.getHeight() * 0.42f;
    g.setColour(cyan.withAlpha(0.55f));
    const int columns = (int) overview.size();
    for (int c = 0; c < columns; ++c)
    {
        const float x = inner.getX() + inner.getWidth() * (float) c / (float) columns;
        const auto [low, high] = overview[(std::size_t) c];
        g.drawVerticalLine(juce::roundToInt(x), mid - high * half, mid - low * half + 1.0f);
    }

    g.setColour(yellow.withAlpha(0.8f));
    g.drawVerticalLine(juce::roundToInt(inner.getX() + middle * inner.getWidth()), inner.getY(), inner.getBottom());

    // Each grain a spark; its height in the view is fixed by its slot, so it does not jump while it plays.
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        if (places[i] < 0.0f)
            continue;
        const float glow = std::sin(juce::MathConstants<float>::pi * std::clamp(phases[i], 0.0f, 1.0f));
        const float x = inner.getX() + places[i] * inner.getWidth();
        const float y = inner.getY() + inner.getHeight() * (0.15f + 0.7f * (float) ((i * 37) % kMaxGrains) / kMaxGrains);
        g.setColour(yellow.withAlpha(0.25f * glow));
        g.fillEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour(yellow.withAlpha(0.4f + 0.6f * glow));
        g.fillEllipse(x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }

    g.setColour(cyan.withAlpha(0.8f));
    g.drawText(juce::String::fromUTF8(source->name.c_str()).toUpperCase(), inner.toNearestInt(),
               juce::Justification::topLeft);
}

SourceSection::SourceSection(GranularProcessor& processor)
    : Section(processor.state, "Quelle",
              { { { "source", "Quelle" }, { "position", "Position" }, { "spray", "Streuung" }, { "scan", "Scan" } } },
              {}, { 0, kDisplay }),
      synth(processor),
      view(processor)
{
    addAndMakeVisible(view);
    load.setTooltip(utf8("Eigene Audiodatei als Quelle laden (oder auf die Anzeige ziehen)"));
    load.onClick = [this] { choose(); };
    addAndMakeVisible(load);
    view.update();
}

void SourceSection::resized()
{
    Section::resized();
    const auto area = displayArea().reduced(0, 4);
    view.setBounds(area);
    load.setBounds(area.getRight() - 78, area.getY() + 4, 72, 20);
}

void SourceSection::choose()
{
    chooser = std::make_unique<juce::FileChooser>(utf8("Audiodatei als Quelle"), synth.userFile(), kAudioFiles);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& c) {
                             if (c.getResult().existsAsFile())
                                 synth.loadFile(c.getResult());
                         });
}

bool SourceSection::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (juce::File(file).hasFileExtension(kAudioFiles))
            return true;
    return false;
}

void SourceSection::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
        if (juce::File(file).hasFileExtension(kAudioFiles) && synth.loadFile(juce::File(file)))
            return;
}

GranularEditor::GranularEditor(GranularProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      source(p),
      grains(p.state, utf8("KÖRNER"),
             { { { "grainSize", utf8("Größe") }, { "density", "Dichte" }, { "jitter", "Zufall" },
                 { "window", "Fenster" }, { "reverse", utf8("Rückwärts") }, { "width", "Breite" } },
               { { "octave", "Oktave" }, { "semi", "Halbton" }, { "fine", "Fein" }, { "scatter", "Streuung" },
                 { "scatterMode", "Raster" }, { "keyFollow", "Tastatur" } } }),
      filter(p.state, "Filter",
             { { { "filterMode", "Typ" }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" },
                 { "keytrack", "Keytrack" } } },
             "filterOn"),
      env1(p.state, utf8("HÜLLKURVE 1 · AMP"),
           { { { "env1Attack", "Attack" }, { "env1Decay", "Decay" }, { "env1Sustain", "Sustain" },
               { "env1Release", "Release" } } }),
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
            { { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" }, { "master", "Master" } } }),
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
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &source, &grains, &filter,
                                                            &env1, &env2, &lfo1, &lfo2, &controllers, &voice,
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

    const int height = kMargin * 2 + kHeader + std::max(source.preferredHeight(), grains.preferredHeight())
                       + std::max({ filter.preferredHeight(), env1.preferredHeight(), env2.preferredHeight() })
                       + lfo1.preferredHeight() + voice.preferredHeight() + kKeyboard + 5 * kGap;
    setSize(kWidth, height);
    timerCallback();
    startTimerHz(30);
}

GranularEditor::~GranularEditor() { setLookAndFeel(nullptr); }

void GranularEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void GranularEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK GRANULAR");
}

void GranularEditor::resized()
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

    layoutRow(area, { &source, &grains });
    layoutRow(area, { &filter, &env1, &env2 });
    layoutRow(area, { &lfo1, &lfo2 });
    layoutRow(area, { &controllers, &voice, &effects });
}

void GranularEditor::timerCallback()
{
    source.update();
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
} // namespace tonwerkgrain
