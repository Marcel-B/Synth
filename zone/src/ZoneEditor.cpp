#include "ZoneEditor.h"
#include "Language.h"

#include "PresetCategories.h"
#include "ZonePresets.h"

namespace tonwerkzone
{
using namespace tonwerkui;

namespace
{
constexpr int kZoneDisplay = 160;
constexpr int kScope = 100;
/** The display's scales: frequency 20 Hz to 20 kHz, response -30 to +6 dB, level -60 to 0 dBFS. */
constexpr float kLowestHz = 20.0f, kHighestHz = 20000.0f;
constexpr float kResponseFloor = -30.0f, kResponseTop = 6.0f;
constexpr float kLevelFloor = -60.0f;
/** How fast a shown level or reduction falls back, in dB per frame (30 a second). */
constexpr float kFall = 1.0f;
} // namespace

ZoneView::ZoneView(ZoneProcessor& p) : processor(p) { startTimerHz(30); }

void ZoneView::refresh()
{
    shown = processor.readSettings();
    // With the LFO on, the curve, the marker and the caption follow the range where it is now.
    if (shown.lfoSemitones > 0.0f)
        shown.frequency = processor.frequency.load();
    keyConnected = processor.keyConnected.load();
    level = std::max(processor.level.exchange(-100.0f), level - kFall);
    reduction = std::max(processor.reduction.exchange(0.0f), reduction - kFall);
    repaint();
}

juce::String ZoneView::caption() const
{
    const auto dot = tonwerkui::tr(" · ");
    const juce::String shapes[] = { tonwerkui::tr("GLOCKE"), tonwerkui::tr("TIEFEN BIS"), tonwerkui::tr("HÖHEN AB") };
    juce::String text = shapes[(int) shown.shape] + " " + juce::String(juce::roundToInt(shown.frequency)) + " HZ";
    if (shown.shape == Shape::bell)
        text << dot << juce::roundToInt(shown.widthSemitones) << tonwerkui::tr(" HT");
    text << dot << (shown.mode == Mode::band ? "BAND" : tonwerkui::tr("BREITBAND"));
    if (shown.source == Source::sidechain)
        text << dot << (keyConnected ? "SIDECHAIN" : tonwerkui::tr("SIDECHAIN FEHLT"));
    if (shown.lfoSemitones > 0.0f)
        text << dot << tonwerkui::tr("LFO ±") << juce::roundToInt(shown.lfoSemitones) << tonwerkui::tr(" HT");
    if (shown.listen)
        text << dot << tonwerkui::tr("HÖREN");
    text << dot << "GR " << juce::String(reduction, 1) << " DB";
    return text;
}

void ZoneView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(shown.source == Source::sidechain && ! keyConnected ? colours::red : colours::cyan.withAlpha(0.8f));
    g.drawText(caption(), inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(4.0f);

    // On the right: the detector's level against the threshold, the reduction hanging from the top.
    auto meter = inner.removeFromRight(46.0f);
    inner.removeFromRight(10.0f);
    auto labels = inner.removeFromBottom(12.0f);
    meter.removeFromBottom(12.0f);
    auto levelY = [&](float db) {
        return meter.getBottom() - std::clamp((db - kLevelFloor) / -kLevelFloor, 0.0f, 1.0f) * meter.getHeight();
    };
    const auto levelBar = meter.removeFromLeft(20.0f);
    g.setColour(colours::border.withAlpha(0.7f));
    g.drawRect(levelBar);
    g.setColour(colours::cyan.withAlpha(0.6f));
    g.fillRect(levelBar.withTop(levelY(level)));
    g.setColour(colours::red);
    g.fillRect(juce::Rectangle<float>(levelBar.getX() - 2.0f, levelY(shown.thresholdDb) - 1.0f, levelBar.getWidth() + 4.0f, 2.0f));
    meter.removeFromLeft(6.0f);
    g.setColour(colours::border.withAlpha(0.7f));
    g.drawRect(meter);
    g.setColour(colours::red.withAlpha(0.8f));
    g.fillRect(meter.withHeight(std::clamp(reduction / 24.0f, 0.0f, 1.0f) * meter.getHeight()));

    const float span = std::log(kHighestHz / kLowestHz);
    auto toX = [&](float hz) { return inner.getX() + std::log(hz / kLowestHz) / span * inner.getWidth(); };
    auto toY = [&](float db) {
        return inner.getBottom()
               - std::clamp((db - kResponseFloor) / (kResponseTop - kResponseFloor), 0.0f, 1.0f) * inner.getHeight();
    };
    g.setColour(colours::border.withAlpha(0.6f));
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine(juce::roundToInt(toX(hz)), inner.getY(), inner.getBottom());
    for (const float db : { -24.0f, -12.0f })
        g.drawHorizontalLine(juce::roundToInt(toY(db)), inner.getX(), inner.getRight());
    g.setColour(colours::muted);
    g.setFont(mono(9.0f));
    for (const auto& [hz, name] : { std::pair { 100.0f, "100" }, { 1000.0f, "1K" }, { 10000.0f, "10K" } })
        g.drawText(name, juce::Rectangle<float>(toX(hz) + 3.0f, labels.getY(), 40.0f, labels.getHeight()),
                   juce::Justification::centredLeft);
    g.setColour(colours::text.withAlpha(0.35f));
    g.drawHorizontalLine(juce::roundToInt(toY(0.0f)), inner.getX(), inner.getRight());

    // The range: the response at the full range, shaded, and the response now, in yellow.
    juce::Path full, now;
    const float deepest = std::pow(10.0f, -std::max(0.5f, shown.rangeDb) / 20.0f);
    const float gain = std::pow(10.0f, -reduction / 20.0f);
    Settings band = shown;
    band.mode = Mode::band;
    for (int px = 0; px <= (int) inner.getWidth(); px += 2)
    {
        const float x = inner.getX() + (float) px;
        const float hz = kLowestHz * std::exp((float) px / inner.getWidth() * span);
        const float yFull = toY(responseDb(hz, band, deepest));
        const float yNow = toY(responseDb(hz, shown, gain));
        if (px == 0)
        {
            full.startNewSubPath(x, toY(0.0f));
            now.startNewSubPath(x, yNow);
        }
        else
            now.lineTo(x, yNow);
        full.lineTo(x, yFull);
    }
    full.lineTo(inner.getRight(), toY(0.0f));
    full.closeSubPath();
    g.setColour(colours::cyan.withAlpha(shown.listen ? 0.35f : 0.15f));
    g.fillPath(full);
    g.setColour(colours::yellow);
    g.strokePath(now, juce::PathStrokeType(2.0f));
    const float x = toX(std::clamp(shown.frequency, kLowestHz, kHighestHz));
    g.setColour(colours::yellow.withAlpha(0.8f));
    const float dashes[] = { 4.0f, 3.0f };
    g.drawDashedLine(juce::Line<float>(x, inner.getY(), x, inner.getBottom()), dashes, 2, 1.2f);
}

ZoneSection::ZoneSection(ZoneProcessor& p)
    : Section(p.state, "Zone",
              { { { "shape", "Form" },
                  { "frequency", "Frequenz" },
                  { "width", "Breite" },
                  { "mode", "Modus" },
                  { "listen", tonwerkui::tr("Hören") } } },
              {}, { 0, kZoneDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void ZoneSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

ZoneEditor::ZoneEditor(ZoneProcessor& p)
    : AudioProcessorEditor(p),
      zone(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kZoneCategories)),
      scope(p.scopeOut, &p.scopeIn),
      compressor(p.state, "Kompressor",
                 { { { "threshold", "Threshold" },
                     { "ratio", "Ratio" },
                     { "attack", "Attack" },
                     { "release", "Release" },
                     { "range", "Bereich" } } }),
      lfo(p.state, "LFO",
          { { { "lfoShape", "Form", 2 },
              { "lfoSync", "Sync" },
              { "lfoDivision", "Takt", 0, "lfoSync", true },
              { "lfoRate", "Rate", 0, "lfoSync", false },
              { "lfoDepth", "Tiefe" } } }),
      output(p.state, "Ausgang", { { { "source", "Quelle" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("ZONE", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &zone, &scope, &compressor, &lfo, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin + std::max(zone.preferredWidth() + 260, compressor.preferredWidth() + lfo.preferredWidth() + output.preferredWidth() + 2 * kGap);
    setSize(width, 2 * kMargin + kHeader + zone.preferredHeight() + kGap + compressor.preferredHeight() + kGap + kScope);
}

ZoneEditor::~ZoneEditor() { setLookAndFeel(nullptr); }

void ZoneEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK ZONE");
}

void ZoneEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &zone });
    layoutRow(area, { &compressor, &lfo, &output });
}
} // namespace tonwerkzone
