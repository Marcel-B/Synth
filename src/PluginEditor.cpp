#include "PluginEditor.h"

namespace tonwerk
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kEnvelopeDisplay = 130;
constexpr int kAlgorithmDisplay = 200;
constexpr int kBottom = 72;
constexpr int kScopeWidth = 300;

/** Room for the steps an envelope takes, so a short attack still shows. */
float shareOf(float seconds, float longest) { return std::sqrt(std::max(0.0f, seconds) / longest); }
} // namespace

EnvelopeView::EnvelopeView(juce::AudioProcessorValueTreeState& state, const juce::String& prefix)
{
    const char* stages[] = { ".attack", ".decay", ".sustain", ".release" };
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = state.getRawParameterValue(prefix + stages[i]);
        jassert(values[i] != nullptr);
    }
    setInterceptsMouseClicks(false, false);
}

void EnvelopeView::setCaption(const juce::String& text, bool lit)
{
    if (text == caption && lit == captionLit)
        return;
    caption = text;
    captionLit = lit;
    repaint();
}

void EnvelopeView::update()
{
    bool changed = false;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const float value = values[i]->load();
        changed = changed || std::abs(value - shown[i]) > 1.0e-4f;
        shown[i] = value;
    }
    if (changed)
        repaint();
}

void EnvelopeView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    if (caption.isNotEmpty())
    {
        g.setFont(mono(10.0f));
        g.setColour(captionLit ? yellow : cyan.withAlpha(0.8f));
        g.drawText(caption, area.reduced(8.0f, 4.0f).toNearestInt(), juce::Justification::topRight);
    }
    if (shown[0] < 0.0f)
        return;

    const auto inner = area.reduced(8.0f, 10.0f).withTrimmedTop(caption.isNotEmpty() ? 8.0f : 0.0f);
    const float attack = shareOf(shown[0], 4.0f), decay = shareOf(shown[1], 4.0f), release = shareOf(shown[3], 6.0f);
    const float hold = 0.35f;
    const float scale = inner.getWidth() / std::max(0.01f, attack + decay + hold + release);
    const float sustainY = inner.getBottom() - shown[2] * inner.getHeight();
    auto x = [&](float share) { return inner.getX() + share * scale; };

    juce::Path curve;
    curve.startNewSubPath(inner.getX(), inner.getBottom());
    curve.lineTo(x(attack), inner.getY());
    // Decay and release fall the way Web Audio's setTargetAtTime does: fast first, then slower.
    constexpr int steps = 24;
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        const float fall = (1.0f - std::exp(-4.0f * t)) / (1.0f - std::exp(-4.0f));
        curve.lineTo(x(attack + decay * t), inner.getY() + fall * (sustainY - inner.getY()));
    }
    curve.lineTo(x(attack + decay + hold), sustainY);
    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        const float fall = (1.0f - std::exp(-4.0f * t)) / (1.0f - std::exp(-4.0f));
        curve.lineTo(x(attack + decay + hold + release * t), sustainY + fall * (inner.getBottom() - sustainY));
    }

    auto filled = curve;
    filled.closeSubPath();
    g.setColour(yellow.withAlpha(0.08f));
    g.fillPath(filled);
    g.setColour(yellow.withAlpha(0.25f));
    g.strokePath(curve, juce::PathStrokeType(5.0f));
    g.setColour(yellow);
    g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

EnvelopeSection::EnvelopeSection(juce::AudioProcessorValueTreeState& state, const juce::String& heading,
                                 const juce::String& prefix, std::vector<Row> rows, Display room)
    : Section(state, heading, std::move(rows), {}, room),
      view(state, prefix)
{
    addAndMakeVisible(view);
}

void EnvelopeSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

AlgorithmView::AlgorithmView(juce::AudioProcessorValueTreeState& state)
    : value(state.getRawParameterValue("fm.algorithm"))
{
    jassert(value != nullptr);
    setInterceptsMouseClicks(false, false);
}

void AlgorithmView::update()
{
    const int now = juce::roundToInt(value->load());
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void AlgorithmView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    if (! juce::isPositiveAndBelow(shown, (int) kAlgorithms.size()))
        return;
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto& algorithm = kAlgorithms[(std::size_t) shown];

    // Carriers on the bottom row, every modulator one row above the highest it feeds.
    std::array<int, 4> level {};
    for (int pass = 0; pass < 4; ++pass)
        for (int m = 0; m < algorithm.modCount; ++m)
        {
            const auto [from, to] = algorithm.mods[(std::size_t) m];
            level[(std::size_t) from] = std::max(level[(std::size_t) from], level[(std::size_t) to] + 1);
        }
    const int levels = *std::max_element(level.begin(), level.end()) + 1;

    const auto inner = area.reduced(10.0f).withTrimmedBottom(14.0f);
    const float box = std::min(40.0f, inner.getHeight() / (float) levels * 0.7f);
    const float rowStep = levels > 1 ? (inner.getHeight() - box) / (float) (levels - 1) : 0.0f;
    std::array<juce::Point<float>, 4> centre {};
    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        const float offset = (float) i - (float) (algorithm.carrierCount - 1) / 2.0f;
        centre[(std::size_t) algorithm.carriers[(std::size_t) i]] = { inner.getCentreX() + offset * box * 2.0f,
                                                                      inner.getBottom() - box / 2.0f };
    }
    for (int l = 1; l < levels; ++l)
    {
        // Above the middle of what each one feeds; two over the same place stand side by side.
        std::vector<std::pair<int, float>> row;
        for (int op = 0; op < 4; ++op)
        {
            if (level[(std::size_t) op] != l)
                continue;
            float sum = 0.0f;
            int count = 0;
            for (int m = 0; m < algorithm.modCount; ++m)
                if (algorithm.mods[(std::size_t) m][0] == op)
                {
                    sum += centre[(std::size_t) algorithm.mods[(std::size_t) m][1]].x;
                    ++count;
                }
            row.emplace_back(op, count > 0 ? sum / (float) count : inner.getCentreX());
        }
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            int same = 0, before = 0;
            for (std::size_t j = 0; j < row.size(); ++j)
                if (std::abs(row[j].second - row[i].second) < 1.0f)
                {
                    ++same;
                    before += j < i ? 1 : 0;
                }
            const float offset = (float) before - (float) (same - 1) / 2.0f;
            centre[(std::size_t) row[i].first] = { row[i].second + offset * box * 1.6f,
                                                   inner.getBottom() - box / 2.0f - rowStep * (float) l };
        }
    }

    g.setColour(cyan.withAlpha(0.6f));
    for (int m = 0; m < algorithm.modCount; ++m)
    {
        const auto from = centre[(std::size_t) algorithm.mods[(std::size_t) m][0]];
        const auto to = centre[(std::size_t) algorithm.mods[(std::size_t) m][1]];
        g.drawLine(from.x, from.y + box / 2.0f, to.x, to.y - box / 2.0f, 1.5f);
    }
    // What is heard: the carriers into one line along the bottom.
    const float out = area.getBottom() - 12.0f;
    float left = inner.getRight(), right = inner.getX();
    for (int i = 0; i < algorithm.carrierCount; ++i)
    {
        const auto c = centre[(std::size_t) algorithm.carriers[(std::size_t) i]];
        g.setColour(yellow.withAlpha(0.6f));
        g.drawLine(c.x, c.y + box / 2.0f, c.x, out, 1.5f);
        left = std::min(left, c.x);
        right = std::max(right, c.x);
    }
    g.drawLine(left, out, right, out, 1.5f);
    // Operator 4 feeds back into itself: a loop out of its right side and back in at the top.
    const auto four = centre[3];
    juce::Path loop;
    loop.startNewSubPath(four.x + box / 2.0f, four.y);
    loop.lineTo(four.x + box / 2.0f + 10.0f, four.y);
    loop.lineTo(four.x + box / 2.0f + 10.0f, four.y - box / 2.0f - 10.0f);
    loop.lineTo(four.x, four.y - box / 2.0f - 10.0f);
    loop.lineTo(four.x, four.y - box / 2.0f);
    g.setColour(cyan.withAlpha(0.6f));
    g.strokePath(loop.createPathWithRoundedCorners(4.0f), juce::PathStrokeType(1.5f));

    g.setFont(mono(14.0f));
    for (int op = 0; op < 4; ++op)
    {
        const auto bounds = juce::Rectangle<float>(box, box).withCentre(centre[(std::size_t) op]);
        const bool carrier = isCarrier(algorithm, op);
        g.setColour(carrier ? yellow : screen);
        g.fillRect(bounds);
        g.setColour(carrier ? yellow : cyan);
        g.drawRect(bounds, 1.5f);
        g.setColour(carrier ? screen : cyan);
        g.drawText(juce::String(op + 1), bounds, juce::Justification::centred);
    }
}

AlgorithmSection::AlgorithmSection(juce::AudioProcessorValueTreeState& state)
    : Section(state, "Algorithmus", { { { "fm.algorithm", "Operatoren", 3 } }, { { "fm.feedback", "Feedback" }, { "fm.volume", utf8("Lautstärke") } } },
              {}, { kAlgorithmDisplay }),
      view(state)
{
    addAndMakeVisible(view);
}

void AlgorithmSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

template <typename SectionType, typename... Args>
SectionType& TonwerkSynthEditor::add(std::vector<std::unique_ptr<Section>>& owner, Args&&... args)
{
    auto section = std::make_unique<SectionType>(processor.state, std::forward<Args>(args)...);
    auto& result = *section;
    owner.push_back(std::move(section));
    addChildComponent(result);
    return result;
}

TonwerkSynthEditor::TonwerkSynthEditor(TonwerkSynthProcessor& owner)
    : AudioProcessorEditor(owner),
      processor(owner),
      keyboard(owner.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      scope(owner.scope)
{
    setLookAndFeel(&lookAndFeel);
    auto& state = processor.state;

    if (processor.edition == Edition::combined)
    {
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
    }

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

    message.setFont(mono(11.0f));
    message.setColour(juce::Label::textColourId, cyan.withAlpha(0.8f));
    message.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(message);

    if (hasPart(processor.edition, Part::analog))
    {
        // The oscillators with their pulse width and its three sources, filter, envelopes, LFO and the plugin's own two.
        auto oscillator = [&](const juce::String& p, const char* name) -> Section& {
            return add<Section>(analogSections, name,
                                std::vector<Section::Row> { { { p + ".wave", "Welle", 2 },
                                                              { p + ".octave", "Oktave" },
                                                              { p + ".detune", "Verstimm." },
                                                              { p + ".level", "Pegel" } },
                                                            { { p + ".width", "Pulsbreite" },
                                                              { p + ".pwm", "PWM LFO" },
                                                              { p + ".pwmLfo", "LFO" },
                                                              { p + ".pwmAmpEnv", "PWM Amp" },
                                                              { p + ".pwmFilterEnv", "PWM Filt." } } });
        };
        auto& osc1 = oscillator("osc1", "Oszillator 1");
        auto& osc2 = oscillator("osc2", "Oszillator 2");
        auto& filter = add<Section>(analogSections, "Filter",
                                    std::vector<Section::Row> { { { "filter.type", "Typ", 2 },
                                                                  { "filter.cutoff", "Cutoff" },
                                                                  { "filter.resonance", "Resonanz" } },
                                                                { { "filter.envAmount", utf8("Hüllkurve") },
                                                                  { "filter.keyTrack", "Keytrack" } } });
        auto& sampleHold = add<Section>(analogSections, "Sample & Hold",
                                        std::vector<Section::Row> { { { "sh.sync", "Sync" },
                                                                      { "sh.rate", "Tempo", 1, "sh.sync", false },
                                                                      { "sh.division", "Notenwert", 2, "sh.sync", true } },
                                                                    { { "sh.filter", "Filter" },
                                                                      { "sh.pitch", utf8("Tonhöhe") } } });
        auto envelope = [&](const char* title, const juce::String& p) -> EnvelopeSection& {
            auto& section = add<EnvelopeSection>(analogSections, utf8(title), p,
                                                 std::vector<Section::Row> { { { p + ".attack", "Attack" },
                                                                               { p + ".decay", "Decay" },
                                                                               { p + ".sustain", "Sustain" },
                                                                               { p + ".release", "Release" } } },
                                                 Section::Display { kEnvelopeDisplay, 0 });
            envelopes.push_back(&section.view);
            return section;
        };
        auto& filterEnv = envelope("FILTER-HÜLLKURVE", "filterEnv");
        auto& ampEnv = envelope("LAUTSTÄRKE-HÜLLKURVE", "ampEnv");
        auto& fold = add<Section>(analogSections, "Wavefolder",
                                  std::vector<Section::Row> { { { "fold.amount", "Menge" },
                                                                { "fold.symmetry", "Symmetrie" },
                                                                { "fold.env", utf8("Hüllkurve") } } });
        auto& mix = add<Section>(analogSections, "Mischung",
                                 std::vector<Section::Row> { { { "noise", "Rauschen" }, { "volume", utf8("Lautstärke") } } });
        auto& lfo = add<Section>(analogSections, "LFO",
                                 std::vector<Section::Row> { { { "lfo.wave", "Welle", 2 },
                                                               { "lfo.sync", "Sync" },
                                                               { "lfo.rate", "Tempo", 1, "lfo.sync", false },
                                                               { "lfo.division", "Notenwert", 2, "lfo.sync", true },
                                                               { "lfo.target", "Ziel", 2 },
                                                               { "lfo.depth", "Tiefe" } } });
        auto& fx = add<Section>(analogSections, "Effekte",
                                std::vector<Section::Row> { { { "fx.delay.mix", "Delay" },
                                                              { "fx.delay.sync", "Sync" },
                                                              { "fx.delay.time", "Zeit", 1, "fx.delay.sync", false },
                                                              { "fx.delay.division", "Notenwert", 2, "fx.delay.sync", true },
                                                              { "fx.delay.feedback", "Feedback" },
                                                              { "fx.delay.tone", "Ton" },
                                                              { "fx.reverb.mix", "Hall" },
                                                              { "fx.reverb.decay", utf8("Länge") } } });
        analogRows = { { &osc1, &osc2, &filter, &sampleHold }, { &filterEnv, &ampEnv, &fold, &mix }, { &lfo, &fx } };
    }

    if (hasPart(processor.edition, Part::fm))
    {
        // The algorithm with feedback and volume, the LFO, the effects, then the four operators with their envelopes.
        algorithmSection = &add<AlgorithmSection>(fmSections);
        auto& lfo = add<Section>(fmSections, "LFO",
                                 std::vector<Section::Row> { { { "fm.lfo.wave", "Welle", 2 }, { "fm.lfo.target", "Ziel", 2 } },
                                                             { { "fm.lfo.sync", "Sync" },
                                                               { "fm.lfo.rate", "Tempo", 1, "fm.lfo.sync", false },
                                                               { "fm.lfo.division", "Notenwert", 2, "fm.lfo.sync", true },
                                                               { "fm.lfo.depth", "Tiefe" } } });
        auto& fx = add<Section>(fmSections, "Effekte",
                                std::vector<Section::Row> { { { "fx.delay.mix", "Delay" },
                                                              { "fx.delay.sync", "Sync" },
                                                              { "fx.delay.time", "Zeit", 1, "fx.delay.sync", false },
                                                              { "fx.delay.division", "Notenwert", 2, "fx.delay.sync", true } },
                                                            { { "fx.delay.feedback", "Feedback" },
                                                              { "fx.delay.tone", "Ton" },
                                                              { "fx.reverb.mix", "Hall" },
                                                              { "fx.reverb.decay", utf8("Länge") } } });
        Row operators;
        for (int i = 1; i <= 4; ++i)
        {
            const juce::String p = "fm.op" + juce::String(i);
            auto& section = add<EnvelopeSection>(fmSections, "Operator " + juce::String(i), p + ".env",
                                                 std::vector<Section::Row> { { { p + ".ratio", "Ratio" },
                                                                               { p + ".detune", "Verstimm." },
                                                                               { p + ".level", "Pegel" },
                                                                               { p + ".velocity", "Anschlag" } },
                                                                             { { p + ".env.attack", "Attack" },
                                                                               { p + ".env.decay", "Decay" },
                                                                               { p + ".env.sustain", "Sustain" },
                                                                               { p + ".env.release", "Release" } } },
                                                 Section::Display { 0, kRow });
            envelopes.push_back(&section.view);
            operatorViews.emplace_back(&section.view, i - 1);
            operators.push_back(&section);
        }
        fmRows = { { algorithmSection, &lfo, &fx }, operators };
    }

    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffcfccc0));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, panel);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, border);
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, yellow.withAlpha(0.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setAvailableRange(24, 108);
    scope.setCaption("OSZILLOSKOP");
    addAndMakeVisible(keyboard);
    addAndMakeVisible(scope);

    refreshPresets();
    const auto fixed = processor.fixedEngine();
    showEngine(fixed ? *fixed : (state.getParameter("engine")->getValue() > 0.5f ? Engine::fm : Engine::analog));
    timerCallback();
    startTimerHz(30);
}

TonwerkSynthEditor::~TonwerkSynthEditor() { setLookAndFeel(nullptr); }

int TonwerkSynthEditor::heightFor(Engine which) const
{
    int height = 2 * kMargin + kHeader + kBottom;
    for (const auto& row : which == Engine::fm ? fmRows : analogRows)
    {
        int tallest = 0;
        for (auto* section : row)
            tallest = std::max(tallest, section->preferredHeight());
        height += tallest + kGap;
    }
    return height;
}

void TonwerkSynthEditor::showEngine(Engine which)
{
    engine = which;
    const bool fm = which == Engine::fm;
    analogButton.setToggleState(! fm, juce::dontSendNotification);
    fmButton.setToggleState(fm, juce::dontSendNotification);
    for (auto& section : analogSections)
        section->setVisible(! fm);
    for (auto& section : fmSections)
        section->setVisible(fm);
    // The engines need different heights; only Tonwerk Synth switches, and the host follows the new size.
    if (getWidth() == kWidth && getHeight() == heightFor(which))
        resized();
    else
        setSize(kWidth, heightFor(which));
}

void TonwerkSynthEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, TonwerkSynthProcessor::editionName(processor.edition).toUpperCase());
}

void TonwerkSynthEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    // The title is painted; the room after it is the switch (Tonwerk Synth only) and the message.
    header.removeFromLeft(processor.edition == Edition::combined ? 215 : 230);
    if (processor.edition == Edition::combined)
    {
        analogButton.setBounds(header.removeFromLeft(64));
        fmButton.setBounds(header.removeFromLeft(64));
        header.removeFromLeft(12);
    }
    deleteButton.setBounds(header.removeFromRight(70));
    header.removeFromRight(4);
    exportButton.setBounds(header.removeFromRight(84));
    header.removeFromRight(4);
    importButton.setBounds(header.removeFromRight(90));
    header.removeFromRight(4);
    tonwerkButton.setBounds(header.removeFromRight(130));
    header.removeFromRight(8);
    presets.setBounds(header.removeFromRight(220));
    header.removeFromRight(8);
    message.setBounds(header);

    auto bottom = area.removeFromBottom(kBottom);
    scope.setBounds(bottom.removeFromRight(kScopeWidth));
    bottom.removeFromRight(kGap);
    keyboard.setBounds(bottom);
    // C1 to C8: 50 white keys across the keyboard's width.
    keyboard.setKeyWidth((float) keyboard.getWidth() / 50.0f);
    area.removeFromBottom(kGap);

    for (const auto& row : engine == Engine::fm ? fmRows : analogRows)
    {
        switch (row.size())
        {
            case 2: layoutRow(area, { row[0], row[1] }); break;
            case 3: layoutRow(area, { row[0], row[1], row[2] }); break;
            case 4: layoutRow(area, { row[0], row[1], row[2], row[3] }); break;
            default: jassertfalse; break;
        }
    }
}

void TonwerkSynthEditor::timerCallback()
{
    for (auto* view : envelopes)
        view->update();
    if (algorithmSection == nullptr)
        return;
    algorithmSection->view.update();
    // What each operator does in the current algorithm, in the corner of its envelope.
    const int index = algorithmSection->view.algorithm();
    if (! juce::isPositiveAndBelow(index, (int) kAlgorithms.size()))
        return;
    const auto& algorithm = kAlgorithms[(std::size_t) index];
    for (auto [view, op] : operatorViews)
    {
        juce::String role;
        if (isCarrier(algorithm, op))
            role = utf8("TRÄGER");
        for (int m = 0; m < algorithm.modCount; ++m)
            if (algorithm.mods[(std::size_t) m][0] == op)
                role += (role.isEmpty() ? juce::String(utf8("MOD → ")) : juce::String(" ")) + juce::String(algorithm.mods[(std::size_t) m][1] + 1);
        if (op == 3)
            role += " + FB";
        view->setCaption(role, isCarrier(algorithm, op));
    }
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
    for (const auto& preset : processor.library.importedFor(processor.edition))
        (preset.source == "tonwerk" ? tonwerk : files).add(preset);
    addGroup(utf8("Werksklänge"), PresetLibrary::factoryPresets(processor.edition));
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
    message.setColour(juce::Label::textColourId, error ? red : cyan.withAlpha(0.8f));
    message.setText(text, juce::dontSendNotification);
}

juce::String TonwerkSynthEditor::otherPlugin() const
{
    return TonwerkSynthProcessor::editionName(processor.edition == Edition::fm ? Edition::analog : Edition::fm);
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
                // All go into the library both plugins share; this one counts what it plays.
                safe->processor.library.add(sounds, "tonwerk");
                safe->refreshPresets();
                int count = 0;
                for (const auto& sound : sounds)
                    count += safe->processor.plays(sound.patch) ? 1 : 0;
                auto text = count == 0 ? utf8("In Tonwerk sind noch keine passenden Klänge gespeichert.")
                                       : juce::String(count) + utf8(count == 1 ? " Klang aus Tonwerk übernommen."
                                                                               : " Klänge aus Tonwerk übernommen.");
                if (count < sounds.size())
                    text += utf8(" Die übrigen stehen in ") + safe->otherPlugin() + ".";
                safe->setMessage(text);
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
                             juce::Array<NamedPatch> playable;
                             for (const auto& sound : sounds)
                                 if (safe->processor.plays(sound.patch))
                                     playable.add(sound);
                             safe->refreshPresets();
                             if (playable.isEmpty())
                             {
                                 safe->setMessage(utf8("Die Datei hat nur Klänge für ") + safe->otherPlugin()
                                                      + utf8("; dort stehen sie jetzt im Menü."),
                                                  true);
                                 return;
                             }
                             safe->processor.applyPatch(playable[0].patch, playable[0].name);
                             safe->refreshPresets();
                             safe->setMessage(playable.size() == 1
                                                  ? utf8("Klang „") + playable[0].name + utf8("“ geladen.")
                                                  : juce::String(playable.size()) + utf8(" Klänge geladen."));
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
                                                  + utf8("“ aus der Liste der Tonwerk-Plugins löschen? In Tonwerk bleibt er."))
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
