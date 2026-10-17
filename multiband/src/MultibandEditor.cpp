#include "MultibandEditor.h"

#include "MultibandPresets.h"
#include "PresetCategories.h"

namespace tonwerkmultiband
{
using namespace tonwerkui;

namespace
{
constexpr int kBandDisplay = 160;
constexpr int kScope = 100;
/** The display's scales: frequency 20 Hz to 20 kHz, level -60 to 0 dBFS. */
constexpr float kLowestHz = 20.0f, kHighestHz = 20000.0f;
constexpr float kLevelFloor = -60.0f;
/** How fast a shown level falls back, in dB per frame (30 a second): a peak stays readable for a moment. */
constexpr float kFall = 1.0f;

juce::String signedDb(float db) { return (db >= 0.05f ? "+" : "") + juce::String(db, 1) + " DB"; }
} // namespace

BandView::BandView(MultibandProcessor& p) : processor(p)
{
    levels.fill(kLevelFloor);
    startTimerHz(30);
}

void BandView::refresh()
{
    shown = processor.readSettings();
    for (std::size_t b = 0; b < kBands; ++b)
    {
        const float level = processor.levels[b].exchange(-100.0f);
        levels[b] = std::max(level, levels[b] - kFall);
        const float change = processor.changes[b].exchange(0.0f);
        // The change eases towards the newest, so the yellow line does not flicker with every block.
        changes[b] += 0.5f * (change - changes[b]);
    }
    repaint();
}

juce::String BandView::caption() const
{
    juce::String text;
    for (std::size_t b = 0; b < kBands; ++b)
    {
        if (b > 0)
            text << utf8(" · ");
        text << juce::String(kBandNames[b]).toUpperCase() << " ";
        if (! shown.bands[b].on)
            text << "AUS";
        else
            text << signedDb(changes[b]);
    }
    if (shown.depth < 0.995f)
        text << utf8(" · TIEFE ") << juce::roundToInt(shown.depth * 100.0f) << " %";
    return text;
}

void BandView::paint(juce::Graphics& g)
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
    auto labels = inner.removeFromBottom(12.0f);

    const float span = std::log(kHighestHz / kLowestHz);
    auto toX = [&](float hz) { return inner.getX() + std::log(std::max(hz, kLowestHz) / kLowestHz) / span * inner.getWidth(); };
    auto toY = [&](float db) {
        return inner.getBottom() - std::clamp((db - kLevelFloor) / -kLevelFloor, 0.0f, 1.0f) * inner.getHeight();
    };

    // The grid: decades across, every 12 dB up.
    g.setColour(colours::border.withAlpha(0.6f));
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine(juce::roundToInt(toX(hz)), inner.getY(), inner.getBottom());
    for (float db = -12.0f; db > kLevelFloor; db -= 12.0f)
        g.drawHorizontalLine(juce::roundToInt(toY(db)), inner.getX(), inner.getRight());
    g.setColour(colours::muted);
    g.setFont(mono(9.0f));
    for (const auto& [hz, name] : { std::pair { 100.0f, "100" }, { 1000.0f, "1K" }, { 10000.0f, "10K" } })
        g.drawText(name, juce::Rectangle<float>(toX(hz) + 3.0f, labels.getY(), 40.0f, labels.getHeight()),
                   juce::Justification::centredLeft);

    bool anySolo = false;
    for (const auto& band : shown.bands)
        anySolo = anySolo || band.solo;
    const float lowHz = shown.lowSplit, highHz = std::max(shown.lowSplit, shown.highSplit);
    const std::array<float, kBands + 1> edges { kLowestHz, lowHz, highHz, kHighestHz };
    for (std::size_t b = 0; b < kBands; ++b)
    {
        const auto& band = shown.bands[b];
        const auto region = juce::Rectangle<float>(toX(edges[b]), inner.getY(), toX(edges[b + 1]) - toX(edges[b]),
                                                   inner.getHeight())
                                .reduced(4.0f, 0.0f);
        if (region.getWidth() < 2.0f)
            continue;
        const bool heard = ! anySolo || band.solo;
        const float alpha = heard ? 1.0f : 0.3f;
        const float level = std::max(kLevelFloor, levels[b]);
        auto fill = region.withTop(toY(level));
        g.setColour(colours::cyan.withAlpha(0.18f * alpha));
        g.fillRect(fill);
        g.setColour(colours::cyan.withAlpha(0.8f * alpha));
        g.drawHorizontalLine(juce::roundToInt(fill.getY()), region.getX(), region.getRight());
        if (! band.on)
            continue;
        g.setColour(colours::red.withAlpha(0.7f * alpha));
        g.drawHorizontalLine(juce::roundToInt(toY(band.thresholdDb)), region.getX(), region.getRight());
        // Where the band ends up: the level moved by its gain change, a bar between the two.
        const float after = level + changes[b];
        const float y = toY(after), from = toY(level);
        const auto bar = juce::Rectangle<float>(region.getCentreX() - 6.0f, std::min(y, from), 12.0f, std::abs(y - from));
        g.setColour((changes[b] < 0.0f ? colours::red : colours::yellow).withAlpha(0.55f * alpha));
        g.fillRect(bar);
        g.setColour(colours::yellow.withAlpha(alpha));
        g.fillRect(juce::Rectangle<float>(region.getX(), y - 1.0f, region.getWidth(), 2.0f));
    }

    // The crossovers.
    for (const float hz : { lowHz, highHz })
    {
        const float x = toX(hz);
        g.setColour(colours::yellow.withAlpha(0.8f));
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine(juce::Line<float>(x, inner.getY(), x, inner.getBottom()), dashes, 2, 1.2f);
        g.setFont(mono(9.0f));
        g.drawText(juce::String(juce::roundToInt(hz)) + " HZ",
                   juce::Rectangle<float>(x - 30.0f, labels.getY(), 60.0f, labels.getHeight()),
                   juce::Justification::centred);
    }
}

BandsSection::BandsSection(MultibandProcessor& p)
    : Section(p.state, "Spektrum",
              { { { "input", "Eingang" },
                  { "lowSplit", "Tief/Mitte" },
                  { "highSplit", "Mitte/Hoch" },
                  { "depth", "Tiefe" },
                  { "mix", "Mix" },
                  { "output", "Ausgang" } } },
              {}, { 0, kBandDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void BandsSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

MultibandEditor::MultibandEditor(MultibandProcessor& p)
    : AudioProcessorEditor(p),
      bands(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kMultibandCategories)),
      scope(p.scopeOut, &p.scopeIn)
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("MULTIBAND", "EINGANG");
    for (std::size_t b = 0; b < kBands; ++b)
    {
        const juce::String id = kBandIds[b];
        bandSections[b] = std::make_unique<Section>(
            p.state, utf8(kBandNames[b]),
            std::vector<Section::Row> { { { id + "Threshold", "Threshold" },
                                          { id + "Ratio", "Ratio" },
                                          { id + "Upward", utf8("Aufwärts") },
                                          { id + "Gain", "Gain" } },
                                        { { id + "Attack", "Attack" }, { id + "Release", "Release" }, { id + "Solo", "Solo" } } },
            id + "On");
        addAndMakeVisible(*bandSections[b]);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &bands, &scope })
        addAndMakeVisible(*c);
    const int bandsWidth = 3 * bandSections[0]->preferredWidth() + 2 * kGap;
    const int width = 2 * kMargin + std::max(bands.preferredWidth() + 200, bandsWidth);
    setSize(width, 2 * kMargin + kHeader + bands.preferredHeight() + kGap + bandSections[0]->preferredHeight() + kGap
                       + kScope);
}

MultibandEditor::~MultibandEditor() { setLookAndFeel(nullptr); }

void MultibandEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK MULTIBAND");
}

void MultibandEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &bands });
    layoutRow(area, { bandSections[0].get(), bandSections[1].get(), bandSections[2].get() });
}
} // namespace tonwerkmultiband
