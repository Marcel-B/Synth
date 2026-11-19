#include "BassEditor.h"

#include <complex>

#include "BassPresets.h"
#include "Platform.h"
#include "PresetCategories.h"

namespace tonwerkbass
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kScope = 120;
constexpr int kKeyboard = 56;
constexpr int kDisplay = 110;
constexpr float kLowestHz = 20.0f, kHighestHz = 20000.0f;
constexpr float kTopDb = 30.0f, kBottomDb = -42.0f;
} // namespace

ResponseView::ResponseView(BassProcessor& p) : processor(p)
{
    refresh();
    startTimerHz(30);
}

void ResponseView::refresh()
{
    shown = processor.readSettings();
    const bool sounding = processor.shownNote.load() >= 0;
    cutoffNow = sounding ? processor.shownCutoff.load() : shown.cutoff;
    accented = sounding && processor.shownAccent.load();
    repaint();
}

float ResponseView::responseDb(float resonance, float cutoff, float hz)
{
    // Four one-pole stages with the feedback k around them, the bass given back by 1 + k/2 as the engine does.
    using C = std::complex<double>;
    const double k = 4.0 * std::pow(std::clamp((double) resonance, 0.0, 1.0), 0.7) * 0.98;
    const C stage = 1.0 / C(1.0, hz / std::max(1.0f, cutoff));
    const C four = stage * stage * stage * stage;
    const C h = (1.0 + 0.5 * k) * four / (1.0 + k * four);
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
    g.drawText(tr("TIEFPASS") + juce::String::fromUTF8(" · 24 DB · ") + juce::String(juce::roundToInt(cutoffNow)) + " HZ"
                   + (accented ? juce::String::fromUTF8(" · ACCENT") : juce::String()),
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
            const float y = toY(responseDb(shown.resonance, cutoff, hz));
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
    g.setColour((accented ? red : yellow).withAlpha(0.12f));
    g.fillPath(filled);
    g.setColour(accented ? red : yellow);
    g.strokePath(now, juce::PathStrokeType(1.6f));
}

WaveView::WaveView(BassProcessor& p) : processor(p)
{
    refresh();
    startTimerHz(10);
}

void WaveView::refresh()
{
    const auto now = processor.readSettings();
    if (now.wave == shown.wave && std::abs(now.sub - shown.sub) < 0.001f && getWidth() > 0 && ! firstPaint)
        return;
    firstPaint = false;
    shown = now;
    repaint();
}

float WaveView::valueAt(const Settings& s, float phase)
{
    const float cycle = 2.0f * phase - std::floor(2.0f * phase);
    const float wave = s.wave == Wave::saw ? 2.0f * cycle - 1.0f : (cycle < 0.5f ? 1.0f : -1.0f);
    return wave + s.sub * (phase < 0.5f ? 1.0f : -1.0f);
}

void WaveView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(cyan.withAlpha(0.8f));
    g.drawText((shown.wave == Wave::saw ? tr("SÄGEZAHN") : tr("RECHTECK"))
                   + (shown.sub > 0.005f ? juce::String::fromUTF8(" · SUB ") + juce::String(juce::roundToInt(shown.sub * 100.0f)) + " %"
                                         : juce::String()),
               inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(4.0f);
    inner.removeFromBottom(2.0f);
    g.setColour(border);
    g.drawHorizontalLine(juce::roundToInt(inner.getCentreY()), inner.getX(), inner.getRight());

    // Two of the sub's cycles, so four of the wave's; scaled so the sub at full never leaves the glass.
    juce::Path path;
    const int points = std::max(2, (int) inner.getWidth());
    for (int i = 0; i <= points; ++i)
    {
        const float phase = 2.0f * (float) i / (float) points;
        const float value = valueAt(shown, phase - std::floor(phase));
        const float x = inner.getX() + (float) i / (float) points * inner.getWidth();
        const float y = inner.getCentreY() - value * 0.5f * inner.getHeight() * 0.45f * 2.0f / 2.0f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(yellow);
    g.strokePath(path, juce::PathStrokeType(1.6f));
}

BassEditor::BassEditor(BassProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      scope(p.scope),
      oscillator(p, "Oszillator",
                 { { { "wave", "Welle" }, { "tune", "Tune" }, { "fine", "Fein" }, { "sub", "Sub" } } }, kDisplay),
      filter(p, "Filter",
             { { { "cutoff", "Cutoff" }, { "resonance", "Resonanz" }, { "envMod", "Env Mod" }, { "decay", "Decay" },
                 { "keytrack", "Keytrack" } } },
             kDisplay),
      accent(p.state, "Accent",
             { { { "accent", "Accent" }, { "accentThreshold", "Ab Anschlag" }, { "sweep", "Sweep" } } }),
      envelope(p.state, "Slide & Ausklang",
               { { { "slide", "Slide" }, { "ampDecay", "Ausklang" }, { "release", "Release" } } }),
      output(p.state, "Ausgang", { { { "drive", "Drive" }, { "bendRange", "Bend" }, { "master", "Master" } } }),
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
    scope.setCaption(tr("OSZILLOSKOP"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &scope, &oscillator, &filter,
                                                            &accent, &envelope, &output, &lfo, &effects, &keyboard })
        addAndMakeVisible(*c);
    // Inside the iPad app the scope beside its keys stands in for this one.
    scope.setVisible(tonwerkui::editorsHaveScope());

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    // The bass's own range, C0 to C4.
    keyboard.setAvailableRange(24, 72);
    keyboard.setLowestVisibleKey(24);

    // As wide as the widest row needs.
    auto rowWidth = [](std::initializer_list<const Section*> row) {
        int width = kGap * ((int) row.size() - 1);
        for (const auto* s : row)
            width += s->preferredWidth();
        return width;
    };
    const int width = 2 * kMargin
                      + std::max({ rowWidth({ &oscillator, &filter }), rowWidth({ &accent, &envelope, &output }),
                                   rowWidth({ &lfo, &effects }) });
    const int height = kMargin * 2 + headerHeight() + (scope.isVisible() ? kScope + kGap : 0) + oscillator.preferredHeight() + accent.preferredHeight()
                       + lfo.preferredHeight() + kKeyboard + 3 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

BassEditor::~BassEditor() { setLookAndFeel(nullptr); }

void BassEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void BassEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + headerHeight());
    if (headerHeight() > 0)
        paintTitle(g, header, "TONWERK BASS");
}

void BassEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(headerHeight()).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));

    keyboard.setBounds(area.removeFromBottom(kKeyboard));
    keyboard.setKeyWidth((float) keyboard.getWidth() / 28.0f);
    area.removeFromBottom(kGap);

    if (scope.isVisible())
    {
        scope.setBounds(area.removeFromTop(kScope));
        area.removeFromTop(kGap);
    }
    layoutRow(area, { &oscillator, &filter });
    layoutRow(area, { &accent, &envelope, &output });
    layoutRow(area, { &lfo, &effects });
}

void BassEditor::timerCallback()
{
    const juce::String caption = tr("OSZILLOSKOP") + synth.scopeNote();
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

std::vector<tonwerkui::CompactPage> bassPages()
{
    using Row = Section::Row;
    return {
        { "Haupt",
          { { "Haupt", Row { { "cutoff", "Cutoff" },
                             { "resonance", "Resonanz" },
                             { "envMod", "Env Mod" },
                             { "decay", "Decay" },
                             { "accent", "Accent" },
                             { "slide", "Slide" },
                             { "drive", "Drive" },
                             { "fxDelayMix", "Delay" },
                             { "fxReverbMix", "Hall" },
                             { "master", "Master" } } } } },
        { "Klang",
          { { "Oszillator", Row { { "wave", "Welle" }, { "tune", "Tune" }, { "fine", "Fein" }, { "sub", "Sub" } } },
            { "Filter", Row { { "cutoff", "Cutoff" },
                              { "resonance", "Resonanz" },
                              { "envMod", "Env Mod" },
                              { "decay", "Decay" },
                              { "keytrack", "Keytrack" } } } } },
        { "Spiel",
          { { "Accent", Row { { "accent", "Accent" }, { "accentThreshold", "Ab Anschlag" }, { "sweep", "Sweep" } } },
            { "Slide & Ausklang", Row { { "slide", "Slide" }, { "ampDecay", "Ausklang" }, { "release", "Release" } } },
            { "Ausgang", Row { { "drive", "Drive" }, { "bendRange", "Bend" }, { "master", "Master" } } } } },
        { "LFO",
          { { "LFO", Row { { "lfoShape", "Form" },
                           { "lfoSync", "Sync" },
                           { "lfoRate", "Rate", 0, "lfoSync", false },
                           { "lfoDivision", "Raster", 0, "lfoSync", true },
                           { "lfoTarget", "Ziel" },
                           { "lfoAmount", "Menge" } } } } },
        { "Effekte",
          { { "Effekte", Row { { "fxDelayMix", "Delay" },
                               { "fxDelaySync", "Sync" },
                               { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                               { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                               { "fxDelayFeedback", "Feedback" },
                               { "fxDelayTone", "Ton" },
                               { "fxReverbMix", "Hall" },
                               { "fxReverbDecay", utf8("Länge") } } } } },
    };
}
} // namespace tonwerkbass
