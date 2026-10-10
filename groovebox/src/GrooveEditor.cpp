#include "GrooveEditor.h"

#include "GroovePresets.h"
#include "PresetCategories.h"

namespace tonwerkgroove
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
juce::RangedAudioParameter* muteOf(GrooveProcessor& processor, int track)
{
    return processor.state.getParameter(juce::String(kTrackIds[(std::size_t) track]) + "Mute");
}

int currentPattern(const GrooveProcessor& processor)
{
    return std::clamp(juce::roundToInt(processor.state.getRawParameterValue("pattern")->load()), 0, kPatterns - 1);
}
} // namespace

StepGrid::StepGrid(GrooveProcessor& processor) : synth(processor)
{
    for (auto* button : { &copy, &paste, &clear })
        addAndMakeVisible(*button);
    copy.onClick = [this] { copyPattern(); };
    paste.onClick = [this] { pastePattern(); };
    clear.onClick = [this] {
        synth.clearPattern(currentPattern(synth));
        repaint();
    };
    paste.setEnabled(false);
}

void StepGrid::copyPattern()
{
    clipboard = synth.pattern(currentPattern(synth));
    paste.setEnabled(true);
}

void StepGrid::pastePattern()
{
    if (! clipboard)
        return;
    synth.setPattern(currentPattern(synth), *clipboard);
    synth.patternsChanged();
    repaint();
}

juce::Rectangle<int> StepGrid::rowArea(int track) const
{
    return { kPad, kTitle + kPad / 2 + track * kLine, getWidth() - 2 * kPad, kLine - 3 };
}

juce::Rectangle<int> StepGrid::nameArea(int track) const { return rowArea(track).withWidth(kName); }

juce::Rectangle<int> StepGrid::muteArea(int track) const
{
    return rowArea(track).withTrimmedLeft(kName + 2).withWidth(kMute - 4);
}

juce::Rectangle<int> StepGrid::stepArea(int track, int step) const
{
    // The steps fill the row; a little room between groups of four, so the beats read at a glance.
    const auto row = rowArea(track).withTrimmedLeft(kName + kMute);
    const float cell = (float) (row.getWidth() - 3 * 7) / (float) kSteps;
    const int x = row.getX() + (int) std::round(step * cell) + 3 * (step / 4);
    const int next = row.getX() + (int) std::round((step + 1) * cell) + 3 * (step / 4);
    return { x, row.getY(), next - x - 2, row.getHeight() };
}

void StepGrid::resized()
{
    auto header = getLocalBounds().removeFromTop(kTitle).reduced(14, 2);
    clear.setBounds(header.removeFromRight(70));
    header.removeFromRight(4);
    paste.setBounds(header.removeFromRight(80));
    header.removeFromRight(4);
    copy.setBounds(header.removeFromRight(80));
}

void StepGrid::update()
{
    const int step = synth.playingStep.load();
    // What the grid shows besides the steps: the pattern, its length, the mutes and the steps' bits.
    std::uint64_t now = (std::uint64_t) currentPattern(synth) * 1000003u
                        + (std::uint64_t) juce::roundToInt(synth.state.getRawParameterValue("length")->load());
    for (int t = 0; t < kTracks; ++t)
    {
        const auto bits = synth.trackPattern(currentPattern(synth), t);
        now = now * 31u + bits.on + ((std::uint64_t) bits.accent << 32)
              + (muteOf(synth, t)->getValue() >= 0.5f ? 7u : 0u);
    }
    bool changed = step != playing || now != shown;
    shown = now;
    playing = step;
    for (int t = 0; t < kTracks; ++t)
    {
        const float peak = synth.trackPeaks[(std::size_t) t].exchange(0.0f);
        const float light = std::max(std::min(1.0f, peak * 3.0f), lights[(std::size_t) t] * 0.8f);
        changed = changed || std::abs(light - lights[(std::size_t) t]) > 0.01f;
        lights[(std::size_t) t] = light < 0.02f ? 0.0f : light;
    }
    if (changed)
        repaint();
}

void StepGrid::paint(juce::Graphics& g)
{
    const int pattern = currentPattern(synth);
    const int length = juce::roundToInt(synth.state.getRawParameterValue("length")->load());
    const int selected = synth.selectedTrack.load();
    paintPanel(g, getLocalBounds(), "Pattern " + juce::String::charToString((juce::juce_wchar) ('A' + pattern)));

    for (int t = 0; t < kTracks; ++t)
    {
        // The name: yellow when its knobs are shown, lit red by each hit.
        const auto name = nameArea(t);
        g.setColour(t == selected ? yellow.withAlpha(0.18f) : screen);
        g.fillRect(name);
        if (lights[(std::size_t) t] > 0.0f)
        {
            g.setColour(red.withAlpha(0.8f * lights[(std::size_t) t]));
            g.fillRect(name.withWidth(5));
        }
        g.setColour(t == selected ? yellow : text);
        g.setFont(mono(11.0f));
        g.drawText(juce::String::fromUTF8(kTrackNames[(std::size_t) t]).toUpperCase(), name.withTrimmedLeft(10),
                   juce::Justification::centredLeft);

        const bool silent = muteOf(synth, t)->getValue() >= 0.5f;
        const auto mute = muteArea(t);
        g.setColour(silent ? red : border);
        g.fillRect(mute);
        g.setColour(silent ? background : muted);
        g.setFont(mono(10.0f));
        g.drawText("M", mute, juce::Justification::centred);

        for (int s = 0; s < kSteps; ++s)
        {
            const auto cell = stepArea(t, s).toFloat();
            const int value = synth.step(pattern, t, s);
            const bool inside = s < length;
            const bool beat = (s / 4) % 2 == 0;
            g.setColour((beat ? panel.brighter(0.12f) : panel.brighter(0.05f)).withMultipliedAlpha(inside ? 1.0f : 0.4f));
            g.fillRect(cell);
            if (value > 0)
            {
                const auto colour = value == 2 ? yellow : cyan;
                g.setColour(colour.withAlpha(inside ? (silent ? 0.35f : 0.9f) : 0.25f));
                g.fillRect(cell.reduced(3.0f));
            }
            if (s == playing && inside)
            {
                g.setColour(text.withAlpha(0.8f));
                g.drawRect(cell, 1.5f);
            }
        }
    }
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    dragTrack = -1;
    for (int t = 0; t < kTracks; ++t)
    {
        if (nameArea(t).contains(event.getPosition()))
        {
            synth.audition(t);
            if (auto* editor = findParentComponentOfClass<GrooveEditor>())
                editor->select(t);
            repaint();
            return;
        }
        if (muteArea(t).contains(event.getPosition()))
        {
            auto* mute = muteOf(synth, t);
            mute->beginChangeGesture();
            mute->setValueNotifyingHost(mute->getValue() >= 0.5f ? 0.0f : 1.0f);
            mute->endChangeGesture();
            repaint();
            return;
        }
        for (int s = 0; s < kSteps; ++s)
            if (stepArea(t, s).expanded(1, 1).contains(event.getPosition()))
            {
                const int pattern = currentPattern(synth);
                dragTrack = t;
                dragValue = (synth.step(pattern, t, s) + 1) % 3;
                synth.setStep(pattern, t, s, dragValue);
                repaint();
                return;
            }
    }
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (dragTrack < 0)
        return;
    const int pattern = currentPattern(synth);
    for (int s = 0; s < kSteps; ++s)
        if (stepArea(dragTrack, s).expanded(1, 1).withY(0).withHeight(getHeight()).contains(event.getPosition())
            && synth.step(pattern, dragTrack, s) != dragValue)
        {
            synth.setStep(pattern, dragTrack, s, dragValue);
            repaint();
        }
}

namespace
{
std::unique_ptr<Section> trackSection(GrooveProcessor& p, int t)
{
    const juce::String prefix = kTrackIds[(std::size_t) t];
    return std::make_unique<Section>(
        p.state, juce::String::fromUTF8(kTrackNames[(std::size_t) t]),
        std::vector<Section::Row> { { { prefix + "Tune", "Stimmung" },
                                      { prefix + "Decay", "Abklingen" },
                                      { prefix + "Tone", juce::String::fromUTF8(kSoundKnobs[(std::size_t) t].first) },
                                      { prefix + "Character",
                                        juce::String::fromUTF8(kSoundKnobs[(std::size_t) t].second) },
                                      { prefix + "Level", "Pegel" },
                                      { prefix + "Pan", "Panorama" },
                                      { prefix + "Delay", "Delay" },
                                      { prefix + "Reverb", "Hall" },
                                      { prefix + "Mute", "Stumm" } } });
}
} // namespace

GrooveEditor::GrooveEditor(GrooveProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      sequencer(p.state, "Sequencer",
                { { { "seqMode", "Lauf" },
                    { "pattern", "Pattern" },
                    { "length", utf8("Länge") },
                    { "rate", "Raster" },
                    { "swing", "Swing" },
                    { "accent", "Akzent" } } }),
      master(p.state, "Summe", { { { "drive", "Drive" }, { "master", "Master" } } }),
      grid(p),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", utf8("Länge") } } })
{
    setLookAndFeel(&lookAndFeel);
    for (int t = 0; t < kTracks; ++t)
        tracks.push_back(trackSection(p, t));

    // Grouped under headings; an item's id stays its program number + 1.
    const auto& factory = factoryKits();
    menu = menuOrder(
        (int) factory.size(), [&factory](int i) { return factory[(std::size_t) i].category; }, kGrooveCategories);
    int group = -1;
    for (const int i : menu)
    {
        const auto& kit = factory[(std::size_t) i];
        const int category = categoryIndex(kit.category, kGrooveCategories);
        if (category != group && category >= 0)
            presets.addSectionHeading(utf8(kit.category));
        group = category;
        presets.addItem(utf8(kit.name), i + 1);
    }
    presets.setTextWhenNothingSelected("Kit");
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
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &sequencer, &master, &grid,
                                                            &effects })
        addAndMakeVisible(*c);
    for (auto& section : tracks)
        addChildComponent(*section);

    const int width = 2 * kMargin
                      + std::max({ grid.preferredWidth(),
                                   sequencer.preferredWidth() + master.preferredWidth() + kGap,
                                   tracks.front()->preferredWidth() + effects.preferredWidth() + kGap });
    const int height = 2 * kMargin + kHeader + sequencer.preferredHeight() + grid.preferredHeight()
                       + std::max(tracks.front()->preferredHeight(), effects.preferredHeight()) + 2 * kGap;
    setSize(width, height);
    select(synth.selectedTrack.load());
    timerCallback();
    startTimerHz(30);
}

GrooveEditor::~GrooveEditor() { setLookAndFeel(nullptr); }

void GrooveEditor::select(int track)
{
    track = std::clamp(track, 0, kTracks - 1);
    synth.selectedTrack.store(track);
    for (int t = 0; t < kTracks; ++t)
        tracks[(std::size_t) t]->setVisible(t == track);
    shownTrack = track;
    grid.repaint();
}

void GrooveEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
    grid.repaint();
}

void GrooveEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK GROOVEBOX");
}

void GrooveEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));

    layoutRow(area, { &sequencer, &master });
    grid.setBounds(area.removeFromTop(grid.preferredHeight()));
    area.removeFromTop(kGap);
    layoutRow(area, { tracks.front().get(), &effects });
    for (auto& section : tracks)
        section->setBounds(tracks.front()->getBounds());
}

void GrooveEditor::timerCallback()
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
        grid.repaint();
    }
}
} // namespace tonwerkgroove
