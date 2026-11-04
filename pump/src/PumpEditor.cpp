#include "PumpEditor.h"

#include "PresetCategories.h"
#include "PumpPresets.h"

namespace tonwerkpump
{
using namespace tonwerkui;

namespace
{
constexpr int kMeterDisplay = 120;
constexpr int kScope = 90;
/** The meter's scales: reduction down to 30 dB, level from -60 dBFS. */
constexpr float kReductionRange = 30.0f;
constexpr float kLevelFloor = -60.0f;

const char* modeName(Mode mode)
{
    switch (mode)
    {
        case Mode::trigger: return "AUSLÖSER";
        case Mode::compressor: return "KOMPRESSOR";
        case Mode::tempo: return "TEMPO";
    }
    return "";
}
} // namespace

MeterView::MeterView(PumpProcessor& p) : processor(p)
{
    levels.fill(kLevelFloor);
    startTimerHz(30);
}

void MeterView::refresh()
{
    shown = processor.readSettings();
    keyConnected = processor.keyConnected.load();
    newest = (newest + 1) % kHistory;
    reductions[(std::size_t) newest] = processor.reduction.exchange(0.0f);
    levels[(std::size_t) newest] = std::max(kLevelFloor, processor.level.exchange(-100.0f));
    hit[(std::size_t) newest] = processor.hits.exchange(0) > 0 && shown.mode == Mode::trigger;
    repaint();
}

juce::String MeterView::caption() const
{
    const auto dot = utf8(" · ");
    juce::String text = "GR " + juce::String(reductions[(std::size_t) newest], 1) + " DB" + dot + tr(modeName(shown.mode));
    if (shown.mode == Mode::tempo)
        text << dot << kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) kDivisions.size() - 1)].name;
    else if (shown.source == Source::input)
        text << dot << tr("EINGANG");
    else
        text << dot << (keyConnected ? juce::String("SIDECHAIN") : tr("SIDECHAIN FEHLT"));
    if (shown.band != Band::full)
        text << dot << (shown.band == Band::low ? tr("TIEFEN") : tr("HÖHEN")) << " "
             << juce::roundToInt(shown.split) << " HZ";
    if (shown.listen)
        text << dot << tr("SIDECHAIN HÖREN");
    return text;
}

void MeterView::paintShape(juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour(colours::border.withAlpha(0.7f));
    g.drawRect(area);
    juce::Path path;
    if (shown.mode == Mode::compressor)
    {
        // The static curve: key level across, reduction down, both on the history's scales.
        for (int k = 0; k <= 60; ++k)
        {
            const float in = kLevelFloor + (float) k;
            const float down = reductionFor(in, shown.thresholdDb, shown.ratio, shown.depthDb);
            const float x = area.getX() + (float) k / 60.0f * area.getWidth();
            const float y = area.getY() + std::clamp(down / kReductionRange, 0.0f, 1.0f) * area.getHeight();
            if (k == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(colours::red.withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(area.getX() + (shown.thresholdDb - kLevelFloor) / -kLevelFloor * area.getWidth()),
                           area.getY(), area.getBottom());
    }
    else
    {
        // One duck as gain over time: a step of the tempo, or the curve's whole length plus a little in trigger mode.
        const double step = kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) kDivisions.size() - 1)].beats * 0.5;
        const double span = shown.mode == Mode::tempo
                                ? step
                                : 1.15 * (std::max(shown.attack, kShortestFall) + shown.hold + shown.release);
        const float release = shown.mode == Mode::tempo
                                  ? (float) std::max(0.0, std::min((double) shown.release,
                                                                   step - std::max(shown.attack, kShortestFall) - shown.hold - 0.001))
                                  : shown.release;
        for (int k = 0; k <= 100; ++k)
        {
            const double t = span * k / 100.0;
            const float down = duckAt(t, 0.0f, shown.depthDb, shown.attack, shown.hold, release, shown.shape);
            const float gain = std::pow(10.0f, -down / 20.0f);
            const float x = area.getX() + (float) k / 100.0f * area.getWidth();
            const float y = area.getBottom() - gain * area.getHeight();
            if (k == 0)
                path.startNewSubPath(x, area.getBottom());
            path.lineTo(x, y);
        }
    }
    g.setColour(colours::yellow.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void MeterView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    const bool missing = shown.mode != Mode::tempo && shown.source == Source::sidechain && ! keyConnected;
    g.setColour(missing ? colours::red : colours::cyan.withAlpha(0.8f));
    g.drawText(caption(), inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(4.0f);

    auto shapeArea = inner.removeFromRight(inner.getHeight() * 1.4f);
    inner.removeFromRight(10.0f);
    paintShape(g, shapeArea);

    // The history, oldest on the left: the key's level as a cyan area, the reduction hanging from the top.
    const float column = inner.getWidth() / (float) kHistory;
    juce::Path level;
    for (int k = 0; k < kHistory; ++k)
    {
        const auto index = (std::size_t) ((newest + 1 + k) % kHistory);
        const float x = inner.getX() + (float) k * column;
        if (hit[index])
        {
            g.setColour(colours::yellow.withAlpha(0.35f));
            g.fillRect(juce::Rectangle<float>(x, inner.getY(), std::max(1.0f, column - 1.0f), inner.getHeight()));
        }
        const float down = std::clamp(reductions[index] / kReductionRange, 0.0f, 1.0f) * inner.getHeight();
        g.setColour(colours::red.withAlpha(0.75f));
        g.fillRect(juce::Rectangle<float>(x, inner.getY(), std::max(1.0f, column - 1.0f), down));
        const float y = inner.getBottom()
                        - std::clamp((levels[index] - kLevelFloor) / -kLevelFloor, 0.0f, 1.0f) * inner.getHeight();
        if (k == 0)
            level.startNewSubPath(x, y);
        else
            level.lineTo(x, y);
    }
    if (shown.mode != Mode::tempo)
    {
        juce::Path filled = level;
        filled.lineTo(inner.getX() + (float) (kHistory - 1) * column, inner.getBottom());
        filled.lineTo(inner.getX(), inner.getBottom());
        filled.closeSubPath();
        g.setColour(colours::cyan.withAlpha(0.15f));
        g.fillPath(filled);
        g.setColour(colours::cyan.withAlpha(0.8f));
        g.strokePath(level, juce::PathStrokeType(1.2f));
        g.setColour(colours::red.withAlpha(0.4f));
        g.drawHorizontalLine(juce::roundToInt(inner.getBottom()
                                              - (shown.thresholdDb - kLevelFloor) / -kLevelFloor * inner.getHeight()),
                             inner.getX(), inner.getRight());
    }
}

MeterSection::MeterSection(PumpProcessor& p)
    : Section(p.state, "Pumpen",
              { { { "mode", "Modus" },
                  { "source", "Quelle" },
                  { "threshold", "Threshold" },
                  { "ratio", "Ratio" },
                  { "dynamics", "Dynamik" },
                  { "division", "Notenwert" } } },
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

PumpEditor::PumpEditor(PumpProcessor& p)
    : AudioProcessorEditor(p),
      meter(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kPumpCategories)),
      scope(p.scopeOut, &p.scopeIn),
      curve(p.state, "Kurve",
            { { { "depth", "Tiefe" }, { "attack", "Attack" }, { "hold", "Hold" }, { "release", "Release" },
                { "shape", "Form" } } }),
      key(p.state, "Sidechain", { { { "keyLowpass", "Tiefpass" }, { "listen", utf8("Hören") } } }),
      band(p.state, "Bereich", { { { "band", "Bereich" }, { "split", "Trennung" } } }),
      output(p.state, "Ausgang", { { { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption(tr("OSZILLOSKOP"));
    scope.setLegend("PUMP", tr("EINGANG"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &meter, &scope, &curve, &key, &band, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(meter.preferredWidth() + 200,
                                 curve.preferredWidth() + key.preferredWidth() + band.preferredWidth()
                                     + output.preferredWidth() + 3 * kGap);
    setSize(width, 2 * kMargin + kHeader + meter.preferredHeight() + kGap + curve.preferredHeight() + kGap + kScope);
}

PumpEditor::~PumpEditor() { setLookAndFeel(nullptr); }

void PumpEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK PUMP");
}

void PumpEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &meter });
    layoutRow(area, { &curve, &key, &band, &output });
}
} // namespace tonwerkpump
