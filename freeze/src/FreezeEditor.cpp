#include "FreezeEditor.h"

#include "FreezePresets.h"
#include "PresetCategories.h"

namespace tonwerkfreeze
{
using namespace tonwerkui;

namespace
{
constexpr int kSpectrum = 150;
constexpr float kFloorDb = -90.0f;
} // namespace

SpectrumView::SpectrumView(FreezeProcessor& p) : processor(p)
{
    live.fill(-120.0f);
    held.fill(-120.0f);
    lastFreezes = p.freezes.load();
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    startTimerHz(30);
}

void SpectrumView::refresh()
{
    for (std::size_t b = 0; b < live.size(); ++b)
    {
        // Falling slower than rising, as a meter does, so the live trace does not flicker.
        const float now = processor.liveBands[b].load(std::memory_order_relaxed);
        live[b] = std::max(now, live[b] - 1.5f);
        held[b] = processor.frozenBands[b].load(std::memory_order_relaxed);
    }
    level = processor.frozenLevel.load();
    if (const int count = processor.freezes.load(); count != lastFreezes)
    {
        lastFreezes = count;
        flash = 1.0f;
    }
    else
        flash = std::max(0.0f, flash - 0.08f);
    repaint();
}

void SpectrumView::toggleFreeze()
{
    if (auto* parameter = processor.state.getParameter("freeze"))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getValue() > 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
}

void SpectrumView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto inner = area.reduced(8.0f, 20.0f).withTrimmedBottom(-12.0f);

    // Octave lines, labelled where a musician looks: 100 Hz, 1 kHz, 10 kHz.
    const auto xOf = [&inner](float hz) {
        return inner.getX() + inner.getWidth() * std::log(hz / kDisplayLowHz) / std::log(kDisplayHighHz / kDisplayLowHz);
    };
    g.setFont(mono(9.0f));
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = xOf(hz);
        g.setColour(colours::cyan.withAlpha(0.12f));
        g.drawVerticalLine(juce::roundToInt(x), inner.getY(), inner.getBottom());
        g.setColour(colours::muted);
        g.drawText(hz < 1000.0f ? "100 Hz" : hz < 10000.0f ? "1 kHz" : "10 kHz",
                   juce::Rectangle<float>(x + 3.0f, inner.getBottom() - 12.0f, 50.0f, 12.0f), juce::Justification::left);
    }

    auto curve = [&](const std::array<float, kDisplayBands>& bands, bool closed) {
        juce::Path path;
        const float w = inner.getWidth() / (float) kDisplayBands;
        for (int b = 0; b < kDisplayBands; ++b)
        {
            const float db = std::clamp(bands[(std::size_t) b], kFloorDb, 0.0f);
            const float x = inner.getX() + ((float) b + 0.5f) * w;
            const float y = inner.getBottom() - (db - kFloorDb) / -kFloorDb * inner.getHeight();
            if (b == 0)
                path.startNewSubPath(inner.getX(), closed ? inner.getBottom() : y);
            if (b == 0 && closed)
                path.lineTo(inner.getX(), y);
            path.lineTo(x, y);
        }
        if (closed)
        {
            path.lineTo(inner.getRight(), inner.getBottom());
            path.closeSubPath();
        }
        return path;
    };
    g.setColour(colours::cyan.withAlpha(0.18f));
    g.fillPath(curve(live, true));
    g.setColour(colours::cyan.withAlpha(0.6f));
    g.strokePath(curve(live, false), juce::PathStrokeType(1.0f));
    if (level > 0.0f)
    {
        g.setColour(colours::yellow.withAlpha(0.22f * level));
        g.fillPath(curve(held, true));
        g.setColour(colours::yellow.withAlpha(0.4f + 0.6f * level));
        g.strokePath(curve(held, false), juce::PathStrokeType(1.6f));
    }
    if (flash > 0.0f)
    {
        g.setColour(colours::yellow.withAlpha(0.15f * flash));
        g.fillRect(inner);
    }

    auto text = area.reduced(10.0f, 5.0f).toNearestInt().removeFromTop(16);
    g.setFont(mono(10.0f));
    g.setColour(colours::cyan.withAlpha(0.8f));
    g.drawText(tr("SPEKTRUM"), text, juce::Justification::centredLeft);
    const bool frozen = processor.state.getRawParameterValue("freeze")->load() > 0.5f;
    g.setFont(mono(13.0f));
    g.setColour(frozen ? colours::yellow : colours::cyan.withAlpha(0.8f));
    g.drawText(frozen ? tr("EINGEFROREN") : juce::String("LIVE"), text, juce::Justification::centredRight);
    g.setFont(mono(9.0f));
    g.setColour(colours::muted);
    g.drawText(frozen ? tr("KLICKEN ZUM LOSLASSEN") : tr("KLICKEN ZUM EINFRIEREN"), text.withTrimmedRight(110),
               juce::Justification::centredRight);
}

FreezeEditor::FreezeEditor(FreezeProcessor& p)
    : AudioProcessorEditor(p),
      spectrum(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kFreezeCategories)),
      freeze(p.state, "Freeze",
             { { { "trigger", utf8("Auslöser") }, { "interval", "Intervall" } },
               { { "capture", "Aufnahme" }, { "fade", utf8("Überblenden") }, { "stack", "Stapeln" } } },
             "freeze"),
      sound(p.state, "Klang", { { { "pitch", utf8("Tonhöhe") }, { "shimmer", "Schimmer" } }, { { "lowCut", "Low Cut" }, { "highCut", "High Cut" } } }),
      motion(p.state, "Bewegung", { { { "motion", "Bewegung" }, { "drift", "Drift" } }, { { "blur", utf8("Unschärfe") }, { "width", "Breite" } } }),
      output(p.state, "Ausgang", { { { "mix", "Mix" } }, { { "level", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &spectrum, &freeze, &sound, &motion, &output })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + kSpectrum + kGap + freeze.preferredHeight());
}

FreezeEditor::~FreezeEditor() { setLookAndFeel(nullptr); }

void FreezeEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK FREEZE");
}

void FreezeEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    spectrum.setBounds(area.removeFromTop(kSpectrum));
    area.removeFromTop(kGap);
    layoutRow(area, { &freeze, &sound, &motion, &output });
}
} // namespace tonwerkfreeze
