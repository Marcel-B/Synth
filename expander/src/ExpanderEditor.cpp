#include "ExpanderEditor.h"
#include "Language.h"

#include "ExpanderPresets.h"
#include "HardwareSheet.h"
#include "Platform.h"
#include "PresetCategories.h"

#include <complex>

namespace tonwerkexpander
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kScope = 110;
constexpr int kDisplay = 110;
constexpr int kKeyboard = 56;
constexpr float kLowestHz = 20.0f, kHighestHz = 20000.0f;
constexpr float kTopDb = 24.0f, kBottomDb = -36.0f;
} // namespace

ResponseView::ResponseView(ExpanderProcessor& p) : processor(p)
{
    shown = p.readSettings();
    startTimerHz(30);
}

void ResponseView::refresh()
{
    shown = processor.readSettings();
    sounding = processor.shownNote.load() >= 0;
    cutoffNow = sounding ? processor.shownCutoff.load() : shown.cutoff;
    repaint();
}

float ResponseView::responseDb(const Settings& s, float cutoff, float hz)
{
    using C = std::complex<double>;
    const double k = dampingFor(s.resonance);
    const C x(0.0, hz / std::max(1.0f, cutoff));
    const C den = x * x + k * x + 1.0;
    C h;
    if (s.bandpass)
        h = 2.0 * x / den;
    else
    {
        const auto w = modeWeights(s.mode);
        h = ((double) w[0] + (double) w[1] * x * x) / den;
    }
    return (float) (20.0 * std::log10(std::max(1.0e-6, std::abs(h))));
}

void ResponseView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
    juce::String name = tonwerkui::tr("TIEFPASS");
    if (shown.bandpass)
        name = tonwerkui::tr("BANDPASS");
    else if (shown.mode > 0.995f)
        name = tonwerkui::tr("HOCHPASS");
    else if (std::abs(shown.mode - 0.5f) < 0.005f)
        name = "NOTCH";
    else if (shown.mode > 0.005f)
        name = shown.mode < 0.5f ? tonwerkui::tr("TIEFPASS / NOTCH") : tonwerkui::tr("NOTCH / HOCHPASS");
    g.drawText(name + tonwerkui::tr(" · 12 DB · ") + juce::String(juce::roundToInt(cutoffNow)) + " HZ",
               inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(2.0f);

    auto toX = [&](float hz) {
        return inner.getX() + std::log(hz / kLowestHz) / std::log(kHighestHz / kLowestHz) * inner.getWidth();
    };
    auto toY = [&](float db) {
        return inner.getY() + (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * inner.getHeight();
    };
    g.setColour(border);
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine(juce::roundToInt(toX(hz)), inner.getY(), inner.getBottom());
    g.drawHorizontalLine(juce::roundToInt(toY(0.0f)), inner.getX(), inner.getRight());

    auto curve = [&](float cutoff) {
        juce::Path path;
        const int points = std::max(2, (int) inner.getWidth());
        for (int i = 0; i <= points; ++i)
        {
            const float hz = kLowestHz * std::pow(kHighestHz / kLowestHz, (float) i / (float) points);
            const float y = toY(responseDb(shown, cutoff, hz));
            if (i == 0)
                path.startNewSubPath(toX(hz), y);
            else
                path.lineTo(toX(hz), y);
        }
        return path;
    };
    g.setColour(cyan.withAlpha(0.35f));
    g.strokePath(curve(shown.cutoff), juce::PathStrokeType(1.0f));
    auto now = curve(cutoffNow);
    auto filled = now;
    filled.lineTo(inner.getRight(), inner.getBottom());
    filled.lineTo(inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour(yellow.withAlpha(0.12f));
    g.fillPath(filled);
    g.setColour(yellow);
    g.strokePath(now, juce::PathStrokeType(1.6f));
    g.setColour(red.withAlpha(0.8f));
    g.drawVerticalLine(juce::roundToInt(toX(std::clamp(cutoffNow, kLowestHz, kHighestHz))), inner.getY(),
                       inner.getBottom());
}

FilterSection::FilterSection(ExpanderProcessor& p)
    : Section(p.state, "Filter",
              { { { "cutoff", "Cutoff" }, { "resonance", "Resonanz" }, { "mode", "LP / HP" }, { "bandpass", "BP" },
                  { "envCutoff", "Env" }, { "keytrack", "Key" }, { "lfoCutoff", "LFO" } } },
              {}, { 0, kDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void FilterSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

ExpanderEditor::ExpanderEditor(ExpanderProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      scope(p.scope),
      osc1(p.state, "VCO 1",
           { { { "osc1Wave", "Form", 2 }, { "osc1Octave", "Oktave" }, { "osc1Width", "Breite" },
               { "osc1Level", "Pegel" } } }),
      osc2(p.state, "VCO 2",
           { { { "osc2Wave", "Form", 2 }, { "osc2Octave", "Oktave" }, { "osc2Semitones", "Halbton" },
               { "osc2Fine", "Fein" }, { "osc2Width", "Breite" }, { "osc2Level", "Pegel" }, { "sync", "Sync" },
               { "envPitch", "Env" } } }),
      filter(p),
      filterEnvelope(p.state, tonwerkui::tr("FILTER-HÜLLKURVE"),
                     { { { "filterAttack", "Attack" }, { "filterDecay", "Decay" }, { "filterSustain", "Sustain" },
                         { "filterRelease", "Release" } } }),
      ampEnvelope(p.state, tonwerkui::tr("AMP-HÜLLKURVE"),
                  { { { "ampAttack", "Attack" }, { "ampDecay", "Decay" }, { "ampSustain", "Sustain" },
                      { "ampRelease", "Release" } } }),
      lfo(p.state, "LFO",
          { { { "lfoShape", "Form", 2 }, { "lfoSync", "Sync" }, { "lfoRate", "Rate", 0, "lfoSync", false },
              { "lfoDivision", "Raster", 0, "lfoSync", true }, { "lfoDelay", "Einsatz" }, { "lfoPitch", "Pitch" },
              { "lfoPwm", "PWM" } } }),
      voices(p.state, "Stimmen",
             { { { "voiceMode", "Modus", 2 }, { "unisonDetune", "Unison" }, { "drift", "Drift" }, { "spread", "Breite" },
                 { "glide", "Glide" }, { "velocity", "Anschlag" }, { "bendRange", "Bend" }, { "master", "Master" } } }),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", tonwerkui::tr("Länge") } } }),
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
            presets.addSectionHeading(tonwerkui::tr(preset.category));
        group = category;
        presets.addItem(utf8(preset.name), i + 1);
    }
    presets.setTextWhenNothingSelected(tonwerkui::tr("Preset"));
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
    hardwareSheet.setTooltip(tonwerkui::tr("Speichert die Reglerstellungen dieses Klangs fürs Hardware-Modul als Bild"));
    hardwareSheet.onClick = [this] { exportHardwareSheet(); };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
    scope.setCaption("OSZILLOSKOP");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &hardwareSheet, &scope, &osc1, &osc2, &filter,
                                                            &filterEnvelope, &ampEnvelope, &lfo, &voices, &effects,
                                                            &keyboard })
        addAndMakeVisible(*c);

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setAvailableRange(36, 96);
    keyboard.setLowestVisibleKey(36);

    // As wide as the widest row needs.
    auto rowWidth = [](std::initializer_list<const Section*> row) {
        int width = kGap * ((int) row.size() - 1);
        for (const auto* s : row)
            width += s->preferredWidth();
        return width;
    };
    const int width = 2 * kMargin
                      + std::max({ rowWidth({ &osc1, &osc2 }), rowWidth({ &filter, &filterEnvelope, &ampEnvelope }),
                                   rowWidth({ &lfo, &voices }), rowWidth({ &effects }) });
    const int height = kMargin * 2 + kHeader + kScope + osc1.preferredHeight() + filter.preferredHeight()
                       + lfo.preferredHeight() + effects.preferredHeight() + kKeyboard + 5 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

ExpanderEditor::~ExpanderEditor() { setLookAndFeel(nullptr); }

void ExpanderEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void ExpanderEditor::exportHardwareSheet()
{
    const auto name = synth.presetName().isNotEmpty() ? synth.presetName() : juce::String(tonwerkui::tr("Klang"));
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                           .getChildFile(juce::File::createLegalFileName(name + " Hardware") + ".png");
    chooser = std::make_unique<juce::FileChooser>(tonwerkui::tr("Hardware-Blatt speichern"), start, "*.png");
    juce::Component::SafePointer<ExpanderEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe, name](const juce::FileChooser& fc) {
                             if (safe == nullptr || tonwerkui::nothingChosen(fc))
                                 return;
                             juce::MemoryOutputStream png;
                             juce::PNGImageFormat().writeImageToStream(
                                 drawHardwareSheet(safe->synth.readSettings(), name), png);
#if JUCE_IOS || TONWERK_SANDBOX
                             // The picker names the file; the sandbox allows only the file chosen.
                             const auto shown = tonwerkui::chosenName(fc);
                             const bool written = tonwerkui::writeChosenData(fc, png.getData(), png.getDataSize());
#else
                             const auto file = fc.getResult().withFileExtension("png");
                             const auto shown = file.getFullPathName();
                             const bool written = tonwerkui::writeInPlace(file, png.getData(), png.getDataSize());
#endif
                             if (! written)
                                 juce::AlertWindow::showAsync(
                                     juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(tonwerkui::tr("Hardware-Blatt"))
                                         .withMessage(shown + tonwerkui::tr(" ließ sich nicht schreiben."))
                                         .withButton("OK")
                                         .withAssociatedComponent(safe.getComponent()),
                                     nullptr);
                         });
}

void ExpanderEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK EXPANDER");
}

void ExpanderEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));
    header.removeFromRight(12);
    hardwareSheet.setBounds(header.removeFromRight(120));

    keyboard.setBounds(area.removeFromBottom(kKeyboard));
    keyboard.setKeyWidth((float) keyboard.getWidth() / 35.0f);
    area.removeFromBottom(kGap);

    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &osc1, &osc2 });
    layoutRow(area, { &filter, &filterEnvelope, &ampEnvelope });
    layoutRow(area, { &lfo, &voices });
    layoutRow(area, { &effects });
}

void ExpanderEditor::timerCallback()
{
    juce::String caption = tonwerkui::tr("OSZILLOSKOP");
    if (const int note = synth.shownNote.load(); note >= 0)
    {
        caption << tonwerkui::tr("  ·  ") << ExpanderProcessor::noteName(note);
        const int count = synth.shownVoices.load();
        caption << tonwerkui::tr("  ·  ") << count << (count == 1 ? tonwerkui::tr(" STIMME") : tonwerkui::tr(" STIMMEN"));
    }
    if (caption != shownCaption)
    {
        shownCaption = caption;
        scope.setCaption(caption);
    }
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
} // namespace tonwerkexpander
