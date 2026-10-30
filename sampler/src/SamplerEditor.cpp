#include "SamplerEditor.h"
#include "Platform.h"

#include "PresetCategories.h"
#include "SamplerPresets.h"

namespace tonwerksampler
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kDisplay = 120;
constexpr int kKeyboard = 56;
const juce::String kAudioFiles = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf";
} // namespace

void WaveView::update()
{
    sample = synth.shownSample();
    settings = synth.readSettings();
    if (sample != nullptr && ! sample->empty() && settings.mode == Mode::slices)
    {
        const double length = (double) sample->length();
        const double from = std::min(settings.start, settings.end) * length;
        const double to = std::max(std::max(settings.start, settings.end) * length, from + 64.0);
        slices = sliceRegion(*sample, from, std::min(to, length), settings.slices, settings.sliceMode);
    }
    else
        slices = {};
    for (std::size_t i = 0; i < heads.size(); ++i)
        heads[i] = synth.playheads[i].load();
    repaint();
}

void WaveView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(6.0f, 4.0f);

    g.setFont(mono(10.0f));
    if (sample == nullptr || sample->empty())
    {
        g.setColour(cyan.withAlpha(0.8f));
        g.drawText(tr("KEINE DATEI · HIERHER ZIEHEN ODER LADEN"), inner.toNearestInt(), juce::Justification::centred);
        return;
    }
    const auto stage = inner.withTrimmedTop(14.0f);
    auto xAt = [&stage](double share) { return stage.getX() + (float) share * stage.getWidth(); };
    const double length = (double) sample->length();

    // The region played, the rest dimmed.
    const float from = std::min(settings.start, settings.end), to = std::max(settings.start, settings.end);
    g.setColour(cyan.withAlpha(0.06f));
    g.fillRect(juce::Rectangle<float>(xAt(from), stage.getY(), xAt(to) - xAt(from), stage.getHeight()));
    if (settings.mode == Mode::classic && settings.loop != LoopMode::off)
    {
        const float a = std::clamp(std::min(settings.loopStart, settings.loopEnd), from, to);
        const float b = std::clamp(std::max(settings.loopStart, settings.loopEnd), from, to);
        g.setColour(yellow.withAlpha(0.12f));
        g.fillRect(juce::Rectangle<float>(xAt(a), stage.getY(), xAt(b) - xAt(a), stage.getHeight()));
        g.setColour(yellow.withAlpha(0.7f));
        g.drawVerticalLine(juce::roundToInt(xAt(a)), stage.getY(), stage.getBottom());
        g.drawVerticalLine(juce::roundToInt(xAt(b)), stage.getY(), stage.getBottom());
    }

    const auto& overview = sample->overview;
    const float mid = stage.getCentreY();
    const float half = stage.getHeight() * 0.45f;
    const int columns = (int) overview.size();
    for (int c = 0; c < columns; ++c)
    {
        const float share = (float) c / (float) columns;
        const bool inside = share >= from && share <= to;
        g.setColour(cyan.withAlpha(inside ? 0.7f : 0.2f));
        const float x = stage.getX() + stage.getWidth() * share;
        const auto [low, high] = overview[(std::size_t) c];
        g.drawVerticalLine(juce::roundToInt(x), mid - high * half, mid - low * half + 1.0f);
    }

    g.setColour(red.withAlpha(0.8f));
    g.drawVerticalLine(juce::roundToInt(xAt(from)), stage.getY(), stage.getBottom());
    g.drawVerticalLine(juce::roundToInt(xAt(to)), stage.getY(), stage.getBottom());

    // Slices: a line at each start, the key that plays it next to it while there is room.
    if (settings.mode == Mode::slices)
        for (int i = 0; i < slices.count; ++i)
        {
            const float x = xAt(slices.bounds[(std::size_t) i] / length);
            g.setColour(yellow.withAlpha(0.8f));
            g.drawVerticalLine(juce::roundToInt(x), stage.getY(), stage.getBottom());
            const float width = xAt(slices.bounds[(std::size_t) i + 1] / length) - x;
            if (width > 22.0f)
            {
                g.setFont(mono(9.0f));
                g.drawText(noteName(kFirstSliceKey + i), juce::Rectangle<float>(x + 2.0f, stage.getBottom() - 12.0f,
                                                                               width - 2.0f, 12.0f),
                           juce::Justification::centredLeft);
            }
        }

    for (const float head : heads)
        if (head >= 0.0f)
        {
            g.setColour(yellow.withAlpha(0.3f));
            g.fillRect(juce::Rectangle<float>(xAt(head) - 2.0f, stage.getY(), 5.0f, stage.getHeight()));
            g.setColour(yellow);
            g.drawVerticalLine(juce::roundToInt(xAt(head)), stage.getY(), stage.getBottom());
        }

    juce::String caption = tr(sample->name.c_str()).toUpperCase();
    caption << juce::String::fromUTF8(" · ") << juce::String(length / sample->sampleRate, 2) << " S";
    caption << juce::String::fromUTF8(" · ") << (sample->channels > 1 ? "STEREO" : "MONO");
    if (settings.mode == Mode::slices)
        caption << juce::String::fromUTF8(" · ") << slices.count << " " << tr("SLICES AB") << " " << noteName(kFirstSliceKey);
    else
        caption << " " + tr("· GRUNDTON") + " " << noteName(settings.root);
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.85f));
    g.drawText(caption, inner.toNearestInt(), juce::Justification::topLeft);
}

SampleSection::SampleSection(SamplerProcessor& processor)
    : Section(processor.state, "Sample",
              { { { "source", "Quelle" }, { "mode", "Modus" }, { "root", "Grundton" }, { "tune", "Transp." },
                  { "fine", "Fein" }, { "start", "Start" }, { "end", "Ende" }, { "reverse", tr("Rückwärts") },
                  { "playback", "Wiedergabe" } } },
              {}, { 0, kDisplay }),
      view(processor),
      synth(processor)
{
    addAndMakeVisible(view);
    load.setTooltip(tr("Eigene Audiodatei laden (oder auf die Anzeige ziehen)"));
    load.onClick = [this] { choose(); };
    addAndMakeVisible(load);
    view.update();
}

void SampleSection::resized()
{
    Section::resized();
    const auto area = displayArea().reduced(0, 4);
    view.setBounds(area);
    load.setBounds(area.getRight() - 78, area.getY() + 4, 72, 20);
}

void SampleSection::choose()
{
    chooser = std::make_unique<juce::FileChooser>(tr("Audiodatei als Sample"), synth.userFile(), kAudioFiles);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& c) {
                             const auto file = tonwerkui::keepChosen(c, tonwerkui::keptFilesFolder("Tonwerk Sampler"));
                             if (file.existsAsFile())
                                 synth.loadFile(file);
                         });
}

bool SampleSection::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (juce::File(file).hasFileExtension(kAudioFiles))
            return true;
    return false;
}

void SampleSection::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
        if (juce::File(file).hasFileExtension(kAudioFiles) && synth.loadFile(juce::File(file)))
            return;
}

SamplerEditor::SamplerEditor(SamplerProcessor& p)
    : AudioProcessorEditor(p),
      sample(p),
      synth(p),
      loop(p.state, "Loop",
           { { { "loop", "Loop" }, { "loopStart", "Start" }, { "loopEnd", "Ende" }, { "crossfade", "Crossfade" } } }),
      slices(p.state, "Slices", { { { "slices", "Anzahl" }, { "sliceMode", "Schnitt" }, { "choke", "Einzeln" } } }),
      voice(p.state, "Stimmen",
            { { { "voiceMode", "Modus" }, { "glide", "Glide" }, { "bendRange", "Bend" }, { "velocity", "Anschlag" },
                { "lofi", "Lo-Fi" }, { "master", "Master" } } }),
      filter(p.state, "Filter",
             { { { "filter", "Typ" }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" },
                 { "envAmount", tr("Hüllkurve") }, { "keytrack", "Keytrack" }, { "filterEnvAttack", "A" },
                 { "filterEnvDecay", "D" }, { "filterEnvSustain", "S" }, { "filterEnvRelease", "R" } } }),
      amp(p.state, "Amp",
          { { { "ampAttack", "Attack" }, { "ampDecay", "Decay" }, { "ampSustain", "Sustain" },
              { "ampRelease", "Release" } } }),
      lfo(p.state, "LFO",
          { { { "lfoShape", "Form" }, { "lfoSync", "Sync" }, { "lfoRate", "Rate", 0, "lfoSync", false },
              { "lfoDivision", "Raster", 0, "lfoSync", true }, { "lfoTarget", "Ziel" }, { "lfoAmount", "Menge" } } }),
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
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &sample, &loop, &slices, &voice,
                                                            &filter, &amp, &lfo, &effects, &keyboard })
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
                      + std::max({ rowWidth({ &sample }), rowWidth({ &loop, &slices, &voice }),
                                   rowWidth({ &filter, &amp }), rowWidth({ &lfo, &effects }) });
    const int height = kMargin * 2 + kHeader + sample.preferredHeight() + loop.preferredHeight()
                       + filter.preferredHeight() + lfo.preferredHeight() + kKeyboard + 4 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

SamplerEditor::~SamplerEditor() { setLookAndFeel(nullptr); }

void SamplerEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void SamplerEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK SAMPLER");
}

void SamplerEditor::resized()
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

    layoutRow(area, { &sample });
    layoutRow(area, { &loop, &slices, &voice });
    layoutRow(area, { &filter, &amp });
    layoutRow(area, { &lfo, &effects });
}

void SamplerEditor::timerCallback()
{
    sample.update();
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
} // namespace tonwerksampler
