#include "LfoEditor.h"
#include "Language.h"

#include "LfoPresets.h"
#include "PresetCategories.h"

namespace tonwerklfo
{
using namespace tonwerkui;

namespace
{
constexpr int kWaveDisplay = 150;
} // namespace

WaveView::WaveView(LfoProcessor& p) : processor(p) { startTimerHz(30); }

void WaveView::refresh()
{
    shown = processor.readSettings();
    phase = processor.phaseNow.load();
    value = processor.valueNow.load();
    repaint();
}

juce::String WaveView::caption() const
{
    const auto dot = tonwerkui::tr(" · ");
    juce::String text = "CC " + juce::String(shown.controller);
    if (sendsFine(shown))
        text << dot << "14 BIT";
    text << dot << tonwerkui::tr("KANAL ") << shown.channel << dot;
    if (shown.sync)
        text << tonwerkwobble::kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) tonwerkwobble::kDivisions.size() - 1)].name;
    else
        text << juce::String(shown.rate, shown.rate < 9.95f ? 1 : 0) << " HZ";
    if (shown.start == Start::note)
        text << dot << tonwerkui::tr("START MIT NOTE");
    text << dot << juce::roundToInt(value * 127.0f);
    return text;
}

void WaveView::paint(juce::Graphics& g)
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
    auto hint = inner.removeFromBottom(12.0f);
    g.setColour(colours::muted);
    g.setFont(mono(9.0f));
    g.drawText(tonwerkui::tr("IM INSTRUMENT: RECHTSKLICK AUF EINEN REGLER › MIDI LERNEN"), hint.toNearestInt(),
               juce::Justification::centredLeft);
    inner.removeFromBottom(4.0f);

    auto toY = [&](float v) { return inner.getBottom() - v * inner.getHeight(); };
    g.setColour(colours::border.withAlpha(0.6f));
    for (const float v : { 0.25f, 0.5f, 0.75f })
        g.drawHorizontalLine(juce::roundToInt(toY(v)), inner.getX(), inner.getRight());
    // The range the swing covers.
    const float low = std::clamp(shown.centre - 0.5f * shown.depth, 0.0f, 1.0f);
    const float high = std::clamp(shown.centre + 0.5f * shown.depth, 0.0f, 1.0f);
    g.setColour(colours::cyan.withAlpha(0.12f));
    g.fillRect(juce::Rectangle<float>(inner.getX(), toY(high), inner.getWidth(), toY(low) - toY(high)));

    // One cycle; the random shape shows four cycles' held values so it reads as steps.
    const int cycles = shown.shape == Shape::random ? 4 : 1;
    juce::Path curve;
    for (int px = 0; px <= (int) inner.getWidth(); ++px)
    {
        const double at = (double) px / inner.getWidth() * cycles;
        const float y = toY(valueAt(shown, at));
        if (px == 0)
            curve.startNewSubPath(inner.getX(), y);
        else
            curve.lineTo(inner.getX() + (float) px, y);
    }
    g.setColour(colours::yellow);
    g.strokePath(curve, juce::PathStrokeType(2.0f));

    const float x = inner.getX() + (float) (phase / (float) cycles - std::floor(phase / (float) cycles)) * inner.getWidth();
    g.setColour(colours::red);
    g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ x, toY(value) }));
}

WaveSection::WaveSection(LfoProcessor& p)
    : Section(p.state, "LFO",
              { { { "shape", "Form", 2 },
                  { "sync", "Sync" },
                  { "division", "Takt", 0, "sync", true },
                  { "rate", "Rate", 0, "sync", false },
                  { "phase", "Phase" },
                  { "start", "Start" } } },
              {}, { 0, kWaveDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void WaveSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

LfoEditor::LfoEditor(LfoProcessor& p)
    : AudioProcessorEditor(p),
      wave(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kLfoCategories)),
      output(p.state, "Ausgabe",
             { { { "centre", "Mitte" },
                 { "depth", "Tiefe" },
                 { "controller", "Controller" },
                 { "channel", "Kanal" },
                 { "fine", "14 Bit" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &wave, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin + std::max(wave.preferredWidth() + 160, output.preferredWidth());
    setSize(width, 2 * kMargin + kHeader + wave.preferredHeight() + kGap + output.preferredHeight());
}

LfoEditor::~LfoEditor() { setLookAndFeel(nullptr); }

void LfoEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK LFO");
}

void LfoEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    layoutRow(area, { &wave });
    layoutRow(area, { &output });
}
} // namespace tonwerklfo
