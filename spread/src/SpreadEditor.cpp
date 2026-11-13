#include "SpreadEditor.h"

#include "PresetCategories.h"
#include "SpreadPresets.h"

namespace tonwerkspread
{
using namespace tonwerkui;

namespace
{
constexpr int kStereoDisplay = 190;
constexpr float kLeanDb = 12.0f;
} // namespace

StereoView::StereoView(SpreadProcessor& p) : processor(p) { startTimerHz(30); }

void StereoView::refresh()
{
    shown = processor.readSettings();
    correlationIn = processor.correlationIn.load();
    correlationOut = processor.correlationOut.load();
    side = processor.sideShare.load();
    processor.inLeft.read(inL.data(), kPoints);
    processor.inRight.read(inR.data(), kPoints);
    processor.outLeft.read(outL.data(), kPoints);
    processor.outRight.read(outR.data(), kPoints);
    repaint();
}

juce::String StereoView::caption() const
{
    const auto dot = utf8(" · ");
    const int mode = std::clamp(shown.mode, 0, (int) kModeNames.size() - 1);
    juce::String text = tr(kModeNames[(std::size_t) mode]).toUpperCase();
    if (mode != bands)
        text << dot << juce::roundToInt(shown.time * 1000.0f) << " MS";
    text << dot << tr("BASS MONO") << " ";
    if (shown.bassMono > kBassOff)
        text << juce::roundToInt(shown.bassMono) << " HZ";
    else
        text << tr("AUS");
    if (shown.monoCheck)
        text << dot << tr("MONO-CHECK");
    return text;
}

void StereoView::paint(juce::Graphics& g)
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

    auto gonio = inner.removeFromLeft(inner.getHeight());
    inner.removeFromLeft(12.0f);
    paintGoniometer(g, gonio);
    auto meters = inner.removeFromBottom(30.0f);
    inner.removeFromBottom(6.0f);
    paintLean(g, inner);
    paintCorrelation(g, meters.removeFromTop(13.0f), tr("EINGANG"), correlationIn, colours::muted);
    meters.removeFromTop(4.0f);
    paintCorrelation(g, meters.removeFromTop(13.0f), tr("AUSGANG"), correlationOut, colours::cyan);
}

void StereoView::paintGoniometer(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto centre = area.getCentre();
    const float radius = 0.5f * std::min(area.getWidth(), area.getHeight());
    g.setColour(colours::border.withAlpha(0.7f));
    g.drawEllipse(area.withSizeKeepingCentre(2.0f * radius, 2.0f * radius), 1.0f);
    g.drawLine(centre.x, centre.y - radius, centre.x, centre.y + radius, 1.0f);
    const float diagonal = radius * 0.7071f;
    g.drawLine(centre.x - diagonal, centre.y - diagonal, centre.x + diagonal, centre.y + diagonal, 0.6f);
    g.drawLine(centre.x - diagonal, centre.y + diagonal, centre.x + diagonal, centre.y - diagonal, 0.6f);
    g.setFont(mono(9.0f));
    g.setColour(colours::muted);
    g.drawText("L", juce::Rectangle<float>(centre.x - diagonal - 12.0f, centre.y - diagonal - 12.0f, 12.0f, 12.0f),
               juce::Justification::centred);
    g.drawText("R", juce::Rectangle<float>(centre.x + diagonal, centre.y - diagonal - 12.0f, 12.0f, 12.0f),
               juce::Justification::centred);

    // Scaled to the louder of the two takes, so a quiet sound fills the circle too.
    float peak = 1.0e-3f;
    for (int i = 0; i < kPoints; ++i)
        peak = std::max({ peak, std::abs(inL[(std::size_t) i]) + std::abs(inR[(std::size_t) i]),
                          std::abs(outL[(std::size_t) i]) + std::abs(outR[(std::size_t) i]) });
    const float scale = 0.95f * radius / peak;
    auto plot = [&](const std::array<float, kPoints>& l, const std::array<float, kPoints>& r, juce::Colour colour) {
        g.setColour(colour);
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = centre.x + (r[(std::size_t) i] - l[(std::size_t) i]) * scale * 0.7071f;
            const float y = centre.y - (l[(std::size_t) i] + r[(std::size_t) i]) * scale * 0.7071f;
            g.fillRect(x - 0.6f, y - 0.6f, 1.2f, 1.2f);
        }
    };
    plot(inL, inR, colours::muted.withAlpha(0.35f));
    plot(outL, outR, colours::cyan.withAlpha(0.75f));
}

void StereoView::paintLean(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto toX = [&](double hz) {
        return area.getX() + (float) (std::log(hz / 20.0) / std::log(1000.0)) * area.getWidth();
    };
    auto toY = [&](float db) {
        return area.getCentreY() - std::clamp(db / kLeanDb, -1.0f, 1.0f) * 0.5f * area.getHeight();
    };
    g.setColour(colours::border.withAlpha(0.6f));
    for (const double hz : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine(juce::roundToInt(toX(hz)), area.getY(), area.getBottom());
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
    g.setFont(mono(9.0f));
    g.setColour(colours::muted);
    g.drawText(tr("LINKS"), area.reduced(3.0f, 1.0f).withHeight(12.0f).toNearestInt(), juce::Justification::topLeft);
    g.drawText(tr("RECHTS"), area.reduced(3.0f, 1.0f).withTrimmedTop(area.getHeight() - 14.0f).toNearestInt(),
               juce::Justification::bottomLeft);
    for (const auto& [hz, name] : { std::pair { 100.0, "100" }, std::pair { 1000.0, "1k" }, std::pair { 10000.0, "10k" } })
        g.drawText(name, juce::Rectangle<float>(toX(hz) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f),
                   juce::Justification::centredLeft);

    if (shown.bassMono > kBassOff)
    {
        const float x = toX(shown.bassMono);
        g.setColour(colours::red.withAlpha(0.18f));
        g.fillRect(juce::Rectangle<float>(area.getX(), area.getY(), x - area.getX(), area.getHeight()));
        g.setColour(colours::red.withAlpha(0.8f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    }

    // How a centred sound leans; the input's own side does not show here, only what the effect makes of a mono one.
    Settings still = shown;
    still.motion = 0.0f;
    // Diffus and Kamm wiggle faster than a pixel up high, so each column shows the span it covers: filled towards the
    // side it leans to, yellow left, cyan right.
    const int columns = std::max(2, (int) area.getWidth());
    const float zero = area.getCentreY();
    for (int k = 0; k < columns; ++k)
    {
        float most = -1000.0f, least = 1000.0f;
        for (int step = 0; step < 4; ++step)
        {
            const double hz = 20.0 * std::pow(1000.0, ((double) k + step / 4.0) / columns);
            const float db = Engine::sideGainAt(still, hz);
            most = std::max(most, db);
            least = std::min(least, db);
        }
        const float x = area.getX() + (float) k;
        if (most > 0.0f)
        {
            g.setColour(colours::yellow.withAlpha(0.75f));
            g.drawVerticalLine((int) x, toY(most), zero);
        }
        if (least < 0.0f)
        {
            g.setColour(colours::cyan.withAlpha(0.6f));
            g.drawVerticalLine((int) x, zero, toY(least));
        }
    }
}

void StereoView::paintCorrelation(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                                  float value, juce::Colour colour)
{
    g.setFont(mono(9.0f));
    g.setColour(colours::muted);
    g.drawText(name, area.removeFromLeft(64.0f).toNearestInt(), juce::Justification::centredLeft);
    auto number = area.removeFromRight(40.0f);
    g.drawText(juce::String(value, 2), number.toNearestInt(), juce::Justification::centredRight);
    auto bar = area.reduced(0.0f, 3.0f);
    g.setColour(colours::border.withAlpha(0.6f));
    g.fillRect(bar);
    const float middle = bar.getCentreX();
    const float x = middle + std::clamp(value, -1.0f, 1.0f) * 0.5f * bar.getWidth();
    // Below zero the two sides work against each other: a mono speaker would lose the sound, so red.
    g.setColour(value < 0.0f ? colours::red : colour);
    g.fillRect(juce::Rectangle<float>(std::min(middle, x), bar.getY(), std::abs(x - middle), bar.getHeight()));
    g.setColour(colours::text.withAlpha(0.5f));
    g.drawVerticalLine(juce::roundToInt(middle), bar.getY() - 1.0f, bar.getBottom() + 1.0f);
}

StereoSection::StereoSection(SpreadProcessor& p)
    : Section(p.state, "Stereobild",
              { { { "width", "Breite" },
                  { "mode", "Modus", 2 },
                  { "amount", "Verbreiterung" },
                  { "time", "Zeit" },
                  { "motion", "Bewegung" } } },
              {}, { 0, kStereoDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void StereoSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

SpreadEditor::SpreadEditor(SpreadProcessor& p)
    : AudioProcessorEditor(p),
      stereo(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kSpreadCategories)),
      tone(p.state, utf8("Tiefen & Höhen"), { { { "bassMono", "Bass Mono" }, { "highCut", "High Cut" } } }),
      output(p.state, "Ausgang",
             { { { "monoCheck", "Mono-Check" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &stereo, &tone, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max(stereo.preferredWidth() + 260, tone.preferredWidth() + output.preferredWidth() + kGap);
    setSize(width, 2 * kMargin + kHeader + stereo.preferredHeight() + kGap + tone.preferredHeight());
}

SpreadEditor::~SpreadEditor() { setLookAndFeel(nullptr); }

void SpreadEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK SPREAD");
}

void SpreadEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    layoutRow(area, { &stereo });
    layoutRow(area, { &tone, &output });
}
} // namespace tonwerkspread
