#include "Catalogue.h"
#include "NightCity.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

/**
 * Tonwerk for the iPad: one app with every plugin in it. Logic Pro for iPad, GarageBand and AUM load the plugins as the
 * AUv3s it carries; on its own the app plays one of them at a time, with the on-screen keyboard, a MIDI keyboard (USB
 * or Bluetooth) and, for the effects, the microphone or the audio interface's inputs. It builds on the desktop too
 * (-DTONWERK_APP=ON), to try it without an iPad.
 */
namespace tonwerkapp
{
namespace
{
using tonwerkui::utf8;
namespace colours = tonwerkui::colours;

/** The device's audio into the plugin, with the input left out until the user turns it on (feedback otherwise). */
class Player : public juce::AudioIODeviceCallback
{
public:
    juce::AudioProcessorPlayer player;
    std::atomic<bool> inputOn { false };

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override
    {
        // With no inputs the player hands the plugin silence on its input channels.
        if (inputOn.load(std::memory_order_relaxed))
            player.audioDeviceIOCallbackWithContext(inputs, numInputs, outputs, numOutputs, numSamples, context);
        else
            player.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, numOutputs, numSamples, context);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override { player.audioDeviceAboutToStart(device); }
    void audioDeviceStopped() override { player.audioDeviceStopped(); }
    void audioDeviceError(const juce::String& message) override { player.audioDeviceError(message); }
};

/** The plugin's editor, scaled to the room it gets with its shape kept, following when it resizes itself. */
class EditorFrame : public juce::Component, private juce::ComponentListener
{
public:
    void show(juce::AudioProcessorEditor* editorToShow)
    {
        if (editor != nullptr)
            editor->removeComponentListener(this);
        editor.reset(editorToShow);
        if (editor != nullptr)
        {
            addAndMakeVisible(*editor);
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

    void fit()
    {
        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0 || getWidth() <= 0
            || getHeight() <= 0)
            return;
        const auto w = (float) editor->getWidth();
        const auto h = (float) editor->getHeight();
        const float scale = std::min((float) getWidth() / w, (float) getHeight() / h);
        editor->setTopLeftPosition(0, 0);
        editor->setTransform(juce::AffineTransform::scale(scale).translated(((float) getWidth() - w * scale) * 0.5f,
                                                                            ((float) getHeight() - h * scale) * 0.5f));
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
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

        juce::PopupMenu menu;
        menu.addSectionHeader("Instrumente");
        const auto& entries = catalogue();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0 && entries[i - 1].instrument && ! entries[i].instrument)
                menu.addSectionHeader("Effekte");
            menu.addItem((int) i + 1, entries[i].name);
        }
        *picker.getRootMenu() = menu;
        picker.onChange = [this] { choose(picker.getSelectedId() - 1); };
        addAndMakeVisible(picker);

        input.setButtonText("Eingang");
        input.setTooltip(utf8("Mikrofon oder Eingänge des Audio-Interfaces ins Plugin (mit Kopfhörern, sonst pfeift es)"));
        input.onClick = [this] { setInput(input.getToggleState()); };
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
        addChildComponent(keyboard);
        keys.addListener(this);

        addAndMakeVisible(frame);

        devices.initialise(0, 2, nullptr, true);
        devices.addAudioCallback(&audio);
        devices.addMidiInputDeviceCallback({}, &audio.player);
        enableMidiInputs();
        startTimer(2000);

        // TONWERK_PLUGIN=<name> opens that plugin, for the simulator's screenshots in CI.
        auto last = juce::SystemStats::getEnvironmentVariable("TONWERK_PLUGIN", {});
        if (last.isEmpty())
            last = settings->getValue("plugin", entries.front().name);
        int index = 0;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == last)
                index = (int) i;
        picker.setSelectedId(index + 1, juce::sendNotificationSync);

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
    }

    void resized() override
    {
        auto area = getLocalBounds();
        // The room iPadOS keeps free: the home bar at the bottom, the camera's edge.
        if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
            area = display->safeAreaInsets.subtractedFrom(area);

        auto bar = area.removeFromTop(tonwerkui::kHeader).reduced(tonwerkui::kMargin, 4);
        title = bar.removeFromLeft(150);
        picker.setBounds(bar.removeFromLeft(240));
        bar.removeFromLeft(tonwerkui::kGap);
        if (input.isVisible())
            input.setBounds(bar.removeFromLeft(110));
        if (bluetooth.isVisible())
            bluetooth.setBounds(bar.removeFromRight(150));

        if (keyboard.isVisible())
        {
            auto keysArea = area.removeFromBottom(juce::jlimit(70, 130, area.getHeight() / 6));
            keyboard.setBounds(keysArea);
            keyboard.setKeyWidth(juce::jmax(16.0f, (float) keysArea.getWidth() / 36.0f));
            keyboard.setLowestVisibleKey(36);
        }
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

        // Most instruments draw their own keyboard; the app's is for those that do not and for Robovox and Vocoder.
        keyboard.setVisible(processor->acceptsMidi() && ! hasOwnKeyboard(processor->getActiveEditor()));
        const bool hearsInput = processor->getTotalNumInputChannels() > 0;
        input.setVisible(hearsInput);
        if (! hearsInput && input.getToggleState())
            input.setToggleState(false, juce::sendNotificationSync);
        settings->setValue("plugin", chosen.name);
        resized();
    }

    static bool hasOwnKeyboard(const juce::Component* component)
    {
        if (component == nullptr)
            return false;
        if (dynamic_cast<const juce::MidiKeyboardComponent*>(component) != nullptr)
            return true;
        for (auto* child : component->getChildren())
            if (hasOwnKeyboard(child))
                return true;
        return false;
    }

    void setInput(bool on)
    {
        if (! on)
        {
            audio.inputOn = false;
            return;
        }
        juce::Component::SafePointer<MainComponent> self(this);
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [self](bool granted) {
            if (self == nullptr)
                return;
            if (! granted)
            {
                self->input.setToggleState(false, juce::dontSendNotification);
                return;
            }
            // Asked for only now, so an instrument never makes iPadOS ask for the microphone.
            if (! self->inputsOpen)
            {
                self->devices.initialise(2, 2, nullptr, true);
                self->inputsOpen = true;
            }
            self->audio.inputOn = true;
        });
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
    bool inputsOpen = false;

    juce::Rectangle<int> title;
    juce::ComboBox picker;
    juce::ToggleButton input;
    juce::TextButton bluetooth;
    juce::MidiKeyboardState keys;
    juce::MidiKeyboardComponent keyboard { keys, juce::MidiKeyboardComponent::horizontalKeyboard };
    EditorFrame frame;
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
