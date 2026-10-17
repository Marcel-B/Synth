#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <memory>

/**
 * What differs on the iPad (AUv3 and the standalone app built for iOS), kept in one place so the plugins' own code stays
 * the same everywhere: the editor's size, files the user chooses. The Mac App Store's build (TONWERK_SANDBOX) shares
 * the iPad's menus and files: its AUv3s run sandboxed in a process of their own, as on the iPad.
 */
namespace tonwerkui
{
/**
 * On the iPad the host decides the editor's size (an AUv3's view, the app's screen), and JUCE gives the editor that size
 * whatever it asked for. The editors are laid out for one size, so there they sit in this frame, which scales them to fit
 * with their shape kept and follows when they resize themselves (Tonwerk Synth per engine, Tonwerk Scope).
 */
class TouchFrame : public juce::AudioProcessorEditor, private juce::ComponentListener
{
public:
    explicit TouchFrame(juce::AudioProcessorEditor* editorToShow)
        : juce::AudioProcessorEditor(editorToShow->processor), inner(editorToShow)
    {
        setOpaque(true);
        addAndMakeVisible(*inner);
        inner->addComponentListener(this);
        setResizable(true, false);
        setResizeLimits(160, 120, 8192, 8192);
        setSize(inner->getWidth(), inner->getHeight());
    }

    ~TouchFrame() override { inner->removeComponentListener(this); }

    // The Night City background, so the bands beside a scaled editor look like part of it.
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0a0b10)); }
    void resized() override { fit(); }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            fit();
    }

    void fit()
    {
        const auto w = (float) inner->getWidth();
        const auto h = (float) inner->getHeight();
        if (w <= 0.0f || h <= 0.0f || getWidth() <= 0 || getHeight() <= 0)
            return;
        const float scale = std::min((float) getWidth() / w, (float) getHeight() / h);
        inner->setTopLeftPosition(0, 0);
        inner->setTransform(juce::AffineTransform::scale(scale).translated(((float) getWidth() - w * scale) * 0.5f,
                                                                           ((float) getHeight() - h * scale) * 0.5f));
    }

    std::unique_ptr<juce::AudioProcessorEditor> inner;
};

/** What a processor's `createEditor` returns: the editor itself, on the iPad inside a `TouchFrame`. */
inline juce::AudioProcessorEditor* forHost(juce::AudioProcessorEditor* editor)
{
#if JUCE_IOS
    return new TouchFrame(editor);
#else
    return editor;
#endif
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

/** Writes the file the user chose to save as; on the iPad through the URL (the picker made the file empty). */
inline bool writeChosenText(const juce::FileChooser& chooser, const juce::String& text)
{
#if JUCE_IOS
    if (auto stream = chooser.getURLResult().createOutputStream())
    {
        const bool written = stream->writeText(text, false, false, nullptr);
        stream->flush();
        return written;
    }
    return false;
#else
    return chooser.getResult().replaceWithText(text);
#endif
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
