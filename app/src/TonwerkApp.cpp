#include "Catalogue.h"
#include "Language.h"
#include "NightCity.h"
#include "Platform.h"
#include "ScopeBuffer.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

/**
 * Tonwerk for the iPad: one app with every plugin in it. Logic Pro for iPad, GarageBand and AUM load the plugins as the
 * AUv3s it carries; on its own the app plays one instrument at a time, with its own on-screen keyboard and a MIDI
 * keyboard (USB or Bluetooth), or shows the microphone's or the audio interface's input in Tonwerk Scope. It builds on
 * the desktop too (-DTONWERK_APP=ON), to try it without an iPad.
 */
namespace tonwerkapp
{
namespace
{
using tonwerkui::utf8;
namespace colours = tonwerkui::colours;

/** The plugin played to the device, its output kept for the app's oscilloscope. */
class Player : public juce::AudioIODeviceCallback
{
public:
    juce::AudioProcessorPlayer player;
    tonwerkui::ScopeBuffer scope;
    /** The input into the plugin and its output kept from the speakers, which would play it back into the microphone. */
    std::atomic<bool> listening { false };

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override
    {
        if (listening.load(std::memory_order_relaxed))
        {
            player.audioDeviceIOCallbackWithContext(inputs, numInputs, outputs, numOutputs, numSamples, context);
            for (int channel = 0; channel < numOutputs; ++channel)
                juce::FloatVectorOperations::clear(outputs[channel], numSamples);
            return;
        }
        // With no inputs the player hands the plugin silence on its input channels.
        player.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, numOutputs, numSamples, context);
        if (numOutputs <= 0)
            return;
        // Left and right averaged, in pieces so the copy stays on the stack.
        std::array<float, 256> mono {};
        for (int start = 0; start < numSamples; start += (int) mono.size())
        {
            const int count = std::min((int) mono.size(), numSamples - start);
            for (int i = 0; i < count; ++i)
            {
                float sum = 0.0f;
                for (int channel = 0; channel < numOutputs; ++channel)
                    sum += outputs[channel][start + i];
                mono[(size_t) i] = sum / (float) numOutputs;
            }
            scope.write(mono.data(), count);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override { player.audioDeviceAboutToStart(device); }
    void audioDeviceStopped() override { player.audioDeviceStopped(); }
    void audioDeviceError(const juce::String& message) override { player.audioDeviceError(message); }
};

/**
 * The plugin's editor at the top of the room it gets, scaled with its shape kept and following when it resizes itself.
 * The editors' own keyboards are too small for a finger and sit wherever the editor ends, so the frame shows only what
 * lies above one (cut off by `window`, which holds the editor); the app's keyboard below takes its place.
 */
class EditorFrame : public juce::Component, private juce::ComponentListener
{
public:
    EditorFrame() { addAndMakeVisible(window); }

    void show(juce::AudioProcessorEditor* editorToShow)
    {
        if (editor != nullptr)
            editor->removeComponentListener(this);
        editor.reset(editorToShow);
        if (editor != nullptr)
        {
            window.addAndMakeVisible(*editor);
            editor->addComponentListener(this);
        }
        fit();
    }

    void paint(juce::Graphics& g) override { g.fillAll(colours::background); }
    void resized() override { fit(); }
    ~EditorFrame() override { show(nullptr); }

    /**
     * The phone's layout: an editor with pages shows them, and without its header, since the app has the sounds and
     * the language itself there.
     */
    void setBare(bool on)
    {
        bare = on;
        fit();
    }

    void languageChanged()
    {
        if (auto* host = dynamic_cast<tonwerkui::HostFrame*>(editor.get()))
            host->languageChanged();
    }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            fit();
    }

    /** The editor's height down to its own keyboard, if it draws one in its lower half. */
    int shownHeight() const
    {
        if (auto* keys = findKeyboard(editor.get()); keys != nullptr && keys != editor.get())
        {
            const auto top = editor->getLocalArea(keys, keys->getLocalBounds()).getY();
            // A little above, where the gap over the keyboard begins, so none of its edge shows.
            if (top > editor->getHeight() / 2)
                return top - tonwerkui::kGap / 2;
        }
        return editor->getHeight();
    }

    static const juce::Component* findKeyboard(const juce::Component* component)
    {
        if (component == nullptr)
            return nullptr;
        if (dynamic_cast<const juce::MidiKeyboardComponent*>(component) != nullptr)
            return component;
        for (auto* child : component->getChildren())
            if (auto* found = findKeyboard(child))
                return found;
        return nullptr;
    }

    void fit()
    {
        if (fitting || editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0 || getWidth() <= 0
            || getHeight() <= 0)
            return;
        const juce::ScopedValueSetter<bool> guard(fitting, true);
        // An editor with pages for small screens shows them unscaled in the phone's layout, and only there: by the
        // size of a knob alone an 11 inch iPad took Tonwerk Analog's pages too (its knobs are 37 points there), and
        // the iPad's editors stay as they are.
        if (auto* host = dynamic_cast<tonwerkui::HostFrame*>(editor.get()); host != nullptr && host->hasCompact())
        {
            const auto full = host->fullEditorSize();
            const bool compact = bare;
            host->setCompact(compact);
            host->setBare(compact);
            if (compact)
            {
                window.setBounds(getLocalBounds());
                editor->setTransform({});
                editor->setBounds(window.getLocalBounds());
                return;
            }
            editor->setSize(full.x, full.y);
        }
        const auto w = (float) editor->getWidth();
        const auto h = (float) shownHeight();
        const float scale = std::min((float) getWidth() / w, (float) getHeight() / h);
        // Centred across, at the top, so every instrument starts under the app's header.
        window.setBounds(juce::roundToInt(((float) getWidth() - w * scale) * 0.5f), 0, juce::roundToInt(w * scale),
                         juce::roundToInt(h * scale));
        editor->setTopLeftPosition(0, 0);
        editor->setTransform(juce::AffineTransform::scale(scale));
    }

    juce::Component window;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool fitting = false;
    bool bare = false;
};

/**
 * A button of the phone's rail: a sign instead of a word, since the rail is as narrow as a thumb and a word (Anzeige)
 * did not fit. Yellow while switched on.
 */
class IconButton : public juce::Button
{
public:
    enum class Icon { plugins, sounds, keys, wave, gear };

    explicit IconButton(Icon shown) : juce::Button({}), icon(shown) {}

    void paintButton(juce::Graphics& g, bool, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto colour = getToggleState() ? colours::yellow : colours::cyan;
        g.setColour(down ? colours::border : colours::panel);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(colour.withAlpha(getToggleState() ? 0.9f : 0.35f));
        g.drawRoundedRectangle(area, 6.0f, 1.0f);

        const float size = std::min(30.0f, std::min(area.getWidth(), area.getHeight()) - 12.0f);
        const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
        const float x = box.getX(), y = box.getY(), u = size / 10.0f;
        g.setColour(colour);
        juce::Path path;
        switch (icon)
        {
            case Icon::plugins:
                // Four tiles, as the chooser it opens shows the plugins.
                for (int i = 0; i < 4; ++i)
                    g.fillRoundedRectangle(x + (float) (i % 2) * 5.5f * u, y + (float) (i / 2) * 5.5f * u, 4.5f * u,
                                           4.5f * u, u);
                break;
            case Icon::sounds:
                // A list.
                for (int i = 0; i < 3; ++i)
                {
                    g.fillEllipse(x, y + (1.0f + (float) i * 3.5f) * u - u * 0.75f, 1.5f * u, 1.5f * u);
                    g.fillRoundedRectangle(x + 3.0f * u, y + (1.0f + (float) i * 3.5f) * u - u * 0.6f, 7.0f * u,
                                           1.2f * u, 0.6f * u);
                }
                break;
            case Icon::keys:
                // Four white keys and the three black ones between them.
                g.drawRoundedRectangle(box.reduced(0.0f, u), u, 1.5f);
                for (int i = 1; i < 4; ++i)
                {
                    g.fillRect(x + (float) i * 2.5f * u - 0.5f, y + 5.5f * u, 1.0f, 3.5f * u);
                    g.fillRect(x + (float) i * 2.5f * u - 0.8f * u, y + u, 1.6f * u, 4.8f * u);
                }
                break;
            case Icon::wave:
                path.startNewSubPath(x, y + 5.0f * u);
                for (int i = 1; i <= 40; ++i)
                {
                    const float t = (float) i / 40.0f;
                    path.lineTo(x + t * size,
                                y + 5.0f * u - std::sin(t * juce::MathConstants<float>::twoPi * 1.5f) * 3.5f * u);
                }
                g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
                break;
            case Icon::gear:
                path.addStar(box.getCentre(), 8, 3.4f * u, 5.0f * u);
                g.strokePath(path, juce::PathStrokeType(1.6f * u, juce::PathStrokeType::curved));
                g.fillPath(path);
                g.setColour(down ? colours::border : colours::panel);
                g.fillEllipse(box.withSizeKeepingCentre(3.4f * u, 3.4f * u));
                break;
        }
    }

private:
    Icon icon;
};

/**
 * The plugin's factory sounds (the host programs) as a list at the side, each line a finger high: a drop-down's
 * lines are too narrow on a phone, and a long one cannot be scrolled there.
 */
class SoundList : public juce::Component, private juce::ListBoxModel
{
public:
    SoundList()
    {
        list.setModel(this);
        list.setRowHeight(44);
        list.setColour(juce::ListBox::backgroundColourId, colours::panel);
        addAndMakeVisible(list);
    }

    void show(juce::AudioProcessor* shown)
    {
        processor = shown;
        list.updateContent();
        if (processor != nullptr)
            list.selectRow(processor->getCurrentProgram(), false, true);
        list.repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::panel);
        g.setColour(colours::cyan.withAlpha(0.5f));
        g.drawRect(getLocalBounds());
    }

    void resized() override { list.setBounds(getLocalBounds().reduced(1)); }

private:
    int getNumRows() override { return processor != nullptr ? processor->getNumPrograms() : 0; }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (processor == nullptr)
            return;
        if (selected)
        {
            g.setColour(colours::yellow.withAlpha(0.15f));
            g.fillRect(0, 0, width, height);
        }
        g.setColour(selected ? colours::yellow : colours::text);
        g.setFont(juce::Font(juce::FontOptions(17.0f)));
        g.drawText(processor->getProgramName(row), 14, 0, width - 20, height, juce::Justification::centredLeft);
        g.setColour(colours::border);
        g.fillRect(0, height - 1, width, 1);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (processor != nullptr)
            processor->setCurrentProgram(row);
    }

    juce::AudioProcessor* processor = nullptr;
    juce::ListBox list;
};

/** The instruments as large tiles over the app, for a finger rather than a drop-down's narrow lines. */
class Chooser : public juce::Component
{
public:
    std::function<void(int)> onChoose;

    Chooser()
    {
        for (const auto& entry : catalogue())
        {
            auto* tile = tiles.add(new juce::TextButton(entry.name));
            const int index = tiles.size() - 1;
            tile->onClick = [this, index] {
                setVisible(false);
                if (onChoose)
                    onChoose(index);
            };
            addAndMakeVisible(tile);
        }
    }

    void open(int current)
    {
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setColour(juce::TextButton::textColourOffId, i == current ? colours::yellow : colours::text);
        setVisible(true);
        toFront(false);
    }

    void paint(juce::Graphics& g) override { g.fillAll(colours::background.withAlpha(0.94f)); }

    // A tap beside the tiles closes it without a change.
    void mouseDown(const juce::MouseEvent&) override { setVisible(false); }

    void resized() override
    {
        constexpr int columns = 4;
        const int rows = (tiles.size() + columns - 1) / columns;
        auto area = getLocalBounds().reduced(tonwerkui::kMargin * 4);
        const int height = juce::jmin(110, area.getHeight() / juce::jmax(1, rows));
        area = area.withSizeKeepingCentre(area.getWidth(), height * rows);
        const int width = area.getWidth() / columns;
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds(juce::Rectangle<int>(area.getX() + (i % columns) * width,
                                                     area.getY() + (i / columns) * height, width, height)
                                    .reduced(tonwerkui::kGap));
    }

private:
    juce::OwnedArray<juce::TextButton> tiles;
};

/**
 * The input's device and channels, for Tonwerk Scope. iOS names every channel of an interface after the interface
 * ("Scarlett 8i6 USB" eight times), so there the channels are a list of their own, numbered; elsewhere JUCE's list
 * shows the drivers' names, and its input device menu, which a list of its own would hide.
 */
class InputChooser : public juce::Component, private juce::ChangeListener, private juce::ComponentListener
{
public:
#if JUCE_IOS
    static constexpr bool numbered = true;
#else
    static constexpr bool numbered = false;
#endif
    // The scope shows at most a stereo pair.
    static constexpr int maxChannels = 2;

    explicit InputChooser(juce::AudioDeviceManager& manager)
        : devices(manager),
          selector(manager, numbered ? 0 : 1, numbered ? 0 : maxChannels, 0, 0, false, false, false, false)
    {
        addAndMakeVisible(selector);
        selector.setSize(kWidth, 200);
        selector.addComponentListener(this);
        if (numbered)
        {
            heading.setText(tonwerkui::tr("Eingänge (höchstens zwei)"), juce::dontSendNotification);
            heading.setColour(juce::Label::textColourId, colours::text);
            addAndMakeVisible(heading);
            view.setViewedComponent(&list, false);
            view.setScrollBarsShown(true, false);
            addAndMakeVisible(view);
            devices.addChangeListener(this);
        }
        rebuild();
    }

    ~InputChooser() override
    {
        devices.removeChangeListener(this);
        selector.removeComponentListener(this);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        selector.setBounds(area.removeFromTop(selector.getHeight()));
        if (! numbered)
            return;
        area.reduce(tonwerkui::kMargin * 2, tonwerkui::kGap);
        heading.setBounds(area.removeFromTop(kRow));
        view.setBounds(area);
        list.setSize(view.getMaximumVisibleWidth(), channels.size() * kRow);
        for (int i = 0; i < channels.size(); ++i)
            channels[i]->setBounds(0, i * kRow, list.getWidth(), kRow);
    }

private:
    static constexpr int kWidth = 420;
    static constexpr int kRow = 30;
    static constexpr int kShownRows = 6;

    void rebuild()
    {
        channels.clear();
        if (numbered)
            if (auto* device = devices.getCurrentAudioDevice())
            {
                const auto names = device->getInputChannelNames();
                const auto active = device->getActiveInputChannels();
                juce::StringArray unique(names);
                unique.removeDuplicates(false);
                // A name of its own says more than a number; the same name on every channel says nothing.
                const bool named = unique.size() == names.size();
                for (int i = 0; i < names.size(); ++i)
                {
                    auto text = tonwerkui::tr("Eingang") + " " + juce::String(i + 1);
                    if (named)
                        text << utf8(" · ") << names[i];
                    auto* button = channels.add(new juce::ToggleButton(text));
                    button->setToggleState(active[i], juce::dontSendNotification);
                    button->onClick = [this, i] { toggle(i); };
                    list.addAndMakeVisible(button);
                }
            }
        layout();
    }

    void layout()
    {
        const int rows = juce::jlimit(1, kShownRows, channels.size());
        const int extra = numbered ? kRow * (rows + 1) + tonwerkui::kGap * 2 : 0;
        if (getWidth() == kWidth && getHeight() == selector.getHeight() + extra)
            resized();
        else
            setSize(kWidth, selector.getHeight() + extra);
    }

    /** Switches a channel on or off; a third one takes the place of the first of the two already on. */
    void toggle(int channel)
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
            return;
        auto bits = device->getActiveInputChannels();
        if (bits[channel])
            bits.clearBit(channel);
        else
        {
            if (bits.countNumberOfSetBits() >= maxChannels)
                bits.clearBit(bits.findNextSetBit(0));
            bits.setBit(channel);
        }
        auto setup = devices.getAudioDeviceSetup();
        setup.inputChannels = bits;
        setup.useDefaultInputChannels = false;
        devices.setAudioDeviceSetup(setup, true);
    }

    // The device changed (another one, or its channels): its list is read again.
    void changeListenerCallback(juce::ChangeBroadcaster*) override { rebuild(); }

    // The selector sets its own height as its rows come and go.
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            layout();
    }

    juce::AudioDeviceManager& devices;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label heading;
    juce::Component list;
    juce::Viewport view;
    juce::OwnedArray<juce::ToggleButton> channels;
};

class MainComponent : public juce::Component, private juce::MidiKeyboardState::Listener, private juce::Timer
{
public:
    MainComponent()
    {
        setLookAndFeel(&look);
        setOpaque(true);

        juce::PropertiesFile::Options options;
        options.applicationName = "Tonwerk";
        options.folderName = "Tonwerk";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile>(options);

        picker.onClick = [this] { chooser.open(current); };
        addAndMakeVisible(picker);
        railPicker.onClick = [this] { chooser.open(current); };
        addChildComponent(railPicker);

        // A large editor (Tonwerk Wavetable) gets the whole screen when the keys are put away.
        showKeyboard.setClickingTogglesState(true);
        showKeyboard.setToggleState(settings->getBoolValue("keyboard", true), juce::dontSendNotification);
        showKeyboard.onClick = [this] {
            settings->setValue("keyboard", showKeyboard.getToggleState());
            resized();
        };
        addAndMakeVisible(showKeyboard);
        railKeys.setClickingTogglesState(true);
        railKeys.onClick = [this] {
            phoneKeys = railKeys.getToggleState();
            phoneScope = phoneScope && roomy;
            resized();
        };
        addChildComponent(railKeys);

        // On a phone the strip under the editor shows the keys, the scope or nothing, chosen in a rail at the side.
        showScope.setClickingTogglesState(true);
        showScope.onClick = [this] {
            phoneScope = showScope.getToggleState();
            phoneKeys = phoneKeys && roomy;
            resized();
        };
        addChildComponent(showScope);

        input.onClick = [this] { chooseInput(); };
        addChildComponent(input);

        // A phone has no bar at the top: what stood there is in the rail, the rarely needed behind this button.
        setup.onClick = [this] { openSetup(); };
        addChildComponent(setup);
        showSounds.setClickingTogglesState(true);
        showSounds.onClick = [this] { resized(); };
        addChildComponent(showSounds);
        addChildComponent(sounds);

#if JUCE_IOS
        bluetooth.setButtonText("Bluetooth-MIDI");
        bluetooth.onClick = [] { juce::BluetoothMidiDevicePairingDialogue::open(); };
        addAndMakeVisible(bluetooth);
#endif

        keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, colours::text);
        keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, colours::panel);
        keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::yellow);
        keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::yellow.withAlpha(0.3f));
        keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, colours::border);
        keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        // The octave buttons beside it move it; its own arrows would be too small to hit.
        keyboard.setScrollButtonsVisible(false);
        addAndMakeVisible(keyboard);
        keys.addListener(this);

        octaveDown.setButtonText("-");
        octaveDown.onClick = [this] { shiftOctave(-1); };
        addAndMakeVisible(octaveDown);
        octaveUp.setButtonText("+");
        octaveUp.onClick = [this] { shiftOctave(1); };
        addAndMakeVisible(octaveUp);
        addAndMakeVisible(scope);
        // Asked with every frame: its name in the current language and, where the instrument says it, the note.
        scope.setCaptionSource([this] {
            juce::String caption = tonwerkui::tr("Oszilloskop");
            if (auto* notes = dynamic_cast<tonwerkui::ScopeNotes*>(processor.get()))
                caption << notes->scopeNote();
            return caption;
        });
        label();

        addAndMakeVisible(frame);
        chooser.onChoose = [this](int index) { choose(index); };
        addChildComponent(chooser);

        // The audio device opens once the window has drawn: when iPadOS's audio was stuck (Marcel's iPad, until a
        // restart), opening it hung, and inside the constructor that kept the screen black. Tonwerk Scope's input
        // opens after it (listen).
        juce::Timer::callAfterDelay(250, [safe = juce::Component::SafePointer<MainComponent>(this)] {
            if (safe == nullptr)
                return;
            safe->devices.initialise(0, 2, nullptr, true);
            safe->devices.addAudioCallback(&safe->audio);
            safe->devices.addMidiInputDeviceCallback({}, &safe->audio.player);
            safe->enableMidiInputs();
            safe->startTimer(2000);
            safe->audioOpen = true;
            if (safe->current >= 0)
                safe->listen(catalogue()[(size_t) safe->current].listens);
        });

        // TONWERK_PLUGIN=<name> opens that plugin, for the simulator's screenshots in CI.
        const auto& entries = catalogue();
        auto last = juce::SystemStats::getEnvironmentVariable("TONWERK_PLUGIN", {});
        if (last.isEmpty())
            last = settings->getValue("plugin", entries.front().name);
        int index = 0;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == last)
                index = (int) i;
        choose(index);

        setSize(1280, 800);

        // TONWERK_SCREENSHOT=<file> (CI's store pictures): once the plugin has drawn, the content laid out at
        // TONWERK_SCREENSHOT_SIZE points is written at twice that as a PNG, whatever screen the simulator or runner has
        // (a booted simulator stands upright, the runner's display is 1024 x 768).
        const auto shot = juce::SystemStats::getEnvironmentVariable("TONWERK_SCREENSHOT", {});
        if (shot.isNotEmpty())
            juce::Timer::callAfterDelay(8000, [safe = juce::Component::SafePointer<MainComponent>(this), shot] {
                if (safe != nullptr)
                    safe->writeScreenshot(shot);
            });
    }

    ~MainComponent() override
    {
        save();
        keys.removeListener(this);
        devices.removeMidiInputDeviceCallback({}, &audio.player);
        devices.removeAudioCallback(&audio);
        audio.player.setProcessor(nullptr);
        frame.show(nullptr);
        processor.reset();
        setLookAndFeel(nullptr);
    }

    /** A relative name lands in the app's Documents folder (inside the container on iOS and in the Mac's sandbox). */
    void writeScreenshot(const juce::String& name)
    {
        const auto size = juce::StringArray::fromTokens(
            juce::SystemStats::getEnvironmentVariable("TONWERK_SCREENSHOT_SIZE", "1280x800"), "x", {});
        // Out of its window, which would hold it to the screen's size; the app is done once the picture is written.
        if (auto* parent = getParentComponent())
            parent->removeChildComponent(this);
        setSize(size[0].getIntValue(), size[1].getIntValue());
        const auto image = createComponentSnapshot(getLocalBounds(), true, 2.0f);
        const auto file = juce::File::isAbsolutePath(name)
                              ? juce::File(name)
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name);
        file.deleteFile();
        juce::FileOutputStream out(file);
        if (out.openedOk())
            juce::PNGImageFormat().writeImageToStream(image, out);
        out.flush();
#if ! JUCE_IOS
        juce::JUCEApplicationBase::quit();
#endif
    }

    /** Keeps the plugin's sound and the choice; iOS may end a suspended app without asking. */
    void save()
    {
        if (processor != nullptr && current >= 0)
        {
            juce::MemoryBlock state;
            processor->getStateInformation(state);
            settings->setValue("state." + catalogue()[(size_t) current].name, state.toBase64Encoding());
            settings->setValue("plugin", catalogue()[(size_t) current].name);
        }
        settings->saveIfNeeded();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::background);
        if (! title.isEmpty())
            tonwerkui::paintTitle(g, title, "TONWERK");
        if (! keyboard.isVisible())
            return;
        g.setColour(colours::muted);
        g.setFont(tonwerkui::mono(13.0f));
        g.drawText(juce::MidiMessage::getMidiNoteName(keyboard.getLowestVisibleKey(), true, true, 3), octaveLabel,
                   juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        // The room iPadOS keeps free: the home bar at the bottom, the camera's edge.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
            area = display->safeAreaInsets.subtractedFrom(area);
        chooser.setBounds(area);

        // A phone in landscape: too low for a bar above the editor. An iPhone always, also one that unfolds to a
        // larger screen: the iPad's editors would be smaller there than a finger needs, the pages just grow.
        phone = tonwerkui::HostFrame::onPhone() || area.getHeight() - tonwerkui::kHeader < 520;
        if (phone)
        {
            // A phone's screen has round corners, and the unfolded one keeps no room free at its sides: without
            // this the rail's first and last button and the last key lost a corner to them.
            const auto bounds = getLocalBounds();
            area = area.getIntersection(bounds.reduced(kCorner, 0).withTrimmedTop(kGapAtEdge).withTrimmedBottom(kGapAtEdge));
        }
        // Keys and scope side by side under the editor only where the pages keep enough height above them.
        roomy = phone && area.getHeight() >= 500 && area.getWidth() >= 800;
        const bool listens = current >= 0 && catalogue()[(size_t) current].listens;
        setup.setVisible(phone);
        showSounds.setVisible(phone && processor != nullptr && processor->getNumPrograms() > 1);
        sounds.setVisible(showSounds.isVisible() && showSounds.getToggleState());
        frame.setBare(phone);
        input.setVisible(listens && ! phone);
        picker.setVisible(! phone);
        railPicker.setVisible(phone);
        showKeyboard.setVisible(plays && ! phone);
        railKeys.setVisible(plays && phone);
        showScope.setVisible(plays && phone);
#if JUCE_IOS
        bluetooth.setVisible(! phone);
#endif
        look.setPopupFontSize(phone ? 18.0f : 14.0f);
        if (current >= 0)
            picker.setButtonText(catalogue()[(size_t) current].name + utf8(" \xe2\x96\xbe"));
        if (phone)
        {
            title = {};
            layoutPhone(area);
            return;
        }

        auto bar = area.removeFromTop(tonwerkui::kHeader).reduced(tonwerkui::kMargin, 4);
        title = bar.removeFromLeft(150);
        picker.setBounds(bar.removeFromLeft(280));
        bar.removeFromLeft(tonwerkui::kGap);
        if (showKeyboard.isVisible())
            showKeyboard.setBounds(bar.removeFromLeft(90));
        if (input.isVisible())
            input.setBounds(bar.removeFromLeft(110));
        if (bluetooth.isVisible())
            bluetooth.setBounds(bar.removeFromRight(150));

        showKeyboard.setToggleState(settings->getBoolValue("keyboard", true), juce::dontSendNotification);

        const bool keysShown = showKeyboard.isVisible() && showKeyboard.getToggleState();
        for (auto* component : std::initializer_list<juce::Component*> { &keyboard, &octaveUp, &octaveDown, &scope })
            component->setVisible(keysShown);
        if (! keysShown)
        {
            frame.setBounds(area);
            repaint();
            return;
        }

        // Large enough to play with a finger, the same height for every instrument.
        auto keysArea = area.removeFromBottom(juce::jlimit(130, 220, area.getHeight() / 4));
        auto octave = keysArea.removeFromLeft(64).reduced(tonwerkui::kGap, 0);
        octaveUp.setBounds(octave.removeFromTop(octave.getHeight() * 2 / 5));
        octaveDown.setBounds(octave.removeFromBottom(octave.getHeight() * 2 / 3));
        octaveLabel = octave;
        // Every instrument gets the same scope; the editors' own ones are cut off with their keyboards.
        scope.setBounds(keysArea.removeFromRight(juce::jmin(320, keysArea.getWidth() / 4)).reduced(tonwerkui::kGap, 0));
        keysArea.removeFromRight(tonwerkui::kGap);
        keyboard.setBounds(keysArea);
        // Two octaves and a C across, large enough for a finger.
        keyboard.setKeyWidth((float) keysArea.getWidth() / 15.0f);
        showKeys();
        area.removeFromBottom(tonwerkui::kGap);
        frame.setBounds(area);
    }

private:
    /** The least room at a phone's sides, clear of its screen's round corners, and at its top and bottom. */
    static constexpr int kCorner = 14;
    static constexpr int kGapAtEdge = 6;

    /**
     * A phone's screen: no bar at the top, so the editor gets the height. A rail at the side, under the thumb, holds
     * the plugin's choice, the switches for the strip below the editor (the keys, the oscilloscope or nothing, which
     * is the start: on a phone a MIDI keyboard plays, and the knobs need the room) and the setup menu. The rail is on
     * the right, or on the left for a left hand.
     */
    void layoutPhone(juce::Rectangle<int> area)
    {
        // Both at once only on a roomy screen; elsewhere the keys win.
        const bool withKeys = plays && phoneKeys;
        const bool withScope = plays && phoneScope && (roomy || ! withKeys);
        railKeys.setToggleState(withKeys, juce::dontSendNotification);
        showScope.setToggleState(withScope, juce::dontSendNotification);

        const bool railLeft = settings->getBoolValue("railLeft", false);
        auto rail = railLeft ? area.removeFromLeft(kRail) : area.removeFromRight(kRail);
        rail = rail.reduced(tonwerkui::kGap / 2, tonwerkui::kGap);
        // The sounds come in over the editor from the side away from the rail, so the thumb at the rail hides none.
        if (sounds.isVisible())
        {
            sounds.setBounds(railLeft ? area.withLeft(area.getRight() - kSounds) : area.withWidth(kSounds));
            sounds.toFront(false);
        }
        std::vector<juce::Component*> buttons { &railPicker };
        if (showSounds.isVisible())
            buttons.push_back(&showSounds);
        if (plays)
            buttons.insert(buttons.end(), { &railKeys, &showScope });
        buttons.push_back(&setup);
        const int each = rail.getHeight() / (int) buttons.size();
        for (auto* button : buttons)
            button->setBounds(rail.removeFromTop(each).reduced(0, 3));

        for (auto* component : std::initializer_list<juce::Component*> { &keyboard, &octaveUp, &octaveDown })
            component->setVisible(withKeys);
        scope.setVisible(withScope);
        if (withKeys || withScope)
        {
            auto below = area.removeFromBottom(kPhoneStrip);
            area.removeFromBottom(tonwerkui::kGap);
            if (withScope)
            {
                scope.setBounds(withKeys ? below.removeFromRight(below.getWidth() / 3) : below);
                if (withKeys)
                    below.removeFromRight(tonwerkui::kGap);
            }
            if (withKeys)
            {
                auto octave = below.removeFromLeft(56).reduced(tonwerkui::kGap, 0);
                octaveUp.setBounds(octave.removeFromTop(octave.getHeight() * 2 / 5));
                octaveDown.setBounds(octave.removeFromBottom(octave.getHeight() * 2 / 3));
                octaveLabel = octave;
                keyboard.setBounds(below);
                keyboard.setKeyWidth((float) below.getWidth() / 15.0f);
                showKeys();
            }
        }
        frame.setBounds(area);
        repaint();
    }

    void choose(int index)
    {
        if (index < 0 || index >= (int) catalogue().size() || index == current)
            return;
        save();
        audio.player.setProcessor(nullptr);
        frame.show(nullptr);
        sounds.show(nullptr);
        processor.reset();
        keys.allNotesOff(0);

        current = index;
        const auto& chosen = catalogue()[(size_t) index];
        processor = chosen.make();
        const auto saved = settings->getValue("state." + chosen.name);
        if (saved.isNotEmpty())
        {
            juce::MemoryBlock state;
            if (state.fromBase64Encoding(saved))
                processor->setStateInformation(state.getData(), (int) state.getSize());
        }
        audio.player.setProcessor(processor.get());
        frame.show(processor->hasEditor() ? processor->createEditorIfNeeded() : nullptr);
        sounds.show(processor.get());

        picker.setButtonText(chosen.name + utf8(" \xe2\x96\xbe"));
        plays = ! chosen.listens;
        input.setVisible(chosen.listens);
        listen(chosen.listens);
        // Each instrument keeps where its keyboard sat; the drums start on General MIDI's kick, C1.
        lowestKey = settings->getIntValue("lowestKey." + chosen.name, 36);
        settings->setValue("plugin", chosen.name);
        resized();
        repaint();
    }

    void shiftOctave(int octaves)
    {
        // The top of two octaves stays within MIDI's notes.
        lowestKey = juce::jlimit(0, 108, lowestKey + 12 * octaves);
        showKeys();
        if (current >= 0)
            settings->setValue("lowestKey." + catalogue()[(size_t) current].name, lowestKey);
        repaint(octaveLabel);
    }

    void listen(bool on)
    {
        if (! on)
        {
            audio.listening = false;
            return;
        }
        // Not before the device is open (it opens once the window has drawn, and calls this then).
        if (! audioOpen)
            return;
        juce::Component::SafePointer<MainComponent> self(this);
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [self](bool granted) {
            if (self == nullptr || ! granted)
                return;
            // Asked for only now, so an instrument never makes iPadOS ask for the microphone.
            if (! self->inputsOpen)
            {
                self->devices.initialise(2, 2, nullptr, true);
                self->inputsOpen = true;
            }
            self->audio.listening = true;
        });
    }

    /** The device and its input channels: the iPad's microphone or a plugged-in interface's inputs. */
    void chooseInput()
    {
        juce::CallOutBox::launchAsynchronously(std::make_unique<InputChooser>(devices),
                                               phone ? setup.getBounds() : input.getBounds(), this);
    }

    /** The phone's setup menu: what the bar at the top holds on a larger screen, and the rail's side. */
    void openSetup()
    {
        juce::PopupMenu menu;
#if JUCE_IOS
        menu.addItem("Bluetooth-MIDI", [] { juce::BluetoothMidiDevicePairingDialogue::open(); });
#endif
        if (current >= 0 && catalogue()[(size_t) current].listens)
            menu.addItem(tonwerkui::tr("Eingang"), [this] { chooseInput(); });
        // The language for every plugin; the editors' own switch is gone with their header.
        const auto speak = [this](tonwerkui::Language chosen) {
            tonwerkui::chooseLanguage(chosen);
            label();
            frame.languageChanged();
        };
        const bool german = tonwerkui::language() == tonwerkui::Language::german;
        menu.addItem("Deutsch", true, german, [speak] { speak(tonwerkui::Language::german); });
        menu.addItem("English", true, ! german, [speak] { speak(tonwerkui::Language::english); });
        menu.addSeparator();
        const bool left = settings->getBoolValue("railLeft", false);
        menu.addItem(tonwerkui::tr("Leiste links"), true, left, [this, left] {
            settings->setValue("railLeft", ! left);
            resized();
        });
        // Lines a finger high.
        menu.setLookAndFeel(&look);
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(&setup)
                               .withParentComponent(this)
                               .withStandardItemHeight(44));
    }

    /** Without its scroll buttons the keyboard always starts at its range's bottom, so the range is what it shows. */
    void showKeys()
    {
        keyboard.setAvailableRange(lowestKey, juce::jmin(127, lowestKey + 24));
        keyboard.setLowestVisibleKey(lowestKey);
    }

    void enableMidiInputs()
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (! devices.isMidiInputDeviceEnabled(device.identifier))
                devices.setMidiInputDeviceEnabled(device.identifier, true);
    }

    // A keyboard plugged in or paired later plays too.
    /** The app's own texts; the plugin's frame changes the language, the app follows it here. */
    void label()
    {
        shownLanguage = tonwerkui::language();
        showKeyboard.setButtonText(tonwerkui::tr("Tasten"));
        input.setButtonText(tonwerkui::tr("Eingang"));
        scope.repaint();
    }

    void timerCallback() override
    {
        enableMidiInputs();
        if (tonwerkui::language() != shownLanguage)
            label();
    }

    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override
    {
        auto message = juce::MidiMessage::noteOn(channel, note, velocity);
        message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        audio.player.getMidiMessageCollector().addMessageToQueue(message);
    }

    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override
    {
        auto message = juce::MidiMessage::noteOff(channel, note, velocity);
        message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        audio.player.getMidiMessageCollector().addMessageToQueue(message);
    }

    tonwerkui::LookAndFeel look;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::AudioDeviceManager devices;
    Player audio;
    std::unique_ptr<juce::AudioProcessor> processor;
    int current = -1;
    int lowestKey = 36;
    bool audioOpen = false;
    tonwerkui::Language shownLanguage = tonwerkui::Language::german;
    bool inputsOpen = false;

    juce::Rectangle<int> title, octaveLabel;
    static constexpr int kRail = 64;
    static constexpr int kSounds = 280;
    static constexpr int kPhoneStrip = 100;
    bool phone = false;
    bool phoneKeys = false, phoneScope = false;
    bool roomy = false;
    juce::TextButton picker, showKeyboard, input;
    IconButton railPicker { IconButton::Icon::plugins }, showSounds { IconButton::Icon::sounds },
        railKeys { IconButton::Icon::keys }, showScope { IconButton::Icon::wave }, setup { IconButton::Icon::gear };
    // Whether the plugin is played (an instrument) rather than listening to the input (Tonwerk Scope).
    bool plays = true;
    juce::TextButton bluetooth;
    juce::MidiKeyboardState keys;
    juce::MidiKeyboardComponent keyboard { keys, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton octaveDown, octaveUp;
    tonwerkui::ScopeView scope { audio.scope };
    EditorFrame frame;
    SoundList sounds;
    Chooser chooser;
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : juce::DocumentWindow("Tonwerk", colours::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
#if JUCE_IOS || JUCE_ANDROID
        setFullScreen(true);
#else
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
#endif
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    void save()
    {
        if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
            main->save();
    }
};

class TonwerkApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Tonwerk"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
#if JUCE_IOS
        // The scope beside the keys is every instrument's: an editor with its own above the knobs showed two. On
        // the Mac the app stays as it was for now (Marcel, 2026-11-19).
        tonwerkui::editorsHaveScope() = false;
#endif
        window = std::make_unique<MainWindow>();
    }
    void shutdown() override { window.reset(); }
    void suspended() override
    {
        if (window != nullptr)
            window->save();
    }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace
} // namespace tonwerkapp

START_JUCE_APPLICATION(tonwerkapp::TonwerkApplication)
