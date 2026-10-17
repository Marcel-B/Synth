#include "Catalogue.h"
#include "NightCity.h"
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
        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0 || getWidth() <= 0
            || getHeight() <= 0)
            return;
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

        // A large editor (Tonwerk Wavetable) gets the whole screen when the keys are put away.
        showKeyboard.setButtonText("Tasten");
        showKeyboard.setClickingTogglesState(true);
        showKeyboard.setToggleState(settings->getBoolValue("keyboard", true), juce::dontSendNotification);
        showKeyboard.onClick = [this] {
            settings->setValue("keyboard", showKeyboard.getToggleState());
            resized();
        };
        addAndMakeVisible(showKeyboard);

        input.setButtonText("Eingang");
        input.onClick = [this] { chooseInput(); };
        addChildComponent(input);

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
        scope.setCaption("Oszilloskop");
        addAndMakeVisible(scope);

        addAndMakeVisible(frame);
        chooser.onChoose = [this](int index) { choose(index); };
        addChildComponent(chooser);

        devices.initialise(0, 2, nullptr, true);
        devices.addAudioCallback(&audio);
        devices.addMidiInputDeviceCallback({}, &audio.player);
        enableMidiInputs();
        startTimer(2000);

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
    void choose(int index)
    {
        if (index < 0 || index >= (int) catalogue().size() || index == current)
            return;
        save();
        audio.player.setProcessor(nullptr);
        frame.show(nullptr);
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

        picker.setButtonText(chosen.name + utf8(" \xe2\x96\xbe"));
        showKeyboard.setVisible(! chosen.listens);
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
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(devices, 1, 2, 0, 0, false, false, false,
                                                                             false);
        selector->setSize(420, 260);
        juce::CallOutBox::launchAsynchronously(std::move(selector), input.getBounds(), this);
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
    void timerCallback() override { enableMidiInputs(); }

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
    bool inputsOpen = false;

    juce::Rectangle<int> title, octaveLabel;
    juce::TextButton picker, showKeyboard, input;
    juce::TextButton bluetooth;
    juce::MidiKeyboardState keys;
    juce::MidiKeyboardComponent keyboard { keys, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton octaveDown, octaveUp;
    tonwerkui::ScopeView scope { audio.scope };
    EditorFrame frame;
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

    void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(); }
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
