#include "ChorusEditor.h"

#include "ChorusPresets.h"
#include "PresetCategories.h"

namespace tonwerkchorus
{
using namespace tonwerkui;

namespace
{
constexpr int kSweepDisplay = 110;
constexpr int kScope = 100;
constexpr const char* kModeNames[] = { "CHORUS", "ENSEMBLE", "FLANGER", "PHASER" };

/** One decimal below 10, whole above, as the parameters show them. */
juce::String shortNumber(double value)
{
    return value < 9.95 ? juce::String(value, 1) : juce::String(juce::roundToInt(value));
}

/** The swept value at `phase` for `voice`: a delay in ms or the phaser's frequency in Hz, as the engine computes it. */
double sweepAt(const Settings& s, double phase, int voice)
{
    const double base = s.delay * 1000.0;
    switch (s.mode)
    {
        case Mode::chorus:
        {
            const int voices = std::clamp(s.voices, 1, 4);
            const double sweep = std::min(0.9 * base, kChorusSweep * 1000.0) * s.depth;
            return base + sweep * lfo(phase + (double) voice / voices, s.shape);
        }
        case Mode::ensemble:
        {
            const double at = phase + voice / 3.0;
            const double slow = std::min(0.8 * base, 3.0) * s.depth, fast = std::min(0.1 * base, 0.4) * s.depth;
            return base + slow * lfo(at, Shape::sine) + fast * lfo(7.0 * at, Shape::sine);
        }
        case Mode::flanger:
            return base * (1.0 - 0.95 * s.depth * (0.5 - 0.5 * lfo(phase, s.shape)));
        case Mode::phaser:
        default:
            return s.frequency * std::exp2(kPhaserOctaves * s.depth * lfo(phase, s.shape));
    }
}

int voicesShown(const Settings& s)
{
    return s.mode == Mode::chorus ? std::clamp(s.voices, 1, 4) : s.mode == Mode::ensemble ? 3 : 1;
}
} // namespace

SweepView::SweepView(ChorusProcessor& p) : processor(p) { startTimerHz(30); }

void SweepView::refresh()
{
    shown = processor.readSettings();
    bpm = processor.bpm.load();
    phase = processor.phase.load();
    repaint();
}

juce::String SweepView::caption() const
{
    juce::String text = kModeNames[(int) shown.mode];
    const auto dot = utf8(" · ");
    // The swept range over a cycle, from the curves themselves.
    double low = 1.0e9, high = 0.0;
    for (int v = 0; v < voicesShown(shown); ++v)
        for (int k = 0; k < 64; ++k)
        {
            const double value = sweepAt(shown, k / 64.0, v);
            low = std::min(low, value);
            high = std::max(high, value);
        }
    switch (shown.mode)
    {
        case Mode::chorus:
            text << dot << std::clamp(shown.voices, 1, 4) << " " << (shown.voices == 1 ? tr("STIMME") : tr("STIMMEN"));
            break;
        case Mode::ensemble:
            text << dot << "3 LEITUNGEN";
            break;
        case Mode::phaser:
            text << dot << kStages[(std::size_t) std::clamp(shown.stages, 0, 3)] << " STUFEN";
            break;
        case Mode::flanger:
            break;
    }
    if (shown.mode == Mode::phaser)
        text << dot << juce::roundToInt(low) << "-" << juce::roundToInt(high) << " HZ";
    else
        text << dot << juce::String(low, 1) << "-" << juce::String(high, 1) << " MS";
    text << dot;
    if (shown.sync)
        text << kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) kDivisions.size() - 1)].name;
    else
        text << shortNumber(shown.rate) << " HZ";
    return text;
}

void SweepView::paint(juce::Graphics& g)
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

    // The y axis: the delay from 0 to its widest reach (ms), or the phaser's two octaves either way on a log scale.
    const bool phaser = shown.mode == Mode::phaser;
    const double top = phaser ? shown.frequency * std::exp2(kPhaserOctaves) : std::max(0.5, shown.delay * 1000.0 + 5.0);
    const double bottom = phaser ? shown.frequency * std::exp2(-kPhaserOctaves) : 0.0;
    auto yOf = [&](double value) {
        const double t = phaser ? std::log(value / bottom) / std::log(top / bottom) : (value - bottom) / (top - bottom);
        return inner.getBottom() - (float) std::clamp(t, 0.0, 1.0) * inner.getHeight();
    };
    g.setColour(colours::border.brighter(0.2f));
    g.drawHorizontalLine(juce::roundToInt(inner.getBottom()), inner.getX(), inner.getRight());

    constexpr int kCycles = 2;
    const double spreadCycles = std::clamp((double) shown.spread, 0.0, 180.0) / 360.0;
    for (int side = 1; side >= 0; --side)
        for (int v = voicesShown(shown) - 1; v >= 0; --v)
        {
            juce::Path curve;
            const int points = (int) inner.getWidth();
            for (int k = 0; k <= points; ++k)
            {
                const double at = (double) k / points * kCycles + (side == 1 ? spreadCycles : 0.0);
                const float x = inner.getX() + (float) k;
                const float y = yOf(sweepAt(shown, at, v));
                if (k == 0)
                    curve.startNewSubPath(x, y);
                else
                    curve.lineTo(x, y);
            }
            const auto colour = side == 0 ? colours::cyan : colours::yellow;
            g.setColour(colour.withAlpha(v == 0 ? 0.9f : 0.35f));
            g.strokePath(curve, juce::PathStrokeType(v == 0 ? 1.5f : 1.0f));
        }

    // Where the LFO is now, in the first of the two cycles shown.
    const float x = inner.getX() + (float) (phase - std::floor(phase)) / kCycles * inner.getWidth();
    g.setColour(colours::red.withAlpha(0.8f));
    g.drawVerticalLine(juce::roundToInt(x), inner.getY(), inner.getBottom());
}

SweepSection::SweepSection(ChorusProcessor& p)
    : Section(p.state, "Modulation",
              { { { "mode", "Modus" },
                  { "shape", "Form" },
                  { "sync", "Sync" },
                  { "rate", "Rate", 0, "sync", false },
                  { "division", "Notenwert", 0, "sync", true },
                  { "depth", "Tiefe" },
                  { "spread", "Stereo" } } },
              {}, { 0, kSweepDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void SweepSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

ChorusEditor::ChorusEditor(ChorusProcessor& p)
    : AudioProcessorEditor(p),
      sweep(p),
      line(p.state, "Leitung",
           { { { "delay", utf8("Verzögerung") }, { "voices", "Stimmen" }, { "feedback", "Feedback" } } }),
      phaser(p.state, "Phaser", { { { "frequency", "Frequenz" }, { "stages", "Stufen" }, { "feedback", "Feedback" } } }),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kChorusCategories)),
      scope(p.scopeOut, &p.scopeIn),
      tone(p.state, "Klang", { { { "lowCut", "Low Cut" }, { "highCut", "High Cut" } } }),
      output(p.state, "Ausgang", { { { "width", "Breite" }, { "mix", "Mix" }, { "output", "Pegel" } } }),
      modeAttachment(*p.state.getParameter("mode"), [this](float value) { modeChanged(value); })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption(tr("OSZILLOSKOP"));
    scope.setLegend("CHORUS", tr("EINGANG"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &sweep, &scope, &line, &phaser, &tone, &output })
        addAndMakeVisible(*c);
    const int lower = std::max(line.preferredWidth(), phaser.preferredWidth()) + tone.preferredWidth()
                      + output.preferredWidth() + 2 * kGap;
    const int width = 2 * kMargin + std::max(sweep.preferredWidth() + 120, lower);
    setSize(width, 2 * kMargin + kHeader + sweep.preferredHeight() + kGap + line.preferredHeight() + kGap + kScope);
    modeAttachment.sendInitialUpdate();
}

ChorusEditor::~ChorusEditor() { setLookAndFeel(nullptr); }

void ChorusEditor::modeChanged(float value)
{
    phaserShown = juce::roundToInt(value) == (int) Mode::phaser;
    line.setVisible(! phaserShown);
    phaser.setVisible(phaserShown);
    resized();
}

void ChorusEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK CHORUS");
}

void ChorusEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &sweep });
    layoutRow(area, { phaserShown ? &phaser : &line, &tone, &output });
}
} // namespace tonwerkchorus
