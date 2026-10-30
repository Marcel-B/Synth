#include "TapeEditor.h"
#include "Language.h"

#include "PresetCategories.h"
#include "TapePresets.h"

namespace tonwerktape
{
using namespace tonwerkui;

namespace
{
constexpr int kScope = 130;
constexpr int kCassette = 190;
constexpr float kFrameRate = 30.0f;
/** The reels' wobble is drawn this many times the real one, or it would not be seen. */
constexpr float kShownWobble = 12.0f;
} // namespace

CassetteView::CassetteView(TapeProcessor& p) : processor(p) { startTimerHz((int) kFrameRate); }

void CassetteView::refresh()
{
    const auto samples = processor.samplesRun.load();
    const bool runningNow = samples != lastSamples;
    lastSamples = samples;
    const float speed = processor.speed.load();
    const int centsNow = runningNow ? juce::roundToInt(1200.0f * std::log2(1.0f + speed)) : 0;
    if (runningNow)
    {
        // The tape runs at one speed past both reels: the full one turns slower than the empty one.
        const float turn = juce::MathConstants<float>::twoPi * 0.5f / kFrameRate * (1.0f + kShownWobble * speed);
        leftAngle += turn * 0.6f;
        rightAngle += turn * 1.0f;
    }
    if (runningNow || runningNow != isRunning || centsNow != shownCents)
    {
        isRunning = runningNow;
        shownCents = centsNow;
        repaint();
    }
}

void CassetteView::paintReel(juce::Graphics& g, juce::Point<float> centre, float hub, float pack, float angle) const
{
    g.setColour(colours::muted.withAlpha(0.35f));
    g.fillEllipse(juce::Rectangle<float>(2.0f * pack, 2.0f * pack).withCentre(centre));
    g.setColour(colours::background);
    g.fillEllipse(juce::Rectangle<float>(2.0f * hub, 2.0f * hub).withCentre(centre));
    g.setColour(colours::cyan);
    g.drawEllipse(juce::Rectangle<float>(2.0f * hub, 2.0f * hub).withCentre(centre), 1.5f);
    // The hub's six teeth turn with the reel.
    for (int i = 0; i < 6; ++i)
    {
        const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 6.0f;
        const auto inner = centre + juce::Point<float>(std::cos(a), std::sin(a)) * hub * 0.45f;
        const auto outer = centre + juce::Point<float>(std::cos(a), std::sin(a)) * hub * 0.9f;
        g.drawLine({ inner, outer }, 2.0f);
    }
}

void CassetteView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    paintScreen(g, area);
    auto inner = area.reduced(8.0f, 6.0f);
    auto readout = inner.removeFromBottom(18.0f);

    // The shell, its label and the window.
    auto shell = inner.withSizeKeepingCentre(std::min(inner.getWidth(), inner.getHeight() * 1.6f), inner.getHeight());
    g.setColour(colours::panel);
    g.fillRoundedRectangle(shell, 6.0f);
    g.setColour(colours::yellow.withAlpha(0.8f));
    g.drawRoundedRectangle(shell, 6.0f, 1.5f);
    auto label = shell.reduced(8.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.drawText("TONWERK C-60", label.removeFromTop(12.0f), juce::Justification::centredLeft);
    const auto window = label.withTrimmedBottom(label.getHeight() * 0.22f).reduced(2.0f, 4.0f);
    g.setColour(colours::screen);
    g.fillRoundedRectangle(window, 4.0f);

    const float hub = std::min(window.getHeight() * 0.18f, window.getWidth() * 0.09f);
    const auto left = juce::Point<float>(window.getX() + window.getWidth() * 0.27f, window.getCentreY());
    const auto right = juce::Point<float>(window.getX() + window.getWidth() * 0.73f, window.getCentreY());
    paintReel(g, left, hub, hub * 2.3f, leftAngle);
    paintReel(g, right, hub, hub * 1.5f, rightAngle);
    // The tape past the head at the bottom edge.
    const float tapeY = shell.getBottom() - 8.0f;
    g.setColour(colours::muted);
    juce::Path tape;
    tape.startNewSubPath(left.x - hub * 2.3f, left.y);
    tape.lineTo(left.x - hub * 1.5f, tapeY);
    tape.lineTo(right.x + hub * 1.5f, tapeY);
    tape.lineTo(right.x + hub * 1.5f, right.y);
    g.strokePath(tape, juce::PathStrokeType(1.2f));

    g.setFont(mono(12.0f));
    if (! isRunning)
    {
        g.setColour(colours::muted);
        g.drawText(tonwerkui::tr("STOPP"), readout, juce::Justification::centred);
        return;
    }
    // The pitch the wobble gives right now: what the ear hears as the tape going out of tune.
    g.setColour(std::abs(shownCents) >= 10 ? colours::red : colours::cyan);
    g.drawText(tonwerkui::tr("TONHÖHE ") + (shownCents > 0 ? "+" : "") + juce::String(shownCents) + " ct", readout,
               juce::Justification::centred);
}

TransportSection::TransportSection(TapeProcessor& p)
    : Section(p.state, "Laufwerk",
              { { { "wow", "Leiern" }, { "flutter", "Flattern" } }, { { "dropouts", "Aussetzer" } } }, {},
              { kCassette, 0 }),
      cassette(p)
{
    addAndMakeVisible(cassette);
}

void TransportSection::resized()
{
    Section::resized();
    cassette.setBounds(displayArea().reduced(0, 4));
}

TapeEditor::TapeEditor(TapeProcessor& p)
    : AudioProcessorEditor(p),
      transport(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kTapeCategories)),
      // The input is read as far behind as the tape delays the output, so the two traces line up.
      scope(p.scopeOut, &p.scopeIn, [&p] { return p.getLatencySamples(); }),
      tape(p.state, "Tonband",
           { { { "type", "Typ" } }, { { "drive", tonwerkui::tr("Sättigung") }, { "input", "Eingang" } } }),
      head(p.state, "Tonkopf", { { { "tone", tonwerkui::tr("Höhen") } }, { { "noise", "Rauschen" } } }),
      output(p.state, "Ausgang", { { { "mix", "Mix" } }, { { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("BAND", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &scope, &transport, &tape, &head, &output })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + kScope + kGap + transport.preferredHeight());
}

TapeEditor::~TapeEditor() { setLookAndFeel(nullptr); }

void TapeEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK TAPE");
}

void TapeEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &transport, &tape, &head, &output });
}
} // namespace tonwerktape
