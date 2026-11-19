#pragma once

#include "NightCity.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

/**
 * An editor for a screen too small for the full one (a phone): the plugin's panels sorted onto pages, one page at a
 * time, its knobs as large as the room allows. A plugin names its pages (`CompactPage`); the panels are the same
 * `Section`s as in the full editor, laid out at their own size and then scaled up as far as the page still fits, so a
 * page with few knobs gets large ones. Each page exists three times, its panels as one row of knobs, as two and as
 * three: a wide, low screen is filled better by rows of large knobs than by one row with half the height left empty.
 * Every page has the same knob size, the largest at which all of them fit (within limits; below the least a page
 * scrolls), and each page takes the arrangement that fits at it, first one that puts its panels side by side. Displays that only decorate (envelope curves, the oscilloscope, the keyboard)
 * are left out; the app puts keys and scope under the editor on demand.
 */
namespace tonwerkui
{
/** One page: its name on the tab and its panels, each one row of controls. */
struct CompactPage
{
    struct Panel
    {
        juce::String title;
        Section::Row row;
    };

    juce::String title;
    std::vector<Panel> panels;
};

class CompactEditor : public juce::AudioProcessorEditor
{
public:
    /**
     * As small as a knob gets (60 points a place at scale 1): the size of the first prototype's oscillator page,
     * which Marcel set as the least on his iPhone. A page that does not fit at it scrolls.
     */
    static constexpr float kSmallest = 1.05f;
    /**
     * As large as a knob gets: 90 points a place. Marcel found 80 best on the first prototype and then asked for
     * more where the keys are put away; the pages are cut so that all of them reach about 85 on a phone.
     */
    static constexpr float kLargest = 1.5f;
    static constexpr int kTabs = 36;
    /** The component property a host sets to take the header (name, sounds) away. */
    static constexpr const char* kBare = "tonwerkBare";
    /** A menu's height before the page's scale: a finger's 44 points at the scales a phone gets. */
    static constexpr int kMenu = 40;

    CompactEditor(juce::AudioProcessor& owner, juce::AudioProcessorValueTreeState& state, const juce::String& name,
                  const std::vector<CompactPage>& pageList)
        : juce::AudioProcessorEditor(owner), title(name)
    {
        setLookAndFeel(&look);
        look.setPopupFontSize(18.0f);
        setOpaque(true);
        for (const auto& described : pageList)
        {
            auto& page = *pages.emplace_back(std::make_unique<Page>());
            for (const auto& panel : described.panels)
                for (std::size_t rows = 1; rows <= page.layouts.size(); ++rows)
                    page.layouts[rows - 1].add(state, panel.title, inRows(panel.row, rows));
            for (auto& layout : page.layouts)
                canvas.addChildComponent(layout.holder);

            auto* tab = tabs.add(new juce::TextButton(tr(described.title)));
            const int index = tabs.size() - 1;
            tab->setClickingTogglesState(true);
            tab->setRadioGroupId(1);
            tab->setColour(juce::TextButton::buttonOnColourId, colours::panel);
            tab->setColour(juce::TextButton::textColourOnId, colours::cyan);
            tab->onClick = [this, index] { show(index); };
            addAndMakeVisible(tab);
        }

        // The factory sounds are the host's programs in every Tonwerk plugin, so one menu serves them all.
        for (int i = 0; i < owner.getNumPrograms(); ++i)
            presets.addItem(owner.getProgramName(i), i + 1);
        presets.setSelectedId(owner.getCurrentProgram() + 1, juce::dontSendNotification);
        presets.setTextWhenNothingSelected(tr("Preset"));
        presets.onChange = [this] {
            if (presets.getSelectedId() > 0)
                processor.setCurrentProgram(presets.getSelectedId() - 1);
        };
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next, &view })
            addAndMakeVisible(*c);

        view.setViewedComponent(&canvas, false);
        view.setScrollBarsShown(true, false);
        if (! tabs.isEmpty())
            tabs[0]->setToggleState(true, juce::dontSendNotification);
        show(0);
        setSize(756, 380);
    }

    ~CompactEditor() override { setLookAndFeel(nullptr); }

    /** The width of one knob's place on the page shown, in points. */
    float slotWidth() const { return scale * (float) kSlot; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::background);
        if (! titleArea.isEmpty())
            paintTitle(g, titleArea, title);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kMargin, 0);
        // The app has the sounds in a list of its own and the language in its setup (`HostFrame::setBare`): there the
        // header only cost a row of the phone's height.
        const bool bare = getProperties()[kBare];
        for (auto* c : std::initializer_list<juce::Component*> { &presets, &previous, &next })
            c->setVisible(! bare);
        titleArea = {};
        if (bare)
            area.removeFromTop(kGap);
        else
        {
            auto header = area.removeFromTop(kHeader).reduced(0, 4);
            next.setBounds(header.removeFromRight(40));
            presets.setBounds(header.removeFromRight(juce::jmin(220, header.getWidth() / 3)).reduced(kGap / 2, 0));
            previous.setBounds(header.removeFromRight(40));
            titleArea = header;
        }

        auto row = area.removeFromTop(kTabs);
        const int each = tabs.isEmpty() ? 0 : (row.getWidth() + kGap) / tabs.size();
        for (auto* tab : tabs)
            tab->setBounds(row.removeFromLeft(each).withTrimmedRight(kGap));
        area.removeFromTop(kGap);
        view.setBounds(area);
        layoutPage();
    }

private:
    /** A page's panels in one arrangement. */
    struct Layout
    {
        void add(juce::AudioProcessorValueTreeState& state, const juce::String& panelTitle,
                 std::vector<Section::Row> rows)
        {
            sections.push_back(std::make_unique<Section>(state, panelTitle, std::move(rows)));
            for (auto* child : sections.back()->getChildren())
                if (auto* control = dynamic_cast<Control*>(child))
                    control->setMenuHeight(kMenu);
            holder.addAndMakeVisible(*sections.back());
        }

        juce::Component holder;
        std::vector<std::unique_ptr<Section>> sections;
    };

    struct Page
    {
        /** The panels with one, two and three rows of controls each. */
        std::array<Layout, 3> layouts;
    };

    /**
     * A row of controls cut into `rows` of about the same width, never between two controls that take turns in one
     * place (`Item::when`); a row too short for it (under three controls a row) stays whole.
     */
    static std::vector<Section::Row> inRows(const Section::Row& row, std::size_t rows)
    {
        if (rows < 2 || row.size() < 3 * rows)
            return { row };
        const auto slotsOf = [](const Section::Item& item) { return std::max(1, item.slots); };
        int total = 0;
        for (const auto& item : row)
            total += slotsOf(item);
        const int perRow = (total + (int) rows - 1) / (int) rows;

        std::vector<Section::Row> result(1);
        int filled = 0;
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            const bool pair = i > 0 && row[i - 1].when.isNotEmpty() && row[i - 1].when == row[i].when;
            if (filled >= perRow && ! pair && result.size() < rows)
            {
                result.emplace_back();
                filled = 0;
            }
            result.back().push_back(row[i]);
            // A pair shares one place, so its second control takes no room of its own.
            filled += pair ? 0 : slotsOf(row[i]);
        }
        return result;
    }

    void show(int index)
    {
        current = juce::jlimit(0, juce::jmax(0, (int) pages.size() - 1), index);
        layoutPage();
    }

    void step(int direction)
    {
        const int to = processor.getCurrentProgram() + direction;
        if (juce::isPositiveAndBelow(to, processor.getNumPrograms()))
            presets.setSelectedId(to + 1, juce::sendNotificationSync);
    }

    /**
     * The panels left to right at their own size, a new line when one does not fit; returns the height they take, or
     * a height no page has when a panel is wider than the line.
     */
    static int flow(Layout& page, int width, bool place)
    {
        int x = 0, y = 0, lineHeight = 0;
        for (auto& section : page.sections)
        {
            const int w = section->preferredWidth(), h = section->preferredHeight();
            if (w > width && ! place)
                return 1 << 20;
            if (x > 0 && x + w > width)
            {
                y += lineHeight + kGap;
                x = 0;
                lineHeight = 0;
            }
            if (place)
                section->setBounds(x, y, w, h);
            x += w + kGap;
            lineHeight = std::max(lineHeight, h);
        }
        return y + lineHeight;
    }

    /** Whether the panels sit in one line at this width. */
    static bool sideBySide(const Layout& layout, int width)
    {
        int needed = -kGap;
        for (const auto& section : layout.sections)
            needed += section->preferredWidth() + kGap;
        return needed <= width;
    }

    /** Whether some arrangement of the page fits the view at this scale without scrolling. */
    bool fits(Page& page, float s) const
    {
        for (auto& layout : page.layouts)
            if ((float) flow(layout, (int) ((float) view.getWidth() / s), false) <= (float) view.getHeight() / s)
                return true;
        return false;
    }

    /** One knob size for all pages: the largest at which every page fits, never under the least. */
    float commonScale() const
    {
        for (float s = kLargest; s > kSmallest; s -= 0.025f)
            if (std::all_of(pages.begin(), pages.end(), [&](const auto& page) { return fits(*page, s); }))
                return s;
        return kSmallest;
    }

    void layoutPage()
    {
        if (pages.empty() || view.getWidth() <= 0 || view.getHeight() <= 0)
            return;
        for (auto& page : pages)
            for (auto& layout : page->layouts)
                layout.holder.setVisible(false);

        scale = commonScale();
        // Of the arrangements that fit, the first with the panels side by side (two oscillators to compare), else the
        // one with the fewest rows; when none fits, the one that scrolls least.
        auto& page = *pages[(std::size_t) current];
        const auto w = (float) view.getWidth();
        const int inner = (int) (w / scale);
        const int room = (int) ((float) view.getHeight() / scale);
        Layout* chosen = nullptr;
        for (auto& layout : page.layouts)
            if (flow(layout, inner, false) <= room && (chosen == nullptr || ! sideBySide(*chosen, inner))
                && (chosen == nullptr || sideBySide(layout, inner)))
                chosen = &layout;
        if (chosen == nullptr)
        {
            int least = 1 << 30;
            for (auto& layout : page.layouts)
                if (const int needs = flow(layout, inner, false); needs < least)
                {
                    least = needs;
                    chosen = &layout;
                }
        }

        const int width = (int) (w / scale);
        const int height = flow(*chosen, width, true);
        int used = 0;
        for (const auto& section : chosen->sections)
            used = std::max(used, section->getRight());
        // In the middle of the room, so a page with few knobs does not sit in a corner.
        const float dx = std::max(0.0f, (w - (float) used * scale) * 0.5f);
        const float dy = std::max(0.0f, ((float) view.getHeight() - (float) height * scale) * 0.5f);
        chosen->holder.setTransform({});
        chosen->holder.setBounds(0, 0, width, height);
        chosen->holder.setTransform(juce::AffineTransform::scale(scale).translated(dx, dy));
        chosen->holder.setVisible(true);
        canvas.setSize(view.getWidth(), juce::jmax(view.getHeight(), (int) std::ceil((float) height * scale)));
    }

    LookAndFeel look;
    juce::String title;
    juce::Rectangle<int> titleArea;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    juce::Viewport view;
    juce::Component canvas;
    std::vector<std::unique_ptr<Page>> pages;
    int current = 0;
    float scale = 1.0f;
};
} // namespace tonwerkui
