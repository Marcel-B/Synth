#pragma once

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
void paintTitle(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

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
};

/** A knob, menu or switch with its name above, tied to a parameter; the knob shows the parameter's own text. */
class Control : public juce::Component
{
public:
    /** `slots` 0 takes the control's own width: two for a menu, so its entries fit, one otherwise. */
    Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label,
            int slots = 0);
    void resized() override;
    int slots() const { return width; }

private:
    juce::Label name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    int width;
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
            : Item(parameterId, utf8(name), width, switchId, shownWhenOn)
        {
        }
        Item(const juce::String& parameterId, const juce::String& name, int width = 0,
             const juce::String& switchId = {}, bool shownWhenOn = false)
            : id(parameterId), label(name), slots(width), when(switchId), whenOn(shownWhenOn)
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
class ScopeView : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView(const ScopeBuffer& source, const ScopeBuffer* behind = nullptr, std::function<int()> lag = {});
    void setCaption(const juce::String& text) { caption = text; }
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
    std::pair<juce::String, juce::String> legend;
    std::array<float, 2048> samples {};
    std::array<float, 2048> behindSamples {};
};
} // namespace tonwerkui
