#include "RobovoxEditor.h"

#include "PresetCategories.h"
#include "RobovoxPresets.h"

namespace tonwerkrobovox
{
using namespace tonwerkui;

namespace
{
constexpr int kScope = 130;
constexpr int kPitchDisplay = 130;
} // namespace

PitchView::PitchView(RobovoxProcessor& p) : processor(p) { startTimerHz(30); }

void PitchView::refresh()
{
    const int note = processor.carrierNote.load();
    const float pitch = processor.sungPitch.load();
    if (note != carrier || std::abs(pitch - sung) > 0.05f)
    {
        carrier = note;
        sung = pitch;
        repaint();
    }
}

void PitchView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    paintScreen(g, area);
    auto text = getLocalBounds().reduced(8, 6);
    g.setFont(mono(11.0f));
    g.setColour(colours::muted);
    g.drawText(tr("TRÄGER"), text.removeFromTop(14), juce::Justification::centredLeft);
    g.setFont(mono(34.0f));
    g.setColour(colours::yellow);
    g.drawText(noteName(carrier), text.removeFromTop(40), juce::Justification::centredLeft);
    g.setFont(mono(11.0f));
    g.setColour(colours::muted);
    g.drawText(tr("STIMME"), text.removeFromTop(14), juce::Justification::centredLeft);
    g.setFont(mono(15.0f));
    if (sung < 0.0f)
    {
        g.setColour(colours::red);
        g.drawText(tr("STILLE"), text.removeFromTop(20), juce::Justification::centredLeft);
        return;
    }
    // The sung note and how far off it the voice is: the quantiser hides that, the display shows it.
    const int nearest = juce::roundToInt(sung);
    const int cents = juce::roundToInt((sung - (float) nearest) * 100.0f);
    g.setColour(colours::cyan);
    g.drawText(noteName(nearest) + (cents >= 0 ? " +" : " ") + juce::String(cents) + " ct", text.removeFromTop(20),
               juce::Justification::centredLeft);
}

PitchSection::PitchSection(RobovoxProcessor& p)
    : Section(p.state, utf8("Tonhöhe"),
              { { { "mode", "Quelle" }, { "key", "Tonart" } },
                { { "scale", "Skala" }, { "octave", "Oktave" }, { "glide", "Gleiten" } } },
              {}, { kPitchDisplay, 0 }),
      view(p)
{
    addAndMakeVisible(view);
}

void PitchSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

RobovoxEditor::RobovoxEditor(RobovoxProcessor& p)
    : AudioProcessorEditor(p),
      pitch(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kRobovoxCategories)),
      // The voice is read as far behind as the vocoder delays the robot, so the two traces line up.
      scope(p.scopeOut, &p.scopeIn, [&p] { return p.getLatencySamples(); }),
      carrier(p.state, utf8("Träger"), { { { "wave", "Welle" } }, { { "bass", "Fundament" } } }),
      voice(p.state, "Stimme", { { { "formant", "Formant" }, { "bandwidth", "Bandbreite" } }, { { "sibilance", "Zischlaute" } } }),
      output(p.state, "Ausgang", { { { "mix", "Mix" } }, { { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption(tr("OSZILLOSKOP"));
    scope.setLegend(tr("ROBOTER"), tr("STIMME"));
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &scope, &pitch, &carrier, &voice, &output })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + kScope + kGap + pitch.preferredHeight());
}

RobovoxEditor::~RobovoxEditor() { setLookAndFeel(nullptr); }

void RobovoxEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK ROBOVOX");
}

void RobovoxEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &pitch, &carrier, &voice, &output });
}
} // namespace tonwerkrobovox
