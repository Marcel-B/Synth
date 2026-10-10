#include "DistortionEditor.h"

#include "DistortionPresets.h"
#include "PresetCategories.h"

namespace tonwerkdistortion
{
using namespace tonwerkui;

namespace
{
constexpr int kScope = 140;
} // namespace

DistortionEditor::DistortionEditor(DistortionProcessor& p)
    : AudioProcessorEditor(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kDistortionCategories)),
      // The input is read as far behind as the oversampling delays the output, so the two traces line up.
      scope(p.scopeOut, &p.scopeIn, [&p] { return p.getLatencySamples(); }),
      pedal(p.state, "Pedal", { { { "level", "Level" }, { "tone", "Tone" }, { "distortion", "Dist" } } }),
      input(p.state, "Eingang", { { { "input", "Gain" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("AUSGANG", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &scope, &pedal, &input })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + kScope + kGap + pedal.preferredHeight());
}

DistortionEditor::~DistortionEditor() { setLookAndFeel(nullptr); }

void DistortionEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK DISTORTION");
}

void DistortionEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(200));
    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &pedal, &input });
}
} // namespace tonwerkdistortion
