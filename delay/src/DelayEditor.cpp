#include "DelayEditor.h"

#include "DelayPresets.h"
#include "PresetCategories.h"

namespace tonwerkdelay
{
using namespace tonwerkui;

namespace
{
constexpr int kEchoDisplay = 110;
constexpr int kScope = 100;
} // namespace

EchoView::EchoView(DelayProcessor& p) : processor(p) { startTimerHz(30); }

void EchoView::refresh()
{
    shown = processor.readSettings();
    bpm = processor.bpm.load();
    duck = processor.duckGain.load();
    repaint();
}

juce::String EchoView::timeText() const
{
    const double seconds = secondsOf(shown, bpm);
    juce::String text;
    if (shown.sync)
        text << kDivisions[(std::size_t) std::clamp(shown.division, 0, (int) kDivisions.size() - 1)].name
             << utf8(" · ");
    text << juce::roundToInt(seconds * 1000.0) << " MS";
    return text;
}

void EchoView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.drawText(timeText() + (shown.hold ? "  HALTEN" : ""), inner.removeFromTop(14.0f).toNearestInt(),
               juce::Justification::centredLeft);

    // The ducking's pull: a bar on the right, full when nothing is taken.
    auto meter = inner.removeFromRight(14.0f);
    inner.removeFromRight(8.0f);
    g.setColour(colours::border.withAlpha(0.6f));
    g.fillRect(meter);
    g.setColour(colours::red.withAlpha(0.85f));
    g.fillRect(meter.withTrimmedBottom(meter.getHeight() * std::clamp(duck, 0.0f, 1.0f)));

    const double seconds = secondsOf(shown, bpm);
    const double rightSeconds = shown.mode == Mode::stereo ? seconds * (1.0 + shown.offset) : seconds;
    // As many repeats across as sound above -40 dB, between 3 and 16.
    const float feedback = shown.hold ? 1.0f : shown.feedback;
    const int repeats = feedback >= 0.99f ? 16
                                          : std::clamp((int) std::ceil(std::log(0.01) / std::log(std::max(0.01f, feedback))),
                                                       3, 16);
    const double span = (repeats + 0.5) * std::max(seconds, rightSeconds);
    const float mid = inner.getCentreY();
    g.setColour(colours::border.brighter(0.2f));
    g.drawHorizontalLine(juce::roundToInt(mid), inner.getX(), inner.getRight());
    auto mark = [&](double at, float level, bool left) {
        if (at > span)
            return;
        const float x = inner.getX() + (float) (at / span) * inner.getWidth();
        // 40 dB from the line to the edge, so the quiet repeats show too.
        const float height = std::clamp(1.0f + juce::Decibels::gainToDecibels(level, -100.0f) / 40.0f, 0.0f, 1.0f);
        const float h = height * (inner.getHeight() * 0.5f - 2.0f);
        const auto colour = left ? colours::cyan : colours::yellow;
        g.setColour(colour.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>(x - 2.0f, left ? mid - h : mid, 5.0f, h));
        g.setColour(colour.withAlpha(0.9f));
        g.fillRect(juce::Rectangle<float>(x - 0.75f, left ? mid - h : mid, 1.5f, h));
    };
    // The dry hit at 0, then each repeat.
    mark(0.0, 1.0f, true);
    mark(0.0, 1.0f, false);
    float level = 1.0f;
    for (int n = 1; n <= repeats; ++n)
    {
        switch (shown.mode)
        {
            case Mode::pingPong:
                // Into the left line only: the first repeat left, the next right, and so on.
                mark(n * seconds, level, n % 2 == 1);
                break;
            case Mode::stereo:
                mark(n * seconds, level, true);
                mark(n * rightSeconds, level, false);
                break;
            case Mode::mono:
                mark(n * seconds, level, true);
                mark(n * seconds, level, false);
                break;
        }
        level *= feedback;
        if (level < 0.01f)
            break;
    }
}

EchoSection::EchoSection(DelayProcessor& p)
    : Section(p.state, "Echo",
              { { { "mode", "Modus" },
                  { "sync", "Sync" },
                  { "time", "Zeit", 0, "sync", false },
                  { "division", "Notenwert", 0, "sync", true },
                  { "offset", "Versatz R" },
                  { "feedback", "Feedback" },
                  { "hold", "Halten" } } },
              {}, { 0, kEchoDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void EchoSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

DelayEditor::DelayEditor(DelayProcessor& p)
    : AudioProcessorEditor(p),
      echo(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kDelayCategories)),
      scope(p.scopeOut, &p.scopeIn),
      tone(p.state, "Klang",
           { { { "lowCut", "Low Cut" }, { "highCut", "High Cut" }, { "drive", utf8("Sättigung") },
               { "wow", "Leiern" } } }),
      ducking(p.state, "Ducking", { { { "duck", "Menge" }, { "duckRelease", "Release" } } }),
      output(p.state, "Ausgang", { { { "width", "Breite" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("DELAY", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &echo, &scope, &tone, &ducking, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(echo.preferredWidth() + 160, tone.preferredWidth() + ducking.preferredWidth()
                                                                  + output.preferredWidth() + 2 * kGap);
    setSize(width, 2 * kMargin + kHeader + echo.preferredHeight() + kGap + tone.preferredHeight() + kGap + kScope);
}

DelayEditor::~DelayEditor() { setLookAndFeel(nullptr); }

void DelayEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK DELAY");
}

void DelayEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &echo });
    layoutRow(area, { &tone, &ducking, &output });
}
} // namespace tonwerkdelay
