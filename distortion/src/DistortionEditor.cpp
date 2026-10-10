#include "DistortionEditor.h"

namespace tonwerkdistortion
{
using namespace tonwerkui;

namespace
{
constexpr int kScope = 140;
} // namespace

DistortionEditor::DistortionEditor(DistortionProcessor& p)
    : AudioProcessorEditor(p),
      // The input is read as far behind as the oversampling delays the output, so the two traces line up.
      scope(p.scopeOut, &p.scopeIn, [&p] { return p.getLatencySamples(); }),
      pedal(p.state, "Pedal", { { { "level", "Level" }, { "tone", "Tone" }, { "distortion", "Dist" } } }),
      input(p.state, "Eingang", { { { "input", "Gain" } } })
{
    setLookAndFeel(&lookAndFeel);
    scope.setCaption("OSZILLOSKOP");
    scope.setLegend("AUSGANG", "EINGANG");
    for (auto* c : std::initializer_list<juce::Component*> { &scope, &pedal, &input })
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
    area.removeFromTop(kHeader);
    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &pedal, &input });
}
} // namespace tonwerkdistortion
