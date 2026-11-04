// Tonwerk Komponist: a few words in ("langsamer Walzer in d-Moll"), Claude writes the piece, out comes a MIDI file
// with one track per instrument to drag into Logic Pro or Ableton Live.
#include "Arrangement.h"
#include "Composer.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <thread>

namespace komponist
{
namespace colours
{
// Night City: Marcel's Cyberpunk palette, as in the plugins.
const juce::Colour background { 0xff0b0b10 };
const juce::Colour panel { 0xff15151d };
const juce::Colour line { 0xff2a2a36 };
const juce::Colour text { 0xffe8e8ee };
const juce::Colour dim { 0xff8a8a99 };
const juce::Colour yellow { 0xfffcee0a };
const juce::Colour cyan { 0xff00f0ff };
const juce::Colour red { 0xffff2a55 };
const juce::Colour tracks[] = { juce::Colour(0xff00f0ff), juce::Colour(0xfffcee0a), juce::Colour(0xffff2a55),
                                juce::Colour(0xff7cff6b), juce::Colour(0xffc77dff), juce::Colour(0xffff9e3d) };
} // namespace colours

juce::Colour trackColour(size_t index) { return colours::tracks[index % std::size(colours::tracks)]; }

juce::String safeFileName(const juce::String& name)
{
    auto cleaned = juce::File::createLegalFileName(name).trim();
    return cleaned.isEmpty() ? juce::String("Stueck") : cleaned;
}

// The piece drawn as lanes, one per track; a lane's name can be dragged out as that track alone.
class PianoRoll : public juce::Component
{
public:
    std::function<void(int track)> onDragTrack;

    void setArrangement(const Arrangement* a)
    {
        arrangement = a;
        repaint();
    }

    static constexpr int headerWidth = 150;
    static constexpr int rulerHeight = 22;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::panel);
        g.setColour(colours::line);
        g.drawRect(getLocalBounds());
        if (arrangement == nullptr || arrangement->tracks.empty())
        {
            g.setColour(colours::dim);
            g.setFont(juce::FontOptions(15.0f));
            g.drawText(de("Noch kein Stück. Beschreib oben, was du hören willst."), getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto& a = *arrangement;
        const auto total = std::max(a.totalLength(), 1.0);
        const auto area = getLocalBounds().reduced(1).withTrimmedLeft(headerWidth).withTrimmedTop(rulerHeight).toFloat();
        const auto xOf = [&](double quarters) { return area.getX() + static_cast<float>(quarters / total) * area.getWidth(); };

        // Sections on the ruler, bar lines through the lanes.
        double offset = 0.0;
        g.setFont(juce::FontOptions(12.0f));
        for (const auto index : a.form)
        {
            const auto& section = a.sections[static_cast<size_t>(index)];
            const auto x = xOf(offset);
            g.setColour(colours::yellow.withAlpha(0.8f));
            g.fillRect(x, 1.0f, 1.0f, area.getBottom());
            g.drawText(section.name, juce::Rectangle<float>(x + 4.0f, 2.0f, xOf(offset + section.bars * a.barLength()) - x - 6.0f, rulerHeight - 4.0f),
                       juce::Justification::centredLeft, true);
            for (int bar = 1; bar < section.bars; ++bar)
            {
                g.setColour(colours::line);
                g.fillRect(xOf(offset + bar * a.barLength()), area.getY(), 1.0f, area.getHeight());
            }
            offset += section.bars * a.barLength();
        }

        const auto lanes = static_cast<int>(a.tracks.size());
        const auto laneHeight = area.getHeight() / static_cast<float>(lanes);
        for (int t = 0; t < lanes; ++t)
        {
            const auto& track = a.tracks[static_cast<size_t>(t)];
            const auto lane = juce::Rectangle<float>(area.getX(), area.getY() + t * laneHeight, area.getWidth(), laneHeight);
            const auto header = juce::Rectangle<float>(1.0f, lane.getY(), headerWidth - 1.0f, laneHeight);
            const auto colour = trackColour(static_cast<size_t>(t));
            g.setColour(t == hoveredHeader ? colour.withAlpha(0.18f) : colours::background);
            g.fillRect(header);
            g.setColour(colours::line);
            g.fillRect(1.0f, lane.getBottom() - 1.0f, static_cast<float>(getWidth() - 2), 1.0f);
            g.setColour(colour);
            g.fillRect(header.withWidth(4.0f).reduced(0.0f, 3.0f));
            g.setColour(colours::text);
            g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            g.drawText(track.name, header.reduced(12.0f, 4.0f).withTrimmedBottom(laneHeight * 0.45f), juce::Justification::bottomLeft, true);
            g.setColour(colours::dim);
            g.setFont(juce::FontOptions(11.0f));
            const auto info = (track.role == Role::drums ? juce::String("Kanal 10") : "Programm " + juce::String(track.program + 1))
                              + juce::String::fromUTF8(" · ziehen");
            g.drawText(info, header.reduced(12.0f, 4.0f).withTrimmedTop(laneHeight * 0.55f), juce::Justification::topLeft, true);

            const auto notes = a.notesOf(t);
            if (notes.empty())
                continue;
            int low = 127, high = 0;
            for (const auto& n : notes)
            {
                low = std::min(low, n.pitch);
                high = std::max(high, n.pitch);
            }
            const auto span = static_cast<float>(std::max(high - low, 11) + 1);
            const auto inner = lane.reduced(0.0f, 4.0f);
            const auto noteHeight = std::max(1.5f, std::min(6.0f, inner.getHeight() / span));
            for (const auto& n : notes)
            {
                const auto y = inner.getBottom() - (static_cast<float>(n.pitch - low) + 1.0f) / span * inner.getHeight();
                const auto x = xOf(n.start);
                const auto w = std::max(2.0f, xOf(n.start + n.length) - x - 0.5f);
                g.setColour(colour.withAlpha(0.35f + 0.65f * static_cast<float>(n.velocity) / 127.0f));
                g.fillRect(x, y, w, noteHeight);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(headerAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto track = headerAt(e.getMouseDownPosition());
        if (track >= 0 && e.getDistanceFromDragStart() > 4 && !dragging && onDragTrack)
        {
            dragging = true;
            onDragTrack(track);
        }
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }

private:
    int headerAt(juce::Point<int> p) const
    {
        if (arrangement == nullptr || arrangement->tracks.empty() || p.x > headerWidth || p.y < rulerHeight)
            return -1;
        const auto laneHeight = (getHeight() - 2 - rulerHeight) / static_cast<float>(arrangement->tracks.size());
        const auto index = static_cast<int>((p.y - rulerHeight - 1) / laneHeight);
        return index >= 0 && index < static_cast<int>(arrangement->tracks.size()) ? index : -1;
    }

    void setHovered(int index)
    {
        if (index != hoveredHeader)
        {
            hoveredHeader = index;
            setMouseCursor(index >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    const Arrangement* arrangement = nullptr;
    int hoveredHeader = -1;
    bool dragging = false;
};

// The whole piece as one file to drag into the DAW.
class DragTile : public juce::Component
{
public:
    std::function<void()> onDrag;
    bool ready = false;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const auto colour = ready ? colours::yellow : colours::line;
        g.setColour(ready && isMouseOver() ? colour.withAlpha(0.2f) : colours::panel);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.setColour(ready ? colours::text : colours::dim);
        g.drawText(ready ? de("Ganzes Stück in Logic oder Live ziehen") : de("Hier liegt gleich dein Stück"), bounds, juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (ready && e.getDistanceFromDragStart() > 4 && !dragging && onDrag)
        {
            dragging = true;
            onDrag();
        }
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }

private:
    bool dragging = false;
};

class MainComponent : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit MainComponent(juce::PropertiesFile& settingsFile) : settings(settingsFile)
    {
        setLookAndFeel(&lookAndFeel);
        lookAndFeel.setColourScheme({ colours::background, colours::panel, colours::panel, colours::line, colours::text,
                                      colours::cyan, colours::background, colours::yellow, colours::text });

        title.setText("TONWERK KOMPONIST", juce::dontSendNotification);
        title.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, colours::yellow);
        addAndMakeVisible(title);

        subtitle.setText(de("Beschreib ein Stück, Claude schreibt die MIDI-Spuren."), juce::dontSendNotification);
        subtitle.setColour(juce::Label::textColourId, colours::dim);
        addAndMakeVisible(subtitle);

        wish.setMultiLine(true, true);
        wish.setReturnKeyStartsNewLine(true);
        wish.setFont(juce::FontOptions(16.0f));
        wish.setTextToShowWhenEmpty(de("Zum Beispiel: Ein langsamer Walzer in d-Moll, melancholisch, mit Klavier, Streichern und Kontrabass"),
                                    colours::dim);
        wish.setText(settings.getValue("lastWish"), false);
        addAndMakeVisible(wish);

        for (size_t i = 0; i < models().size(); ++i)
            model.addItem(juce::String::fromUTF8(models()[i].label), static_cast<int>(i) + 1);
        model.setSelectedItemIndex(0, juce::dontSendNotification);
        for (size_t i = 0; i < models().size(); ++i)
            if (settings.getValue("model") == models()[i].id)
                model.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
        model.onChange = [this] { settings.setValue("model", juce::String(models()[static_cast<size_t>(model.getSelectedItemIndex())].id)); };
        addAndMakeVisible(model);

        create.setButtonText("Neu erzeugen");
        create.onClick = [this] { start(false); };
        addAndMakeVisible(create);
        change.setButtonText(de("Ändern"));
        change.setTooltip(de("Schickt das aktuelle Stück mit deinem Text als Änderungswunsch, etwa: Melodie eine Oktave höher, Schlagzeug weg."));
        change.onClick = [this] { start(true); };
        addAndMakeVisible(change);
        cancel.setButtonText("Abbrechen");
        cancel.onClick = [this] { cancelled = true; };
        addAndMakeVisible(cancel);

        status.setColour(juce::Label::textColourId, colours::dim);
        addAndMakeVisible(status);
        info.setColour(juce::Label::textColourId, colours::cyan);
        info.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(info);

        roll.onDragTrack = [this](int track) { dragTrack(track); };
        addAndMakeVisible(roll);
        tile.onDrag = [this] { dragTrack(-1); };
        addAndMakeVisible(tile);

        save.setButtonText("Speichern unter ...");
        save.onClick = [this] { saveAs(); };
        addAndMakeVisible(save);
        reveal.setButtonText("Im Finder zeigen");
        reveal.onClick = [this] {
            if (currentFile.existsAsFile())
                currentFile.revealToUser();
        };
        addAndMakeVisible(reveal);

        keyLabel.setText(de("API-Schlüssel"), juce::dontSendNotification);
        keyLabel.setColour(juce::Label::textColourId, colours::dim);
        addAndMakeVisible(keyLabel);
        apiKey.setPasswordCharacter(static_cast<juce::juce_wchar>(0x2022));
        apiKey.setText(settings.getValue("apiKey"), false);
        apiKey.setTextToShowWhenEmpty(findApiKey({}).isNotEmpty() ? de("aus ANTHROPIC_API_KEY oder ~/.config/tonwerk/anthropic.key")
                                                                  : de("sk-ant-... von console.anthropic.com"),
                                      colours::dim);
        apiKey.onTextChange = [this] { settings.setValue("apiKey", apiKey.getText().trim()); };
        addAndMakeVisible(apiKey);

        updateState();
        setSize(1080, 720);

        // KOMPONIST_OPEN=<piece.json> shows a piece in Claude's format as if it had just arrived, for trying the app
        // and taking pictures of it without a key.
        const auto open = juce::SystemStats::getEnvironmentVariable("KOMPONIST_OPEN", {});
        if (open.isNotEmpty())
        {
            Outcome outcome;
            outcome.parsed = parseArrangement(juce::File(open).loadFileAsString());
            outcome.errorTitle = juce::File(open).getFileName();
            started = juce::Time::getMillisecondCounter();
            finished(std::move(outcome));
        }
    }

    ~MainComponent() override
    {
        cancelled = true;
        if (worker.joinable())
            worker.join();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override { g.fillAll(colours::background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto top = area.removeFromTop(30);
        title.setBounds(top.removeFromLeft(260));
        subtitle.setBounds(top);
        area.removeFromTop(8);
        wish.setBounds(area.removeFromTop(84));
        area.removeFromTop(8);
        auto controls = area.removeFromTop(30);
        model.setBounds(controls.removeFromLeft(250));
        controls.removeFromLeft(8);
        create.setBounds(controls.removeFromLeft(130));
        controls.removeFromLeft(8);
        change.setBounds(controls.removeFromLeft(100));
        controls.removeFromLeft(8);
        cancel.setBounds(controls.removeFromLeft(100));
        controls.removeFromLeft(12);
        status.setBounds(controls);
        area.removeFromTop(8);
        info.setBounds(area.removeFromTop(22));

        auto bottom = area.removeFromBottom(26);
        keyLabel.setBounds(bottom.removeFromLeft(110));
        apiKey.setBounds(bottom.removeFromLeft(420));
        area.removeFromBottom(10);
        auto files = area.removeFromBottom(56);
        reveal.setBounds(files.removeFromRight(150).withSizeKeepingCentre(150, 30));
        files.removeFromRight(8);
        save.setBounds(files.removeFromRight(150).withSizeKeepingCentre(150, 30));
        files.removeFromRight(12);
        tile.setBounds(files);
        area.removeFromBottom(10);
        roll.setBounds(area);
    }

private:
    void start(bool asChange)
    {
        if (busy)
            return;
        const auto text = wish.getText().trim();
        if (text.isEmpty())
        {
            status.setText(de("Schreib erst, was du hören willst."), juce::dontSendNotification);
            return;
        }
        const auto key = findApiKey(apiKey.getText());
        if (key.isEmpty())
        {
            status.setText(de("Unten fehlt der API-Schlüssel (console.anthropic.com)."), juce::dontSendNotification);
            apiKey.grabKeyboardFocus();
            return;
        }
        settings.setValue("lastWish", text);
        const auto modelId = juce::String(models()[static_cast<size_t>(std::max(0, model.getSelectedItemIndex()))].id);
        std::optional<Arrangement> base;
        if (asChange && arrangement)
            base = *arrangement;

        busy = true;
        cancelled = false;
        started = juce::Time::getMillisecondCounter();
        received = 0;
        updateState();
        startProgress();
        if (worker.joinable())
            worker.join();
        juce::Component::SafePointer<MainComponent> self(this);
        worker = std::thread([self, key, modelId, text, base = std::move(base), this] {
            auto outcome = compose(key, modelId, text, base ? &*base : nullptr, [this](int chars) { received = chars; }, cancelled);
            juce::MessageManager::callAsync([self, outcome = std::move(outcome)]() mutable {
                if (self != nullptr)
                    self->finished(std::move(outcome));
            });
        });
    }

    void startProgress()
    {
        juce::Component::SafePointer<MainComponent> self(this);
        juce::Timer::callAfterDelay(250, [self] {
            if (self == nullptr || !self->busy)
                return;
            const auto seconds = static_cast<int>((juce::Time::getMillisecondCounter() - self->started) / 1000);
            const auto chars = self->received.load();
            self->status.setText(chars == 0 ? "Claude denkt nach ... " + juce::String(seconds) + " s"
                                            : "Claude schreibt ... " + juce::String(chars / 1024.0, 1) + " kB, " + juce::String(seconds) + " s",
                                 juce::dontSendNotification);
            self->startProgress();
        });
    }

    void finished(Outcome outcome)
    {
        busy = false;
        if (!outcome.parsed.arrangement)
        {
            status.setText(outcome.errorTitle + ": " + outcome.parsed.error, juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, colours::red);
            updateState();
            return;
        }
        status.setColour(juce::Label::textColourId, colours::dim);
        arrangement = std::move(outcome.parsed.arrangement);
        warnings = outcome.parsed.warnings;
        trackFiles.clear();

        // Every piece is kept in Music/Tonwerk Komponist, so a drag has a file that stays where the DAW found it.
        const auto folder = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tonwerk Komponist");
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S");
        currentFile = folder.getChildFile(stamp + " " + safeFileName(arrangement->title) + ".mid");
        const auto written = writeMidiFile(*arrangement, currentFile);
        const auto seconds = static_cast<int>((juce::Time::getMillisecondCounter() - started) / 1000);
        auto text = written ? juce::String("Fertig in ") + juce::String(seconds) + " s" : de("Fertig, aber die Datei ließ sich nicht schreiben");
        if (outcome.model.isNotEmpty())
            text << " (" << outcome.model << ")";
        if (!warnings.isEmpty())
            text << de(". Repariert: ") << warnings[0] << (warnings.size() > 1 ? " (+" + juce::String(warnings.size() - 1) + ")" : juce::String());
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(warnings.joinIntoString("\n"));
        updateState();
    }

    void updateState()
    {
        create.setEnabled(!busy);
        change.setEnabled(!busy && arrangement.has_value());
        cancel.setEnabled(busy);
        wish.setReadOnly(busy);
        const auto ready = arrangement.has_value() && currentFile.existsAsFile();
        tile.ready = ready;
        tile.repaint();
        save.setEnabled(ready);
        reveal.setEnabled(ready);
        roll.setArrangement(arrangement ? &*arrangement : nullptr);
        if (arrangement)
        {
            const auto& a = *arrangement;
            juce::String form;
            for (const auto index : a.form)
                form << (form.isEmpty() ? "" : " ") << a.sections[static_cast<size_t>(index)].name;
            info.setText(a.title + juce::String::fromUTF8("   ·   ") + juce::String(juce::roundToInt(a.tempo)) + " BPM, "
                             + juce::String(a.numerator) + "/" + juce::String(a.denominator) + ", " + a.tonic + (a.minor ? "-Moll" : "-Dur")
                             + ", " + juce::String(a.totalBars()) + " Takte" + juce::String::fromUTF8("   ·   ") + form,
                         juce::dontSendNotification);
        }
        else
            info.setText({}, juce::dontSendNotification);
    }

    // -1 is the whole piece; a track goes into a file of its own beside it, made on its first drag.
    void dragTrack(int track)
    {
        if (!arrangement || !currentFile.existsAsFile())
            return;
        auto file = currentFile;
        if (track >= 0)
        {
            if (trackFiles.count(track) == 0)
            {
                const auto name = currentFile.getFileNameWithoutExtension() + " - " + safeFileName(arrangement->tracks[static_cast<size_t>(track)].name) + ".mid";
                const auto trackFile = currentFile.getSiblingFile("Spuren").getChildFile(name);
                if (!writeMidiFile(*arrangement, trackFile, track))
                    return;
                trackFiles[track] = trackFile;
            }
            file = trackFiles[track];
        }
        performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
    }

    void saveAs()
    {
        if (!arrangement)
            return;
        chooser = std::make_unique<juce::FileChooser>("MIDI-Datei speichern", currentFile.getSiblingFile(safeFileName(arrangement->title) + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& c) {
                                 const auto target = c.getResult();
                                 if (target == juce::File() || !arrangement)
                                     return;
                                 if (!writeMidiFile(*arrangement, target.withFileExtension("mid")))
                                     status.setText(de("Speichern ging nicht: ") + target.getFullPathName(), juce::dontSendNotification);
                             });
    }

    juce::PropertiesFile& settings;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::Label title, subtitle, status, info, keyLabel;
    juce::TextEditor wish, apiKey;
    juce::ComboBox model;
    juce::TextButton create, change, cancel, save, reveal;
    PianoRoll roll;
    DragTile tile;
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<juce::FileChooser> chooser;

    std::optional<Arrangement> arrangement;
    juce::StringArray warnings;
    juce::File currentFile;
    std::map<int, juce::File> trackFiles;

    std::thread worker;
    std::atomic<bool> cancelled { false };
    std::atomic<int> received { 0 };
    bool busy = false;
    juce::uint32 started = 0;
};

class Application : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Tonwerk Komponist"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Tonwerk Komponist";
        options.filenameSuffix = ".settings";
        options.folderName = "Tonwerk Komponist";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile>(options);
        window = std::make_unique<Window>(*settings);
    }

    void shutdown() override
    {
        window = nullptr;
        if (settings)
            settings->saveIfNeeded();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window(juce::PropertiesFile& settings)
            : DocumentWindow("Tonwerk Komponist", colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(settings), true);
            setResizable(true, true);
            setResizeLimits(820, 560, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Window> window;
};
} // namespace komponist

START_JUCE_APPLICATION(komponist::Application)
