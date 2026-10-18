#include "HardwareSheet.h"

#include "HardwarePanel.h"

namespace tonwerkexpander
{
namespace
{
using namespace hardware;

constexpr int kWidth = 1800, kHeight = 1480;
const juce::Colour kPanel(0xffece7da), kInk(0xff1e1e1e), kMuted(0xff6b665c), kKnob(0xff151515), kAccent(0xffc23a22),
    kGrey(0xffb9b3a6);
constexpr float kTurn = juce::MathConstants<float>::pi * 5.0f / 6.0f; // 7 and 5 o'clock either side of 12

juce::String utf8(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }

class Sheet
{
public:
    explicit Sheet(juce::Image& image) : g(image) { g.fillAll(kPanel); }

    void text(float cx, float cy, const juce::String& s, float size, juce::Colour colour = kInk, bool bold = false,
              juce::Justification justification = juce::Justification::centred, float width = 420.0f)
    {
        g.setColour(colour);
        g.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
        float x = cx - width / 2.0f;
        if (justification == juce::Justification::centredLeft)
            x = cx;
        else if (justification == juce::Justification::centredRight)
            x = cx - width;
        g.drawFittedText(s, juce::Rectangle<float>(x, cy - size, width, size * 2.0f).toNearestInt(), justification, 1,
                         0.8f);
    }

    void box(float x0, float y0, float x1, float y1, const juce::String& title)
    {
        juce::Path frame;
        frame.addRectangle(x0, y0, x1 - x0, y1 - y0);
        juce::Path dashed;
        const float dashes[] { 8.0f, 6.0f };
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, frame, dashes, 2);
        g.setColour(kMuted);
        g.fillPath(dashed);
        if (title.isNotEmpty())
            text((x0 + x1) / 2.0f, y0 + 30.0f, title, 28.0f, kInk, true);
    }

    void knob(float cx, float cy, float r, const juce::String& label, const Knob& k, const juce::String& left = {},
              const juce::String& right = {})
    {
        text(cx, cy - r - 32.0f, label, 18.0f, kInk, true);
        const float ring = r + 12.0f;
        juce::Path travel;
        travel.addCentredArc(cx, cy, ring, ring, 0.0f, -kTurn, kTurn, true);
        g.setColour(kGrey);
        g.strokePath(travel, juce::PathStrokeType(5.0f));
        const float angle = -kTurn + 2.0f * kTurn * (k.set ? k.position : 0.5f);
        if (k.set && k.position > 0.002f)
        {
            juce::Path set;
            set.addCentredArc(cx, cy, ring, ring, 0.0f, -kTurn, angle, true);
            g.setColour(kAccent);
            g.strokePath(set, juce::PathStrokeType(7.0f));
        }
        g.setColour(kKnob);
        g.fillEllipse(cx - r, cy - r, 2.0f * r, 2.0f * r);
        g.setColour(k.set ? juce::Colours::white : juce::Colour(0xff555555));
        g.drawLine(cx, cy, cx + (r - 6.0f) * std::sin(angle), cy - (r - 6.0f) * std::cos(angle), 6.0f);
        if (left.isNotEmpty())
        {
            text(cx - ring - 4.0f, cy + ring, left, 14.0f, kMuted, false, juce::Justification::centredRight, 80.0f);
            text(cx + ring + 4.0f, cy + ring, right, 14.0f, kMuted, false, juce::Justification::centredLeft, 80.0f);
        }
        if (! k.set)
        {
            text(cx, cy + ring + 26.0f, k.value.empty() ? juce::String::fromUTF8("nach Gehör") : utf8(k.value), 16.0f, kMuted,
                 false, juce::Justification::centred, 2.4f * ring + 60.0f);
            return;
        }
        text(cx, cy + ring + 26.0f, utf8(clockOf(k.position)) + " Uhr", 22.0f, kAccent, true);
        if (! k.value.empty())
            text(cx, cy + ring + 52.0f, utf8(k.value), 16.0f, kMuted, false, juce::Justification::centred,
                 2.4f * ring + 60.0f);
    }

    /** A slide switch; `chosen` -1 leaves every position grey. */
    void slide(float cx, float cy, std::initializer_list<const char*> options, int chosen, const juce::String& label = {},
               float cell = 60.0f)
    {
        const float x0 = cx - cell * (float) options.size() / 2.0f;
        if (label.isNotEmpty())
            text(cx, cy - 48.0f, label, 18.0f, kInk, true);
        int i = 0;
        for (const auto* option : options)
        {
            const float x = x0 + cell * (float) i;
            const bool on = i == chosen;
            text(x + cell / 2.0f, cy - 20.0f, juce::String::fromUTF8(option), 14.0f, on ? kAccent : kMuted, on,
                 juce::Justification::centred, cell);
            g.setColour(on ? kKnob : kGrey);
            g.fillRect(x + 4.0f, cy - 6.0f, cell - 8.0f, 24.0f);
            ++i;
        }
    }

    juce::Graphics g;
};

int sourceIndex(VcoSource s) { return s == VcoSource::env ? 0 : s == VcoSource::envAndVibrato ? 1 : s == VcoSource::lfo ? 2 : -1; }
int sourceIndex(VcfSource s) { return s == VcfSource::env ? 0 : s == VcfSource::ext ? 1 : s == VcfSource::lfo ? 2 : -1; }
} // namespace

juce::Image drawHardwareSheet(const Settings& settings, const juce::String& name)
{
    const Panel p = panelFor(settings);
    juce::Image image(juce::Image::RGB, kWidth, kHeight, true);
    Sheet s(image);
    s.text(40.0f, 50.0f, "Tonwerk Expander " + juce::String::fromUTF8("· ") + name, 36.0f, kInk, true,
           juce::Justification::centredLeft, 1100.0f);
    s.text(kWidth - 40.0f, 50.0f,
           juce::String::fromUTF8(p.secondSilent ? "Modul 2 stumm" : "beide Module gleich einstellen"), 20.0f,
           kMuted, false, juce::Justification::centredRight, 600.0f);

    // Oscillators.
    const float vcoX[2] { 40.0f, 420.0f };
    for (int n = 0; n < 2; ++n)
    {
        const auto i = (size_t) n;
        const float x0 = vcoX[i], cx = x0 + 145.0f;
        s.box(x0, 110.0f, x0 + 290.0f, 880.0f, "VCO " + juce::String(n + 1));
        s.knob(cx, 250.0f, 52.0f, "FREQUENCY", Knob { 0.5f, p.tuning[i], false });
        s.knob(cx, 470.0f, 32.0f, "MODULATION", p.vcoModulation[i], "FREQ", "PW");
        s.slide(cx, 650.0f, { "ENV", "E+V", "LFO" }, sourceIndex(p.vcoSource[i]));
        s.knob(cx, 760.0f, 32.0f, "PULSE WIDTH", p.pulseWidth[i], "10%", "90%");
    }
    s.slide(375.0f, 270.0f, { "aus", "ON" }, p.sync ? 1 : 0, "SYNC", 40.0f);

    // Filter and its mixer.
    s.box(760.0f, 110.0f, 1330.0f, 880.0f, "VCF");
    s.knob(905.0f, 250.0f, 52.0f, "FREQUENCY", p.cutoff);
    s.knob(1185.0f, 250.0f, 52.0f, "RESONANCE", p.resonance);
    s.knob(905.0f, 470.0f, 32.0f, "MODULATION", p.vcfModulation, juce::String::fromUTF8("−"), "+");
    s.slide(905.0f, 650.0f, { "ENV 2", "EXT", "LFO" }, sourceIndex(p.vcfSource));
    s.knob(1185.0f, 470.0f, 32.0f, "NOTCH", p.notch, "LP", "HP");
    s.slide(1185.0f, 650.0f, { "LP-HP", "BP" }, p.bandpass ? 1 : 0);
    s.knob(860.0f, 760.0f, 30.0f, "VCO 1", p.mixer[0], "SAW", "PUL");
    s.knob(1045.0f, 760.0f, 30.0f, "VCO 2", p.mixer[1], "SAW", "PUL");
    s.knob(1230.0f, 760.0f, 30.0f, "EXT", p.ext, "IN", "NOISE");

    // Envelopes, LFO and VCA.
    s.box(40.0f, 910.0f, 560.0f, 1160.0f, "ENV 1 (VCA)");
    s.box(760.0f, 910.0f, 1330.0f, 1160.0f, "ENV 2");
    const char* stages[] { "ATTACK", "DECAY", "SUSTAIN" };
    const float ampX[] { 130.0f, 300.0f, 470.0f }, filterX[] { 860.0f, 1045.0f, 1230.0f };
    for (size_t i = 0; i < 3; ++i)
    {
        s.knob(ampX[i], 1040.0f, 30.0f, stages[i], p.ampEnvelope[i]);
        s.knob(filterX[i], 1040.0f, 30.0f, stages[i], p.filterEnvelope[i]);
    }
    s.knob(660.0f, 960.0f, 30.0f, "LFO", p.lfoRate);
    s.slide(660.0f, 1130.0f, { "ENV", "E+V", "EXT" }, 0, "VCA", 52.0f);

    // The two-voice controller.
    s.box(1370.0f, 110.0f, 1760.0f, 1160.0f, "STEUERMODUL");
    s.slide(1565.0f, 230.0f, { "UNI", "SPLIT", "DUO" }, p.assign == Assign::uni ? 0 : p.assign == Assign::split ? 1 : 2,
            "ASSIGN");
    const float xmX[] { 1470.0f, 1660.0f };
    for (size_t i = 0; i < 2; ++i)
    {
        const float cx = xmX[i];
        s.text(cx, 310.0f, "STIMME " + juce::String((int) i + 1), 24.0f, kInk, true);
        s.knob(cx, 410.0f, 30.0f, "LEVEL", i == 1 && p.secondSilent ? Knob { 0.0f, "zu" } : Knob { 0.5f, "", false });
        s.knob(cx, 630.0f, 30.0f, "PAN", p.pan[i]);
        s.knob(cx, 860.0f, 30.0f, "PORTAMENTO", i == 0 ? p.portamento : Knob { p.portamento.position, "wie Stimme 1" });
    }
    s.slide(1565.0f, 1110.0f, { "aus", "ON" }, p.portamentoOn ? 1 : 0, "PORTAMENTO");

    // What does not carry over.
    float y = 1200.0f;
    s.text(40.0f, y, juce::String::fromUTF8(p.notes.empty() ? "Alles überträgt sich." : "Geht nicht 1:1:"), 21.0f,
           kInk, true, juce::Justification::centredLeft, 600.0f);
    for (size_t i = 0; i < std::min<size_t>(p.notes.size(), 7); ++i)
    {
        y += 30.0f;
        s.text(60.0f, y, juce::String::fromUTF8("• ") + utf8(p.notes[i]), 17.0f, kInk, false,
               juce::Justification::centredLeft, kWidth - 100.0f);
    }
    s.text(kWidth - 40.0f, kHeight - 26.0f,
           juce::String::fromUTF8("Stellung als Uhrzeit: ganz links 7 Uhr, Mitte 12 Uhr, ganz rechts 5 Uhr. Die Skalen "
                                  "des Moduls sind geschätzt; die Feinheiten macht das Ohr."),
           14.0f, kMuted, false, juce::Justification::centredRight, 1400.0f);
    return image;
}
} // namespace tonwerkexpander
