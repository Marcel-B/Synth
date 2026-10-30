#include "RumbleEditor.h"

#include "PresetCategories.h"
#include "RumblePresets.h"

namespace tonwerkrumble
{
using namespace tonwerkui;

namespace
{
constexpr int kView = 130;
/** Amplitude to height: a square root, so a quiet rumble under a loud kick still shows. */
float heightOf(float level) { return std::sqrt(std::clamp(level, 0.0f, 1.0f)); }
} // namespace

RumbleView::RumbleView(RumbleProcessor& p) : processor(p)
{
    duck.fill(1.0f);
    startTimerHz(30);
}

void RumbleView::refresh()
{
    // Oldest first: the column after the newest is the oldest one still kept.
    const int newest = processor.written.load(std::memory_order_acquire);
    for (int i = 0; i < RumbleProcessor::kColumns; ++i)
    {
        const auto from = (std::size_t) ((newest + i) % RumbleProcessor::kColumns);
        kick[(std::size_t) i] = processor.kickHistory[from].load(std::memory_order_relaxed);
        rumble[(std::size_t) i] = processor.rumbleHistory[from].load(std::memory_order_relaxed);
        duck[(std::size_t) i] = processor.duckHistory[from].load(std::memory_order_relaxed);
    }
    cutoff = processor.cutoffNow.load(std::memory_order_relaxed);
    repaint();
}

void RumbleView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    auto caption = inner.removeFromTop(14.0f);
    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.85f));
    g.drawText("KICK", caption, juce::Justification::centredLeft);
    g.setColour(colours::yellow);
    g.drawText("RUMBLE", caption.withTrimmedLeft(44.0f), juce::Justification::centredLeft);
    g.setColour(colours::red.withAlpha(0.85f));
    g.drawText("DUCKING", caption.withTrimmedLeft(104.0f), juce::Justification::centredLeft);
    g.setColour(colours::muted);
    g.drawText(tr("TIEFPASS") + " " + juce::String(juce::roundToInt(cutoff)) + " HZ", caption, juce::Justification::centredRight);
    inner.removeFromTop(2.0f);

    const int n = RumbleProcessor::kColumns;
    const float step = inner.getWidth() / (float) n;
    auto xOf = [&](int i) { return inner.getX() + (float) i * step; };
    auto yOf = [&](float level) { return inner.getBottom() - heightOf(level) * inner.getHeight(); };

    g.setColour(colours::border);
    g.drawHorizontalLine(juce::roundToInt(yOf(0.25f)), inner.getX(), inner.getRight());

    // The kick as bars: each one a hit.
    g.setColour(colours::cyan.withAlpha(0.45f));
    for (int i = 0; i < n; ++i)
    {
        const float top = yOf(kick[(std::size_t) i]);
        g.fillRect(juce::Rectangle<float>(xOf(i), top, std::max(1.0f, step), inner.getBottom() - top));
    }

    // The rumble as a filled outline over them.
    juce::Path line;
    for (int i = 0; i < n; ++i)
    {
        const float x = xOf(i) + 0.5f * step, y = yOf(rumble[(std::size_t) i]);
        if (i == 0)
            line.startNewSubPath(x, y);
        else
            line.lineTo(x, y);
    }
    auto filled = line;
    filled.lineTo(inner.getRight(), inner.getBottom());
    filled.lineTo(inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour(colours::yellow.withAlpha(0.18f));
    g.fillPath(filled);
    g.setColour(colours::yellow);
    g.strokePath(line, juce::PathStrokeType(1.6f));

    // The ducking hangs from the top: the deeper the line, the further the rumble was held down.
    juce::Path ducked;
    for (int i = 0; i < n; ++i)
    {
        const float x = xOf(i) + 0.5f * step;
        const float y = inner.getY() + (1.0f - std::clamp(duck[(std::size_t) i], 0.0f, 1.0f)) * inner.getHeight();
        if (i == 0)
            ducked.startNewSubPath(x, y);
        else
            ducked.lineTo(x, y);
    }
    g.setColour(colours::red.withAlpha(0.8f));
    g.strokePath(ducked, juce::PathStrokeType(1.2f));
}

RumbleEditor::RumbleEditor(RumbleProcessor& p)
    : AudioProcessorEditor(p),
      view(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kRumbleCategories)),
      room(p.state, "Raum",
           { { { "tone", "Vorfilter" },
               { "space", "Modus", 2 },
               { "decay", "Hallzeit" },
               { "size", utf8("Größe") },
               { "predelay", "Predelay" },
               { "echoDivision", "Echo" },
               { "feedback", "Feedback" } } }),
      distortion(p.state, "Verzerrung", { { { "drive", "Drive" }, { "curve", "Kurve", 2 } } }),
      filter(p.state, "Filter", { { { "cutoff", "Tiefpass" }, { "resonance", "Resonanz" }, { "lowCut", "Hochpass" } } }),
      ducking(p.state, "Ducking",
              { { { "duck", "Tiefe" },
                  { "duckRelease", "Release" },
                  { "duckSync", "Takt" },
                  { "duckDivision", "Raster", 0, "duckSync", true },
                  { "duckFilter", "Filter" } } }),
      output(p.state, "Ausgang", { { { "rumble", "Rumble" }, { "kick", "Kick" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &view, &room, &distortion, &filter, &ducking, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max({ room.preferredWidth(), distortion.preferredWidth() + filter.preferredWidth() + kGap,
                                   ducking.preferredWidth() + output.preferredWidth() + kGap });
    setSize(width, 2 * kMargin + kHeader + kView + kGap + room.preferredHeight() + kGap + distortion.preferredHeight()
                       + kGap + ducking.preferredHeight());
}

RumbleEditor::~RumbleEditor() { setLookAndFeel(nullptr); }

void RumbleEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK RUMBLE");
}

void RumbleEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    view.setBounds(area.removeFromTop(kView));
    area.removeFromTop(kGap);
    layoutRow(area, { &room });
    layoutRow(area, { &distortion, &filter });
    layoutRow(area, { &ducking, &output });
}
} // namespace tonwerkrumble
