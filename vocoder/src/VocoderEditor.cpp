#include "VocoderEditor.h"

#include "PresetCategories.h"
#include "VocoderPresets.h"

namespace tonwerkvocoder
{
using namespace tonwerkui;

namespace
{
constexpr int kBandDisplay = 120;
constexpr int kScope = 110;
} // namespace

BandView::BandView(VocoderProcessor& p) : processor(p) { startTimerHz(30); }

void BandView::refresh()
{
    bands = std::clamp(processor.bandCount.load(), 1, kMaxBands);
    for (std::size_t b = 0; b < kMaxBands; ++b)
    {
        // Bars fall slowly, as on a hardware meter, so short syllables stay readable.
        const float level = processor.bandLevels[b].load();
        shown[b] = std::max(level, shown[b] * 0.85f);
    }
    juce::String text;
    for (const auto& note : processor.carrierNotes)
        if (const int n = note.load(); n >= 0)
            text << (text.isEmpty() ? "" : " ") << VocoderProcessor::noteName(n);
    notes = text;
    repaint();
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
    g.drawText(juce::String(bands) + " " + tr("BÄNDER") + utf8("  ·  ") + (notes.isEmpty() ? tr("KEINE NOTE") : notes),
               inner.removeFromTop(14.0f).toNearestInt(), juce::Justification::centredLeft);
    auto labels = inner.removeFromBottom(12.0f);
    const float step = inner.getWidth() / (float) bands;
    for (int b = 0; b < bands; ++b)
    {
        // 60 dB across the height, from -66 dBFS.
        const float db = juce::Decibels::gainToDecibels(shown[(std::size_t) b], -100.0f);
        const float fill = std::clamp((db + 66.0f) / 60.0f, 0.0f, 1.0f);
        const auto column = juce::Rectangle<float>(inner.getX() + (float) b * step + 1.0f, inner.getY(),
                                                   std::max(1.0f, step - 2.0f), inner.getHeight());
        g.setColour(colours::border.withAlpha(0.5f));
        g.fillRect(column);
        const auto bar = column.withTop(column.getBottom() - fill * column.getHeight());
        g.setColour((b % 2 == 0 ? colours::cyan : colours::yellow).withAlpha(0.85f));
        g.fillRect(bar);
    }
    // The lowest, middle and highest centre under their bars.
    g.setColour(colours::muted);
    for (const int b : { 0, bands / 2, bands - 1 })
    {
        const double hz = Engine::centre(b, bands);
        const juce::String text = hz >= 1000.0 ? juce::String(juce::roundToInt(hz / 1000.0)) + " kHz"
                                               : juce::String(juce::roundToInt(hz)) + " Hz";
        const float x = inner.getX() + ((float) b + 0.5f) * step;
        g.drawText(text, juce::Rectangle<float>(x - 30.0f, labels.getY(), 60.0f, labels.getHeight()).toNearestInt(),
                   b == 0 ? juce::Justification::centredLeft
                          : (b == bands - 1 ? juce::Justification::centredRight : juce::Justification::centred));
    }
}

BankSection::BankSection(VocoderProcessor& p)
    : Section(p.state, utf8("Filterbank"),
              { { { "bands", utf8("Bänder") },
                  { "formant", "Formant" },
                  { "sharpness", utf8("Schärfe") },
                  { "attack", "Attack" },
                  { "release", "Release" },
                  { "sibilance", "Zischlaute" },
                  { "freeze", "Einfrieren" } } },
              {}, { 0, kBandDisplay }),
      view(p)
{
    addAndMakeVisible(view);
}

void BankSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

VocoderEditor::VocoderEditor(VocoderProcessor& p)
    : AudioProcessorEditor(p),
      bank(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kVocoderCategories)),
      scope(p.scopeOut, &p.scopeIn),
      carrier(p.state, utf8("Träger"),
              { { { "wave", "Welle" },
                  { "detune", "Verstimmung" },
                  { "octave", "Oktave" },
                  { "carrierAttack", "Attack" },
                  { "carrierRelease", "Release" },
                  { "noise", "Rauschen" },
                  { "latch", "Halten" } } }),
      output(p.state, "Ausgang", { { { "width", "Breite" }, { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption(tr("OSZILLOSKOP"));
    scope.setLegend("VOCODER", tr("STIMME"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &bank, &scope, &carrier, &output })
        addAndMakeVisible(*c);
    const int width = 2 * kMargin + std::max(bank.preferredWidth() + 200,
                                             carrier.preferredWidth() + kGap + output.preferredWidth());
    setSize(width, 2 * kMargin + kHeader + bank.preferredHeight() + kGap + carrier.preferredHeight() + kGap + kScope);
}

VocoderEditor::~VocoderEditor() { setLookAndFeel(nullptr); }

void VocoderEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK VOCODER");
}

void VocoderEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromBottom(kScope));
    area.removeFromBottom(kGap);
    layoutRow(area, { &bank });
    layoutRow(area, { &carrier, &output });
}
} // namespace tonwerkvocoder
