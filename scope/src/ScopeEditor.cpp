#include "ScopeEditor.h"

#include "PresetCategories.h"
#include "ScopePresets.h"

namespace tonwerkscope
{
using namespace tonwerkui;

namespace
{
constexpr int kDivisionsAcross = 10, kDivisionsUp = 8;
constexpr float kMeterWidth = 52.0f;
constexpr float kMeterFloor = -60.0f;
/** The most auto zoom lifts a quiet signal, so hiss is not blown up to full height. */
constexpr float kMaxZoom = 63.0f; // 36 dB
const juce::Colour kGreen { 0xff39ff14 };

juce::String dbText(float db) { return db <= -99.0f ? juce::String("-INF") : juce::String(db, 1); }

/** A time per division as a scope writes it: µs below a millisecond, ms below a second. */
juce::String perDivision(double seconds)
{
    if (seconds < 0.001)
        return juce::String(juce::roundToInt(seconds * 1.0e6)) + utf8(" µS/DIV");
    if (seconds < 1.0)
        return (seconds < 0.01 ? juce::String(seconds * 1000.0, 1) : juce::String(juce::roundToInt(seconds * 1000.0)))
               + " MS/DIV";
    return juce::String(seconds, 1) + " S/DIV";
}

/** The path drawn three times, wide and faint to narrow and bright: the glow of a lit trace. */
void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width = 1.4f)
{
    g.setColour(colour.withAlpha(0.07f));
    g.strokePath(path, juce::PathStrokeType(width * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour.withAlpha(0.22f));
    g.strokePath(path, juce::PathStrokeType(width * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour.brighter(0.4f));
    g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

ScopeScreen::ScopeScreen(ScopeProcessor& p) : processor(p) { startTimerHz(30); }

void ScopeScreen::timerCallback()
{
    if (isShowing())
        refresh();
}

juce::Rectangle<float> ScopeScreen::plotArea() const
{
    auto inner = getLocalBounds().toFloat().reduced(1.0f).reduced(12.0f, 8.0f);
    inner.removeFromTop(16.0f);
    inner.removeFromBottom(26.0f);
    inner.removeFromRight(kMeterWidth + 10.0f);
    return inner;
}

void ScopeScreen::resized()
{
    scale = (float) juce::Component::getApproximateScaleFactorForComponent(this);
    const auto plot = plotArea();
    layer = juce::Image(juce::Image::ARGB, std::max(1, juce::roundToInt(plot.getWidth() * scale)),
                        std::max(1, juce::roundToInt(plot.getHeight() * scale)), true);
}

juce::Colour ScopeScreen::traceColour() const
{
    switch (shown.colour)
    {
        case 1: return colours::yellow;
        case 2: return colours::red;
        case 3: return kGreen;
        default: break;
    }
    return colours::cyan;
}

float ScopeScreen::zoomFor(float peak)
{
    if (! shown.autoGain)
        return zoom = std::pow(10.0f, shown.gainDb / 20.0f);
    const float wanted = peak > 1.0e-4f ? std::clamp(0.9f / peak, 0.5f, kMaxZoom) : zoom;
    // Up slowly so a decaying note does not swell, down at once so a loud one never runs off the screen.
    zoom = wanted < zoom ? wanted : zoom + 0.1f * (wanted - zoom);
    return zoom;
}

juce::String ScopeScreen::caption() const
{
    const auto dot = utf8(" · ");
    juce::String text;
    switch (shown.mode)
    {
        case Mode::wave:
        {
            text << "WELLE" << dot << juce::roundToInt(shown.timeMs) << " MS" << dot
                 << perDivision(shown.timeMs * 0.001 / kDivisionsAcross);
            const char* triggers[] = { "STEIGEND", "FALLEND", "FREI" };
            text << dot << triggers[(int) shown.trigger];
            if (shown.trigger != Trigger::free)
                text << " " << juce::roundToInt(shown.triggerLevel * 100.0f) << " %";
            break;
        }
        case Mode::bars:
            text << "TAKT" << dot << juce::String(kWindowNames[(std::size_t) shown.window]).toUpperCase() << dot
                 << juce::roundToInt(processor.tempo.load()) << " BPM" << dot
                 << (processor.playing.load() ? "SONG" : "EIGENER TAKT");
            break;
        case Mode::xy: text << "XY" << dot << "GONIOMETER" << dot << juce::roundToInt(shown.timeMs) << " MS"; break;
    }
    if (shown.mode != Mode::xy)
    {
        const char* channels[] = { "SUMME", "LINKS", "RECHTS", "L + R", "SEITE" };
        text << dot << channels[(int) shown.channel];
    }
    return text;
}

void ScopeScreen::refresh()
{
    shown = processor.readSettings();
    if (shown.hold)
    {
        repaint();
        return;
    }
    const double rate = processor.rate.load();
    if (std::abs(rate - estimatorRate) > 0.5)
    {
        estimator.prepare(rate);
        estimatorRate = rate;
    }
    const auto written = processor.ring.written();
    silent = written == lastWritten;
    lastWritten = written;

    // Enough for the window, a stretch before it to find the trigger in, and the pitch.
    const int length = juce::jlimit(16, Ring::kSize / 2, juce::roundToInt(shown.timeMs * 0.001 * rate));
    const int count = std::min(Ring::kSize, std::max(length + std::max(length, 2048) + 2, estimator.needed()));
    for (auto* v : { &left, &right, &signal, &second })
        v->resize((std::size_t) count);
    processor.ring.read(left.data(), right.data(), count);

    // The meters over the last frame's worth of samples, the correlation over the last 4096.
    const int recent = std::min(count, std::max(256, juce::roundToInt(rate / 30.0)));
    for (std::size_t side = 0; side < 2; ++side)
    {
        const auto& v = side == 0 ? left : right;
        float peak = 0.0f;
        double sum = 0.0;
        for (int n = count - recent; n < count; ++n)
        {
            peak = std::max(peak, std::abs(v[(std::size_t) n]));
            sum += (double) v[(std::size_t) n] * v[(std::size_t) n];
        }
        const float db = juce::Decibels::gainToDecibels(peak, -100.0f);
        // Falls back 1 dB a frame, as a peak meter's needle does.
        peakDb[side] = std::max(db, peakDb[side] - 1.0f);
        rmsDb[side] = juce::Decibels::gainToDecibels((float) std::sqrt(sum / recent), -100.0f);
    }
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int n = std::max(0, count - 4096); n < count; ++n)
    {
        lr += (double) left[(std::size_t) n] * right[(std::size_t) n];
        ll += (double) left[(std::size_t) n] * left[(std::size_t) n];
        rr += (double) right[(std::size_t) n] * right[(std::size_t) n];
    }
    const float now = ll * rr > 1.0e-12 ? (float) (lr / std::sqrt(ll * rr)) : 0.0f;
    correlation += 0.3f * (now - correlation);

    for (int n = 0; n < count; ++n)
    {
        const float l = left[(std::size_t) n], r = right[(std::size_t) n];
        switch (shown.channel)
        {
            case Channel::left: signal[(std::size_t) n] = l; break;
            case Channel::right: signal[(std::size_t) n] = r; break;
            case Channel::side: signal[(std::size_t) n] = 0.5f * (l - r); break;
            case Channel::sum:
            case Channel::both: signal[(std::size_t) n] = 0.5f * (l + r); break;
        }
    }
    if (++frame % 3 == 0)
    {
        for (std::size_t n = 0; n < left.size(); ++n)
            second[n] = 0.5f * (left[n] + right[n]);
        const float found = estimator.estimate(second.data(), count);
        // Held a little while it is unsure, so the readout does not blink between notes.
        if (found > 0.0f)
            pitch = found;
        else if (frame % 30 == 0)
            pitch = 0.0f;
    }

    // The layer: what was there fades by the afterglow, the new trace goes on top.
    if (! layer.isValid())
        resized();
    const float keep = shown.mode == Mode::bars ? 0.0f : std::clamp(shown.persistence, 0.0f, 1.0f) * 0.92f;
    if (keep <= 0.0f)
        layer.clear(layer.getBounds());
    else
        layer.multiplyAllAlphas(keep);
    {
        juce::Graphics g(layer);
        g.addTransform(juce::AffineTransform::scale(scale));
        const auto plot = plotArea().withZeroOrigin();
        if (shown.mode == Mode::wave)
            drawWave(g, plot);
        else if (shown.mode == Mode::xy)
            drawXy(g, plot);
        else
            drawBars(g, plot);
    }
    repaint();
}

void ScopeScreen::drawWave(juce::Graphics& g, juce::Rectangle<float> plot)
{
    const int count = (int) signal.size();
    const int length = juce::jlimit(16, Ring::kSize / 2, juce::roundToInt(shown.timeMs * 0.001 * processor.rate.load()));
    const int start = findTrigger(signal.data(), count, length, shown.triggerLevel, shown.trigger);
    float peak = 0.0f;
    for (int n = start; n < start + length; ++n)
        peak = std::max({ peak, std::abs(left[(std::size_t) n]), std::abs(right[(std::size_t) n]) });
    const float gain = zoomFor(peak);
    const float half = 0.5f * plot.getHeight();

    auto trace = [&](const std::vector<float>& v) {
        juce::Path path;
        const int width = std::max(1, juce::roundToInt(plot.getWidth()));
        auto toY = [&](float x) { return plot.getCentreY() - std::clamp(x * gain, -1.05f, 1.05f) * half; };
        if (length <= 2 * width)
        {
            for (int i = 0; i < length; ++i)
            {
                const float x = plot.getX() + (float) i / (float) (length - 1) * plot.getWidth();
                const float y = toY(v[(std::size_t) (start + i)]);
                if (i == 0)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }
            return path;
        }
        // More samples than pixels: each column draws its lowest and highest, so nothing between them is lost.
        for (int px = 0; px < width; ++px)
        {
            const int a = start + (int) ((long long) px * length / width);
            const int b = start + (int) ((long long) (px + 1) * length / width);
            float lo = v[(std::size_t) a], hi = lo;
            for (int n = a + 1; n < b; ++n)
            {
                lo = std::min(lo, v[(std::size_t) n]);
                hi = std::max(hi, v[(std::size_t) n]);
            }
            const float x = plot.getX() + (float) px;
            if (px == 0)
                path.startNewSubPath(x, toY(lo));
            else
                path.lineTo(x, toY(lo));
            path.lineTo(x, toY(hi));
        }
        return path;
    };

    if (shown.channel == Channel::both)
    {
        const auto other = traceColour() == colours::red ? colours::cyan : colours::red;
        strokeGlow(g, trace(right), other, 1.2f);
        strokeGlow(g, trace(left), traceColour());
    }
    else
        strokeGlow(g, trace(signal), traceColour());
}

void ScopeScreen::drawBars(juce::Graphics& g, juce::Rectangle<float> plot)
{
    const auto& bars = processor.bars;
    const int width = std::max(1, juce::roundToInt(plot.getWidth()));
    // The sides each column draws: one for the sum, left, right or side, both for "both".
    auto valueOf = [&](int column, bool highest, int side) {
        const auto pick = [&](int s) { return highest ? bars.maximum(column, s) : bars.minimum(column, s); };
        switch (shown.channel)
        {
            case Channel::left: return pick(0);
            case Channel::right: return pick(1);
            // Only the sides' extremes are kept, so the side's are an upper bound: wide enough to show width.
            case Channel::side: return 0.5f * (highest ? bars.maximum(column, 0) - bars.minimum(column, 1)
                                                       : bars.minimum(column, 0) - bars.maximum(column, 1));
            case Channel::both: return pick(side);
            case Channel::sum: break;
        }
        return 0.5f * (pick(0) + pick(1));
    };
    float peak = 0.0f;
    for (int c = 0; c < BeatTrace::kColumns; ++c)
        peak = std::max({ peak, std::abs(valueOf(c, true, 0)), std::abs(valueOf(c, false, 0)),
                          std::abs(valueOf(c, true, 1)), std::abs(valueOf(c, false, 1)) });
    const float gain = zoomFor(peak);
    const float half = 0.5f * plot.getHeight();
    auto toY = [&](float x) { return plot.getCentreY() - std::clamp(x * gain, -1.0f, 1.0f) * half; };

    auto envelope = [&](int side, juce::Colour colour) {
        juce::Path fill, top;
        std::vector<float> lows((std::size_t) width);
        for (int px = 0; px < width; ++px)
        {
            const int a = px * BeatTrace::kColumns / width;
            const int b = std::max(a + 1, (px + 1) * BeatTrace::kColumns / width);
            float lo = valueOf(a, false, side), hi = valueOf(a, true, side);
            for (int c = a + 1; c < b; ++c)
            {
                lo = std::min(lo, valueOf(c, false, side));
                hi = std::max(hi, valueOf(c, true, side));
            }
            lows[(std::size_t) px] = toY(lo);
            const float x = plot.getX() + (float) px;
            if (px == 0)
            {
                fill.startNewSubPath(x, toY(hi));
                top.startNewSubPath(x, toY(hi));
            }
            else
            {
                fill.lineTo(x, toY(hi));
                top.lineTo(x, toY(hi));
            }
        }
        for (int px = width - 1; px >= 0; --px)
            fill.lineTo(plot.getX() + (float) px, lows[(std::size_t) px]);
        fill.closeSubPath();
        g.setColour(colour.withAlpha(0.35f));
        g.fillPath(fill);
        g.setColour(colour.withAlpha(0.9f));
        g.strokePath(fill, juce::PathStrokeType(1.0f));
    };
    if (shown.channel == Channel::both)
    {
        envelope(1, traceColour() == colours::red ? colours::cyan : colours::red);
        envelope(0, traceColour());
    }
    else
        envelope(0, traceColour());

    // The beats across the window, bar lines brighter, and where the song is now.
    const double beats = kWindowBeats[(std::size_t) shown.window];
    for (int beat = 1; beat < (int) beats; ++beat)
    {
        const float x = plot.getX() + (float) (beat / beats) * plot.getWidth();
        g.setColour(colours::text.withAlpha(beat % 4 == 0 ? 0.35f : 0.12f));
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }
    const float x = plot.getX() + (float) (bars.playheadColumn() + 1) / BeatTrace::kColumns * plot.getWidth();
    g.setColour(colours::yellow);
    g.fillRect(juce::Rectangle<float>(x - 1.0f, plot.getY(), 2.0f, plot.getHeight()));
}

void ScopeScreen::drawXy(juce::Graphics& g, juce::Rectangle<float> plot)
{
    const int count = (int) left.size();
    const int length = std::min(count, juce::jlimit(256, 8192, juce::roundToInt(shown.timeMs * 0.001 * processor.rate.load())));
    float peak = 0.0f;
    for (int n = count - length; n < count; ++n)
        peak = std::max({ peak, std::abs(left[(std::size_t) n]), std::abs(right[(std::size_t) n]) });
    const float gain = zoomFor(peak);
    const float radius = 0.5f * std::min(plot.getWidth(), plot.getHeight());
    const auto centre = plot.getCentre();
    juce::Path path;
    for (int n = count - length; n < count; ++n)
    {
        // Mid up, side across: a mono signal is a vertical line, left alone leans to the left.
        const float l = left[(std::size_t) n] * gain, r = right[(std::size_t) n] * gain;
        const float mid = std::clamp(0.70710678f * (l + r), -1.05f, 1.05f);
        const float side = std::clamp(0.70710678f * (r - l), -1.05f, 1.05f);
        const juce::Point<float> point(centre.x + side * radius, centre.y - mid * radius);
        if (n == count - length)
            path.startNewSubPath(point);
        else
            path.lineTo(point);
    }
    strokeGlow(g, path, traceColour(), 1.0f);
}

void ScopeScreen::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto glass = paintScreen(g, area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass);
    const auto plot = plotArea();
    const auto colour = traceColour();

    // The graticule: divisions, the centre lines brighter with ticks, as on a bench scope.
    g.setColour(colours::border.withAlpha(0.55f));
    if (shown.mode == Mode::xy)
    {
        const float radius = 0.5f * std::min(plot.getWidth(), plot.getHeight());
        const auto c = plot.getCentre();
        g.drawEllipse(juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(c), 1.0f);
        g.drawEllipse(juce::Rectangle<float>(radius, radius).withCentre(c), 1.0f);
        g.setColour(colours::text.withAlpha(0.25f));
        g.drawLine(c.x - radius * 0.7071f, c.y - radius * 0.7071f, c.x + radius * 0.7071f, c.y + radius * 0.7071f, 1.0f);
        g.drawLine(c.x + radius * 0.7071f, c.y - radius * 0.7071f, c.x - radius * 0.7071f, c.y + radius * 0.7071f, 1.0f);
        g.drawVerticalLine(juce::roundToInt(c.x), c.y - radius, c.y + radius);
        g.setFont(mono(9.0f));
        g.setColour(colours::muted);
        g.drawText("L", juce::Rectangle<float>(c.x - radius * 0.7071f - 14.0f, c.y - radius * 0.7071f - 14.0f, 12.0f, 12.0f),
                   juce::Justification::centred);
        g.drawText("R", juce::Rectangle<float>(c.x + radius * 0.7071f + 2.0f, c.y - radius * 0.7071f - 14.0f, 12.0f, 12.0f),
                   juce::Justification::centred);
        g.drawText("M", juce::Rectangle<float>(c.x + 4.0f, c.y - radius, 12.0f, 12.0f), juce::Justification::centred);
    }
    else
    {
        for (int i = 1; i < kDivisionsAcross; ++i)
            g.drawVerticalLine(juce::roundToInt(plot.getX() + plot.getWidth() * (float) i / kDivisionsAcross), plot.getY(),
                               plot.getBottom());
        for (int i = 1; i < kDivisionsUp; ++i)
            g.drawHorizontalLine(juce::roundToInt(plot.getY() + plot.getHeight() * (float) i / kDivisionsUp), plot.getX(),
                                 plot.getRight());
        g.setColour(colours::text.withAlpha(0.22f));
        g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());
        g.drawVerticalLine(juce::roundToInt(plot.getCentreX()), plot.getY(), plot.getBottom());
        for (int i = 0; i <= 5 * kDivisionsAcross; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (5 * kDivisionsAcross);
            g.drawVerticalLine(juce::roundToInt(x), plot.getCentreY() - 3.0f, plot.getCentreY() + 3.0f);
        }
        for (int i = 0; i <= 5 * kDivisionsUp; ++i)
        {
            const float y = plot.getY() + plot.getHeight() * (float) i / (5 * kDivisionsUp);
            g.drawHorizontalLine(juce::roundToInt(y), plot.getCentreX() - 3.0f, plot.getCentreX() + 3.0f);
        }
        // Where the trigger waits.
        if (shown.mode == Mode::wave && shown.trigger != Trigger::free)
        {
            const float y = plot.getCentreY() - std::clamp(shown.triggerLevel * zoom, -1.0f, 1.0f) * 0.5f * plot.getHeight();
            juce::Path mark;
            mark.addTriangle(plot.getX() - 8.0f, y - 4.0f, plot.getX() - 8.0f, y + 4.0f, plot.getX() - 2.0f, y);
            g.setColour(colours::yellow.withAlpha(0.9f));
            g.fillPath(mark);
        }
    }
    g.setColour(colours::border);
    g.drawRect(plot, 1.0f);

    g.drawImage(layer, plot);

    // The readouts.
    const auto inner = area.reduced(12.0f, 8.0f);
    g.setFont(mono(10.0f));
    g.setColour(colour.withAlpha(0.85f));
    g.drawText(caption(), inner.withHeight(14.0f).toNearestInt(), juce::Justification::centredLeft);
    juce::String readout;
    if (shown.hold)
        readout = "GEHALTEN";
    else if (pitch > 0.0f)
    {
        const double note = noteOf(pitch);
        const int nearest = (int) std::lround(note);
        const int cents = (int) std::lround((note - nearest) * 100.0);
        readout << (pitch < 100.0f ? juce::String(pitch, 1) : juce::String(juce::roundToInt(pitch))) << " HZ" << utf8(" · ") << noteName(nearest)
              << noteOctave(nearest) << " " << (cents >= 0 ? "+" : "") << cents << " CT";
    }
    else
        readout = silent ? "KEIN SIGNAL" : "-- HZ";
    g.setColour(shown.hold || silent ? colours::red : colours::yellow);
    g.drawText(readout, inner.withHeight(14.0f).withTrimmedRight(kMeterWidth + 10.0f).toNearestInt(),
               juce::Justification::centredRight);

    const auto bottom = inner.withTop(inner.getBottom() - 16.0f).withTrimmedRight(kMeterWidth + 10.0f);
    const float peak = std::max(peakDb[0], peakDb[1]);
    g.setColour(peak > -0.1f ? colours::red : colours::cyan.withAlpha(0.8f));
    juce::String levels = "PEAK " + dbText(peak) + " DBFS" + utf8(" · ") + "RMS " + dbText(std::max(rmsDb[0], rmsDb[1]))
                          + " DBFS";
    levels << utf8(" · ZOOM ") << (zoom >= 1.0f ? "+" : "") << juce::roundToInt(juce::Decibels::gainToDecibels(zoom))
           << " DB";
    g.drawText(levels, bottom.toNearestInt(), juce::Justification::centredLeft);
    // The correlation as a needle on a bar, -1 (out of phase) to +1 (mono).
    auto bar = bottom.withLeft(bottom.getRight() - 180.0f).reduced(0.0f, 5.0f);
    g.setColour(colours::muted);
    g.setFont(mono(9.0f));
    g.drawText("-1", bar.withWidth(14.0f).translated(-16.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
    g.drawText("+1", bar.withX(bar.getRight() + 2.0f).withWidth(16.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour(colours::border);
    g.drawRect(bar, 1.0f);
    g.drawVerticalLine(juce::roundToInt(bar.getCentreX()), bar.getY(), bar.getBottom());
    const float needle = bar.getX() + (correlation + 1.0f) * 0.5f * bar.getWidth();
    g.setColour(correlation < 0.0f ? colours::red : kGreen);
    g.fillRect(juce::Rectangle<float>(needle - 1.5f, bar.getY() - 2.0f, 3.0f, bar.getHeight() + 4.0f));

    // The meters: both sides' peak (bright) over RMS (filled), -60 to 0 dBFS, with the scale between.
    auto meters = area.reduced(12.0f, 8.0f).removeFromRight(kMeterWidth);
    meters.removeFromTop(16.0f);
    meters.removeFromBottom(26.0f);
    auto toY = [&](float db) {
        return meters.getBottom() - std::clamp((db - kMeterFloor) / -kMeterFloor, 0.0f, 1.0f) * meters.getHeight();
    };
    for (int side = 0; side < 2; ++side)
    {
        const auto column = juce::Rectangle<float>(meters.getX() + (side == 0 ? 0.0f : meters.getWidth() - 14.0f),
                                                   meters.getY(), 14.0f, meters.getHeight());
        g.setColour(colours::border.withAlpha(0.6f));
        g.drawRect(column, 1.0f);
        const float rms = rmsDb[(std::size_t) side], peakSide = peakDb[(std::size_t) side];
        juce::ColourGradient fill(colours::red, column.getX(), column.getY(), colour, column.getX(), column.getBottom(), false);
        fill.addColour(0.25, colours::yellow);
        g.setGradientFill(fill);
        g.fillRect(column.withTop(toY(rms)).reduced(2.0f, 0.0f));
        g.setColour(peakSide > -0.1f ? colours::red : colours::text);
        g.fillRect(juce::Rectangle<float>(column.getX() + 1.0f, toY(peakSide) - 1.0f, column.getWidth() - 2.0f, 2.0f));
    }
    g.setFont(mono(8.0f));
    g.setColour(colours::muted);
    for (const float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        g.drawText(juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>(meters.getX() + 14.0f, toY(db) - 5.0f, meters.getWidth() - 28.0f, 10.0f),
                   juce::Justification::centred);
    g.drawText("L", juce::Rectangle<float>(meters.getX(), meters.getBottom() + 2.0f, 14.0f, 10.0f), juce::Justification::centred);
    g.drawText("R", juce::Rectangle<float>(meters.getRight() - 14.0f, meters.getBottom() + 2.0f, 14.0f, 10.0f),
               juce::Justification::centred);
}

ScopeEditor::ScopeEditor(ScopeProcessor& p)
    : AudioProcessorEditor(p),
      screen(p),
      processor(p),
      presets(p, p.programs, factoryPresets(),
              menuOrder((int) factoryPresets().size(), [](int i) { return factoryPresets()[(std::size_t) i].category; },
                        kScopeCategories)),
      view(p.state, "Anzeige",
           { { { "mode", "Modus" }, { "time", "Zeit" }, { "window", "Fenster" }, { "channel", "Kanal" },
               { "colour", "Farbe" } } }),
      trigger(p.state, "Trigger", { { { "trigger", "Flanke" }, { "triggerLevel", "Pegel" } } }),
      picture(p.state, "Bild",
              { { { "gain", utf8("Verstärkung") }, { "autoGain", "Auto" }, { "persistence", "Nachleuchten" },
                  { "hold", "Halten" } } })
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &screen, &view, &trigger, &picture })
        addAndMakeVisible(*c);
    const int minimumWidth = 2 * kMargin + view.preferredWidth() + trigger.preferredWidth() + picture.preferredWidth() + 2 * kGap;
    const int minimumHeight = 2 * kMargin + kHeader + 260 + kGap + view.preferredHeight();
    // The size the user left it at, kept with the project; read first, since setting the limits resizes and stores.
    const int width = (int) p.state.state.getProperty("editorWidth", minimumWidth);
    const int height = (int) p.state.state.getProperty("editorHeight", minimumHeight + 220);
    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, 2600, 1800);
    setSize(std::max(width, minimumWidth), std::max(height, minimumHeight));
}

ScopeEditor::~ScopeEditor() { setLookAndFeel(nullptr); }

void ScopeEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const auto header = getLocalBounds().reduced(kMargin + 4, 0).removeFromTop(kMargin + kHeader);
    paintTitle(g, header, "TONWERK SCOPE");
}

void ScopeEditor::resized()
{
    processor.state.state.setProperty("editorWidth", getWidth(), nullptr);
    processor.state.state.setProperty("editorHeight", getHeight(), nullptr);
    auto area = getLocalBounds().reduced(kMargin);
    presets.setBounds(area.removeFromTop(kHeader).reduced(0, 7).removeFromRight(220));
    auto controls = area.removeFromBottom(view.preferredHeight());
    area.removeFromBottom(kGap);
    screen.setBounds(area);
    layoutRow(controls, { &view, &trigger, &picture });
}
} // namespace tonwerkscope
