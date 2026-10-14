#include "ShimmerEditor.h"

#include "PresetCategories.h"
#include "ShimmerPresets.h"

namespace tonwerkshimmer
{
using namespace tonwerkui;

namespace
{
constexpr int kTailDisplay = 110;
constexpr int kScope = 100;
constexpr float kFloorDb = -60.0f;
} // namespace

TailView::TailView(ShimmerProcessor& p) : processor(p) { startTimerHz(30); }

void TailView::refresh()
{
    shown = processor.readSettings();
    level = processor.tailLevel.load();
    repaint();
}

juce::String TailView::caption() const
{
    const auto dot = utf8(" · ");
    juce::String text = "DECAY " + (shown.decay < 9.95f ? juce::String(shown.decay, 1)
                                                        : juce::String(juce::roundToInt(shown.decay)))
                        + " S" + dot + "PREDELAY " + juce::String(juce::roundToInt(shown.predelay * 1000.0f)) + " MS";
    if (shown.hold)
        text << dot << "HALTEN";
    else if (shown.shimmer > 0.0f)
        text << dot
             << utf8(kIntervals[(std::size_t) std::clamp(shown.interval, 0, (int) kIntervals.size() - 1)].name)
                    .toUpperCase()
             << " " << juce::roundToInt(shown.shimmer * 100.0f) << " %";
    return text;
}

void TailView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.drawText(caption(), inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(4.0f);

    // Time across: the predelay and the decay with a little room, at least two seconds.
    const double decay = shown.hold ? 30.0 : std::max(0.3, (double) shown.decay);
    const double span = shown.predelay + std::max(2.0, std::min(decay * 1.15, 30.0));
    auto toX = [&](double t) { return inner.getX() + (float) (t / span) * inner.getWidth(); };
    auto toY = [&](float db) {
        return inner.getBottom() - std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f) * inner.getHeight();
    };
    // The shimmer's share after a round trip: a quarter of `shimmer` (two of eight lines), compounding.
    const double size = 0.35 + 1.25 * std::clamp((double) shown.size, 0.0, 1.0);
    const double trip = 0.0511 * size;
    const double kept = 1.0 - 0.25 * (shown.hold ? 0.0 : (double) shown.shimmer);
    juce::Path tailPath, liftedPath;
    const int points = std::max(2, (int) inner.getWidth());
    tailPath.startNewSubPath(toX(0.0), inner.getBottom());
    liftedPath.startNewSubPath(toX(shown.predelay), inner.getBottom());
    tailPath.lineTo(toX(shown.predelay), inner.getBottom());
    for (int k = 0; k <= points; ++k)
    {
        const double t = shown.predelay + (double) k / points * (span - shown.predelay);
        const double after = t - shown.predelay;
        const auto db = (float) (-60.0 * after / decay);
        const double lifted = 1.0 - std::pow(kept, after / trip);
        tailPath.lineTo(toX(t), toY(db));
        liftedPath.lineTo(toX(t), toY(db + (float) (20.0 * std::log10(std::max(1.0e-6, lifted)))));
    }
    tailPath.lineTo(toX(span), inner.getBottom());
    tailPath.closeSubPath();
    liftedPath.lineTo(toX(span), inner.getBottom());
    liftedPath.closeSubPath();
    g.setColour(colours::cyan.withAlpha(0.18f));
    g.fillPath(tailPath);
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.strokePath(tailPath, juce::PathStrokeType(1.2f));
    if (shown.shimmer > 0.0f && ! shown.hold)
    {
        g.setColour(colours::yellow.withAlpha(0.35f));
        g.fillPath(liftedPath);
    }
    // Where the tail stands now.
    if (level > 1.0e-4f)
    {
        g.setColour(colours::red.withAlpha(0.8f));
        g.drawHorizontalLine(juce::roundToInt(toY(juce::Decibels::gainToDecibels(level))), inner.getX(),
                             inner.getRight());
    }
}

TailSection::TailSection(ShimmerProcessor& p)
    : Section(p.state, "Raum",
              { { { "size", utf8("Größe") },
                  { "decay", "Decay" },
                  { "predelay", "Predelay" },
                  { "damping", utf8("Dämpfung") },
                  { "lowCut", "Low Cut" },
                  { "modulation", "Modulation" },
                  { "hold", "Halten" } } },
              {}, { 0, kTailDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void TailSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

ShimmerEditor::ShimmerEditor(ShimmerProcessor& p)
    : AudioProcessorEditor(p),
      tail(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kShimmerCategories)),
      scope(p.scopeOut, &p.scopeIn),
      shimmer(p.state, "Shimmer", { { { "shimmer", "Menge" }, { "interval", "Intervall", 2 } } }),
      output(p.state, "Ausgang", { { { "width", "Breite" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("SHIMMER", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &tail, &scope, &shimmer, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(tail.preferredWidth() + 120, shimmer.preferredWidth() + output.preferredWidth() + kGap);
    setSize(width, 2 * kMargin + kHeader + tail.preferredHeight() + kGap + shimmer.preferredHeight() + kGap + kScope);
}

ShimmerEditor::~ShimmerEditor() { setLookAndFeel(nullptr); }

void ShimmerEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK SHIMMER");
}

void ShimmerEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &tail });
    layoutRow(area, { &shimmer, &output });
}
} // namespace tonwerkshimmer
