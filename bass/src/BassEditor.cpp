#include "BassEditor.h"
#include "Language.h"

#include "BassPresets.h"
#include "PresetCategories.h"

namespace tonwerkbass
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kScope = 120;
constexpr int kKeyboard = 56;
} // namespace

BassEditor::BassEditor(BassProcessor& p)
    : AudioProcessorEditor(p),
      synth(p),
      scope(p.scope),
      oscillator(p.state, "Oszillator",
                 { { { "wave", "Welle" }, { "tune", "Tune" }, { "fine", "Fein" }, { "sub", "Sub" } } }),
      filter(p.state, "Filter",
             { { { "cutoff", "Cutoff" }, { "resonance", "Resonanz" }, { "envMod", "Env Mod" }, { "decay", "Decay" },
                 { "keytrack", "Keytrack" } } }),
      accent(p.state, "Accent",
             { { { "accent", "Accent" }, { "accentThreshold", "Ab Anschlag" }, { "sweep", "Sweep" } } }),
      envelope(p.state, "Slide & Ausklang",
               { { { "slide", "Slide" }, { "ampDecay", "Ausklang" }, { "release", "Release" } } }),
      output(p.state, "Ausgang", { { { "drive", "Drive" }, { "bendRange", "Bend" }, { "master", "Master" } } }),
      lfo(p.state, "LFO",
          { { { "lfoShape", "Form" }, { "lfoSync", "Sync" }, { "lfoRate", "Rate", 0, "lfoSync", false },
              { "lfoDivision", "Raster", 0, "lfoSync", true }, { "lfoTarget", "Ziel" }, { "lfoAmount", "Menge" } } }),
      effects(p.state, "Effekte",
              { { { "fxDelayMix", "Delay" },
                  { "fxDelaySync", "Sync" },
                  { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                  { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true },
                  { "fxDelayFeedback", "Feedback" },
                  { "fxDelayTone", "Ton" },
                  { "fxReverbMix", "Hall" },
                  { "fxReverbDecay", tonwerkui::tr("Länge") } } }),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);
    // Grouped under headings; an item's id stays its program number + 1.
    const auto& factory = factoryPresets();
    menu = menuOrder((int) factory.size(), [&factory](int i) { return factory[(std::size_t) i].category; });
    int group = -1;
    for (const int i : menu)
    {
        const auto& preset = factory[(std::size_t) i];
        const int category = categoryIndex(preset.category);
        if (category != group && category >= 0)
            presets.addSectionHeading(tonwerkui::tr(preset.category));
        group = category;
        presets.addItem(utf8(preset.name), i + 1);
    }
    presets.setTextWhenNothingSelected(tonwerkui::tr("Preset"));
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            choosePreset(presets.getSelectedId() - 1);
    };
    auto step = [this](int direction) {
        const auto at = std::find(menu.begin(), menu.end(), synth.getCurrentProgram());
        const auto from = at == menu.end() ? -1 : (int) std::distance(menu.begin(), at);
        const int to = from + direction;
        if (juce::isPositiveAndBelow(to, (int) menu.size()))
            choosePreset(menu[(std::size_t) to]);
    };
    previous.onClick = [step] { step(-1); };
    next.onClick = [step] { step(1); };
    scope.setCaption("OSZILLOSKOP");
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &scope, &oscillator, &filter,
                                                            &accent, &envelope, &output, &lfo, &effects, &keyboard })
        addAndMakeVisible(*c);

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    // The bass's own range, C0 to C4.
    keyboard.setAvailableRange(24, 72);
    keyboard.setLowestVisibleKey(24);

    // As wide as the widest row needs.
    auto rowWidth = [](std::initializer_list<const Section*> row) {
        int width = kGap * ((int) row.size() - 1);
        for (const auto* s : row)
            width += s->preferredWidth();
        return width;
    };
    const int width = 2 * kMargin
                      + std::max({ rowWidth({ &oscillator, &filter }), rowWidth({ &accent, &envelope, &output }),
                                   rowWidth({ &lfo, &effects }) });
    const int height = kMargin * 2 + kHeader + kScope + oscillator.preferredHeight() + accent.preferredHeight()
                       + lfo.preferredHeight() + kKeyboard + 4 * kGap;
    setSize(width, height);
    timerCallback();
    startTimerHz(30);
}

BassEditor::~BassEditor() { setLookAndFeel(nullptr); }

void BassEditor::choosePreset(int index)
{
    if (! juce::isPositiveAndBelow(index, synth.getNumPrograms()))
        return;
    synth.setCurrentProgram(index);
    presets.setSelectedId(index + 1, juce::dontSendNotification);
}

void BassEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK BASS");
}

void BassEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    next.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(4);
    previous.setBounds(header.removeFromRight(28));

    keyboard.setBounds(area.removeFromBottom(kKeyboard));
    keyboard.setKeyWidth((float) keyboard.getWidth() / 28.0f);
    area.removeFromBottom(kGap);

    scope.setBounds(area.removeFromTop(kScope));
    area.removeFromTop(kGap);
    layoutRow(area, { &oscillator, &filter });
    layoutRow(area, { &accent, &envelope, &output });
    layoutRow(area, { &lfo, &effects });
}

void BassEditor::timerCallback()
{
    juce::String caption = tonwerkui::tr("OSZILLOSKOP");
    if (const int note = synth.shownNote.load(); note >= 0)
    {
        caption << tonwerkui::tr("  ·  ") << BassProcessor::noteName(note);
        if (synth.shownAccent.load())
            caption << "  ACCENT";
        if (synth.shownSlide.load())
            caption << "  SLIDE";
    }
    if (caption != shownCaption)
    {
        shownCaption = caption;
        scope.setCaption(caption);
    }
    if (synth.presetName() != shownPreset)
    {
        shownPreset = synth.presetName();
        presets.setSelectedId(0, juce::dontSendNotification);
        for (int i = 0; i < presets.getNumItems(); ++i)
            if (presets.getItemText(i) == shownPreset)
                presets.setSelectedId(presets.getItemId(i), juce::dontSendNotification);
        if (presets.getSelectedId() == 0)
            presets.setText(shownPreset, juce::dontSendNotification);
    }
}
} // namespace tonwerkbass
