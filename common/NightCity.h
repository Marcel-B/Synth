#pragma once

#include "Language.h"
#include "Platform.h"
#include "ScopeBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * The look Tonwerk Wavetable started and the other Tonwerk plugins share: Night City, as in Chrome Glitch. Near black,
 * the yellow, cyan and red of Cyberpunk 2077's interface, panels with a cut corner, screens with scan lines.
 */
namespace tonwerkui
{
namespace colours
{
inline const juce::Colour background { 0xff0a0b10 };
inline const juce::Colour panel { 0xff14151c };
inline const juce::Colour screen { 0xff04070a };
inline const juce::Colour border { 0xff2a2d3a };
inline const juce::Colour text { 0xffe8e6d9 };
inline const juce::Colour muted { 0xff8d8f9c };
inline const juce::Colour yellow { 0xfffcee0a };
inline const juce::Colour cyan { 0xff00f0ff };
inline const juce::Colour red { 0xffff003c };
} // namespace colours

/** One control's width. */
inline constexpr int kSlot = 60;
/** One row of controls: name, knob, value. */
inline constexpr int kRow = 84;
inline constexpr int kTitle = 22;
inline constexpr int kPad = 6;
inline constexpr int kGap = 6;
inline constexpr int kMargin = 8;
inline constexpr int kHeader = 40;

/** juce::String reads a plain `const char*` as ASCII; text with umlauts comes through here. */
inline juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

juce::Font mono(float size);

/** A panel with its top right corner cut off and a thin cyan edge, the shape of the game's HUD boxes. */
void paintPanel(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

/** A dark screen with rounded corners and scan lines; returns its outline, to clip what is drawn on it. */
juce::Path paintScreen(juce::Graphics&, juce::Rectangle<float> area);

/** The plugin's name in the header, with the game's colour split: red and cyan ghosts either side of the yellow. */
/**
 * The picture of a waveform by its name in a menu, German or English, in a unit square (y down); empty for a name
 * that is none. Menus whose entries all have one show pictures (`Control`).
 */
juce::Path waveIcon(const juce::String& name);

void paintTitle(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

/** An editor's header height: `kHeader`, or 0 in the app on the iPad, whose bar has name and sounds (`editorsHaveHeader`). */
inline int headerHeight() { return headerHeight(kHeader); }
/** A menu's height and its list's font where a finger chooses (`touchMenus`), before the editor's scale. */
inline constexpr int kTouchMenu = 36;
inline constexpr float kTouchPopupFont = 17.0f;

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position, float minPosition,
                          float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    /** The size of the menus' entries; larger where a finger chooses them. */
    void setPopupFontSize(float size) { popupFontSize = size; }
    juce::Component* getParentComponentForMenuOptions(const juce::PopupMenu::Options&) override;
    /** A switch is a lit key, dark when off (Marcel, 2026-11-19: a tick box looked like office software). */
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    /** A menu of waveforms (`Control` marks it) shows the chosen wave drawn instead of its name. */
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    /** A menu's list has rows as high as its box, so a higher box brings a list a finger can choose from. */
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

private:
    float popupFontSize = touchMenus() ? kTouchPopupFont : 14.0f;
};

/** A knob, menu or switch with its name above, tied to a parameter; the knob shows the parameter's own text. */
class Control : public juce::Component, private juce::ChangeListener
{
public:
    /** `slots` 0 takes the control's own width: two for a menu, so its entries fit, one otherwise. */
    Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label,
            int slots = 0);
    ~Control() override;
    void resized() override;
    int slots() const { return width; }
    /** A menu's height; the compact editor makes it a finger high. */
    void setMenuHeight(int height)
    {
        menuHeight = height;
        resized();
    }

    /**
     * In a plugin with MIDI learn (`MidiLearn`), a right click on a knob offers to learn a controller or forget it;
     * the name shows the controller (“Cutoff · CC20”) or that it is listening.
     */
    void showMidiMenu();
    juce::String shownName() const { return name.getText(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { showMapping(); }
    void showMapping();

    juce::AudioProcessorValueTreeState& state;
    juce::String id, label;
    juce::Label name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    int width;
    int menuHeight = touchMenus() ? kTouchMenu : 24;
};

/** Room for a display in a section: `inset` pixels on the left or `top` pixels above the controls. */
struct DisplayRoom
{
    int inset = 0;
    int top = 0;
};

/**
 * A titled panel of controls in rows, with an optional power switch in its title bar and room for a display, either
 * on the left (`inset` pixels wide, growing when the panel is wider than its controls) or above the rows (`top`
 * pixels high).
 */
class Section : public juce::Component
{
public:
    struct Item
    {
        Item(const juce::String& parameterId, const char* name, int width = 0, const juce::String& switchId = {},
             bool shownWhenOn = false)
            : Item(parameterId, tr(name), width, switchId, shownWhenOn)
        {
        }
        Item(const juce::String& parameterId, const juce::String& name, int width = 0,
             const juce::String& switchId = {}, bool shownWhenOn = false)
            : id(parameterId), label(tr(name)), slots(width), when(switchId), whenOn(shownWhenOn)
        {
        }

        juce::String id;
        juce::String label;
        int slots;
        /**
         * A switch parameter this control depends on: it is shown only while the switch is `whenOn`. Two neighbours
         * with the same switch, one for each state, take turns in one place (a rate knob and its note value).
         */
        juce::String when;
        bool whenOn;
    };
    using Row = std::vector<Item>;

    using Display = DisplayRoom;

    Section(juce::AudioProcessorValueTreeState& state, const juce::String& title, std::vector<Row> rows,
            const juce::String& powerId = {}, Display display = {});
    /** The width the rows (and a display on the left) need. */
    int preferredWidth() const;
    int preferredHeight() const;
    bool hasDisplay() const { return display.inset > 0 || display.top > 0; }
    /** Points of air around each row of controls, for an editor with height to spare. */
    void setAir(int points)
    {
        air = points;
        resized();
    }
    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    /** Where a display goes. */
    juce::Rectangle<int> displayArea() const;

private:
    /** Each control's place in its row, in slots from the left, and its width; a pair taking turns shares one. */
    std::vector<std::pair<int, int>> places(std::size_t row) const;
    int rowSlots(std::size_t row) const;

    juce::String title;
    Display display;
    int air = 0;
    std::vector<Row> items;
    std::vector<std::vector<std::unique_ptr<Control>>> controls;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> switches;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
};

/**
 * Lays out sections in a row across `area` (taking the row's height off its top): the space left over goes to the
 * sections with a display, or to all of them when none has one, so the row ends flush on the right.
 */
void layoutRow(juce::Rectangle<int>& area, std::initializer_list<Section*> sections);

/**
 * An oscilloscope of what the plugin plays, like Tonwerk's on its instruments page: triggered on a rising zero
 * crossing so a held note stands still, quiet sounds scaled up to be seen. An optional second buffer is drawn faint
 * behind, `lag` samples back (an effect's input under its output).
 */
/**
 * A processor that has something to say beside its scope's name: the note that sounds, an accent, a slide. Its own
 * editor shows it, and so does the app's scope where that stands in for the editor's (`editorsHaveScope`).
 */
struct ScopeNotes
{
    virtual ~ScopeNotes() = default;
    /** What follows the caption, starting with its separator, or nothing while no note sounds. */
    virtual juce::String scopeNote() const = 0;
};

class ScopeView : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView(const ScopeBuffer& source, const ScopeBuffer* behind = nullptr, std::function<int()> lag = {});
    void setCaption(const juce::String& text) { caption = text; }
    /** A caption that changes while it plays, asked with every frame; it replaces the fixed one. */
    void setCaptionSource(std::function<juce::String()> text) { captionSource = std::move(text); }
    /** Names for the two traces, in their colours in the top right corner. */
    void setLegend(const juce::String& mainName, const juce::String& behindName)
    {
        legend = { mainName, behindName };
    }
    void paint(juce::Graphics&) override;
    /** Takes the newest samples and draws them; the timer does this while the view is on screen. */
    void refresh();

private:
    void timerCallback() override;

    const ScopeBuffer& source;
    const ScopeBuffer* behind;
    std::function<int()> lag;
    juce::String caption;
    std::function<juce::String()> captionSource;
    std::pair<juce::String, juce::String> legend;
    std::array<float, 2048> samples {};
    std::array<float, 2048> behindSamples {};
};
} // namespace tonwerkui
