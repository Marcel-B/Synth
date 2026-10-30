#pragma once

#include "Language.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cstring>
#include <memory>

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

    // The Night City background, so the bands beside a scaled editor look like part of it.
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0a0b10)); }
    void resized() override { fit(); }

    void parentHierarchyChanged() override
    {
        // Inside another plugin's frame (Tonwerk Rig) that one's switch is enough.
        languageSwitch.setVisible(findParentComponentOfClass<HostFrame>() == nullptr);
    }

private:
    void build()
    {
        if (inner != nullptr)
            inner->removeComponentListener(this);
        removeChildComponent(&languageSwitch);
        inner.reset();
        inner.reset(make());
        addAndMakeVisible(*inner);
        addChildComponent(languageSwitch);
        parentHierarchyChanged();
        inner->addComponentListener(this);
#if ! JUCE_IOS
        if (inner->isResizable())
        {
            setResizable(true, false);
            if (auto* limits = inner->getConstrainer())
                setResizeLimits(limits->getMinimumWidth(), limits->getMinimumHeight(), limits->getMaximumWidth(),
                                limits->getMaximumHeight());
        }
#endif
        if (getWidth() == inner->getWidth() && getHeight() == inner->getHeight())
            fit();
        else
            setSize(inner->getWidth(), inner->getHeight());
    }

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
        if (! wasResized || fitting)
            return;
#if JUCE_IOS
        fit();
#else
        setSize(inner->getWidth(), inner->getHeight());
#endif
    }

    void fit()
    {
        if (inner == nullptr || getWidth() <= 0 || getHeight() <= 0)
            return;
        const juce::ScopedValueSetter<bool> guard(fitting, true);
#if JUCE_IOS
        const auto w = (float) inner->getWidth();
        const auto h = (float) inner->getHeight();
        if (w <= 0.0f || h <= 0.0f)
            return;
        const float scale = std::min((float) getWidth() / w, (float) getHeight() / h);
        inner->setTopLeftPosition(0, 0);
        const auto placed = juce::AffineTransform::scale(scale).translated(((float) getWidth() - w * scale) * 0.5f,
                                                                           ((float) getHeight() - h * scale) * 0.5f);
        inner->setTransform(placed);
        const auto place = switchPlace().toFloat();
        const auto corner = place.getTopLeft().transformedBy(placed);
        languageSwitch.setBounds((int) corner.x, (int) corner.y, juce::roundToInt(place.getWidth() * scale),
                                 juce::roundToInt(place.getHeight() * scale));
#else
        inner->setBounds(getLocalBounds());
        languageSwitch.setBounds(switchPlace());
#endif
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

    Factory make;
    std::unique_ptr<juce::AudioProcessorEditor> inner;
    LanguageSwitch languageSwitch;
    bool fitting = false;
};

/** What a processor's `createEditor` returns: the editor of type `Editor` inside a `HostFrame`. */
template <typename Editor, typename Processor>
juce::AudioProcessorEditor* forHost(Processor& processor)
{
    return new HostFrame(processor, [&processor] { return new Editor(processor); });
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
