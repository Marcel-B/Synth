#include "RigEditor.h"
#include "Language.h"

#include "RigCatalogue.h"

namespace tonwerkrig
{
using namespace tonwerkui;

namespace
{
constexpr int kChain = 46;
constexpr int kInstrumentWidth = 170;
constexpr int kSlotWidth = 128;
constexpr int kArrow = 18;
/** What an empty slot shows below the chain. */
constexpr int kEmptyWidth = 720, kEmptyHeight = 160;

/** A plugin's name without the family name, so it fits its box. */
juce::String shortName(const juce::String& name) { return name.startsWith("Tonwerk ") ? name.substring(8) : name; }

juce::String kindName(const std::vector<Kind>& kinds, const juce::String& id)
{
    const auto* found = find(kinds, id);
    return found != nullptr ? shortName(found->name) : juce::String();
}
} // namespace

void ChainButton::show(const juce::String& newTitle, const juce::String& newDetail, bool isInstrument, bool on, bool empty)
{
    title = newTitle;
    detail = newDetail;
    instrument = isInstrument;
    enabled = on;
    isEmpty = empty;
    repaint();
}

void ChainButton::setSelected(bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void ChainButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto accent = instrument ? colours::cyan : colours::yellow;
    g.setColour(down ? colours::border : colours::panel);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(selected ? accent : (highlighted ? colours::muted : colours::border));
    g.drawRoundedRectangle(area, 4.0f, selected ? 2.0f : 1.0f);

    auto inside = area.reduced(8.0f, 4.0f);
    if (isEmpty)
    {
        g.setColour(colours::muted);
        g.setFont(mono(18.0f));
        g.drawText("+", inside, juce::Justification::centred);
        return;
    }
    const auto colour = enabled ? (selected ? accent : colours::text) : colours::muted;
    g.setColour(colour);
    g.setFont(mono(12.0f));
    if (detail.isEmpty())
        g.drawFittedText(title.toUpperCase(), inside.toNearestInt(), juce::Justification::centred, 1);
    else
    {
        g.drawFittedText(title.toUpperCase(), inside.removeFromTop(inside.getHeight() * 0.55f).toNearestInt(),
                         juce::Justification::centredBottom, 1);
        g.setFont(mono(10.0f));
        g.setColour(enabled ? colours::cyan : colours::muted);
        g.drawFittedText(detail, inside.toNearestInt(), juce::Justification::centredTop, 1);
    }
    if (! enabled)
    {
        g.setFont(mono(9.0f));
        g.drawText(tonwerkui::tr("AUS"), area.reduced(5.0f, 3.0f), juce::Justification::topRight);
    }
}

RigEditor::RigEditor(RigProcessor& p) : AudioProcessorEditor(p), rig(p)
{
    setLookAndFeel(&lookAndFeel);

    // The factory rigs grouped as the other plugins group their sounds; the ids are program numbers + 1.
    const auto& rigs = factoryRigs();
    for (const auto* category : kRigCategories)
    {
        presets.addSectionHeading(tonwerkui::tr(category));
        for (int i = 0; i < (int) rigs.size(); ++i)
            if (juce::String::fromUTF8(rigs[(std::size_t) i].category) == utf8(category))
                presets.addItem(utf8(rigs[(std::size_t) i].name), i + 1);
    }
    presets.setTextWhenNothingSelected("Rig");
    presets.onChange = [this] {
        if (const int id = presets.getSelectedId(); id > 0)
            rig.setCurrentProgram(id - 1);
    };
    addAndMakeVisible(presets);

    instrumentButton.onClick = [this] { clicked(-1, juce::ModifierKeys::getCurrentModifiers().isPopupMenu()); };
    addAndMakeVisible(instrumentButton);
    for (int i = 0; i < kSlots; ++i)
    {
        slotButtons[(std::size_t) i].onClick = [this, i] {
            clicked(i, juce::ModifierKeys::getCurrentModifiers().isPopupMenu());
        };
        addAndMakeVisible(slotButtons[(std::size_t) i]);
    }

    emptyHint.setText(tonwerkui::tr("Leerer Platz. Tippe noch einmal darauf, um einen Effekt zu wählen."),
                      juce::dontSendNotification);
    emptyHint.setJustificationType(juce::Justification::centred);
    emptyHint.setColour(juce::Label::textColourId, colours::muted);
    emptyHint.setFont(mono(13.0f));
    addChildComponent(emptyHint);

    rig.setListener(this);
    refresh();
    showInner();
}

RigEditor::~RigEditor()
{
    rig.setListener(nullptr);
    rigWillChange();
    setLookAndFeel(nullptr);
}

void RigEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK RIG");

    // Arrows between the places of the chain, the way the sound flows.
    g.setColour(colours::muted);
    auto arrowAfter = [&](const juce::Component& c) {
        const float x = (float) c.getRight() + kArrow * 0.5f, y = (float) c.getBounds().getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath(x - 5.0f, y);
        arrow.lineTo(x + 4.0f, y);
        arrow.startNewSubPath(x, y - 4.0f);
        arrow.lineTo(x + 4.0f, y);
        arrow.lineTo(x, y + 4.0f);
        g.strokePath(arrow, juce::PathStrokeType(1.5f));
    };
    arrowAfter(instrumentButton);
    for (int i = 0; i + 1 < kSlots; ++i)
        arrowAfter(slotButtons[(std::size_t) i]);
}

void RigEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin, 0);
    auto header = area.removeFromTop(kMargin + kHeader).withTrimmedTop(kMargin);
    presets.setBounds(header.reduced(0, 7).removeFromRight(240));

    auto chain = area.removeFromTop(kChain);
    instrumentButton.setBounds(chain.removeFromLeft(kInstrumentWidth));
    for (auto& button : slotButtons)
    {
        chain.removeFromLeft(kArrow);
        button.setBounds(chain.removeFromLeft(kSlotWidth));
    }
    area.removeFromTop(kGap);
    area = getLocalBounds().withTrimmedTop(area.getY());
    if (inner != nullptr)
        inner->setTopLeftPosition(area.getX() + std::max(0, (area.getWidth() - inner->getWidth()) / 2), area.getY());
    emptyHint.setBounds(area.withHeight(kEmptyHeight));
}

void RigEditor::fitSize()
{
    const int chainWidth = 2 * kMargin + kInstrumentWidth + kSlots * (kArrow + kSlotWidth);
    const int innerWidth = inner != nullptr ? inner->getWidth() : kEmptyWidth;
    const int innerHeight = inner != nullptr ? inner->getHeight() : kEmptyHeight;
    setSize(std::max(chainWidth, innerWidth), kMargin + kHeader + kChain + kGap + innerHeight);
    resized();
}

void RigEditor::componentMovedOrResized(juce::Component&, bool, bool wasResized)
{
    if (wasResized)
        fitSize();
}

void RigEditor::rigWillChange()
{
    // The plugin inside may be replaced: let go of its editor first.
    if (inner != nullptr)
    {
        inner->removeComponentListener(this);
        removeChildComponent(inner.get());
        // What a host does before it deletes an editor, so the plugin forgets it.
        inner->processor.editorBeingDeleted(inner.get());
        inner.reset();
    }
}

void RigEditor::rigChanged()
{
    refresh();
    showInner();
}

void RigEditor::refresh()
{
    const int program = rig.getCurrentProgram();
    if (rig.presetName() == rig.getProgramName(program))
        presets.setSelectedId(program + 1, juce::dontSendNotification);
    else
        presets.setText(rig.presetName(), juce::dontSendNotification);

    instrumentButton.show(kindName(instruments(), rig.instrumentKind()), {}, true, true, false);
    instrumentButton.setSelected(selected == -1);
    for (int i = 0; i < kSlots; ++i)
    {
        auto& button = slotButtons[(std::size_t) i];
        const bool empty = rig.effect(i) == nullptr;
        const auto source = rig.sourceOf(i);
        button.show(kindName(effects(), rig.effectKind(i)), source > 0 ? RigProcessor::sourceName(source) : juce::String(),
                    false, rig.isOn(i), empty);
        button.setSelected(selected == i);
    }
}

void RigEditor::showInner()
{
    rigWillChange();
    auto* chosen = selected < 0 ? rig.instrument() : rig.effect(selected);
    if (chosen != nullptr && chosen->hasEditor())
    {
        inner.reset(chosen->createEditorIfNeeded());
        if (inner != nullptr)
        {
            addAndMakeVisible(*inner);
            inner->addComponentListener(this);
        }
    }
    emptyHint.setVisible(inner == nullptr);
    fitSize();
    repaint();
}

void RigEditor::select(int place)
{
    selected = place;
    refresh();
    showInner();
}

void RigEditor::clicked(int place, bool menu)
{
    const bool empty = place >= 0 && rig.effect(place) == nullptr;
    if (menu || place == selected || empty)
    {
        if (place != selected)
            select(place);
        if (place < 0)
            showInstrumentMenu();
        else
            showSlotMenu(place);
        return;
    }
    select(place);
}

void RigEditor::showInstrumentMenu()
{
    juce::PopupMenu menu;
    const auto& kinds = instruments();
    for (int i = 0; i < (int) kinds.size(); ++i)
        menu.addItem(i + 1, kinds[(std::size_t) i].name, true, rig.instrumentKind() == kinds[(std::size_t) i].id);
    juce::Component::SafePointer<RigEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&instrumentButton), [safe](int result) {
        if (safe == nullptr || result <= 0)
            return;
        safe->rig.setInstrument(instruments()[(std::size_t) (result - 1)].id);
    });
}

void RigEditor::showSlotMenu(int slot)
{
    enum : int
    {
        kClear = 1,
        kOn = 2,
        kEarlier = 3,
        kLater = 4,
        kEffect = 100,
        kSource = 200,
    };
    juce::PopupMenu menu;
    const auto& kinds = effects();
    const bool filled = rig.effect(slot) != nullptr;
    for (int i = 0; i < (int) kinds.size(); ++i)
        menu.addItem(kEffect + i, kinds[(std::size_t) i].name, true, rig.effectKind(slot) == kinds[(std::size_t) i].id);
    if (filled)
    {
        menu.addSeparator();
        if (rig.hasTracks())
        {
            juce::PopupMenu sources;
            for (int s = 0; s <= kTrackSources; ++s)
                sources.addItem(kSource + s, RigProcessor::sourceName(s), true, rig.sourceOf(slot) == s);
            menu.addSubMenu(tonwerkui::tr("Quelle"), sources);
        }
        menu.addItem(kOn, "Eingeschaltet", true, rig.isOn(slot));
        menu.addItem(kEarlier, "Nach links", slot > 0);
        menu.addItem(kLater, "Nach rechts", slot + 1 < kSlots);
        menu.addItem(kClear, "Entfernen");
    }
    juce::Component::SafePointer<RigEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slotButtons[(std::size_t) slot]),
                       [safe, slot](int result) {
                           if (safe == nullptr || result <= 0)
                               return;
                           auto& r = safe->rig;
                           if (result >= kSource)
                               r.setSource(slot, result - kSource);
                           else if (result >= kEffect)
                               r.setEffect(slot, effects()[(std::size_t) (result - kEffect)].id);
                           else if (result == kClear)
                               r.setEffect(slot, {});
                           else if (result == kOn)
                               r.setOn(slot, ! r.isOn(slot));
                           else if (result == kEarlier || result == kLater)
                           {
                               const int to = slot + (result == kEarlier ? -1 : 1);
                               r.moveEffect(slot, to - slot);
                               safe->select(to);
                           }
                       });
}
} // namespace tonwerkrig
