#include "ReverbEditor.h"

#include "PresetCategories.h"
#include "ReverbPresets.h"

namespace tonwerkreverb
{
using namespace tonwerkui;

namespace
{
constexpr int kSpaceDisplay = 110;
constexpr int kScope = 100;
constexpr float kFloorDb = -60.0f;
} // namespace

SpaceView::SpaceView(ReverbProcessor& p) : processor(p) { startTimerHz(30); }

void SpaceView::refresh()
{
    shown = processor.readSettings();
    bpm = processor.bpm.load();
    level = processor.tailLevel.load();
    duck = processor.duckGain.load();
    repaint();
}

juce::String SpaceView::caption() const
{
    const auto dot = utf8(" · ");
    juce::String text = tr(spaceOf(shown.space).name).toUpperCase() + dot + "DECAY "
                        + (shown.decay < 9.95f ? juce::String(shown.decay, 1) : juce::String(juce::roundToInt(shown.decay)))
                        + " S" + dot + "PREDELAY ";
    if (shown.sync)
        text << kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) kDivisions.size() - 1)].name << " ";
    text << juce::roundToInt(predelaySeconds(shown, bpm) * 1000.0) << " MS";
    if (shown.freeze)
        text << dot << tr("EINGEFROREN");
    return text;
}

void SpaceView::paint(juce::Graphics& g)
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

    // The ducking's pull: a bar on the right, full when nothing is taken.
    auto meter = inner.removeFromRight(10.0f);
    inner.removeFromRight(8.0f);
    g.setColour(colours::border.withAlpha(0.6f));
    g.fillRect(meter);
    g.setColour(colours::red.withAlpha(0.85f));
    g.fillRect(meter.withTrimmedBottom(meter.getHeight() * std::clamp(duck, 0.0f, 1.0f)));

    // Time across: the predelay and the longer of the two decays with a little room, at least a second.
    const double predelay = predelaySeconds(shown, bpm);
    const double decay = shown.freeze ? 20.0 : std::max(0.2, (double) shown.decay);
    const double bassDecay = shown.freeze ? 20.0 : decay * std::clamp((double) shown.bassTime, 0.25, 4.0);
    const double span = predelay + std::max(1.0, std::min(std::max(decay, bassDecay) * 1.1, 25.0));
    auto toX = [&](double t) { return inner.getX() + (float) (t / span) * inner.getWidth(); };
    auto toY = [&](float db) {
        return inner.getBottom() - std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f) * inner.getHeight();
    };
    const auto& space = spaceOf(shown.space);
    // The tail sets in after the first reflections, once the diffusers have spread the sound.
    const double onset = predelay + 0.25 * space.earlySpan * sizeScale(shown.size);
    juce::Path tailPath, bassPath;
    const int points = std::max(2, (int) inner.getWidth() / 2);
    tailPath.startNewSubPath(toX(0.0), inner.getBottom());
    tailPath.lineTo(toX(onset), inner.getBottom());
    for (int k = 0; k <= points; ++k)
    {
        const double t = onset + (double) k / points * (span - onset);
        const double after = t - onset;
        tailPath.lineTo(toX(t), toY((float) (-6.0 - 60.0 * after / decay)));
        if (k == 0)
            bassPath.startNewSubPath(toX(t), toY((float) (-6.0 - 60.0 * after / bassDecay)));
        else
            bassPath.lineTo(toX(t), toY((float) (-6.0 - 60.0 * after / bassDecay)));
    }
    tailPath.lineTo(toX(span), inner.getBottom());
    tailPath.closeSubPath();
    g.setColour(colours::cyan.withAlpha(0.18f));
    g.fillPath(tailPath);
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.strokePath(tailPath, juce::PathStrokeType(1.2f));
    if (std::abs(shown.bassTime - 1.0f) > 0.01f && ! shown.freeze)
    {
        g.setColour(colours::yellow.withAlpha(0.7f));
        g.strokePath(bassPath, juce::PathStrokeType(1.0f));
    }

    // The early reflections, falling as the engine's taps do.
    const float early = std::clamp(shown.early, 0.0f, 1.0f) * space.earlyLevel;
    if (early > 0.0f && space.earlySpan > 0.0)
    {
        const auto& times = Engine::earlyTimes();
        for (std::size_t k = 0; k < times.size(); ++k)
        {
            const double t = predelay + space.earlySpan * sizeScale(shown.size) * times[k];
            const double fall = std::pow(1.0 - (double) k / (double) times.size(), 1.5) * early;
            const float y = toY((float) juce::Decibels::gainToDecibels(fall, -100.0));
            g.setColour(colours::yellow.withAlpha(0.9f));
            g.fillRect(juce::Rectangle<float>(toX(t) - 0.75f, y, 1.5f, inner.getBottom() - y));
        }
    }

    // Where the wet stands now.
    if (level > 1.0e-4f)
    {
        g.setColour(colours::red.withAlpha(0.8f));
        g.drawHorizontalLine(juce::roundToInt(toY(juce::Decibels::gainToDecibels(level))), inner.getX(),
                             inner.getRight());
    }
}

SpaceSection::SpaceSection(ReverbProcessor& p)
    : Section(p.state, "Raum",
              { { { "space", "Raumtyp", 2 },
                  { "size", utf8("Größe") },
                  { "decay", "Decay" },
                  { "sync", "Sync" },
                  { "predelay", "Predelay", 0, "sync", false },
                  { "division", "Notenwert", 0, "sync", true },
                  { "freeze", "Einfrieren" } } },
              {}, { 0, kSpaceDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void SpaceSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

ReverbEditor::ReverbEditor(ReverbProcessor& p)
    : AudioProcessorEditor(p),
      space(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kReverbCategories)),
      scope(p.scopeOut, &p.scopeIn),
      tone(p.state, "Klang",
           { { { "early", "Reflexionen" },
               { "diffusion", "Diffusion" },
               { "bassTime", "Bass-Decay" },
               { "damping", utf8("Dämpfung") },
               { "modulation", "Modulation" } } }),
      filter(p.state, "Filter & Ducking",
             { { { "lowCut", "Low Cut" }, { "highCut", "High Cut" }, { "duck", "Ducking" },
                 { "duckRelease", "Release" } } }),
      output(p.state, "Ausgang", { { { "width", "Breite" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption(tr("OSZILLOSKOP"));
    scope.setLegend("REVERB", tr("EINGANG"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &space, &scope, &tone, &filter, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(space.preferredWidth() + 160, tone.preferredWidth() + filter.preferredWidth()
                                                                   + output.preferredWidth() + 2 * kGap);
    setSize(width, 2 * kMargin + kHeader + space.preferredHeight() + kGap + tone.preferredHeight() + kGap + kScope);
}

ReverbEditor::~ReverbEditor() { setLookAndFeel(nullptr); }

void ReverbEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK REVERB");
}

void ReverbEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &space });
    layoutRow(area, { &tone, &filter, &output });
}
} // namespace tonwerkreverb
