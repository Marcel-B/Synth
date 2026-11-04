#include "DrumsEditor.h"

#include "DrumsPresets.h"
#include "PresetCategories.h"

namespace tonwerkdrums
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
float valueOf(const DrumsProcessor& processor, int pad, const char* suffix)
{
    return processor.state.getRawParameterValue(juce::String(kPadIds[(std::size_t) pad]) + suffix)->load();
}

juce::String noteName(int note) { return noteNames()[note % 12] + juce::String(octaveOf(note)); }
} // namespace

PadGrid::PadGrid(DrumsProcessor& processor) : synth(processor) {}

juce::Rectangle<int> PadGrid::cellArea(int place) const
{
    const auto area = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitle).withTrimmedBottom(kPad);
    const int width = area.getWidth() / 4, height = area.getHeight() / 4;
    return juce::Rectangle<int>(area.getX() + (place % 4) * width, area.getY() + (place / 4) * height, width, height)
        .reduced(3);
}

void PadGrid::update()
{
    // What the pads show besides their lights: the chosen one, the notes and the mutes.
    std::uint64_t now = (std::uint64_t) synth.selectedPad.load();
    for (int p = 0; p < kPads; ++p)
        now = now * 131u + (std::uint64_t) juce::roundToInt(valueOf(synth, p, "Note"))
              + (valueOf(synth, p, "Mute") >= 0.5f ? 128u : 0u);
    bool changed = now != shown;
    shown = now;
    for (int p = 0; p < kPads; ++p)
    {
        const float peak = synth.padPeaks[(std::size_t) p].exchange(0.0f);
        const float light = std::max(std::min(1.0f, peak * 3.0f), lights[(std::size_t) p] * 0.8f);
        changed = changed || std::abs(light - lights[(std::size_t) p]) > 0.01f;
        lights[(std::size_t) p] = light < 0.02f ? 0.0f : light;
    }
    if (changed)
        repaint();
}

void PadGrid::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds(), "Pads");
    const int selected = synth.selectedPad.load();
    for (int place = 0; place < kPads; ++place)
    {
        const int p = (int) kGrid[(std::size_t) place];
        const auto cell = cellArea(place).toFloat();
        const bool chosen = p == selected;
        const bool silent = valueOf(synth, p, "Mute") >= 0.5f;
        const float light = lights[(std::size_t) p];
        g.setColour(screen);
        g.fillRoundedRectangle(cell, 4.0f);
        if (light > 0.0f)
        {
            g.setColour(cyan.withAlpha(0.45f * light));
            g.fillRoundedRectangle(cell, 4.0f);
        }
        g.setColour(chosen ? yellow : border);
        g.drawRoundedRectangle(cell.reduced(0.5f), 4.0f, chosen ? 2.0f : 1.0f);
        auto inside = cell.reduced(8.0f, 6.0f).toNearestInt();
        g.setColour(silent ? red : chosen ? yellow : text);
        g.setFont(mono(11.0f));
        g.drawFittedText(tr(kPadNames[(std::size_t) p]).toUpperCase(), inside.removeFromTop(inside.getHeight() / 2),
                         juce::Justification::topLeft, 2);
        g.setColour(muted);
        g.setFont(mono(10.0f));
        g.drawText(noteName(juce::roundToInt(valueOf(synth, p, "Note"))), inside, juce::Justification::bottomLeft);
        if (silent)
        {
            g.setColour(red);
            g.drawText("M", inside, juce::Justification::bottomRight);
        }
    }
}

void PadGrid::mouseDown(const juce::MouseEvent& event)
{
    for (int place = 0; place < kPads; ++place)
        if (const auto cell = cellArea(place); cell.contains(event.getPosition()))
        {
            const int p = (int) kGrid[(std::size_t) place];
            // The top of a pad hits hardest, its bottom softly.
            const float height = (float) (event.getPosition().y - cell.getY()) / (float) std::max(1, cell.getHeight());
            synth.audition(p, 1.0f - 0.7f * std::clamp(height, 0.0f, 1.0f));
            if (auto* editor = findParentComponentOfClass<DrumsEditor>())
                editor->select(p);
            repaint();
            return;
        }
}

namespace
{
std::unique_ptr<Section> padSection(DrumsProcessor& p, int pad)
{
    const juce::String prefix = kPadIds[(std::size_t) pad];
    return std::make_unique<Section>(
        p.state, tr(kPadNames[(std::size_t) pad]),
        std::vector<Section::Row> { { { prefix + "Tune", "Stimmung" },
                                      { prefix + "Decay", "Abklingen" },
                                      { prefix + "Tone", tr(kSoundKnobs[(std::size_t) pad].first) },
                                      { prefix + "Character", tr(kSoundKnobs[(std::size_t) pad].second) },
                                      { prefix + "Level", "Pegel" },
                                      { prefix + "Pan", "Panorama" },
                                      { prefix + "Delay", "Delay" },
                                      { prefix + "Reverb", "Hall" },
                                      { prefix + "Mute", "Stumm" } },
                                    { { prefix + "Attack", "Attack" },
                                      { prefix + "Hold", "Halten" },
                                      { prefix + "PitchMod", "Pitch-Mod" },
                                      { prefix + "ModDecay", "Mod-Zeit" },
                                      { prefix + "Filter", "Filter" },
                                      { prefix + "Cutoff", "Cutoff" },
                                      { prefix + "Resonance", "Resonanz" },
                                      { prefix + "FilterMod", "Filter-Mod" } },
                                    { { prefix + "Drive", "Zerrung" },
                                      { prefix + "Bits", "Bits" },
                                      { prefix + "Crush", "Rate" },
                                      { prefix + "Velocity", "Dynamik" },
                                      { prefix + "Random", "Zufall" },
                                      { prefix + "Machine", "Maschine", 2 },
                                      { prefix + "Note", "Note" } } });
}
} // namespace

DrumsEditor::DrumsEditor(DrumsProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      grid(p),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", tr("Länge") } } },
              "fxOn"),
      master(p.state, "Summe", { { { "drive", "Drive" }, { "master", "Master" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (int pad = 0; pad < kPads; ++pad)
        pads.push_back(padSection(p, pad));

    // Grouped under headings; an item's id stays its program number + 1.
    const auto& factory = factoryKits();
    menu = menuOrder(
        (int) factory.size(), [&factory](int i) { return factory[(std::size_t) i].category.toRawUTF8(); },
        kDrumsCategories);
    int group = -1;
    for (const int i : menu)
    {
        const auto& kit = factory[(std::size_t) i];
        const int category = categoryIndex(kit.category.toRawUTF8(), kDrumsCategories);
        if (category != group && category >= 0)
            presets.addSectionHeading(tr(kit.category));
        group = category;
        presets.addItem(kit.name, i + 1);
    }
    presets.setTextWhenNothingSelected(tr("Kit"));
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            choosePreset(presets.getSelectedId() - 1);
    };
    auto step = [this](int direction) {
        const auto at = std::find(menu.begin(), menu.end(), synth.getCurrentProgram());
        const auto from = at == menu.end() ? -1 : (int) std::distance(menu.begin(), at);
        const int to = from + direction;
        if (juce::isPositiveAndBelow(to, (int) menu.size()))
            choosePreset(menu[(std::size_t) to]);
    };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &grid, &effects, &master })
        addAndMakeVisible(*c);
    for (auto& section : pads)
        addChildComponent(*section);

    const int width = 2 * kMargin
                      + std::max(grid.preferredWidth() + pads.front()->preferredWidth() + kGap,
                                 effects.preferredWidth() + master.preferredWidth() + kGap);
    const int height = 2 * kMargin + kHeader + pads.front()->preferredHeight()
                       + std::max(effects.preferredHeight(), master.preferredHeight()) + kGap;
    setSize(width, height);
    select(synth.selectedPad.load());
    timerCallback();
    startTimerHz(30);
}

DrumsEditor::~DrumsEditor() { setLookAndFeel(nullptr); }

void DrumsEditor::select(int pad)
{
    pad = std::clamp(pad, 0, kPads - 1);
    synth.selectedPad.store(pad);
    for (int p = 0; p < kPads; ++p)
        pads[(std::size_t) p]->setVisible(p == pad);
    grid.repaint();
}

void DrumsEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
    grid.repaint();
}

void DrumsEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK DRUMS");
}

void DrumsEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));

    auto top = area.removeFromTop(pads.front()->preferredHeight());
    grid.setBounds(top.removeFromLeft(grid.preferredWidth()));
    top.removeFromLeft(kGap);
    for (auto& section : pads)
        section->setBounds(top);
    area.removeFromTop(kGap);
    layoutRow(area, { &effects, &master });
}

void DrumsEditor::timerCallback()
{
    grid.update();
    if (synth.presetName() != shownPreset)
    {
        shownPreset = synth.presetName();
        presets.setSelectedId(0, juce::dontSendNotification);
        for (int i = 0; i < presets.getNumItems(); ++i)
            if (presets.getItemText(i) == shownPreset)
                presets.setSelectedId(presets.getItemId(i), juce::dontSendNotification);
        if (presets.getSelectedId() == 0)
            presets.setText(shownPreset, juce::dontSendNotification);
    }
}
} // namespace tonwerkdrums
