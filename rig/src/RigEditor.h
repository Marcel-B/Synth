#pragma once

#include "NightCity.h"
#include "RigProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tonwerkrig
{
/** A place in the chain: the instrument or an effect slot, as a box with its name and, for a track, its source. */
class ChainButton : public juce::Button
{
public:
    ChainButton() : juce::Button({}) {}
    void show(const juce::String& title, const juce::String& detail, bool isInstrument, bool on, bool empty);
    void setSelected(bool selected);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String title, detail;
    bool instrument = false, enabled = true, isEmpty = true, selected = false;
};

/**
 * Tonwerk Rig's editor: the rig's name and factory rigs above, the chain (instrument, then the effect slots) below it,
 * and under that the editor of the place chosen, the plugin's own. A tap chooses a place; a tap on the chosen one (or on
 * an empty slot, or a right click) opens its menu: the instrument or effect, the source, on/off, moving it.
 */
class RigEditor : public juce::AudioProcessorEditor, private RigProcessor::Listener, private juce::ComponentListener
{
public:
    explicit RigEditor(RigProcessor&);
    ~RigEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void rigWillChange() override;
    void rigChanged() override;
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override;

    void select(int place);
    void clicked(int place, bool menu);
    void showInstrumentMenu();
    void showSlotMenu(int slot);
    void refresh();
    void showInner();
    void fitSize();

    RigProcessor& rig;
    tonwerkui::LookAndFeel lookAndFeel;
    juce::ComboBox presets;
    ChainButton instrumentButton;
    std::array<ChainButton, kSlots> slotButtons;
    std::unique_ptr<juce::AudioProcessorEditor> inner;
    juce::Label emptyHint;
    /** -1 the instrument, 0 to kSlots - 1 an effect slot. */
    int selected = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RigEditor)
};
} // namespace tonwerkrig
