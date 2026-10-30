#include "ArpEditor.h"

#include "ArpPresets.h"
#include "PresetCategories.h"

namespace tonwerkarp
{
using namespace tonwerkui;

namespace
{
constexpr int kNotesDisplay = 150;
constexpr int kLabels = 74;
constexpr int kNumberLane = 16;
constexpr int kOnLane = 24;
constexpr int kNoteLane = 72;
constexpr int kVelocityLane = 52;
constexpr int kTieLane = 22;
constexpr int kLaneGap = 4;
/** Pixels of drag per semitone. */
constexpr float kNoteDrag = 5.0f;
} // namespace

NotesView::NotesView(ArpProcessor& p) : processor(p) { startTimerHz(30); }

void NotesView::refresh()
{
    std::vector<int> now;
    const int count = std::min(processor.keyCount.load(), (int) processor.keys.size());
    for (int i = 0; i < count; ++i)
        now.push_back(processor.keys[(std::size_t) i].load());
    std::sort(now.begin(), now.end());
    const int last = processor.lastNote.load();
    if (now != keys || last != note)
    {
        keys = std::move(now);
        note = last;
        repaint();
    }
}

void NotesView::paint(juce::Graphics& g)
{
    paintScreen(g, getLocalBounds().toFloat());
    auto text = getLocalBounds().reduced(8, 6);
    g.setFont(mono(11.0f));
    g.setColour(colours::muted);
    g.drawText("NOTE", text.removeFromTop(14), juce::Justification::centredLeft);
    g.setFont(mono(34.0f));
    g.setColour(colours::yellow);
    g.drawText(noteName(note), text.removeFromTop(40), juce::Justification::centredLeft);
    g.setFont(mono(11.0f));
    g.setColour(colours::muted);
    g.drawText(tr("TASTEN"), text.removeFromTop(14), juce::Justification::centredLeft);
    if (keys.empty())
    {
        g.setFont(mono(15.0f));
        g.setColour(colours::red);
        g.drawText(tr("KEINE"), text.removeFromTop(20), juce::Justification::centredLeft);
        return;
    }
    juce::StringArray names;
    for (const int key : keys)
        names.add(noteName(key));
    g.setFont(mono(13.0f));
    g.setColour(colours::cyan);
    g.drawFittedText(names.joinIntoString(" "), text, juce::Justification::topLeft, 3);
}

NotesSection::NotesSection(ArpProcessor& p)
    : Section(p.state, "Arpeggio",
              { { { "mode", "Modus" }, { "order", "Reihenfolge" } }, { { "octaves", "Oktaven" }, { "latch", "Halten" } } },
              {}, { kNotesDisplay, 0 }),
      view(p)
{
    addAndMakeVisible(view);
}

void NotesSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

StepGrid::StepGrid(ArpProcessor& p) : processor(p) { startTimerHz(30); }

StepGrid::~StepGrid()
{
    for (auto* touchedParameter : touched)
        touchedParameter->endChangeGesture();
}

juce::RangedAudioParameter* StepGrid::parameter(int step, const char* field) const
{
    return processor.state.getParameter(ArpProcessor::stepId(step, field));
}

float StepGrid::value(int step, const char* field) const
{
    auto* p = parameter(step, field);
    return p->convertFrom0to1(p->getValue());
}

void StepGrid::set(int step, const char* field, float plainValue)
{
    if (! juce::isPositiveAndBelow(step, kSteps))
        return;
    auto* p = parameter(step, field);
    if (touched.insert(p).second)
        p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    repaint();
}

juce::Rectangle<int> StepGrid::laneArea(Lane lane) const
{
    auto area = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitle + 2);
    area.removeFromTop(kNumberLane);
    const std::array<std::pair<Lane, int>, 4> lanes {
        { { Lane::on, kOnLane }, { Lane::note, kNoteLane }, { Lane::velocity, kVelocityLane }, { Lane::tie, kTieLane } }
    };
    for (const auto& [which, height] : lanes)
    {
        area.removeFromTop(kLaneGap);
        const auto here = area.removeFromTop(height);
        if (which == lane)
            return here;
    }
    return {};
}

juce::Rectangle<int> StepGrid::cell(Lane lane, int step) const
{
    const auto area = lane == Lane::none ? getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitle + 2).withHeight(kNumberLane)
                                         : laneArea(lane);
    const float width = (float) (area.getWidth() - kLabels) / (float) kSteps;
    const int left = area.getX() + kLabels + juce::roundToInt(width * (float) step);
    const int right = area.getX() + kLabels + juce::roundToInt(width * (float) (step + 1));
    return { left + 2, area.getY(), right - left - 4, area.getHeight() };
}

int StepGrid::stepAt(int x) const
{
    const auto area = getLocalBounds().reduced(kPad, 0);
    const float width = (float) (area.getWidth() - kLabels) / (float) kSteps;
    const int step = (int) std::floor((float) (x - area.getX() - kLabels) / width);
    return x < area.getX() + kLabels ? -1 : std::clamp(step, 0, kSteps - 1);
}

StepGrid::Lane StepGrid::laneAt(int y) const
{
    for (const auto lane : { Lane::on, Lane::note, Lane::velocity, Lane::tie })
        if (const auto area = laneArea(lane); y >= area.getY() - kLaneGap / 2 && y < area.getBottom() + kLaneGap / 2)
            return lane;
    return Lane::none;
}

void StepGrid::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds(), "Schritte");
    const int length = juce::roundToInt(processor.state.getRawParameterValue("length")->load());
    const int playing = processor.playingStep.load();

    // The lanes' names on the left.
    g.setFont(mono(10.0f));
    g.setColour(colours::muted);
    const std::array<std::pair<Lane, const char*>, 4> names {
        { { Lane::on, "AN" }, { Lane::note, "TONHÖHE" }, { Lane::velocity, "ANSCHLAG" }, { Lane::tie, "BINDEN" } }
    };
    for (const auto& [lane, name] : names)
        g.drawText(tr(name), laneArea(lane).withWidth(kLabels - 6), juce::Justification::centredLeft);

    for (int s = 0; s < kSteps; ++s)
    {
        const bool inPattern = s < length;
        const float dim = inPattern ? 1.0f : 0.3f;
        const bool on = value(s, "On") > 0.5f;
        const bool tie = value(s, "Tie") > 0.5f;

        auto number = cell(Lane::none, s);
        g.setFont(mono(11.0f));
        g.setColour((s == playing ? colours::yellow : (s % 4 == 0 ? colours::text : colours::muted)).withMultipliedAlpha(dim));
        g.drawText(juce::String(s + 1), number, juce::Justification::centred);

        // On: a lit block, red where the step plays.
        const auto box = cell(Lane::on, s).toFloat();
        g.setColour((on ? (s == playing ? colours::red : colours::yellow) : colours::border).withMultipliedAlpha(dim));
        g.fillRect(box);

        // The note: a bar up or down from the middle line, its semitones written in it.
        const auto noteBox = cell(Lane::note, s).toFloat();
        g.setColour(colours::screen);
        g.fillRect(noteBox);
        const float note = value(s, "Note");
        const float middle = noteBox.getCentreY();
        const float reach = note / 24.0f * (noteBox.getHeight() / 2.0f - 2.0f);
        g.setColour(colours::cyan.withMultipliedAlpha(dim * (on ? 1.0f : 0.4f)));
        g.fillRect(juce::Rectangle<float>(noteBox.getX() + 2.0f, std::min(middle, middle - reach),
                                          noteBox.getWidth() - 4.0f, std::max(1.0f, std::abs(reach))));
        g.setColour(colours::border);
        g.drawHorizontalLine((int) middle, noteBox.getX(), noteBox.getRight());
        g.setColour(colours::text.withMultipliedAlpha(dim));
        g.setFont(mono(11.0f));
        const int semitones = juce::roundToInt(note);
        g.drawText(semitones > 0 ? "+" + juce::String(semitones) : juce::String(semitones),
                   noteBox.toNearestInt().withTrimmedTop(semitones > 0 ? (int) noteBox.getHeight() / 2 : 0)
                       .withHeight((int) noteBox.getHeight() / 2),
                   juce::Justification::centred);

        // The velocity: a bar from the bottom.
        const auto velocityBox = cell(Lane::velocity, s).toFloat();
        g.setColour(colours::screen);
        g.fillRect(velocityBox);
        const float velocity = value(s, "Velocity");
        g.setColour(colours::yellow.withMultipliedAlpha(dim * (on ? 0.85f : 0.3f)));
        g.fillRect(velocityBox.withTrimmedTop(velocityBox.getHeight() * (1.0f - velocity)));

        // A tie: a bracket reaching back to the step before.
        const auto tieBox = cell(Lane::tie, s).toFloat();
        g.setColour(colours::border.withMultipliedAlpha(dim));
        g.drawRect(tieBox, 1.0f);
        if (tie)
        {
            g.setColour(colours::cyan.withMultipliedAlpha(dim));
            g.fillRect(tieBox.reduced(3.0f));
            g.drawLine(tieBox.getX() - 4.0f, tieBox.getCentreY(), tieBox.getX() + 3.0f, tieBox.getCentreY(), 2.0f);
        }

        if (s == playing)
        {
            g.setColour(colours::yellow.withAlpha(0.7f));
            g.drawRect(cell(Lane::on, s).getUnion(cell(Lane::tie, s)).expanded(1), 1);
        }
    }
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    dragLane = laneAt(e.y);
    dragStep = stepAt(e.x);
    if (dragStep < 0)
        return;
    switch (dragLane)
    {
        case Lane::on:
            paintValue = value(dragStep, "On") > 0.5f ? 0.0f : 1.0f;
            set(dragStep, "On", paintValue);
            break;
        case Lane::tie:
            paintValue = value(dragStep, "Tie") > 0.5f ? 0.0f : 1.0f;
            set(dragStep, "Tie", paintValue);
            break;
        case Lane::note:
            dragStart = value(dragStep, "Note");
            break;
        case Lane::velocity:
            mouseDrag(e);
            break;
        case Lane::none: break;
    }
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    const int step = stepAt(e.x);
    switch (dragLane)
    {
        // Switches paint the first click's state across the steps the drag passes.
        case Lane::on:
            set(step, "On", paintValue);
            break;
        case Lane::tie:
            set(step, "Tie", paintValue);
            break;
        case Lane::note:
            set(dragStep, "Note",
                std::clamp(std::round(dragStart - (float) e.getDistanceFromDragStartY() / kNoteDrag), -24.0f, 24.0f));
            break;
        case Lane::velocity:
        {
            const auto area = laneArea(Lane::velocity);
            set(step, "Velocity", std::clamp(1.0f - (float) (e.y - area.getY()) / (float) area.getHeight(), 0.0f, 1.0f));
            break;
        }
        case Lane::none: break;
    }
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    for (auto* p : touched)
        p->endChangeGesture();
    touched.clear();
    dragLane = Lane::none;
}

void StepGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (laneAt(e.y) == Lane::note && stepAt(e.x) >= 0)
    {
        set(stepAt(e.x), "Note", 0.0f);
        mouseUp(e);
    }
}

void StepGrid::timerCallback()
{
    // Repaints when a value (host automation, a preset) or the playing step changed.
    std::size_t hash = (std::size_t) (processor.playingStep.load() + 1);
    hash = hash * 31 + (std::size_t) juce::roundToInt(processor.state.getRawParameterValue("length")->load());
    for (int s = 0; s < kSteps; ++s)
        for (const auto* field : { "On", "Note", "Velocity", "Tie" })
            hash = hash * 31 + (std::size_t) juce::roundToInt(parameter(s, field)->getValue() * 1000.0f);
    if (hash != shown)
    {
        shown = hash;
        repaint();
    }
}

ArpEditor::ArpEditor(ArpProcessor& p)
    : AudioProcessorEditor(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kArpCategories)),
      notes(p),
      timing(p.state, "Takt", { { { "division", "Notenwert" }, { "swing", "Swing" } }, { { "gate", utf8("Notenlänge") }, { "length", utf8("Länge") } } }),
      velocity(p.state, "Anschlag", { { { "velocitySource", "Quelle" } }, { { "fixedVelocity", "Fest" }, { "transpose", "Transp." } } }),
      grid(p)
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &notes, &timing, &velocity, &grid })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + notes.preferredHeight() + kGap + StepGrid::kHeight);
}

ArpEditor::~ArpEditor() { setLookAndFeel(nullptr); }

void ArpEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK ARP");
}

void ArpEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(240));
    layoutRow(area, { &notes, &timing, &velocity });
    grid.setBounds(area.removeFromTop(StepGrid::kHeight));
}
} // namespace tonwerkarp
