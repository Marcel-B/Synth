#pragma once

#include "Language.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>
#include <functional>

/**
 * What differs on the iPad (AUv3 and the standalone app built for iOS), kept in one place so the plugins' own code stays
 * the same everywhere: the editor's size, files the user chooses. The Mac App Store's build (TONWERK_SANDBOX) shares
 * the iPad's menus and files: its AUv3s run sandboxed in a process of their own, as on the iPad.
 */
namespace tonwerkui
{
/**
 * The language switch every editor carries in its top right corner: DE and EN, the spoken one lit. A choice holds for
 * every Tonwerk plugin (`chooseLanguage`); the frame around the editor builds it again in that language.
 */
class LanguageSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kWidth = 46;
    static constexpr int kHeight = 16;

    LanguageSwitch()
    {
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        updateTooltip();
    }

    std::function<void()> onChange;

    void paint(juce::Graphics& g) override
    {
        const auto cyan = juce::Colour(0xff00f0ff);
        const auto yellow = juce::Colour(0xfffcee0a);
        auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colour(0xff04070a).withAlpha(0.85f));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(cyan.withAlpha(isMouseOver() ? 0.7f : 0.35f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
        const bool german = language() == Language::german;
        auto left = getLocalBounds().withWidth(getWidth() / 2);
        auto right = getLocalBounds().withLeft(getWidth() / 2);
        g.setColour(german ? yellow : cyan.withAlpha(0.45f));
        g.drawText("DE", left, juce::Justification::centred);
        g.setColour(german ? cyan.withAlpha(0.45f) : yellow);
        g.drawText("EN", right, juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (! getLocalBounds().contains(event.getPosition()))
            return;
        chooseLanguage(language() == Language::german ? Language::english : Language::german);
        updateTooltip();
        repaint();
        if (onChange)
            onChange();
    }

    void updateTooltip() { setTooltip(tr("Sprache der Tonwerk-Plugins")); }
};

/**
 * Whether the editors show their header (name, sounds, language). The app's bar on the iPad has all three itself
 * (Marcel, 2026-11-19), so it clears this and every editor is lower by its header.
 */
inline bool& editorsHaveHeader()
{
    static bool shown = true;
    return shown;
}

/**
 * Held by an editor that shows other plugins' editors inside itself (Tonwerk Rig): their sounds are chosen in their
 * own headers, which the app's bar cannot replace, so while one lives the editors inside keep theirs.
 */
struct InnerHeaders
{
    InnerHeaders() { ++count(); }
    ~InnerHeaders() { --count(); }
    InnerHeaders(const InnerHeaders&) = delete;
    InnerHeaders& operator=(const InnerHeaders&) = delete;
    static int& count() { static int held = 0; return held; }
};

/** The height an editor gives its header: `full`, or nothing where the app's bar stands in for it. */
inline int headerHeight(int full) { return editorsHaveHeader() || InnerHeaders::count() > 0 ? full : 0; }

/**
 * Whether sounds are loaded from and saved to files in the editor's header. Not on the iPad or iPhone (Marcel,
 * 2026-11-19): the buttons crowd the header there, and the app is to get one way to load, save and export sounds
 * for all its instruments. A test sets it to see the other header on any system.
 */
inline bool& soundFilesInHeader()
{
#if JUCE_IOS
    static bool shown = false;
#else
    static bool shown = true;
#endif
    return shown;
}

/**
 * Whether the frame puts its own bar above the editor: a plugin in a host and JUCE's standalone app on the desktop
 * (Marcel, 2026-11-20). The bar is the iPad app's in short, the name and the sound between its arrows; the language
 * comes from the system and the sounds also from the host's own menu, so it has no buttons. Not on iOS (an AUv3 there
 * keeps the editor's header for now), not in the Tonwerk app (its bar is its own) and not in the tests, which set it
 * to see it.
 */
inline bool& pluginBarForced()
{
    static bool forced = juce::SystemStats::getEnvironmentVariable("TONWERK_PLUGIN_BAR", {}) == "1";
    return forced;
}

inline bool hasPluginBar(const juce::AudioProcessor& processor)
{
#if JUCE_IOS
    juce::ignoreUnused(processor);
    return false;
#else
    return pluginBarForced() || processor.wrapperType != juce::AudioProcessor::wrapperType_Undefined;
#endif
}

/**
 * What an editor offers beside its sounds that a host's menu cannot (Tonwerk DX's SysEx, Tonwerk Expander's
 * Hardware-Blatt; Marcel, 2026-11-20): the frame's bar shows each as a button, since the editor's header is gone.
 */
struct BarActions
{
    struct Action
    {
        juce::String name, tooltip;
        std::function<void()> run;
    };
    virtual ~BarActions() = default;
    virtual std::vector<Action> barActions() = 0;
};

/**
 * The bar above a plugin's editor: its name in the title's colours on the left, on the right the sound it plays (the
 * host program's name) as a plain cyan label between two arrows, as in the iPad app's bar. A click on the name opens
 * the list of sounds; the arrows step through it, around its ends. The host is told, so its own menu follows.
 */
class PluginBar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kHeight = 44;

    explicit PluginBar(juce::AudioProcessor& shown) : processor(shown)
    {
        setOpaque(true);
        for (auto* arrow : { &previous, &next })
        {
            arrow->setMouseCursor(juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible(*arrow);
        }
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        read();
        startTimer(400);
    }

    /** For the tests: the editor's buttons in the bar. */
    int actionCount() const { return (int) buttons.size(); }

    /** The editor's own buttons, left of the sound (`BarActions`). */
    void setActions(std::vector<BarActions::Action> offered)
    {
        buttons.clear();
        for (auto& action : offered)
        {
            auto& button = *buttons.emplace_back(std::make_unique<ActionButton>(action.name));
            button.setTooltip(action.tooltip);
            button.onClick = std::move(action.run);
            addAndMakeVisible(button);
        }
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0b10));
        // The plugin's name with the game's colour split, as the editors' headers wrote it.
        const auto title = getLocalBounds().withTrimmedLeft(kMargin).withRight(nameRight);
        const auto name = processor.getName().toUpperCase();
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffff003c).withAlpha(0.7f));
        g.drawText(name, title.translated(-2, 0), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff00f0ff).withAlpha(0.6f));
        g.drawText(name, title.translated(2, 0), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xfffcee0a));
        g.drawText(name, title, juce::Justification::centredLeft);
        if (! soundArea.isEmpty() && sound.isNotEmpty())
        {
            g.setColour(juce::Colour(0xff00f0ff));
            g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
            g.drawFittedText(sound, soundArea, juce::Justification::centred, 1, 0.8f);
        }
        // A thin line under the bar, the panels' edge colour.
        g.setColour(juce::Colour(0xff00f0ff).withAlpha(0.18f));
        g.fillRect(getLocalBounds().removeFromBottom(1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kMargin, 0);
        const bool sounds = processor.getNumPrograms() > 1;
        previous.setVisible(sounds);
        next.setVisible(sounds);
        soundArea = {};
        if (sounds)
        {
            auto right = area.removeFromRight(juce::jmin(kSound + 2 * kArrow, area.getWidth() / 2));
            next.setBounds(right.removeFromRight(kArrow));
            previous.setBounds(right.removeFromLeft(kArrow));
            soundArea = right;
            area.removeFromRight(kMargin);
        }
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
        {
            auto& button = **it;
            const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(ActionButton::font(),
                                                                                    button.getButtonText()))
                              + 24;
            button.setBounds(area.removeFromRight(width).withSizeKeepingCentre(width, 28));
            area.removeFromRight(8);
        }
        nameRight = area.getRight();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (! soundArea.contains(event.getPosition()) || processor.getNumPrograms() < 2)
            return;
        juce::PopupMenu menu;
        const int current = processor.getCurrentProgram();
        for (int i = 0; i < processor.getNumPrograms(); ++i)
            menu.addItem(i + 1, processor.getProgramName(i), true, i == current);
        juce::Component::SafePointer<PluginBar> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(
                               localAreaToGlobal(soundArea)),
                           [safe](int chosen) {
                               if (safe != nullptr && chosen > 0)
                                   safe->choose(chosen - 1);
                           });
    }

private:
    /** A word in a frame, as the editors' header buttons were drawn. */
    class ActionButton : public juce::Button
    {
    public:
        explicit ActionButton(const juce::String& name) : juce::Button(name)
        {
            setButtonText(name);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        static juce::Font font() { return juce::Font(juce::FontOptions(15.0f)); }
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto area = getLocalBounds().toFloat().reduced(0.5f);
            const auto yellow = juce::Colour(0xfffcee0a);
            g.setColour(juce::Colour(0xff12141c).brighter(down ? 0.15f : 0.0f));
            g.fillRoundedRectangle(area, 4.0f);
            g.setColour(yellow.withAlpha(over ? 0.8f : 0.45f));
            g.drawRoundedRectangle(area, 4.0f, 1.0f);
            g.setColour(yellow);
            g.setFont(font());
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    /** An arrow beside the sound's name, without a frame, so it reads as part of the name. */
    class Arrow : public juce::Button
    {
    public:
        explicit Arrow(bool pointsLeft) : juce::Button(pointsLeft ? "previous" : "next"), left(pointsLeft)
        {
            setTitle(pointsLeft ? tr("Voriger Klang") : tr("Nächster Klang"));
        }
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto area = getLocalBounds().toFloat();
            const float size = std::min(16.0f, std::min(area.getWidth(), area.getHeight()) - 10.0f);
            const auto box = juce::Rectangle<float>(size * 0.8f, size).withCentre(area.getCentre());
            juce::Path arrow;
            if (left)
                arrow.addTriangle(box.getRight(), box.getY(), box.getRight(), box.getBottom(), box.getX(), box.getCentreY());
            else
                arrow.addTriangle(box.getX(), box.getY(), box.getX(), box.getBottom(), box.getRight(), box.getCentreY());
            g.setColour(juce::Colour(0xfffcee0a).withAlpha(down ? 0.5f : over ? 1.0f : 0.85f));
            g.fillPath(arrow);
            g.strokePath(arrow, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        bool left;
    };

    void step(int by)
    {
        const int count = processor.getNumPrograms();
        if (count > 1)
            choose((processor.getCurrentProgram() + by + count) % count);
    }

    void choose(int program)
    {
        processor.setCurrentProgram(program);
        // So the host's own menu (Logic's above the plugin) shows the sound chosen here.
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
        read();
    }

    void timerCallback() override { read(); }

    void read()
    {
        const auto name = processor.getNumPrograms() > 1 ? processor.getProgramName(processor.getCurrentProgram())
                                                         : juce::String();
        if (name != sound)
        {
            sound = name;
            repaint();
        }
    }

    static constexpr int kMargin = 12;
    static constexpr int kArrow = 32;
    static constexpr int kSound = 260;
    juce::AudioProcessor& processor;
    Arrow previous { true }, next { false };
    std::vector<std::unique_ptr<ActionButton>> buttons;
    juce::Rectangle<int> soundArea;
    int nameRight = 0;
    juce::String sound;
};

/**
 * What every editor sits in: it carries the language switch and builds the editor again when the language changes
 * (here or in another plugin, read once a second), so names and menus follow at once. The host's own view of the
 * parameters (names in Logic's automation) keeps the language the plugin was loaded in.
 *
 * On the iPad the host decides the editor's size (an AUv3's view, the app's screen), and JUCE gives the editor that size
 * whatever it asked for. The editors are laid out for one size, so there the frame scales them to fit with their shape
 * kept and follows when they resize themselves (Tonwerk Synth per engine, Tonwerk Scope). Elsewhere it takes the
 * editor's size, and a resizable editor's limits.
 */
class HostFrame : public juce::AudioProcessorEditor, private juce::ComponentListener, private juce::Timer
{
public:
    using Factory = std::function<juce::AudioProcessorEditor*()>;

    HostFrame(juce::AudioProcessor& owner, Factory makeEditor)
        : juce::AudioProcessorEditor(owner), make(std::move(makeEditor))
    {
        setOpaque(true);
        languageSwitch.onChange = [this] { rebuildSoon(); };
        if (hasPluginBar(owner))
        {
            // The bar has the name and the sounds, so the editors leave their headers out (as in the iPad app) and
            // the buttons for files with them (Marcel, 2026-11-20: the host's menu chooses the sounds).
            editorsHaveHeader() = false;
            soundFilesInHeader() = false;
            // Without the switch the language is the system's; a choice saved earlier would have no way back.
            followSystemLanguage();
            bar = std::make_unique<PluginBar>(owner);
            addAndMakeVisible(*bar);
        }
        build();
#if JUCE_IOS
        setResizable(true, false);
        setResizeLimits(160, 120, 8192, 8192);
#endif
        startTimer(1000);
    }

    ~HostFrame() override
    {
        if (inner != nullptr)
            inner->removeComponentListener(this);
    }

    juce::AudioProcessorEditor* editor() const { return inner.get(); }
    /** The bar above the editor in a host, or nullptr. */
    PluginBar* pluginBar() const { return bar.get(); }

    /**
     * An editor of pages for a screen too small for the full one (`CompactEditor`). With one, the frame shows it
     * on an iPhone whenever the full editor, scaled to the room it gets, would leave a knob smaller than a finger
     * needs.
     */
    void setCompactFactory(Factory makeCompactEditor)
    {
        makeCompact = std::move(makeCompactEditor);
#if ! JUCE_IOS
        // TONWERK_COMPACT=<width>x<height> shows the compact editor at that size on the desktop, to look at it.
        const auto wanted = juce::StringArray::fromTokens(
            juce::SystemStats::getEnvironmentVariable("TONWERK_COMPACT", {}), "x", {});
        if (wanted.size() == 2 && makeCompact)
        {
            forced = compact = true;
            build();
            setSize(wanted[0].getIntValue(), wanted[1].getIntValue());
        }
#endif
    }
    bool hasCompact() const { return makeCompact != nullptr; }
    bool isCompact() const { return compact; }
    /** The full editor's own size, whichever editor shows. */
    juce::Point<int> fullEditorSize() const { return fullSize; }

    /** Whether this is an iPhone (the simulator names its model the same way). */
    static bool onPhone()
    {
#if JUCE_IOS
        static const bool phone = juce::SystemStats::getDeviceDescription().startsWithIgnoreCase("iPhone");
        return phone;
#else
        return false;
#endif
    }

    /** One knob's place in the full editors, and the least a finger needs (Apple's 44 points). */
    static constexpr float kFullSlot = 60.0f;
    static constexpr float kFinger = 44.0f;
    static bool tooSmallAt(float scale) { return kFullSlot * scale < kFinger; }

    /** For a host that scales the frame itself (the app): it decides, and the frame no longer does. */
    void setCompact(bool on)
    {
        forced = true;
        if (on == compact || ! makeCompact)
            return;
        compact = on;
        build();
    }

    /**
     * For a host with the language and the sounds in menus of its own (the app on a phone): no language switch, and
     * the compact editor without its header.
     */
    void setBare(bool on)
    {
        if (on == bare)
            return;
        bare = on;
        parentHierarchyChanged();
        if (inner != nullptr)
        {
            inner->getProperties().set("tonwerkBare", bare);
            inner->resized();
        }
    }

    /** The host changed the language itself (`chooseLanguage`): the editor is built again in it. */
    void languageChanged()
    {
        languageSwitch.repaint();
        rebuildSoon();
    }

    // The Night City background, so the bands beside a scaled editor look like part of it.
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0a0b10)); }
    void resized() override { fit(); }

    void parentHierarchyChanged() override
    {
        // Inside another plugin's frame (Tonwerk Rig) that one's switch is enough.
        languageSwitch.setVisible(! bare && editorsHaveHeader() && findParentComponentOfClass<HostFrame>() == nullptr);
    }

private:
    void build()
    {
        if (inner != nullptr)
            inner->removeComponentListener(this);
        removeChildComponent(&languageSwitch);
        inner.reset();
        inner.reset(compact ? makeCompact() : make());
        inner->getProperties().set("tonwerkBare", bare);
        if (! compact)
            fullSize = { inner->getWidth(), inner->getHeight() };
        addAndMakeVisible(*inner);
        if (bar != nullptr)
        {
            auto* offers = dynamic_cast<BarActions*>(inner.get());
            bar->setActions(offers != nullptr ? offers->barActions() : std::vector<BarActions::Action> {});
        }
        addChildComponent(languageSwitch);
        parentHierarchyChanged();
        inner->addComponentListener(this);
        // The compact editor takes the room there is, and so does any editor built again on the iPad.
        bool keepsSize = compact;
#if JUCE_IOS
        keepsSize = keepsSize || getWidth() > 0;
#endif
        if (keepsSize)
        {
            fit();
            return;
        }
#if ! JUCE_IOS
        if (inner->isResizable())
        {
            setResizable(true, false);
            if (auto* limits = inner->getConstrainer())
                setResizeLimits(limits->getMinimumWidth(), limits->getMinimumHeight() + barHeight(),
                                limits->getMaximumWidth(), limits->getMaximumHeight() + barHeight());
        }
#endif
        if (getWidth() == inner->getWidth() && getHeight() == inner->getHeight() + barHeight())
            fit();
        else
            setSize(inner->getWidth(), inner->getHeight() + barHeight());
    }

    int barHeight() const { return bar != nullptr ? PluginBar::kHeight : 0; }

    void rebuildSoon()
    {
        // Not from inside the switch's own click: building again deletes what the click came through.
        juce::Component::SafePointer<HostFrame> safe(this);
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr)
                safe->build();
        });
    }

    void timerCallback() override
    {
        if (refreshLanguage())
        {
            languageSwitch.repaint();
            build();
        }
    }

    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (! wasResized || fitting || compact)
            return;
#if JUCE_IOS
        fit();
#else
        setSize(inner->getWidth(), inner->getHeight() + barHeight());
#endif
    }

    void fit()
    {
        if (inner == nullptr || getWidth() <= 0 || getHeight() <= 0)
            return;
        const juce::ScopedValueSetter<bool> guard(fitting, true);
#if JUCE_IOS
        // Only on a phone: an iPad's hosts show the full editor scaled, as they did before there were pages.
        if (makeCompact && ! forced && onPhone() && fullSize.x > 0)
        {
            const bool wanted = tooSmallAt(std::min((float) getWidth() / (float) fullSize.x,
                                                    (float) getHeight() / (float) fullSize.y));
            if (wanted != compact)
            {
                compact = wanted;
                rebuildSoon();
                return;
            }
        }
#endif
        const int top = barHeight();
#if JUCE_IOS
        const bool takesRoom = compact;
#else
        const bool takesRoom = compact || inner->isResizable();
#endif
        // The compact editor and a resizable one (Tonwerk Scope) take the room there is.
        if (takesRoom)
        {
            if (bar != nullptr)
                bar->setBounds(getLocalBounds().removeFromTop(top));
            inner->setTransform({});
            inner->setBounds(getLocalBounds().withTrimmedTop(top));
            languageSwitch.setBounds(switchPlace().translated(0, top));
            return;
        }
        // Every other editor keeps the size it was laid out for. Where the host gives it another (the iPad; Logic's
        // AUv3 window on the Mac, which zooms with its Ansicht and kept the width's share when dragged), it is
        // scaled whole with its shape kept, never laid out again: that cut panels off (Marcel's video, 2026-11-20).
        const auto w = (float) inner->getWidth();
        const auto h = (float) (inner->getHeight() + top);
        if (w <= 0.0f || h <= 0.0f)
            return;
        const float scale = std::min((float) getWidth() / w, (float) getHeight() / h);
        const auto placed = juce::AffineTransform::scale(scale).translated(((float) getWidth() - w * scale) * 0.5f,
                                                                           ((float) getHeight() - h * scale) * 0.5f);
        if (bar != nullptr)
        {
            bar->setBounds(0, 0, inner->getWidth(), top);
            bar->setTransform(placed);
        }
        inner->setTopLeftPosition(0, top);
        inner->setTransform(placed);
        const auto place = switchPlace().toFloat().translated(0.0f, (float) top);
        const auto corner = place.getTopLeft().transformedBy(placed);
        languageSwitch.setBounds((int) corner.x, (int) corner.y, juce::roundToInt(place.getWidth() * scale),
                                 juce::roundToInt(place.getHeight() * scale));
    }

    /**
     * Where the switch goes in the editor: in its header, just left of the group of controls on the right (the preset
     * menu and its buttons), so it covers nothing; at the top right corner of an editor without such controls.
     */
    juce::Rectangle<int> switchPlace() const
    {
        std::vector<juce::Rectangle<int>> header;
        for (auto* child : inner->getChildren())
        {
            const auto b = child->getBounds();
            // An empty label (room for a message) is not a control.
            if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText().isEmpty())
                continue;
            if (child->isVisible() && b.getY() < 44 && b.getBottom() <= 56 && b.getWidth() < inner->getWidth() / 2)
                header.push_back(b);
        }
        std::sort(header.begin(), header.end(), [](auto a, auto b) { return a.getX() > b.getX(); });
        if (header.empty())
            return { inner->getWidth() - LanguageSwitch::kWidth - 8, 6, LanguageSwitch::kWidth, LanguageSwitch::kHeight };
        // Walk left from the rightmost control while the next one is close by: that is one group.
        auto group = header.front();
        for (const auto& b : header)
            if (b.getRight() >= group.getX() - 30)
                group = group.getUnion(b);
        return { group.getX() - LanguageSwitch::kWidth - 12, group.getCentreY() - LanguageSwitch::kHeight / 2,
                 LanguageSwitch::kWidth, LanguageSwitch::kHeight };
    }

    Factory make, makeCompact;
    bool compact = false;
    bool bare = false;
    // Whether someone else chooses between the full and the compact editor.
    bool forced = false;
    juce::Point<int> fullSize;
    std::unique_ptr<juce::AudioProcessorEditor> inner;
    std::unique_ptr<PluginBar> bar;
    LanguageSwitch languageSwitch;
    bool fitting = false;
};

/** What a processor's `createEditor` returns: the editor of type `Editor` inside a `HostFrame`. */
template <typename Editor, typename Processor>
juce::AudioProcessorEditor* forHost(Processor& processor)
{
    return new HostFrame(processor, [&processor] { return new Editor(processor); });
}

/** The same with an editor of pages for small screens, made by `makeCompact`. */
template <typename Editor, typename Processor>
juce::AudioProcessorEditor* forHost(Processor& processor, HostFrame::Factory makeCompact)
{
    auto* frame = new HostFrame(processor, [&processor] { return new Editor(processor); });
    frame->setCompactFactory(std::move(makeCompact));
    return frame;
}

/** The editor inside what `createEditor` returned, as `Editor` (nullptr when it is another). */
template <typename Editor>
Editor* innerEditor(juce::AudioProcessorEditor* created)
{
    if (auto* frame = dynamic_cast<HostFrame*>(created))
        return dynamic_cast<Editor*>(frame->editor());
    return dynamic_cast<Editor*>(created);
}

/**
 * The parent of a popup menu, for the look and feels' `getParentComponentForMenuOptions`. On the iPad the editor lives in
 * the host's view (an AUv3) and is scaled to fit it; a menu opens inside the editor so it shows there and at its scale.
 */
inline juce::Component* menuParentFor(const juce::PopupMenu::Options& options)
{
#if JUCE_IOS || TONWERK_SANDBOX
    if (options.getParentComponent() == nullptr)
        if (auto* target = options.getTargetComponent())
            return target->getTopLevelComponent();
#endif
    return options.getParentComponent();
}

/**
 * A file chooser opened from `from`. In an AUv3 on the iPad JUCE may not open a window of its own for the picker; without
 * a parent it shows nothing but still makes itself modal, so the editor took no touch any more (a user's Granular and
 * Sampler froze on "Laden"). The parent is the editor's top level, which the picker covers.
 */
inline std::unique_ptr<juce::FileChooser> makeChooser(juce::Component& from, const juce::String& title,
                                                      const juce::File& start, const juce::String& patterns)
{
#if JUCE_IOS
    juce::Component* parent = from.getTopLevelComponent();
#else
    juce::Component* parent = nullptr;
    juce::ignoreUnused(from);
#endif
    return std::make_unique<juce::FileChooser>(title, start, patterns, true, false, parent);
}

/**
 * Whether menus are made for a finger: the boxes higher and their lists' rows as high as the box (Marcel, 2026-11-20:
 * the entries of the wave menus were too small on the iPad and the iPhone). True on iOS, in the app and in a host;
 * the desktop app sets it with its iPad bar, a test to see the larger menus on any system.
 */
inline bool& touchMenus()
{
#if JUCE_IOS
    static bool touch = true;
#else
    static bool touch = false;
#endif
    return touch;
}

/**
 * Whether an editor draws its own oscilloscope. Not inside the iPad app, which has one beside its keys for every
 * instrument: editors with theirs above the knobs (Tonwerk Bass, Expander, Neuro) showed two there, since the app
 * can only cut an editor off above its keyboard. The app clears this before it builds an editor; an AUv3 in a host
 * is its own process and keeps its scope.
 */
inline bool& editorsHaveScope()
{
    static bool shown = true;
    return shown;
}

/** Whether the chooser came back without a file. */
inline bool nothingChosen(const juce::FileChooser& chooser) { return chooser.getURLResult().isEmpty(); }

/**
 * The bytes of the file the user chose. On the iPad a chosen file lies outside the plugin's sandbox and opens only
 * through the chooser's URL, which carries the permission; elsewhere the URL is the file.
 */
inline bool readChosen(const juce::FileChooser& chooser, juce::MemoryBlock& data)
{
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress);
    if (auto stream = chooser.getURLResult().createInputStream(options))
    {
        data.reset();
        stream->readIntoMemoryBlock(data);
        return data.getSize() > 0;
    }
    return false;
}

inline juce::String readChosenText(const juce::FileChooser& chooser)
{
    juce::MemoryBlock data;
    return readChosen(chooser, data) ? data.toString() : juce::String();
}

/**
 * Writes `size` bytes into `file` in place. `File::replaceWithData` writes a hidden sibling first and moves it over the
 * file, which the Mac's sandbox refuses (only the chosen file itself may be written), so a save there did nothing.
 */
inline bool writeInPlace(const juce::File& file, const void* data, size_t size)
{
    juce::FileOutputStream stream(file);
    if (! stream.openedOk() || ! stream.setPosition(0) || stream.truncate().failed())
        return false;
    const bool written = stream.write(data, size);
    stream.flush();
    return written && stream.getStatus().wasOk();
}

/** Writes the file the user chose to save as; on the iPad through the URL (the picker made the file empty). */
inline bool writeChosenData(const juce::FileChooser& chooser, const void* data, size_t size)
{
#if JUCE_IOS
    if (auto stream = chooser.getURLResult().createOutputStream())
    {
        const bool written = stream->write(data, size);
        stream->flush();
        return written;
    }
    return false;
#else
    return writeInPlace(chooser.getResult(), data, size);
#endif
}

inline bool writeChosenText(const juce::FileChooser& chooser, const juce::String& text)
{
    const auto utf8 = text.toRawUTF8();
    return writeChosenData(chooser, utf8, std::strlen(utf8));
}

/** Where a plugin keeps copies of the files chosen on the iPad: Application Support/<plugin>/Dateien. */
inline juce::File keptFilesFolder(const juce::String& plugin)
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile(plugin)
        .getChildFile("Dateien");
}

/** The name the chosen file has for the user (a URL's last part is escaped). */
inline juce::String chosenName(const juce::FileChooser& chooser)
{
    return juce::URL::removeEscapeChars(chooser.getURLResult().getFileName());
}

/**
 * A chosen file the plugin can open again later, when a project loads: on the iPad and in the Mac's sandbox a copy in
 * `folder` (the permission for the original ends with the chooser, or with the process), elsewhere the file itself.
 */
inline juce::File keepChosen(const juce::FileChooser& chooser, const juce::File& folder)
{
#if JUCE_IOS || TONWERK_SANDBOX
    juce::MemoryBlock data;
    if (! readChosen(chooser, data) || ! folder.createDirectory())
        return {};
    const auto copy = folder.getChildFile(juce::File::createLegalFileName(chosenName(chooser)));
    return copy.replaceWithData(data.getData(), data.getSize()) ? copy : juce::File();
#else
    juce::ignoreUnused(folder);
    return chooser.getResult();
#endif
}

/**
 * A file a project names, found again: iOS moves an app's container when the app is updated, so a kept copy is looked
 * for by its name in `folder` when the saved path is gone.
 */
inline juce::File findKept(const juce::File& saved, const juce::File& folder)
{
    if (saved.existsAsFile() || saved == juce::File())
        return saved;
    const auto moved = folder.getChildFile(saved.getFileName());
    return moved.existsAsFile() ? moved : saved;
}
} // namespace tonwerkui
