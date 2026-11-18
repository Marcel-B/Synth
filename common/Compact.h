#pragma once

#include "NightCity.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

/**
 * An editor for a screen too small for the full one (a phone): the plugin's panels sorted onto pages, one page at a
 * time, its knobs as large as the room allows. A plugin names its pages (`CompactPage`); the panels are the same
 * `Section`s as in the full editor, laid out at their own size and then scaled up as far as the page still fits, so a
 * page with few knobs gets large ones. Displays that only decorate (envelope curves, the oscilloscope, the keyboard)
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
    /** A knob below this scale would be smaller than a finger needs (60 points wide at scale 1). */
    static constexpr float kSmallest = 0.75f;
    static constexpr float kLargest = 1.6f;
    static constexpr int kTabs = 36;

    CompactEditor(juce::AudioProcessor& owner, juce::AudioProcessorValueTreeState& state, const juce::String& name,
                  const std::vector<CompactPage>& pageList)
        : juce::AudioProcessorEditor(owner), title(name)
    {
        setLookAndFeel(&look);
        setOpaque(true);
        for (const auto& described : pageList)
        {
            auto& page = *pages.emplace_back(std::make_unique<Page>());
            for (const auto& panel : described.panels)
            {
                page.sections.push_back(
                    std::make_unique<Section>(state, panel.title, std::vector<Section::Row> { panel.row }));
                page.holder.addAndMakeVisible(*page.sections.back());
            }
            canvas.addChildComponent(page.holder);

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
        paintTitle(g, titleArea, title);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kMargin, 0);
        auto header = area.removeFromTop(kHeader).reduced(0, 4);
        next.setBounds(header.removeFromRight(40));
        presets.setBounds(header.removeFromRight(juce::jmin(220, header.getWidth() / 3)).reduced(kGap / 2, 0));
        previous.setBounds(header.removeFromRight(40));
        titleArea = header;

        auto row = area.removeFromTop(kTabs);
        const int each = tabs.isEmpty() ? 0 : (row.getWidth() + kGap) / tabs.size();
        for (auto* tab : tabs)
            tab->setBounds(row.removeFromLeft(each).withTrimmedRight(kGap));
        area.removeFromTop(kGap);
        view.setBounds(area);
        layoutPage();
    }

private:
    struct Page
    {
        juce::Component holder;
        std::vector<std::unique_ptr<Section>> sections;
    };

    void show(int index)
    {
        current = juce::jlimit(0, juce::jmax(0, (int) pages.size() - 1), index);
        for (std::size_t i = 0; i < pages.size(); ++i)
            pages[i]->holder.setVisible((int) i == current);
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
    static int flow(Page& page, int width, bool place)
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

    void layoutPage()
    {
        if (pages.empty() || view.getWidth() <= 0 || view.getHeight() <= 0)
            return;
        auto& page = *pages[(std::size_t) current];
        const auto w = (float) view.getWidth(), h = (float) view.getHeight();
        // The largest scale at which the page still fits; below the smallest it scrolls instead.
        scale = kSmallest;
        for (float s = kLargest; s >= kSmallest; s -= 0.025f)
            if ((float) flow(page, (int) (w / s), false) <= h / s)
            {
                scale = s;
                break;
            }
        const int inner = (int) (w / scale);
        const int height = flow(page, inner, true);
        page.holder.setTransform({});
        page.holder.setBounds(0, 0, inner, height);
        page.holder.setTransform(juce::AffineTransform::scale(scale));
        canvas.setSize(view.getWidth(), juce::jmax(1, (int) std::ceil((float) height * scale)));
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
