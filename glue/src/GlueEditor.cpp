#include "GlueEditor.h"

#include "GluePresets.h"
#include "PresetCategories.h"

namespace tonwerkglue
{
using namespace tonwerkui;

namespace
{
constexpr int kMeterDisplay = 120;
constexpr int kScope = 100;
/** The meter's scales: reduction down to 18 dB, level from -60 dBFS. */
constexpr float kReductionRange = 18.0f;
constexpr float kLevelFloor = -60.0f;
} // namespace

MeterView::MeterView(GlueProcessor& p) : processor(p)
{
    levels.fill(kLevelFloor);
    startTimerHz(30);
}

void MeterView::refresh()
{
    shown = processor.readSettings();
    keyConnected = processor.keyConnected.load();
    newest = (newest + 1) % kHistory;
    reductions[(std::size_t) newest] = processor.reduction.load();
    pumps[(std::size_t) newest] = shown.pump ? processor.pumping.load() : 0.0f;
    levels[(std::size_t) newest] = processor.level.load();
    repaint();
}

juce::String MeterView::caption() const
{
    const auto dot = utf8(" · ");
    const float ratio = kRatios[(std::size_t) std::clamp(shown.ratio, 0, (int) kRatios.size() - 1)];
    const float attack = kAttacks[(std::size_t) std::clamp(shown.attack, 0, (int) kAttacks.size() - 1)] * 1000.0f;
    juce::String text = "GR " + juce::String(reductions[(std::size_t) newest], 1) + " DB" + dot
                        + juce::String(ratio, ratio < 2.0f ? 1 : 0) + ":1" + dot
                        + (attack < 1.0f ? juce::String(attack, 1) : juce::String(juce::roundToInt(attack))) + " MS"
                        + dot;
    if (shown.release >= kAutoRelease)
        text << "AUTO";
    else
        text << juce::String(kReleases[(std::size_t) std::max(0, shown.release)], 1) << " S";
    if (shown.source == Source::sidechain)
        text << dot << (keyConnected ? "SIDECHAIN" : "SIDECHAIN FEHLT");
    if (shown.pump)
        text << dot << "PUMPEN " << kDivisions[(std::size_t) std::clamp(shown.pumpDivision, 0, (int) kDivisions.size() - 1)].name;
    return text;
}

void MeterView::paint(juce::Graphics& g)
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

    // The static curve on the right, square: input level across, output up, both -60 to 0 dB.
    auto curveArea = inner.removeFromRight(inner.getHeight());
    inner.removeFromRight(10.0f);
    g.setColour(colours::border.withAlpha(0.7f));
    g.drawRect(curveArea);
    const float ratio = kRatios[(std::size_t) std::clamp(shown.ratio, 0, (int) kRatios.size() - 1)];
    auto toX = [&](float db) { return curveArea.getX() + (db - kLevelFloor) / -kLevelFloor * curveArea.getWidth(); };
    auto toY = [&](float db) {
        return curveArea.getBottom() - std::clamp((db - kLevelFloor) / -kLevelFloor, 0.0f, 1.0f) * curveArea.getHeight();
    };
    juce::Path curve;
    for (int k = 0; k <= 60; ++k)
    {
        const float in = kLevelFloor + (float) k;
        const float out = in - reductionFor(in, shown.thresholdDb, ratio, shown.kneeDb);
        if (k == 0)
            curve.startNewSubPath(toX(in), toY(out));
        else
            curve.lineTo(toX(in), toY(out));
    }
    g.setColour(colours::yellow.withAlpha(0.9f));
    g.strokePath(curve, juce::PathStrokeType(1.5f));
    g.setColour(colours::red.withAlpha(0.5f));
    g.drawVerticalLine(juce::roundToInt(toX(shown.thresholdDb)), curveArea.getY(), curveArea.getBottom());
    const float now = std::max(kLevelFloor, levels[(std::size_t) newest]);
    g.setColour(colours::cyan);
    g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f)
                      .withCentre({ toX(now), toY(now - reductionFor(now, shown.thresholdDb, ratio, shown.kneeDb)) }));

    // The history, oldest on the left: level as a cyan line, reductions hanging from the top.
    const float column = inner.getWidth() / (float) kHistory;
    juce::Path level;
    for (int k = 0; k < kHistory; ++k)
    {
        const auto index = (std::size_t) ((newest + 1 + k) % kHistory);
        const float x = inner.getX() + (float) k * column;
        const float compressed = std::clamp(reductions[index] / kReductionRange, 0.0f, 1.0f) * inner.getHeight();
        const float pumped = std::clamp(pumps[index] / kReductionRange, 0.0f, 1.0f) * inner.getHeight();
        g.setColour(colours::red.withAlpha(0.75f));
        g.fillRect(juce::Rectangle<float>(x, inner.getY(), std::max(1.0f, column - 1.0f), compressed));
        g.setColour(colours::yellow.withAlpha(0.6f));
        g.fillRect(juce::Rectangle<float>(x, inner.getY() + compressed, std::max(1.0f, column - 1.0f), pumped));
        const float y = inner.getBottom()
                        - std::clamp((levels[index] - kLevelFloor) / -kLevelFloor, 0.0f, 1.0f) * inner.getHeight();
        if (k == 0)
            level.startNewSubPath(x, y);
        else
            level.lineTo(x, y);
    }
    // The level as a faint area from the bottom, so the reductions hanging from the top read over it.
    juce::Path filled = level;
    filled.lineTo(inner.getX() + (float) (kHistory - 1) * column, inner.getBottom());
    filled.lineTo(inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour(colours::cyan.withAlpha(0.15f));
    g.fillPath(filled);
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.strokePath(level, juce::PathStrokeType(1.2f));
    // The threshold across the history, on the level's scale.
    g.setColour(colours::red.withAlpha(0.4f));
    g.drawHorizontalLine(juce::roundToInt(inner.getBottom()
                                          - (shown.thresholdDb - kLevelFloor) / -kLevelFloor * inner.getHeight()),
                         inner.getX(), inner.getRight());
}

MeterSection::MeterSection(GlueProcessor& p)
    : Section(p.state, "Kompressor",
              { { { "threshold", "Threshold" },
                  { "ratio", "Ratio" },
                  { "attack", "Attack" },
                  { "release", "Release" },
                  { "knee", "Knie" },
                  { "makeup", "Make-up" } } },
              {}, { 0, kMeterDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void MeterSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

GlueEditor::GlueEditor(GlueProcessor& p)
    : AudioProcessorEditor(p),
      meter(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kGlueCategories)),
      scope(p.scopeOut, &p.scopeIn),
      key(p.state, "Sidechain", { { { "source", "Quelle" }, { "scHighpass", "Hochpass" } } }),
      pump(p.state, "Pumpen",
           { { { "pumpDivision", "Notenwert" }, { "pumpDepth", "Tiefe" }, { "pumpLength", utf8("Länge") },
               { "pumpCurve", "Kurve" } } },
           "pump"),
      output(p.state, "Ausgang", { { { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("GLUE", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &meter, &scope, &key, &pump, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(meter.preferredWidth() + 160,
                                 key.preferredWidth() + pump.preferredWidth() + output.preferredWidth() + 2 * kGap);
    setSize(width, 2 * kMargin + kHeader + meter.preferredHeight() + kGap + key.preferredHeight() + kGap + kScope);
}

GlueEditor::~GlueEditor() { setLookAndFeel(nullptr); }

void GlueEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK GLUE");
}

void GlueEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &meter });
    layoutRow(area, { &key, &pump, &output });
}
} // namespace tonwerkglue
