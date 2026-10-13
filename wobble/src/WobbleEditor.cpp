#include "WobbleEditor.h"

#include "PresetCategories.h"
#include "WobblePresets.h"

namespace tonwerkwobble
{
using namespace tonwerkui;

namespace
{
/** The curve's room in a lane's panel, at least; a wider window gives it more. */
constexpr int kCurveWidth = 330;
constexpr float kGrab = 7.0f;
constexpr int kGrid = 16;

enum MenuAction
{
    invert = 100,
    reverse,
    twice,
    copyOther,
};
} // namespace

CurveView::CurveView(WobbleProcessor& p, Lane which) : processor(p), lane(which), curve(p.curve(which))
{
    form.setTooltip(utf8("Grundform wählen, umkehren oder spiegeln"));
    form.onClick = [this] { showMenu(); };
    addAndMakeVisible(form);
    setRepaintsOnMouseActivity(false);
    startTimerHz(30);
}

juce::Colour CurveView::colour() const { return lane == Lane::filter ? colours::yellow : colours::cyan; }

juce::Rectangle<float> CurveView::plot() const { return getLocalBounds().toFloat().reduced(10.0f, 22.0f); }

juce::Point<float> CurveView::toScreen(float x, float y) const
{
    const auto area = plot();
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

juce::Point<float> CurveView::fromScreen(juce::Point<float> at) const
{
    const auto area = plot();
    return { std::clamp((at.x - area.getX()) / area.getWidth(), 0.0f, 1.0f),
             std::clamp((area.getBottom() - at.y) / area.getHeight(), 0.0f, 1.0f) };
}

juce::Point<float> CurveView::handlePosition(std::size_t segment) const
{
    const auto& a = curve.points[segment];
    const auto& b = curve.points[segment + 1];
    return toScreen((a.x + b.x) / 2.0f, curve.segmentValue(segment, 0.5f));
}

int CurveView::cornerAt(juce::Point<float> at) const
{
    for (int i = (int) curve.points.size(); i-- > 0;)
    {
        const auto& p = curve.points[(std::size_t) i];
        if (toScreen(p.x, p.y).getDistanceFrom(at) <= kGrab)
            return i;
    }
    return -1;
}

int CurveView::handleAt(juce::Point<float> at) const
{
    for (std::size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto& a = curve.points[i];
        const auto& b = curve.points[i + 1];
        // A step or a level line has nothing to bend.
        if (toScreen(b.x, 0).x - toScreen(a.x, 0).x < 3.0f * kGrab || std::abs(b.y - a.y) < 0.02f)
            continue;
        if (handlePosition(i).getDistanceFrom(at) <= kGrab)
            return (int) i;
    }
    return -1;
}

void CurveView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto glass = paintScreen(g, bounds);
    g.saveState();
    g.reduceClipRegion(glass);
    const auto area = plot();
    const bool on = processor.state.getRawParameterValue(lane == Lane::filter ? "filterOn" : "volumeOn")->load() > 0.5f;
    const auto ink = on ? colour() : colours::muted;

    // Sixteenths of the cycle, quarters brighter: where a click snaps to.
    for (int i = 0; i <= kGrid; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / kGrid;
        g.setColour(colours::cyan.withAlpha(i % 4 == 0 ? 0.18f : 0.07f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    }
    g.setColour(colours::cyan.withAlpha(0.1f));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());

    juce::Path line;
    line.startNewSubPath(toScreen(curve.points.front().x, curve.points.front().y));
    for (std::size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto& b = curve.points[i + 1];
        if (std::abs(curve.points[i].bend) > 1.0e-6f && b.x > curve.points[i].x)
            for (int s = 1; s < 24; ++s)
            {
                const float t = (float) s / 24.0f;
                line.lineTo(toScreen(curve.points[i].x + (b.x - curve.points[i].x) * t, curve.segmentValue(i, t)));
            }
        line.lineTo(toScreen(b.x, b.y));
    }
    juce::Path fill(line);
    fill.lineTo(area.getRight(), area.getBottom());
    fill.lineTo(area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(ink.withAlpha(0.25f), area.getX(), area.getY(), ink.withAlpha(0.02f),
                                           area.getX(), area.getBottom(), false));
    g.fillPath(fill);
    g.setColour(ink);
    g.strokePath(line, juce::PathStrokeType(2.0f));

    for (std::size_t i = 0; i < curve.points.size(); ++i)
    {
        const auto& p = curve.points[i];
        const auto at = toScreen(p.x, p.y);
        const bool lit = (int) i == (hoveredHandle ? -2 : hovered) || ((int) i == dragging && ! bending);
        g.setColour(lit ? colours::text : ink);
        g.fillRect(juce::Rectangle<float>(7.0f, 7.0f).withCentre(at));
    }
    for (std::size_t i = 0; i + 1 < curve.points.size(); ++i)
    {
        const auto& a = curve.points[i];
        const auto& b = curve.points[i + 1];
        if (toScreen(b.x, 0).x - toScreen(a.x, 0).x < 3.0f * kGrab || std::abs(b.y - a.y) < 0.02f)
            continue;
        const bool lit = (hoveredHandle && (int) i == hovered) || (bending && (int) i == dragging);
        g.setColour(lit ? colours::text : ink.withAlpha(0.6f));
        g.drawEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(handlePosition(i)), 1.5f);
    }

    if (on)
    {
        const float x = area.getX() + playhead * area.getWidth();
        g.setColour(colours::red.withAlpha(0.8f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        g.setColour(colours::red);
        g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(toScreen(playhead, curve.valueAt(playhead))));
    }

    g.setFont(mono(11.0f));
    g.setColour(colours::muted);
    g.drawText(lane == Lane::filter ? "FILTER-KURVE" : "VOLUME-KURVE", getLocalBounds().reduced(10, 4).removeFromTop(14),
               juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(utf8("Klick: Punkt · Doppelklick: löschen · Kreis ziehen: biegen · Shift: frei"),
               getLocalBounds().reduced(10, 4).removeFromBottom(14), juce::Justification::centredLeft);
    g.restoreState();
}

void CurveView::resized() { form.setBounds(getWidth() - 62, 4, 54, 18); }

void CurveView::refresh()
{
    const float now = processor.phase[lane == Lane::filter ? 0 : 1].load(std::memory_order_relaxed);
    bool changed = std::abs(now - playhead) > 1.0e-4f;
    playhead = now;
    // A preset or a project may have drawn a new curve.
    if (dragging < 0 && processor.curve(lane).toString() != curve.toString())
    {
        curve = processor.curve(lane);
        changed = true;
    }
    if (changed)
        repaint();
}

void CurveView::moveCorner(int index, juce::Point<float> at, bool snap)
{
    auto& points = curve.points;
    auto value = fromScreen(at);
    if (snap)
        value.x = std::round(value.x * kGrid) / kGrid;
    const auto i = (std::size_t) index;
    if (i == 0)
        value.x = 0.0f;
    else if (i + 1 == points.size())
        value.x = 1.0f;
    else
        value.x = std::clamp(value.x, points[i - 1].x, points[i + 1].x);
    points[i].x = value.x;
    points[i].y = value.y;
}

void CurveView::mouseDown(const juce::MouseEvent& e)
{
    const auto at = e.position;
    if (const int corner = cornerAt(at); corner >= 0)
    {
        // A right click removes a corner, as the double click does; the ends stay.
        if (e.mods.isPopupMenu())
        {
            if (corner > 0 && corner + 1 < (int) curve.points.size())
            {
                curve.points.erase(curve.points.begin() + corner);
                processor.setCurve(lane, curve);
                repaint();
            }
            return;
        }
        dragging = corner;
        bending = false;
        return;
    }
    if (const int handle = handleAt(at); handle >= 0)
    {
        dragging = handle;
        bending = true;
        return;
    }
    if (e.mods.isPopupMenu() || curve.points.size() >= Curve::kMaxPoints)
        return;
    auto value = fromScreen(at);
    if (! e.mods.isShiftDown())
        value.x = std::round(value.x * kGrid) / kGrid;
    // After the last corner at or before x, so a click on a step lands after it.
    std::size_t i = 0;
    while (i + 2 < curve.points.size() && curve.points[i + 1].x <= value.x)
        ++i;
    const float bend = curve.points[i].bend;
    curve.points.insert(curve.points.begin() + (long) i + 1, { value.x, value.y, bend });
    dragging = (int) i + 1;
    bending = false;
    moveCorner(dragging, at, ! e.mods.isShiftDown());
    processor.setCurve(lane, curve);
    repaint();
}

void CurveView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    if (bending)
    {
        const auto i = (std::size_t) dragging;
        curve.points[i].bend = Curve::bendThrough(curve.points[i].y, curve.points[i + 1].y, fromScreen(e.position).y);
    }
    else
        moveCorner(dragging, e.position, ! e.mods.isShiftDown());
    processor.setCurve(lane, curve);
    repaint();
}

void CurveView::mouseUp(const juce::MouseEvent&)
{
    dragging = -1;
    bending = false;
    curve = processor.curve(lane);
    repaint();
}

void CurveView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const int corner = cornerAt(e.position); corner > 0 && corner + 1 < (int) curve.points.size())
        curve.points.erase(curve.points.begin() + corner);
    else if (const int handle = handleAt(e.position); handle >= 0)
        curve.points[(std::size_t) handle].bend = 0.0f;
    else
        return;
    processor.setCurve(lane, curve);
    repaint();
}

void CurveView::mouseMove(const juce::MouseEvent& e)
{
    int now = cornerAt(e.position);
    bool handle = false;
    if (now < 0 && (now = handleAt(e.position)) >= 0)
        handle = true;
    if (now != hovered || handle != hoveredHandle)
    {
        hovered = now;
        hoveredHandle = handle;
        setMouseCursor(now >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void CurveView::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("Grundform");
    for (std::size_t i = 0; i < kShapeNames.size(); ++i)
        menu.addItem((int) i + 1, utf8(kShapeNames[i]));
    menu.addSeparator();
    menu.addItem(invert, "Umkehren (oben/unten)");
    menu.addItem(reverse, utf8("Spiegeln (rückwärts)"));
    menu.addItem(twice, "Verdoppeln (zweimal pro Zyklus)", curve.points.size() * 2 <= Curve::kMaxPoints);
    menu.addItem(copyOther, lane == Lane::filter ? utf8("Volume-Kurve übernehmen") : utf8("Filter-Kurve übernehmen"));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&form),
                       [safe = juce::Component::SafePointer<CurveView>(this)](int id) {
                           if (safe != nullptr && id > 0)
                               safe->applyMenu(id);
                       });
}

void CurveView::applyMenu(int id)
{
    if (id >= 1 && id <= (int) kShapeNames.size())
        curve = shapeCurve((Shape) (id - 1));
    else if (id == invert)
        curve = curve.inverted();
    else if (id == reverse)
        curve = curve.reversed();
    else if (id == twice)
    {
        std::vector<CurvePoint> points;
        for (const float start : { 0.0f, 0.5f })
            for (const auto& p : curve.points)
                points.push_back({ start + p.x / 2.0f, p.y, p.bend });
        curve = Curve(points);
    }
    else if (id == copyOther)
        curve = processor.curve(lane == Lane::filter ? Lane::volume : Lane::filter);
    else
        return;
    processor.setCurve(lane, curve);
    curve = processor.curve(lane);
    repaint();
}

LaneSection::LaneSection(WobbleProcessor& p, Lane lane, const juce::String& name, std::vector<Row> rows)
    : Section(p.state, name, std::move(rows), lane == Lane::filter ? "filterOn" : "volumeOn", { kCurveWidth, 0 }),
      view(p, lane)
{
    addAndMakeVisible(view);
}

void LaneSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

WobbleEditor::WobbleEditor(WobbleProcessor& p)
    : AudioProcessorEditor(p),
      filter(p, Lane::filter, "Filter",
             { { { "filterSync", "Sync" },
                 { "filterDivision", "Takt", 0, "filterSync", true },
                 { "filterRate", "Rate", 0, "filterSync", false },
                 { "filterPhase", "Phase" },
                 { "filterType", "Typ" } },
               { { "filterLow", "Unten" },
                 { "filterHigh", "Oben" },
                 { "resonance", "Reso" },
                 { "drive", "Drive" },
                 { "filterSlope", "Flanke" } } }),
      volume(p, Lane::volume, "Volume",
             { { { "volumeSync", "Sync" },
                 { "volumeDivision", "Takt", 0, "volumeSync", true },
                 { "volumeRate", "Rate", 0, "volumeSync", false },
                 { "volumePhase", "Phase" } },
               { { "volumeDepth", "Tiefe" } } }),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kWobbleCategories)),
      output(p.state, "Ausgang", { { { "smooth", utf8("Glätten") }, { "spread", "Stereo" } },
                                   { { "mix", "Mix" }, { "output", "Pegel" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &filter, &volume, &output })
        addAndMakeVisible(*c);
    setSize(kWidth, 2 * kMargin + kHeader + filter.preferredHeight() + kGap + volume.preferredHeight());
}

WobbleEditor::~WobbleEditor() { setLookAndFeel(nullptr); }

void WobbleEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK WOBBLE");
}

void WobbleEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(240));
    layoutRow(area, { &filter });
    layoutRow(area, { &volume, &output });
}
} // namespace tonwerkwobble
