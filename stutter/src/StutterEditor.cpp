#include "StutterEditor.h"
#include "Language.h"

#include "PresetCategories.h"
#include "StutterPresets.h"

namespace tonwerkstutter
{
using namespace tonwerkui;

namespace
{
constexpr int kDisplay = 110;
constexpr int kScope = 90;
} // namespace

PatternView::PatternView(StutterProcessor& p) : processor(p) { startTimerHz(30); }

void PatternView::refresh()
{
    shown = processor.readSettings();
    position = processor.positionNow.load();
    repeat = processor.repeatNow.load();
    repaint();
}

void PatternView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);

    const double intervalBeats = kIntervals[(std::size_t) shown.interval].beats;
    const int sixteenths = (int) std::lround(intervalBeats * 4.0);
    const int offset = std::min(shown.offset, sixteenths - 1);
    const double gridBeats = kGrids[(std::size_t) shown.grid].beats;

    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.8f));
    const auto dot = tonwerkui::tr(" · ");
    juce::String caption = juce::String(tonwerkui::tr(kIntervals[(std::size_t) shown.interval].name)).toUpperCase() + dot
                           + "CHANCE " + juce::String(juce::roundToInt(shown.chance * 100.0f)) + " %" + dot + tonwerkui::tr("RASTER ")
                           + tonwerkui::tr(kGrids[(std::size_t) shown.grid].name);
    if (repeat > 0)
        caption << dot << tonwerkui::tr("WIEDERHOLUNG ") << repeat;
    else if (repeat == 0)
        caption << dot << tonwerkui::tr("AUFNAHME");
    g.drawText(caption, inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    inner.removeFromTop(6.0f);

    // One cell per sixteenth; beats a little brighter.
    const float cell = inner.getWidth() / (float) sixteenths;
    for (int i = 0; i < sixteenths; ++i)
    {
        const auto box = juce::Rectangle<float>(inner.getX() + cell * i, inner.getY(), cell, inner.getHeight()).reduced(1.0f);
        g.setColour(colours::border.withAlpha(i % 4 == 0 ? 1.0f : 0.5f));
        g.drawRect(box, 1.0f);
    }

    // The stretch: the live slice, then its repeats, each slice a block.
    const double startBeat = offset * 0.25;
    const double gateBeats = std::min(shown.gate * 0.25, intervalBeats * 4.0);
    const float toX = inner.getWidth() / (float) intervalBeats;
    const bool lit = repeat >= 0;
    int slice = 0;
    for (double at = startBeat; at < startBeat + gateBeats - 1.0e-9; at += gridBeats, ++slice)
    {
        const double end = std::min(at + gridBeats, startBeat + gateBeats);
        // Past the interval's end it goes on in the next one: drawn wrapped, dimmer.
        const double from = std::fmod(at, intervalBeats), to = from + (end - at);
        const auto box = juce::Rectangle<float>(inner.getX() + (float) from * toX, inner.getY() + 8.0f,
                                                std::max(1.5f, (float) (to - from) * toX - 1.0f), inner.getHeight() - 16.0f);
        const auto colour = slice == 0 ? colours::cyan : (lit ? colours::yellow : colours::muted);
        g.setColour(colour.withAlpha(at >= intervalBeats ? 0.25f : (lit ? 0.75f : 0.45f)));
        g.fillRect(box);
        if (slice > 200)
            break;
    }

    g.setColour(colours::red);
    const float x = inner.getX() + position * inner.getWidth();
    g.drawLine(x, inner.getY(), x, inner.getBottom(), 2.0f);
}

PatternSection::PatternSection(StutterProcessor& p)
    : Section(p.state, "Muster",
              { { { "interval", "Intervall", 2 }, { "offset", "Versatz" }, { "gate", tonwerkui::tr("Länge") },
                  { "chance", "Chance" }, { "hold", "Halten" } } },
              {}, { 0, kDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void PatternSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

StutterEditor::StutterEditor(StutterProcessor& p)
    : AudioProcessorEditor(p),
      pattern(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kStutterCategories)),
      scope(p.scopeOut, &p.scopeIn),
      grid(p.state, "Raster", { { { "grid", "Raster", 2 }, { "variation", "Variation" }, { "seed", "Variante" } } }),
      sound(p.state, "Klang",
            { { { "decay", "Ausklang" }, { "pitch", tonwerkui::tr("Tonhöhe") },
                { "reverse", tonwerkui::tr("Rückwärts") },
                { "filter", "Filter" } } }),
      output(p.state, "Ausgang", { { { "mode", "Modus", 2 }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("STUTTER", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &pattern, &scope, &grid, &sound, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(pattern.preferredWidth() + 240,
                                 grid.preferredWidth() + sound.preferredWidth() + output.preferredWidth() + 2 * kGap);
    setSize(width, 2 * kMargin + kHeader + pattern.preferredHeight() + kGap + grid.preferredHeight() + kGap + kScope);
}

StutterEditor::~StutterEditor() { setLookAndFeel(nullptr); }

void StutterEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK STUTTER");
}

void StutterEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &pattern });
    layoutRow(area, { &grid, &sound, &output });
}
} // namespace tonwerkstutter
