#include "FilterEditor.h"

#include "FilterPresets.h"
#include "PresetCategories.h"

#include <complex>

namespace tonwerkfilter
{
using namespace tonwerkui;

namespace
{
constexpr int kDisplay = 120;
constexpr int kScope = 90;
constexpr float kLowestHz = 20.0f, kHighestHz = 20000.0f;
constexpr float kTopDb = 18.0f, kBottomDb = -36.0f;
} // namespace

ResponseView::ResponseView(FilterProcessor& p) : processor(p) { startTimerHz(30); }

void ResponseView::refresh()
{
    shown = processor.readSettings();
    cutoffNow = processor.cutoffNow.load();
    envelope = processor.envelopeNow.load();
    repaint();
}

float ResponseView::responseDb(const Settings& s, float cutoff, float hz)
{
    using C = std::complex<double>;
    const double r = std::clamp((double) s.resonance, 0.0, 1.0);
    const double k = 1.41421356 - (1.41421356 - 0.06) * r;
    const C x(0.0, hz / std::max(1.0f, cutoff));
    const C den = x * x + k * x + 1.0;
    const C flat = x * x + 1.41421356 * x + 1.0;
    C h = 1.0;
    switch (s.type)
    {
        case Type::lowpass12: h = 1.0 / den; break;
        case Type::lowpass24: h = 1.0 / (den * flat); break;
        case Type::highpass12: h = x * x / den; break;
        case Type::highpass24: h = x * x * x * x / (den * flat); break;
        case Type::bandpass: h = k * x / den; break;
        case Type::notch: h = (x * x + 1.0) / den; break;
        case Type::ladder:
        {
            const double big = 4.0 * r;
            h = (1.0 + 0.5 * big) / (std::pow(1.0 + x, 4) + big);
            break;
        }
        case Type::comb:
        {
            const double feedback = 0.97 * r;
            const C turn = std::polar(1.0, -2.0 * 3.14159265358979 * hz / std::max(1.0f, cutoff));
            h = std::sqrt(1.0 - feedback * feedback) / (1.0 - feedback * turn);
            break;
        }
    }
    return (float) (20.0 * std::log10(std::max(1.0e-6, std::abs(h))));
}

void ResponseView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    auto inner = area.reduced(10.0f, 6.0f);
    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.8f));
    const auto dot = utf8(" · ");
    juce::String caption = juce::String(utf8(kTypeNames[(std::size_t) shown.type])).toUpperCase() + dot
                           + juce::String(juce::roundToInt(cutoffNow)) + " HZ";
    if (std::abs(shown.envelopeAmount) > 0.005f)
        caption << dot << utf8("HÜLLKURVE ") << juce::roundToInt(envelope * 100.0f) << " %";
    g.drawText(caption, inner.removeFromTop(14.0f).toNearestInt(),
               juce::Justification::centredLeft);
    inner.removeFromTop(2.0f);

    auto toX = [&](float hz) {
        return inner.getX() + std::log(hz / kLowestHz) / std::log(kHighestHz / kLowestHz) * inner.getWidth();
    };
    auto toY = [&](float db) {
        return inner.getY() + (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * inner.getHeight();
    };
    // Lines at 100 Hz, 1 kHz, 10 kHz and 0 dB.
    g.setColour(colours::border);
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine(juce::roundToInt(toX(hz)), inner.getY(), inner.getBottom());
    g.drawHorizontalLine(juce::roundToInt(toY(0.0f)), inner.getX(), inner.getRight());

    auto curve = [&](float cutoff) {
        juce::Path path;
        const int points = std::max(2, (int) inner.getWidth());
        for (int i = 0; i <= points; ++i)
        {
            const float hz = kLowestHz * std::pow(kHighestHz / kLowestHz, (float) i / (float) points);
            const float y = toY(responseDb(shown, cutoff, hz));
            if (i == 0)
                path.startNewSubPath(toX(hz), y);
            else
                path.lineTo(toX(hz), y);
        }
        return path;
    };
    g.setColour(colours::cyan.withAlpha(0.35f));
    g.strokePath(curve(shown.cutoff), juce::PathStrokeType(1.0f));
    auto now = curve(cutoffNow);
    auto filled = now;
    filled.lineTo(inner.getRight(), inner.getBottom());
    filled.lineTo(inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour(colours::yellow.withAlpha(0.12f));
    g.fillPath(filled);
    g.setColour(colours::yellow);
    g.strokePath(now, juce::PathStrokeType(1.6f));
    g.setColour(colours::red.withAlpha(0.8f));
    g.drawVerticalLine(juce::roundToInt(toX(std::clamp(cutoffNow, kLowestHz, kHighestHz))), inner.getY(),
                       inner.getBottom());
}

FilterSection::FilterSection(FilterProcessor& p)
    : Section(p.state, "Filter",
              { { { "type", "Typ", 2 }, { "cutoff", "Cutoff" }, { "resonance", "Resonanz" }, { "drive", "Drive" } } },
              {}, { 0, kDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void FilterSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

FilterEditor::FilterEditor(FilterProcessor& p)
    : AudioProcessorEditor(p),
      filter(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kFilterCategories)),
      scope(p.scopeOut, &p.scopeIn),
      envelope(p.state, utf8("HÜLLKURVE"),
               { { { "envAmount", "Menge" }, { "sensitivity", "Pegel" }, { "attack", "Attack" }, { "release", "Release" } } }),
      lfo(p.state, "LFO",
          { { { "lfoShape", "Form", 2 },
              { "lfoSync", "Sync" },
              { "lfoDivision", "Takt", 0, "lfoSync", true },
              { "lfoRate", "Rate", 0, "lfoSync", false },
              { "lfoAmount", "Tiefe" },
              { "spread", "Stereo" } } }),
      output(p.state, "Ausgang", { { { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("FILTER", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &filter, &scope, &envelope, &lfo, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin
                      + std::max({ filter.preferredWidth() + 200, envelope.preferredWidth() + output.preferredWidth() + kGap,
                                   lfo.preferredWidth() });
    setSize(width, 2 * kMargin + kHeader + filter.preferredHeight() + kGap + envelope.preferredHeight() + kGap
                       + lfo.preferredHeight() + kGap + kScope);
}

FilterEditor::~FilterEditor() { setLookAndFeel(nullptr); }

void FilterEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK FILTER");
}

void FilterEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &filter });
    layoutRow(area, { &envelope, &output });
    layoutRow(area, { &lfo });
}
} // namespace tonwerkfilter
