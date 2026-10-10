#include "DxEditor.h"

#include "DxPresets.h"
#include "PresetCategories.h"

namespace tonwerkdx
{
using namespace tonwerkui;
using namespace tonwerkui::colours;

namespace
{
constexpr int kAlgorithmDisplay = 230;
constexpr int kOperatorDisplay = 64;
constexpr int kDetailDisplay = 300;
constexpr int kBottom = 72;
constexpr int kScopeWidth = 300;

/** Room for a stage on screen: long stages squeezed, so a fast attack still shows. */
float shareOf(double seconds) { return (float) std::sqrt(std::max(0.0, seconds) / 10.0); }

juce::String opId(int op, const char* field) { return "op" + juce::String(op + 1) + field; }

/** What an operator does in an algorithm, for the corner of its envelope. */
juce::String roleOf(const Algorithm& algorithm, int op)
{
    juce::String role;
    if (algorithm.isCarrier(op))
        role = utf8("TRÄGER");
    juce::String targets;
    for (int to = 0; to < kOperators; ++to)
        if (algorithm.modulates(op, to))
            targets += (targets.isEmpty() ? "" : " ") + juce::String(to + 1);
    if (targets.isNotEmpty())
        role += (role.isEmpty() ? juce::String() : juce::String(" ")) + utf8("MOD → ") + targets;
    if (op == algorithm.feedback)
        role += " + FB";
    return role;
}
} // namespace

DxEnvelopeView::DxEnvelopeView(juce::AudioProcessorValueTreeState& state, int op)
{
    for (int s = 0; s < 4; ++s)
    {
        values[(std::size_t) s] = state.getRawParameterValue(opId(op, "R") + juce::String(s + 1));
        values[(std::size_t) s + 4] = state.getRawParameterValue(opId(op, "L") + juce::String(s + 1));
    }
    jassert(std::all_of(values.begin(), values.end(), [](auto* v) { return v != nullptr; }));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void DxEnvelopeView::setCaption(const juce::String& text, bool lit)
{
    if (text == caption && lit == captionLit)
        return;
    caption = text;
    captionLit = lit;
    repaint();
}

void DxEnvelopeView::setSelected(bool isSelected)
{
    if (selected != isSelected)
    {
        selected = isSelected;
        repaint();
    }
}

void DxEnvelopeView::update()
{
    bool changed = first;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const float value = values[i]->load();
        changed = changed || std::abs(value - shown[i]) > 1.0e-4f;
        shown[i] = value;
    }
    first = false;
    if (changed)
        repaint();
}

void DxEnvelopeView::mouseUp(const juce::MouseEvent& event)
{
    if (onClick && getLocalBounds().contains(event.getPosition()))
        onClick();
}

void DxEnvelopeView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    if (selected)
    {
        g.setColour(yellow.withAlpha(0.8f));
        g.drawRoundedRectangle(area.reduced(1.0f), 4.0f, 1.5f);
    }
    if (caption.isNotEmpty())
    {
        g.setFont(mono(10.0f));
        g.setColour(captionLit ? yellow : cyan.withAlpha(0.8f));
        g.drawText(caption, area.reduced(8.0f, 4.0f).toNearestInt(), juce::Justification::topRight);
    }
    if (first)
        return;

    const auto inner = area.reduced(8.0f, 8.0f).withTrimmedTop(caption.isNotEmpty() ? 10.0f : 0.0f);
    auto rate = [&](int s) { return (double) shown[(std::size_t) s]; };
    auto level = [&](int s) { return (double) shown[(std::size_t) s + 4]; };
    // Each stage takes the time its rate needs for the distance it travels.
    auto seconds = [&](int s, double from) { return std::abs(level(s) - from) / 99.0 * stageSeconds(rate(s)); };
    const std::array<float, 4> shares { shareOf(seconds(0, level(3))), shareOf(seconds(1, level(0))),
                                        shareOf(seconds(2, level(1))), shareOf(seconds(3, level(2))) };
    const float hold = 0.25f;
    const float total = std::max(0.05f, shares[0] + shares[1] + shares[2] + hold + shares[3]);
    const float scale = inner.getWidth() / total;
    auto y = [&](double steps) { return inner.getBottom() - (float) (steps / 99.0) * inner.getHeight(); };

    juce::Path curve;
    float x = inner.getX();
    curve.startNewSubPath(x, y(level(3)));
    for (int s = 0; s < 3; ++s)
    {
        x += shares[(std::size_t) s] * scale;
        curve.lineTo(x, y(level(s)));
    }
    x += hold * scale;
    curve.lineTo(x, y(level(2)));
    x += shares[3] * scale;
    curve.lineTo(x, y(level(3)));

    auto filled = curve;
    filled.lineTo(x, inner.getBottom());
    filled.lineTo(inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour(yellow.withAlpha(0.08f));
    g.fillPath(filled);
    g.setColour(yellow.withAlpha(0.25f));
    g.strokePath(curve, juce::PathStrokeType(5.0f));
    g.setColour(yellow);
    g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

OperatorSection::OperatorSection(juce::AudioProcessorValueTreeState& state, const juce::String& heading, int op,
                                 std::vector<Row> rows, const juce::String& powerId, Display room)
    : Section(state, heading, std::move(rows), powerId, room),
      view(state, op)
{
    addAndMakeVisible(view);
}

void OperatorSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

DxAlgorithmView::DxAlgorithmView(juce::AudioProcessorValueTreeState& state)
    : value(state.getRawParameterValue("algorithm"))
{
    jassert(value != nullptr);
    setInterceptsMouseClicks(false, false);
}

void DxAlgorithmView::update()
{
    const int now = juce::roundToInt(value->load());
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

std::array<juce::Point<float>, kOperators> DxAlgorithmView::placesFor(const Algorithm& algorithm)
{
    // Carriers on row 0, every modulator one row above the highest operator it feeds.
    std::array<int, kOperators> row {};
    for (int pass = 0; pass < kOperators; ++pass)
        for (int from = 0; from < kOperators; ++from)
            for (int to = 0; to < kOperators; ++to)
                if (algorithm.modulates(from, to))
                    row[(std::size_t) from] = std::max(row[(std::size_t) from], row[(std::size_t) to] + 1);

    std::array<juce::Point<float>, kOperators> place {};
    constexpr float kSpacing = 1.4f;
    int carriers = 0;
    for (int op = 0; op < kOperators; ++op)
        if (row[(std::size_t) op] == 0)
            place[(std::size_t) op] = { kSpacing * (float) carriers++, 0.0f };

    const int rows = *std::max_element(row.begin(), row.end()) + 1;
    for (int r = 1; r < rows; ++r)
    {
        // Each one over the middle of what it feeds, then pushed apart so no two boxes touch; the row as a whole
        // stays where its wishes put it.
        std::vector<std::pair<float, int>> wish;
        for (int op = 0; op < kOperators; ++op)
        {
            if (row[(std::size_t) op] != r)
                continue;
            float sum = 0.0f;
            int count = 0;
            for (int to = 0; to < kOperators; ++to)
                if (algorithm.modulates(op, to))
                {
                    sum += place[(std::size_t) to].x;
                    ++count;
                }
            wish.emplace_back(count > 0 ? sum / (float) count : 0.0f, op);
        }
        std::stable_sort(wish.begin(), wish.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<float> xs;
        float wanted = 0.0f, got = 0.0f;
        for (const auto& [x, op] : wish)
        {
            const float at = xs.empty() ? x : std::max(x, xs.back() + kSpacing);
            xs.push_back(at);
            wanted += x;
            got += at;
        }
        const float shift = (wanted - got) / (float) xs.size();
        for (std::size_t i = 0; i < wish.size(); ++i)
            place[(std::size_t) wish[i].second] = { xs[i] + shift, (float) r };
    }

    float left = place[0].x;
    for (const auto& p : place)
        left = std::min(left, p.x);
    for (auto& p : place)
        p.x -= left;
    return place;
}

void DxAlgorithmView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    if (! juce::isPositiveAndBelow(shown, kAlgorithms))
        return;
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto& algorithm = algorithms()[(std::size_t) shown];
    const auto place = placesFor(algorithm);

    float width = 0.0f, height = 0.0f;
    for (const auto& p : place)
    {
        width = std::max(width, p.x);
        height = std::max(height, p.y);
    }
    g.setFont(mono(11.0f));
    g.setColour(cyan.withAlpha(0.7f));
    g.drawText("ALG " + juce::String(shown + 1), area.reduced(8.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);

    const auto inner = area.reduced(14.0f, 12.0f).withTrimmedTop(10.0f).withTrimmedBottom(12.0f);
    const float box = std::min({ 30.0f, inner.getWidth() / (width + 1.6f), inner.getHeight() / (height + 1.0f) * 0.62f });
    const float stepX = width > 0.0f ? (inner.getWidth() - box * 1.6f) / width : 0.0f;
    const float scaleX = std::min(stepX, box * 1.15f);
    const float stepY = height > 0.0f ? (inner.getHeight() - box) / height : 0.0f;
    const float originX = inner.getCentreX() - width * scaleX / 2.0f;
    std::array<juce::Point<float>, kOperators> centre {};
    for (int op = 0; op < kOperators; ++op)
        centre[(std::size_t) op] = { originX + place[(std::size_t) op].x * scaleX,
                                     inner.getBottom() - box / 2.0f - place[(std::size_t) op].y * stepY };

    g.setColour(cyan.withAlpha(0.6f));
    for (int from = 0; from < kOperators; ++from)
        for (int to = 0; to < kOperators; ++to)
            if (algorithm.modulates(from, to))
            {
                const auto a = centre[(std::size_t) from], b = centre[(std::size_t) to];
                g.drawLine(a.x, a.y + box / 2.0f, b.x, b.y - box / 2.0f, 1.5f);
            }
    // What is heard: the carriers into one line along the bottom.
    const float out = area.getBottom() - 10.0f;
    float left = inner.getRight(), right = inner.getX();
    g.setColour(yellow.withAlpha(0.6f));
    for (int op = 0; op < kOperators; ++op)
        if (algorithm.isCarrier(op))
        {
            const auto c = centre[(std::size_t) op];
            g.drawLine(c.x, c.y + box / 2.0f, c.x, out, 1.5f);
            left = std::min(left, c.x);
            right = std::max(right, c.x);
        }
    g.drawLine(left, out, right, out, 1.5f);
    // The feedback: a loop out of the operator's right side and back in at its top.
    const auto fb = centre[(std::size_t) algorithm.feedback];
    const float reach = box * 0.3f;
    juce::Path loop;
    loop.startNewSubPath(fb.x + box / 2.0f, fb.y);
    loop.lineTo(fb.x + box / 2.0f + reach, fb.y);
    loop.lineTo(fb.x + box / 2.0f + reach, fb.y - box / 2.0f - reach);
    loop.lineTo(fb.x, fb.y - box / 2.0f - reach);
    loop.lineTo(fb.x, fb.y - box / 2.0f);
    g.setColour(cyan.withAlpha(0.6f));
    g.strokePath(loop.createPathWithRoundedCorners(3.0f), juce::PathStrokeType(1.5f));

    g.setFont(mono(std::max(10.0f, box * 0.45f)));
    for (int op = 0; op < kOperators; ++op)
    {
        const auto bounds = juce::Rectangle<float>(box, box).withCentre(centre[(std::size_t) op]);
        const bool carrier = algorithm.isCarrier(op);
        g.setColour(carrier ? yellow : screen);
        g.fillRect(bounds);
        g.setColour(carrier ? yellow : cyan);
        g.drawRect(bounds, 1.5f);
        g.setColour(carrier ? screen : cyan);
        g.drawText(juce::String(op + 1), bounds, juce::Justification::centred);
    }
}

DxAlgorithmSection::DxAlgorithmSection(juce::AudioProcessorValueTreeState& state)
    : Section(state, "Algorithmus",
              { { { "algorithm", "Algorithmus", 2 }, { "feedback", "Feedback" } },
                { { "transpose", "Transpon." }, { "volume", utf8("Lautstärke") } } },
              {}, { kAlgorithmDisplay, 0 }),
      view(state)
{
    addAndMakeVisible(view);
}

void DxAlgorithmSection::resized()
{
    Section::resized();
    view.setBounds(displayArea().reduced(0, 4));
}

DxEditor::DxEditor(DxProcessor& owner)
    : AudioProcessorEditor(owner),
      processor(owner),
      keyboard(owner.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      scope(owner.scope)
{
    setLookAndFeel(&lookAndFeel);
    auto& state = processor.state;

    presets.setTextWhenNothingSelected("Klang");
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0)
            choose(presets.getSelectedId() - 1);
    };
    addAndMakeVisible(presets);
    previous.onClick = [this] { step(-1); };
    next.onClick = [this] { step(1); };
    previous.setConnectedEdges(juce::Button::ConnectedOnRight);
    next.setConnectedEdges(juce::Button::ConnectedOnLeft);
    sysexButton.setButtonText("SysEx laden");
    sysexButton.onClick = [this] { importSysex(); };
    for (auto* button : { &previous, &next, &sysexButton })
        addAndMakeVisible(*button);
    message.setFont(mono(11.0f));
    message.setColour(juce::Label::textColourId, cyan.withAlpha(0.8f));
    message.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(message);

    algorithmSection = std::make_unique<DxAlgorithmSection>(state);
    lfoSection = std::make_unique<Section>(state, "LFO",
                                           std::vector<Section::Row> { { { "lfoWave", "Welle", 2 },
                                                                         { "lfoSpeed", "Tempo" },
                                                                         { "lfoDelay", utf8("Verzög.") } },
                                                                       { { "lfoPitch", utf8("Tonhöhe") },
                                                                         { "lfoPitchSens", "Empfindl." },
                                                                         { "lfoAmp", utf8("Lautst.") } } });
    fxSection = std::make_unique<Section>(state, "Effekte",
                                          std::vector<Section::Row> { { { "fxDelayMix", "Delay" },
                                                                        { "fxDelaySync", "Sync" },
                                                                        { "fxDelayTime", "Zeit", 1, "fxDelaySync", false },
                                                                        { "fxDelayDivision", "Notenwert", 2, "fxDelaySync", true } },
                                                                      { { "fxDelayFeedback", "Feedback" },
                                                                        { "fxDelayTone", "Ton" },
                                                                        { "fxReverbMix", "Hall" },
                                                                        { "fxReverbDecay", utf8("Länge") } } });
    for (auto* section : std::initializer_list<Section*> { algorithmSection.get(), lfoSection.get(), fxSection.get() })
        addAndMakeVisible(*section);

    for (int op = 0; op < kOperators; ++op)
    {
        const auto name = juce::String(op + 1);
        operators[(std::size_t) op] = std::make_unique<OperatorSection>(
            state, "OP " + name, op,
            std::vector<Section::Row> { { { opId(op, "Coarse"), "Ratio" }, { opId(op, "Fine"), "Fein" }, { opId(op, "Level"), "Pegel" } } },
            opId(op, "On"), Section::Display { 0, kOperatorDisplay });
        operators[(std::size_t) op]->view.onClick = [this, op] { selectOperator(op); };
        addAndMakeVisible(*operators[(std::size_t) op]);

        details[(std::size_t) op] = std::make_unique<OperatorSection>(
            state, "Operator " + name + utf8(" · Details"), op,
            std::vector<Section::Row> { { { opId(op, "R1"), "Rate 1" },
                                          { opId(op, "R2"), "Rate 2" },
                                          { opId(op, "R3"), "Rate 3" },
                                          { opId(op, "R4"), "Rate 4" },
                                          { opId(op, "Detune"), "Verstimm." },
                                          { opId(op, "Velocity"), "Anschlag" } },
                                        { { opId(op, "L1"), "Pegel 1" },
                                          { opId(op, "L2"), "Pegel 2" },
                                          { opId(op, "L3"), "Pegel 3" },
                                          { opId(op, "L4"), "Pegel 4" },
                                          { opId(op, "RateScale"), "Tast.-Rate" },
                                          { opId(op, "Ams"), "LFO-Pegel" } } },
            opId(op, "On"), Section::Display { kDetailDisplay, 0 });
        addChildComponent(*details[(std::size_t) op]);
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
    selectOperator(0);
    timerCallback();
    setSize(kWidth, preferredHeight());
    startTimerHz(30);
}

DxEditor::~DxEditor() { setLookAndFeel(nullptr); }

int DxEditor::preferredHeight() const
{
    return 2 * kMargin + kHeader + kBottom + 3 * kGap
           + std::max({ algorithmSection->preferredHeight(), lfoSection->preferredHeight(), fxSection->preferredHeight() })
           + operators[0]->preferredHeight() + details[0]->preferredHeight();
}

void DxEditor::selectOperator(int op)
{
    selected = std::clamp(op, 0, kOperators - 1);
    for (int i = 0; i < kOperators; ++i)
    {
        operators[(std::size_t) i]->view.setSelected(i == selected);
        details[(std::size_t) i]->setVisible(i == selected);
    }
}

void DxEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK DX");
}

void DxEditor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeader).reduced(0, 7);
    header.removeFromLeft(200);
    sysexButton.setBounds(header.removeFromRight(110));
    header.removeFromRight(8);
    next.setBounds(header.removeFromRight(28));
    previous.setBounds(header.removeFromRight(28));
    header.removeFromRight(4);
    presets.setBounds(header.removeFromRight(240));
    header.removeFromRight(8);
    message.setBounds(header);

    auto bottom = area.removeFromBottom(kBottom);
    scope.setBounds(bottom.removeFromRight(kScopeWidth));
    bottom.removeFromRight(kGap);
    keyboard.setBounds(bottom);
    keyboard.setKeyWidth((float) keyboard.getWidth() / 50.0f);
    area.removeFromBottom(kGap);

    layoutRow(area, { algorithmSection.get(), lfoSection.get(), fxSection.get() });
    layoutRow(area, { operators[0].get(), operators[1].get(), operators[2].get(), operators[3].get(),
                      operators[4].get(), operators[5].get() });
    // The six detail panels share one place; only the chosen operator's is shown.
    auto place = area;
    layoutRow(place, { details[0].get() });
    for (int op = 1; op < kOperators; ++op)
        details[(std::size_t) op]->setBounds(details[0]->getBounds());
}

void DxEditor::timerCallback()
{
    algorithmSection->view.update();
    const int index = algorithmSection->view.algorithm();
    const auto& algorithm = algorithms()[(std::size_t) std::clamp(index, 0, kAlgorithms - 1)];
    for (int op = 0; op < kOperators; ++op)
    {
        auto& small = operators[(std::size_t) op]->view;
        auto& large = details[(std::size_t) op]->view;
        small.update();
        large.update();
        const auto role = roleOf(algorithm, op);
        small.setCaption(role, algorithm.isCarrier(op));
        large.setCaption(role, algorithm.isCarrier(op));
    }
}

void DxEditor::refreshPresets()
{
    presets.clear(juce::dontSendNotification);
    menuPatches.clear();
    auto addGroup = [this](const juce::String& heading, const std::vector<NamedDxPatch>& list) {
        if (list.empty())
            return;
        presets.addSectionHeading(heading);
        for (const auto& sound : list)
        {
            menuPatches.push_back(sound);
            presets.addItem(utf8(sound.name.c_str()), (int) menuPatches.size());
        }
    };
    // The factory sounds under their groups' headings, then each imported bank under its name.
    const auto& factory = factoryPresets();
    int group = -1;
    for (const int i : menuOrder((int) factory.size(), [&factory](int n) { return factory[(std::size_t) n].category; }))
    {
        const auto& sound = factory[(std::size_t) i];
        const int category = categoryIndex(sound.category);
        if (category != group && category >= 0)
            presets.addSectionHeading(utf8(sound.category));
        group = category;
        menuPatches.push_back(sound);
        presets.addItem(utf8(sound.name.c_str()), (int) menuPatches.size());
    }
    for (const auto& bank : DxProcessor::banks())
        addGroup(bank.name, bank.voices);

    const auto current = processor.presetName();
    for (std::size_t i = 0; i < menuPatches.size(); ++i)
        if (utf8(menuPatches[i].name.c_str()) == current)
        {
            presets.setSelectedId((int) i + 1, juce::dontSendNotification);
            break;
        }
    if (presets.getSelectedId() == 0)
        presets.setText(current, juce::dontSendNotification);
}

void DxEditor::choose(int index)
{
    if (! juce::isPositiveAndBelow(index, (int) menuPatches.size()))
        return;
    const auto& sound = menuPatches[(std::size_t) index];
    processor.applyPatch(sound.patch, utf8(sound.name.c_str()));
    presets.setSelectedId(index + 1, juce::dontSendNotification);
    setMessage({});
}

void DxEditor::step(int direction)
{
    if (menuPatches.empty())
        return;
    const int count = (int) menuPatches.size();
    const int now = presets.getSelectedId() - 1;
    choose(now < 0 ? 0 : (now + direction + count) % count);
}

void DxEditor::setMessage(const juce::String& text, bool error)
{
    message.setColour(juce::Label::textColourId, error ? red : cyan.withAlpha(0.8f));
    message.setText(text, juce::dontSendNotification);
}

void DxEditor::importSysex()
{
    chooser = std::make_unique<juce::FileChooser>("DX7-SysEx laden", juce::File(), "*.syx;*.SYX");
    juce::Component::SafePointer<DxEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser& fc) {
                             if (safe == nullptr || fc.getResult() == juce::File())
                                 return;
                             const auto file = fc.getResult();
                             const int count = DxProcessor::importSysex(file);
                             if (count == 0)
                             {
                                 safe->setMessage(utf8("Keine DX7-Klänge in der Datei (erwartet: Bank mit 32 Klängen "
                                                       "oder ein einzelner Klang)."),
                                                  true);
                                 return;
                             }
                             safe->refreshPresets();
                             // The bank's first voice, so its sound is heard at once.
                             const auto bank = file.getFileNameWithoutExtension();
                             int first = -1;
                             int index = 0;
                             for (const auto& b : DxProcessor::banks())
                             {
                                 if (b.name == bank)
                                 {
                                     first = (int) factoryPresets().size() + index;
                                     break;
                                 }
                                 index += (int) b.voices.size();
                             }
                             if (first >= 0)
                                 safe->choose(first);
                             safe->setMessage(utf8("Bank „") + bank + utf8("“: ") + juce::String(count)
                                              + utf8(count == 1 ? " Klang geladen." : " Klänge geladen."));
                         });
}
} // namespace tonwerkdx
