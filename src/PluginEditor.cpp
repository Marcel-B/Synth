#include "PluginEditor.h"

namespace tonwerk
{
namespace
{
const juce::Colour kBackground { 0xff17191c };
const juce::Colour kPanel { 0xff22262b };
const juce::Colour kBorder { 0xff343a41 };
const juce::Colour kText { 0xffe6e8eb };
const juce::Colour kMuted { 0xff9aa3ad };
/** Tonwerk's accent, Aura's green. */
const juce::Colour kAccent { 0xff10b981 };
const juce::Colour kError { 0xfff87171 };

constexpr int kMargin = 12;
constexpr int kSlot = 56;
constexpr int kSectionPadding = 8;
constexpr int kSectionTitle = 20;
constexpr int kRowHeight = 112;
constexpr int kGap = 8;
constexpr int kTopBar = 32;
constexpr int kMessage = 20;
constexpr int kKeyboard = 72;
constexpr int kScopeWidth = 300;
constexpr int kWidth = 1272;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }
} // namespace

TonwerkLookAndFeel::TonwerkLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kMuted);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kBorder);
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::ComboBox::backgroundColourId, kPanel.brighter(0.05f));
    setColour(juce::ComboBox::outlineColourId, kBorder);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::arrowColourId, kMuted);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.35f));
    setColour(juce::PopupMenu::headerTextColourId, kMuted);
    setColour(juce::TextButton::buttonColourId, kPanel.brighter(0.05f));
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, kMuted);
    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::AlertWindow::backgroundColourId, kPanel);
    setColour(juce::AlertWindow::textColourId, kText);
    setColour(juce::TextEditor::backgroundColourId, kBackground);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kBorder);
    setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
}

void TonwerkLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float line = 3.0f;
    const float arcRadius = radius - line / 2.0f;
    const float angle = start + position * (end - start);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Ranges around zero (detune, envelope amounts) fill from the middle, so the knob shows which way they go.
    const auto& range = slider.getNormalisableRange();
    const bool bipolar = range.start < 0.0 && range.end > 0.0;
    const float from = bipolar ? start + (float) range.convertTo0to1(0.0) * (end - start) : start;
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, angle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(value, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius * 0.62f;
    g.setColour(kPanel.brighter(0.12f));
    g.fillEllipse(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
    const juce::Point<float> tip = centre.getPointOnCircumference(inner * 0.85f, angle);
    g.setColour(kText);
    g.drawLine({ centre.getPointOnCircumference(inner * 0.3f, angle), tip }, 2.0f);
}

Control::Control(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& label, int slotCount)
    : width(slotCount)
{
    name.setText(label, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setFont(juce::FontOptions(12.0f));
    name.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(name);

    const auto* parameter = state.getParameter(parameterId);
    jassert(parameter != nullptr);
    if (auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(parameter))
    {
        menu = std::make_unique<juce::ComboBox>();
        menu->addItemList(choice->choices, 1);
        addAndMakeVisible(*menu);
        menuAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, parameterId, *menu);
    }
    else if (dynamic_cast<const juce::AudioParameterBool*>(parameter) != nullptr)
    {
        toggle = std::make_unique<juce::ToggleButton>();
        addAndMakeVisible(*toggle);
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, parameterId, *toggle);
    }
    else
    {
        slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kSlot, 16);
        // The text box is made before this control has a parent, so it cannot take these from the editor's look.
        slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour(juce::Slider::textBoxTextColourId, kMuted);
        slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(*slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, *slider);
        // Two decimals are enough for every knob; Hz and cents read better whole.
        const auto unit = parameter->getLabel();
        const int decimals = unit == "Hz" || unit == "ct" ? 0 : 2;
        slider->textFromValueFunction = [decimals, unit](double value) {
            return juce::String(value, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
        };
        slider->updateText();
    }
}

void Control::resized()
{
    auto area = getLocalBounds();
    name.setBounds(area.removeFromTop(16));
    if (slider)
        slider->setBounds(area);
    if (menu)
        menu->setBounds(area.withSizeKeepingCentre(area.getWidth() - 4, 24).withY(area.getY() + 14));
    if (toggle)
        toggle->setBounds(area.withSizeKeepingCentre(26, 26).withY(area.getY() + 12));
}

Section::Section(juce::AudioProcessorValueTreeState& state, const juce::String& sectionTitle, std::initializer_list<Item> list)
    : title(sectionTitle), items(list)
{
    for (const auto& item : items)
    {
        controls.push_back(std::make_unique<Control>(state, item.id, item.label, item.slots));
        addAndMakeVisible(*controls.back());
    }
    juce::StringArray switchIds;
    for (const auto& item : items)
        if (item.when.isNotEmpty())
            switchIds.addIfNotAlreadyThere(item.when);
    for (const auto& id : switchIds)
    {
        auto* parameter = state.getParameter(id);
        jassert(parameter != nullptr);
        switches.push_back(std::make_unique<juce::ParameterAttachment>(*parameter, [this, id](float value) {
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].when == id)
                    controls[i]->setVisible((value > 0.5f) == items[i].whenOn);
        }));
        switches.back()->sendInitialUpdate();
    }
}

std::vector<std::pair<int, int>> Section::places() const
{
    std::vector<std::pair<int, int>> result;
    int at = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const bool sharesPlace = i > 0 && items[i].when.isNotEmpty() && items[i].when == items[i - 1].when
                                 && items[i].whenOn != items[i - 1].whenOn;
        if (sharesPlace)
        {
            auto& previous = result.back();
            const int width = std::max(previous.second, items[i].slots);
            at += width - previous.second;
            previous.second = width;
            result.push_back(previous);
        }
        else
        {
            result.push_back({ at, items[i].slots });
            at += items[i].slots;
        }
    }
    return result;
}

int Section::preferredWidth() const
{
    int slots = 0;
    for (const auto& [at, width] : places())
        slots = std::max(slots, at + width);
    return slots * kSlot + 2 * kSectionPadding;
}

void Section::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(title, getLocalBounds().reduced(kSectionPadding, 0).removeFromTop(kSectionTitle),
               juce::Justification::centredLeft);
}

void Section::resized()
{
    auto area = getLocalBounds().reduced(kSectionPadding, 0);
    area.removeFromTop(kSectionTitle);
    area.removeFromBottom(kSectionPadding);
    const auto where = places();
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setBounds(area.getX() + where[i].first * kSlot, area.getY(), where[i].second * kSlot, area.getHeight());
}

ScopeView::ScopeView(const ScopeBuffer& buffer) : source(buffer) { startTimerHz(30); }

void ScopeView::timerCallback()
{
    if (isShowing())
        refresh();
}

void ScopeView::refresh()
{
    source.read(samples.data(), (int) samples.size());
    repaint();
}

void ScopeView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    const auto area = bounds.reduced(6.0f);
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());

    // Half the samples are shown; the trigger is looked for in the first half, so the view always has enough after it.
    const int length = (int) samples.size();
    const int shown = length / 2;
    int start = 0;
    for (int i = 1; i < length - shown; ++i)
    {
        if (samples[(size_t) i - 1] <= 0.0f && samples[(size_t) i] > 0.0f)
        {
            start = i;
            break;
        }
    }
    float peak = 0.0f;
    for (float sample : samples)
        peak = std::max(peak, std::abs(sample));
    // Quiet sounds are scaled up to be seen, but not a hum of noise to full height.
    if (peak <= 0.02f)
        return;
    const float scale = 0.9f / peak;
    juce::Path trace;
    const int width = juce::roundToInt(area.getWidth());
    for (int x = 0; x <= width; ++x)
    {
        const float sample = samples[(size_t) (start + std::min(shown - 1, x * shown / std::max(1, width)))] * scale;
        const float y = area.getCentreY() - sample * area.getHeight() / 2.0f;
        if (x == 0)
            trace.startNewSubPath(area.getX(), y);
        else
            trace.lineTo(area.getX() + (float) x, y);
    }
    g.setColour(kAccent);
    g.strokePath(trace, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

Section& TonwerkSynthEditor::add(std::vector<std::unique_ptr<Section>>& owner, Section* section)
{
    owner.emplace_back(section);
    addChildComponent(*section);
    return *section;
}

TonwerkSynthEditor::TonwerkSynthEditor(TonwerkSynthProcessor& owner)
    : AudioProcessorEditor(owner),
      processor(owner),
      keyboard(owner.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      scope(owner.scope)
{
    setLookAndFeel(&lookAndFeel);
    auto& state = processor.state;

    title.setText("Tonwerk Synth", juce::dontSendNotification);
    title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(title);

    for (auto* button : { &analogButton, &fmButton })
    {
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
    }
    analogButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    fmButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    engineAttachment = std::make_unique<juce::ParameterAttachment>(
        *state.getParameter("engine"), [this](float value) { showEngine(value > 0.5f ? Engine::fm : Engine::analog); });
    analogButton.onClick = [this] { engineAttachment->setValueAsCompleteGesture(0.0f); };
    fmButton.onClick = [this] { engineAttachment->setValueAsCompleteGesture(1.0f); };

    presets.setTextWhenNothingSelected("Klang");
    presets.onChange = [this] { presetChosen(); };
    addAndMakeVisible(presets);
    tonwerkButton.setButtonText("Aus Tonwerk laden");
    tonwerkButton.onClick = [this] { fetchFromTonwerk(); };
    importButton.setButtonText("Datei laden");
    importButton.onClick = [this] { importFile(); };
    exportButton.setButtonText("Speichern");
    exportButton.onClick = [this] { exportFile(); };
    deleteButton.setButtonText(utf8("Löschen"));
    deleteButton.onClick = [this] { deletePreset(); };
    for (auto* button : { &tonwerkButton, &importButton, &exportButton, &deleteButton })
        addAndMakeVisible(*button);

    message.setFont(juce::FontOptions(13.0f));
    message.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(message);

    // Analog: the oscillators each with their pulse width and its three sources, then filter, envelopes and LFO.
    auto oscillator = [&](const char* prefix, const char* name) {
        const juce::String p(prefix);
        return new Section(state, name,
                           { { (p + ".wave"), "Welle", 2 },
                             { (p + ".octave"), "Oktave" },
                             { (p + ".detune"), "Verstimm." },
                             { (p + ".level"), "Pegel" },
                             { (p + ".width"), "Pulsbreite" },
                             { (p + ".pwm"), "PWM LFO" },
                             { (p + ".pwmLfo"), "LFO an" },
                             { (p + ".pwmAmpEnv"), "PWM Amp" },
                             { (p + ".pwmFilterEnv"), "PWM Filt." } });
    };
    auto& osc1 = add(analogSections, oscillator("osc1", "Oszillator 1"));
    auto& osc2 = add(analogSections, oscillator("osc2", "Oszillator 2"));
    auto& filter = add(analogSections, new Section(state, "Filter",
                                                   { { "filter.type", "Typ", 2 },
                                                     { "filter.cutoff", "Cutoff" },
                                                     { "filter.resonance", "Resonanz" },
                                                     { "filter.envAmount", utf8("Hüllkurve") },
                                                     { "filter.keyTrack", "Keytrack" } }));
    auto& filterEnv = add(analogSections, new Section(state, utf8("Filter-Hüllkurve"),
                                                      { { "filterEnv.attack", "Attack" },
                                                        { "filterEnv.decay", "Decay" },
                                                        { "filterEnv.sustain", "Sustain" },
                                                        { "filterEnv.release", "Release" } }));
    auto& ampEnv = add(analogSections, new Section(state, utf8("Lautstärke-Hüllkurve"),
                                                   { { "ampEnv.attack", "Attack" },
                                                     { "ampEnv.decay", "Decay" },
                                                     { "ampEnv.sustain", "Sustain" },
                                                     { "ampEnv.release", "Release" } }));
    auto& lfo = add(analogSections, new Section(state, "LFO",
                                                { { "lfo.wave", "Welle", 2 },
                                                  { "lfo.sync", "Sync" },
                                                  { "lfo.rate", "Tempo", 1, "lfo.sync", false },
                                                  { "lfo.division", "Notenwert", 2, "lfo.sync", true },
                                                  { "lfo.target", "Ziel", 2 },
                                                  { "lfo.depth", "Tiefe" } }));
    auto& mix = add(analogSections, new Section(state, "Mischung", { { "noise", "Rauschen" }, { "volume", "Volume" } }));

    // FM: the algorithm with feedback, the LFO, then the four operators with their envelopes.
    auto& algorithm = add(fmSections, new Section(state, "Algorithmus",
                                                  { { "fm.algorithm", "Operatoren", 3 },
                                                    { "fm.feedback", "Feedback" },
                                                    { "fm.volume", "Volume" } }));
    auto& fmLfo = add(fmSections, new Section(state, "LFO",
                                              { { "fm.lfo.wave", "Welle", 2 },
                                                { "fm.lfo.sync", "Sync" },
                                                { "fm.lfo.rate", "Tempo", 1, "fm.lfo.sync", false },
                                                { "fm.lfo.division", "Notenwert", 2, "fm.lfo.sync", true },
                                                { "fm.lfo.target", "Ziel", 2 },
                                                { "fm.lfo.depth", "Tiefe" } }));
    std::vector<Section*> operators;
    for (int i = 1; i <= 4; ++i)
    {
        const juce::String p = "fm.op" + juce::String(i);
        operators.push_back(&add(fmSections, new Section(state, "Operator " + juce::String(i),
                                                         { { p + ".ratio", "Ratio" },
                                                           { p + ".detune", "Verstimm." },
                                                           { p + ".level", "Pegel" },
                                                           { p + ".velocity", "Anschlag" },
                                                           { p + ".env.attack", "Attack" },
                                                           { p + ".env.decay", "Decay" },
                                                           { p + ".env.sustain", "Sustain" },
                                                           { p + ".env.release", "Release" } })));
    }

    fxSection = std::make_unique<Section>(state, "Effekte",
                                          std::initializer_list<Section::Item> { { "fx.delay.mix", "Delay" },
                                                                                 { "fx.delay.sync", "Sync" },
                                                                                 { "fx.delay.time", "Zeit", 1, "fx.delay.sync", false },
                                                                                 { "fx.delay.division", "Notenwert", 2, "fx.delay.sync", true },
                                                                                 { "fx.delay.feedback", "Feedback" },
                                                                                 { "fx.delay.tone", "Ton" },
                                                                                 { "fx.reverb.mix", "Hall" },
                                                                                 { "fx.reverb.decay", utf8("Länge") } });
    addAndMakeVisible(*fxSection);

    analogRows = { { &osc1, fxSection.get() }, { &osc2, &filter }, { &filterEnv, &ampEnv, &lfo, &mix } };
    fmRows = { { &algorithm, &fmLfo, fxSection.get() }, { operators[0], operators[1] }, { operators[2], operators[3] } };

    addAndMakeVisible(keyboard);
    addAndMakeVisible(scope);
    keyboard.setAvailableRange(24, 108);

    refreshPresets();
    showEngine(processor.state.getParameter("engine")->getValue() > 0.5f ? Engine::fm : Engine::analog);
    setSize(kWidth, kMargin * 2 + kTopBar + kGap + kMessage + 3 * kRowHeight + 4 * kGap + kKeyboard);
}

TonwerkSynthEditor::~TonwerkSynthEditor() { setLookAndFeel(nullptr); }

void TonwerkSynthEditor::paint(juce::Graphics& g) { g.fillAll(kBackground); }

void TonwerkSynthEditor::showEngine(Engine engine)
{
    const bool fm = engine == Engine::fm;
    analogButton.setToggleState(! fm, juce::dontSendNotification);
    fmButton.setToggleState(fm, juce::dontSendNotification);
    for (auto& section : analogSections)
        section->setVisible(! fm);
    for (auto& section : fmSections)
        section->setVisible(fm);
    resized();
}

void TonwerkSynthEditor::layoutRows(const std::vector<Row>& rows, juce::Rectangle<int> area)
{
    for (const auto& row : rows)
    {
        auto line = area.removeFromTop(kRowHeight);
        area.removeFromTop(kGap);
        for (auto* section : row)
        {
            // The effects keep their place at the right of the first row whichever engine plays.
            if (section == fxSection.get())
            {
                section->setBounds(line.removeFromRight(section->preferredWidth()));
                continue;
            }
            section->setBounds(line.removeFromLeft(section->preferredWidth()));
            line.removeFromLeft(kGap);
        }
    }
}

void TonwerkSynthEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto top = area.removeFromTop(kTopBar);
    title.setBounds(top.removeFromLeft(140));
    analogButton.setBounds(top.removeFromLeft(70));
    fmButton.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(16);
    deleteButton.setBounds(top.removeFromRight(80));
    top.removeFromRight(6);
    exportButton.setBounds(top.removeFromRight(90));
    top.removeFromRight(6);
    importButton.setBounds(top.removeFromRight(100));
    top.removeFromRight(6);
    tonwerkButton.setBounds(top.removeFromRight(150));
    top.removeFromRight(12);
    presets.setBounds(top);
    area.removeFromTop(kGap);
    message.setBounds(area.removeFromTop(kMessage));
    area.removeFromTop(kGap);

    auto bottom = area.removeFromBottom(kKeyboard);
    scope.setBounds(bottom.removeFromRight(kScopeWidth));
    bottom.removeFromRight(kGap);
    keyboard.setBounds(bottom);
    // C1 to C8: 50 white keys across the keyboard's width.
    keyboard.setKeyWidth((float) keyboard.getWidth() / 50.0f);
    area.removeFromBottom(kGap);
    const bool fm = fmButton.getToggleState();
    layoutRows(fm ? fmRows : analogRows, area);
}

void TonwerkSynthEditor::refreshPresets()
{
    processor.library.reload();
    presets.clear(juce::dontSendNotification);
    menuPresets.clear();
    auto addGroup = [this](const juce::String& heading, const juce::Array<PresetLibrary::Preset>& list) {
        if (list.isEmpty())
            return;
        presets.addSectionHeading(heading);
        for (const auto& preset : list)
        {
            menuPresets.add(preset);
            presets.addItem(preset.name, menuPresets.size());
        }
    };
    juce::Array<PresetLibrary::Preset> tonwerk, files;
    for (const auto& preset : processor.library.imported())
        (preset.source == "tonwerk" ? tonwerk : files).add(preset);
    addGroup(utf8("Werksklänge"), PresetLibrary::factoryPresets());
    addGroup("Aus Tonwerk", tonwerk);
    addGroup("Aus Dateien", files);

    const auto current = processor.presetName();
    for (int i = 0; i < menuPresets.size(); ++i)
        if (menuPresets.getReference(i).name == current)
            presets.setSelectedId(i + 1, juce::dontSendNotification);
    if (presets.getSelectedId() == 0)
        presets.setText(current, juce::dontSendNotification);
    deleteButton.setEnabled(presets.getSelectedId() > 0
                            && menuPresets[presets.getSelectedId() - 1].source != "factory");
}

void TonwerkSynthEditor::presetChosen()
{
    const int id = presets.getSelectedId();
    if (id <= 0)
        return;
    const auto preset = menuPresets[id - 1];
    processor.applyPatch(preset.patch, preset.name);
    deleteButton.setEnabled(preset.source != "factory");
    setMessage({});
}

void TonwerkSynthEditor::setMessage(const juce::String& text, bool error)
{
    message.setColour(juce::Label::textColourId, error ? kError : kMuted);
    message.setText(text, juce::dontSendNotification);
}

void TonwerkSynthEditor::fetchFromTonwerk()
{
    auto* window = new juce::AlertWindow("Aus Tonwerk laden",
                                         utf8("Die Adresse von Tonwerk, wie sie im Browser steht. Alle gespeicherten "
                                              "Klänge werden übernommen; gleichnamige werden ersetzt."),
                                         juce::MessageBoxIconType::NoIcon,
                                         this);
    window->addTextEditor("url", processor.library.tonwerkUrl(), "Adresse");
    window->addButton("Laden", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Abbrechen", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TonwerkSynthEditor> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
        if (result != 1 || safe == nullptr)
            return;
        const auto url = window->getTextEditorContents("url").trim();
        safe->processor.library.setTonwerkUrl(url);
        safe->setMessage(utf8("Lade Klänge aus Tonwerk …"));
        juce::Thread::launch([safe, url] {
            juce::Array<NamedPatch> sounds;
            const auto fetched = PresetLibrary::fetchFromTonwerk(url, sounds);
            juce::MessageManager::callAsync([safe, fetched, sounds] {
                if (safe == nullptr)
                    return;
                if (fetched.failed())
                {
                    safe->setMessage(fetched.getErrorMessage(), true);
                    return;
                }
                const int count = safe->processor.library.add(sounds, "tonwerk");
                safe->refreshPresets();
                safe->setMessage(count == 0 ? utf8("In Tonwerk sind noch keine Klänge gespeichert.")
                                            : juce::String(count) + utf8(count == 1 ? " Klang aus Tonwerk übernommen."
                                                                                    : " Klänge aus Tonwerk übernommen."));
            });
        });
    }),
                             true);
}

void TonwerkSynthEditor::importFile()
{
    chooser = std::make_unique<juce::FileChooser>("Klang laden", juce::File(), "*.json");
    juce::Component::SafePointer<TonwerkSynthEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser& fc) {
                             if (safe == nullptr || fc.getResult() == juce::File())
                                 return;
                             const auto file = fc.getResult();
                             const auto sounds = namedPatchesFromJson(file.loadFileAsString(),
                                                                      file.getFileNameWithoutExtension());
                             if (sounds.isEmpty())
                             {
                                 safe->setMessage(utf8("In der Datei steht kein Klang."), true);
                                 return;
                             }
                             safe->processor.library.add(sounds, "file");
                             safe->processor.applyPatch(sounds[0].patch, sounds[0].name);
                             safe->refreshPresets();
                             safe->setMessage(sounds.size() == 1
                                                  ? utf8("Klang „") + sounds[0].name + utf8("“ geladen.")
                                                  : juce::String(sounds.size()) + utf8(" Klänge geladen."));
                         });
}

void TonwerkSynthEditor::exportFile()
{
    const auto name = processor.presetName().isNotEmpty() ? processor.presetName() : juce::String("Klang");
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                           .getChildFile(juce::File::createLegalFileName(name) + ".json");
    chooser = std::make_unique<juce::FileChooser>("Klang speichern", start, "*.json");
    juce::Component::SafePointer<TonwerkSynthEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe, name](const juce::FileChooser& fc) {
                             if (safe == nullptr || fc.getResult() == juce::File())
                                 return;
                             // The shape of one entry in Tonwerk's preset list, so the file loads back as this name.
                             auto entry = new juce::DynamicObject();
                             entry->setProperty("name", name);
                             entry->setProperty("patch", safe->processor.currentPatchJson());
                             const auto file = fc.getResult().withFileExtension("json");
                             if (file.replaceWithText(juce::JSON::toString(juce::var(entry))))
                                 safe->setMessage("Gespeichert in " + file.getFullPathName());
                             else
                                 safe->setMessage("Konnte " + file.getFullPathName() + " nicht schreiben.", true);
                         });
}

void TonwerkSynthEditor::deletePreset()
{
    const int id = presets.getSelectedId();
    if (id <= 0 || menuPresets[id - 1].source == "factory")
        return;
    const auto name = menuPresets[id - 1].name;
    juce::Component::SafePointer<TonwerkSynthEditor> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle(utf8("Klang löschen"))
                                     .withMessage(utf8("„") + name
                                                  + utf8("“ aus der Liste des Plugins löschen? In Tonwerk bleibt er."))
                                     .withButton(utf8("Löschen"))
                                     .withButton("Abbrechen")
                                     .withAssociatedComponent(this),
                                 [safe, name](int result) {
                                     if (result != 1 || safe == nullptr)
                                         return;
                                     safe->processor.library.remove(name);
                                     safe->refreshPresets();
                                     safe->setMessage(utf8("„") + name + utf8("“ gelöscht."));
                                 });
}
} // namespace tonwerk
