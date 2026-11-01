// Host test of the module's DSP and panel logic: builds on Linux or macOS with a plain C++ compiler (see Makefile).
// Compares the port with Tonwerk Wavetable's own voice from a checkout of Marcel-B/Synth, checks the tuning, the
// panel's pick-up and buttons and the V/Oct calibration, measures the render time and writes listening examples.

#include "app/Bench.h"
#include "app/Module.h"
#include "dsp/Tables.h"
#include "dsp/Voice.h"
#include "hw/UpdateImage.h"

#include "WaveEngine.h" // Tonwerk Wavetable, from the Synth repository

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

double rms(const std::vector<float>& x, std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt(sum / (double) (x.size() - from));
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

/** The frequency of `x` from its rising zero crossings, interpolated between samples. */
double frequency(const std::vector<float>& x)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double at = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
            if (first < 0.0)
                first = at;
            else
                ++crossings;
            last = at;
        }
    return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    const uint32_t frames = (uint32_t) left.size(), bytes = frames * 4;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32((uint32_t) kRate); u32((uint32_t) kRate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float v : { left[i], right[i] })
        {
            const auto s = (int16_t) std::lround(std::clamp(twm::softClip(v), -1.0f, 1.0f) * 32000.0f);
            std::fwrite(&s, 2, 1, f);
        }
    std::fclose(f);
}

/** Renders `seconds` of the port's voice; `change` may move the parameters at each control step. */
template <typename Change>
void renderPort(twm::Voice& v, twm::VoiceParams& p, float seconds, std::vector<float>& osc, std::vector<float>& out,
                Change change)
{
    const int total = (int) (seconds * kRate);
    osc.assign((std::size_t) total, 0.0f);
    out.assign((std::size_t) total, 0.0f);
    for (int done = 0; done < total; done += twm::Voice::kControl)
    {
        change((float) done / kRate, p);
        v.render(osc.data() + done, out.data() + done, std::min(twm::Voice::kControl, total - done), p);
    }
}

/** The same sound through the plugin's voice: oscillator A only, filter as given, one voice, mono. */
std::vector<float> renderPlugin(const twm::VoiceParams& p, float seconds)
{
    tonwerkwave::Settings s;
    s.a.table = p.a.table;
    s.a.position = p.a.position;
    s.a.warp = (tonwerkwave::Warp) p.a.warp;
    s.a.warpAmount = p.a.warpAmount;
    s.a.level = p.a.level;
    s.a.width = 0.0f;
    s.filter.on = p.filter != twm::FilterMode::off;
    s.filter.mode = (tonwerkwave::FilterMode) p.filter;
    s.filter.cutoff = p.cutoff;
    s.filter.resonance = p.resonance;
    tonwerkwave::Voice v;
    v.prepare(kRate);
    v.start((int) p.note, 1.0f, s, true, false);
    tonwerkwave::Shared shared;
    shared.sampleRate = kRate;
    const int total = (int) (seconds * kRate);
    std::vector<float> left((std::size_t) total, 0.0f), right((std::size_t) total, 0.0f);
    for (int done = 0; done < total; done += tonwerkwave::Voice::kControl)
        v.render(left.data() + done, right.data() + done, std::min(tonwerkwave::Voice::kControl, total - done), s,
                 shared);
    return left;
}

void comparesWithPlugin()
{
    struct Case
    {
        const char* name;
        int table;
        float position;
        twm::Warp warp;
        float warpAmount;
        twm::FilterMode filter;
        float cutoff, resonance;
        float note;
    };
    const Case cases[] = {
        { "saw C3, 24 dB lowpass", 0, 2.0f / 3.0f, twm::Warp::off, 0.0f, twm::FilterMode::low24, 800.0f, 0.3f, 48 },
        { "square C4, no filter", 0, 1.0f, twm::Warp::off, 0.0f, twm::FilterMode::off, 20000.0f, 0.0f, 60 },
        { "vowel table C2, 12 dB lowpass", 3, 0.4f, twm::Warp::off, 0.0f, twm::FilterMode::low12, 2000.0f, 0.5f, 36 },
        { "FM growl with sync warp, band", 4, 0.5f, twm::Warp::sync, 0.4f, twm::FilterMode::band12, 1200.0f, 0.4f, 43 },
        { "rough table, bend warp, comb", 9, 0.7f, twm::Warp::bend, 0.5f, twm::FilterMode::comb, 300.0f, 0.6f, 40 },
    };
    for (const auto& c : cases)
    {
        twm::VoiceParams p;
        p.note = c.note;
        p.a.table = c.table;
        p.a.position = c.position;
        p.a.warp = c.warp;
        p.a.warpAmount = c.warpAmount;
        p.filter = c.filter;
        p.cutoff = c.cutoff;
        p.resonance = c.resonance;
        p.gate = true;
        twm::Voice port;
        port.prepare(kRate);
        std::vector<float> osc, out;
        renderPort(port, p, 0.5f, osc, out, [](float, twm::VoiceParams&) {});
        const auto plugin = renderPlugin(p, 0.5f);
        std::vector<float> difference(out.size());
        for (std::size_t i = 0; i < out.size(); ++i)
            difference[i] = out[i] - plugin[i];
        const double error = db(rms(difference) / rms(plugin));
        const double level = db(rms(out) / rms(plugin));
        char line[200];
        std::snprintf(line, sizeof line, "like the plugin: %s (difference %.1f dB below the signal, level %+.2f dB)",
                      c.name, error, level);
        check(error < -40.0 && std::abs(level) < 0.1, line);
    }
}

void tuning()
{
    for (float note : { 24.0f, 36.0f, 48.0f, 60.0f, 72.0f, 84.0f, 96.0f })
    {
        twm::VoiceParams p;
        p.note = note;
        p.a.position = 0.0f; // sine
        p.gate = true;
        twm::Voice v;
        v.prepare(kRate);
        std::vector<float> osc, out;
        renderPort(v, p, 1.0f, osc, out, [](float, twm::VoiceParams&) {});
        const double expected = twm::noteToHz(note);
        const double cents = 1200.0 * std::log2(frequency(osc) / expected);
        char line[120];
        std::snprintf(line, sizeof line, "in tune at note %.0f (%.1f Hz): %+.3f cents", note, expected, cents);
        check(std::abs(cents) < 0.5, line);
    }
}

void panel()
{
    twm::Module m;
    twm::PanelInputs in;
    twm::VoiceParams p;
    for (auto& v : in.pot)
        v = 0.5f;
    for (int i = 0; i < 10; ++i)
        m.update(in, 2, p);
    const float cutoffBefore = p.cutoff;

    // Holding FILTER and turning CUTOFF sets the envelope amount, not the cutoff.
    in.filterButton = true;
    for (int i = 0; i < 10; ++i)
        m.update(in, 2, p);
    for (float v = 0.5f; v >= -0.001f; v -= 0.01f)
    {
        in.pot[twm::cutoff] = std::max(0.0f, v);
        m.update(in, 2, p);
    }
    for (float v = 0.0f; v <= 0.8f; v += 0.01f)
    {
        in.pot[twm::cutoff] = v;
        m.update(in, 2, p);
    }
    in.filterButton = false;
    m.update(in, 2, p);
    check(std::abs(p.envToCutoff - 6.0f * 0.8f) < 0.1f, "hold FILTER + CUTOFF sets the envelope amount");
    check(std::abs(p.cutoff - cutoffBefore) < 1.0f, "the cutoff stays put when FILTER is let go (pick-up)");
    check(p.filter == twm::FilterMode::low24, "turning a knob while holding FILTER does not switch the mode");
    // The cutoff follows again once the knob passes its old place.
    for (float v = 0.8f; v >= 0.3f; v -= 0.01f)
    {
        in.pot[twm::cutoff] = v;
        m.update(in, 2, p);
    }
    check(std::abs(p.cutoff - 20.0f * std::exp2(10.0f * 0.3f)) < 1.0f, "the cutoff picks up when the knob passes it");

    // A tap switches the mode.
    in.filterButton = true;
    for (int i = 0; i < 50; ++i)
        m.update(in, 2, p);
    in.filterButton = false;
    m.update(in, 2, p);
    check(p.filter == twm::FilterMode::high12, "a tap on FILTER steps to the next mode");
    in.warpButton = true;
    for (int i = 0; i < 300; ++i)
        m.update(in, 2, p);
    in.warpButton = false;
    m.update(in, 2, p);
    check(p.a.warp == twm::Warp::off, "a long hold on WARP does not switch the warp mode");

    // A tap still counts when knobs rest where their hidden values are (TUNE and FINE centred) and the ADC jitters
    // by a few steps, as the ESP32's does on a still pot.
    for (auto& v : in.pot)
        v = 0.5f;
    for (int i = 0; i < 300; ++i)
        m.update(in, 2, p);
    in.warpButton = true;
    for (int i = 0; i < 60; ++i)
    {
        for (int k = 0; k < twm::kPots; ++k)
            in.pot[k] = 0.5f + ((i + k) % 3 - 1) * 0.002f;
        m.update(in, 2, p);
    }
    in.warpButton = false;
    m.update(in, 2, p);
    check(p.a.warp == twm::Warp::sync, "a tap on WARP counts while the knobs jitter around their hidden values");

    // V/Oct: the nominal calibration against the input stage's formula (hardware/schaltplan.svg).
    const twm::Calibration cal;
    double worst = 0.0;
    for (float volts = -1.0f; volts <= 6.95f; volts += 0.25f)
    {
        const float adc = 1.905f * 1.374f - 0.374f * volts;
        worst = std::max(worst, (double) std::abs(cal.pitchVolts(adc / 3.0f * 4096.0f) - volts));
    }
    char line[120];
    std::snprintf(line, sizeof line, "nominal V/Oct calibration within %.0f cents from -1 to +7 V", worst * 1200.0);
    check(worst * 1200.0 < 25.0, line);
    twm::Calibration measured;
    measured.pitchCode1V = 3050.0f;
    measured.pitchCode3V = 2030.0f;
    check(std::abs(measured.pitchVolts(3050.0f - 510.0f * 3.0f) - 4.0f) < 1.0e-4f,
          "a measured calibration maps 1 V and 3 V and extends in a straight line");
}

void speed()
{
    // The heaviest patch the panel allows: both oscillators with four unison voices, FM warp (both always run), sub,
    // 24 dB lowpass with drive.
    twm::VoiceParams p;
    p.note = 40.0f;
    p.a.unison = p.b.unison = 4;
    p.b.on = true;
    p.a.warp = p.b.warp = twm::Warp::fm;
    p.a.warpAmount = p.b.warpAmount = 0.5f;
    p.subOn = true;
    p.filter = twm::FilterMode::low24;
    p.drive = 0.5f;
    p.gate = true;
    twm::Voice v;
    v.prepare(kRate);
    std::vector<float> osc, out;
    const auto start = std::chrono::steady_clock::now();
    renderPort(v, p, 10.0f, osc, out, [](float t, twm::VoiceParams& q) { q.a.position = 0.5f + 0.5f * std::sin(t); });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("info  heaviest patch on this computer: %.2f %% of real time (%.0f ns per sample)\n",
                100.0 * seconds / 10.0, 1.0e9 * seconds / (10.0 * kRate));
}

/**
 * The bench firmware's sound: Bench plays the panel through Module into the voice at the firmware's pace (a control
 * tick every 2 ms, blocks of 32 samples), scaled by its headphone level.
 */
std::vector<float> renderBench(float seconds, int& tablesSeen, int& notes)
{
    constexpr float kBenchLevel = 0.15f; // as in main.cpp
    twm::Bench bench;
    twm::Module module;
    twm::Voice v;
    v.prepare(kRate);
    twm::PanelInputs in;
    twm::VoiceParams p;
    std::vector<float> out;
    float osc[twm::Voice::kControl], block[twm::Voice::kControl];
    bool seen[twm::kTables] {}, wasGate = false;
    notes = 0;
    const int ticks = (int) (seconds * 500.0f);
    for (int tick = 0; tick < ticks; ++tick)
    {
        bench.update(2, in, module.stored);
        module.update(in, 2, p);
        seen[p.a.table] = true;
        notes += p.gate && ! wasGate;
        wasGate = p.gate;
        for (int b = 0; b < 3; ++b)
        {
            v.render(osc, block, twm::Voice::kControl, p);
            for (float x : block)
                out.push_back(x * kBenchLevel);
        }
    }
    tablesSeen = 0;
    for (bool s : seen)
        tablesSeen += s;
    return out;
}

void benchTest()
{
    // One round through all ten tables (40 s) and into the next, where the first warp mode comes in.
    int tables = 0, notes = 0;
    const auto out = renderBench(44.0f, tables, notes);
    float peak = 0.0f;
    bool finite = true;
    for (float x : out)
    {
        peak = std::max(peak, std::abs(x));
        finite = finite && std::isfinite(x);
    }
    check(finite && tables == twm::kTables, "bench: plays all ten tables (" + std::to_string(tables) + ")");
    // Four a second, and the one starting at 44 s itself.
    check(notes == 177, "bench: four notes a second (" + std::to_string(notes) + " up to 44 s)");
    char line[160];
    std::snprintf(line, sizeof line, "bench: peak %.2f, below the DAC's full scale", peak);
    check(peak > 0.03f && peak < 0.5f, line);
    // Every note sounds: the quietest eighth is within 30 dB of the loudest peak.
    double quietest = 1.0;
    const std::size_t step = (std::size_t) (kRate / 4.0f);
    for (std::size_t from = 0; from + step <= out.size(); from += step)
    {
        const std::vector<float> part(out.begin() + (long) from, out.begin() + (long) (from + step));
        quietest = std::min(quietest, rms(part));
    }
    std::snprintf(line, sizeof line, "bench: the quietest note %.1f dB below the peak", -db(quietest / peak));
    check(db(quietest / peak) > -30.0, line);
}

void examples(const std::string& dir)
{
    // 0: the bench firmware, the first twenty seconds (five tables).
    {
        int tables = 0, notes = 0;
        const auto out = renderBench(20.0f, tables, notes);
        writeWav(dir + "/00_steckbrett_test.wav", out, out);
    }
    // 1: a bass line on C, the position swept by an LFO, through the lowpass with an envelope: the voice output.
    {
        twm::Voice v;
        v.prepare(kRate);
        twm::VoiceParams p;
        p.a.table = 4;
        p.a.unison = 2;
        p.a.detune = 12.0f;
        p.subOn = true;
        p.subLevel = 0.5f;
        p.filter = twm::FilterMode::low24;
        p.cutoff = 150.0f;
        p.resonance = 0.45f;
        p.envToCutoff = 3.0f;
        p.decay = p.release = 0.25f;
        p.sustain = 0.4f;
        const int notes[] = { 36, 36, 48, 36, 39, 36, 43, 41 };
        std::vector<float> osc, out;
        renderPort(v, p, 8.0f, osc, out, [&](float t, twm::VoiceParams& q) {
            const int step = (int) (t * 4.0f);
            q.note = (float) notes[step % 8];
            q.gate = std::fmod(t * 4.0f, 1.0f) < 0.6f;
            q.a.position = 0.5f - 0.5f * std::cos(2.0f * twm::kPi * t * 2.0f);
        });
        writeWav(dir + "/01_bass_fm_growl.wav", out, out);
    }
    // 2: every table swept from its first frame to its last, as the plain oscillator output (two seconds each).
    {
        twm::Voice v;
        v.prepare(kRate);
        twm::VoiceParams p;
        p.note = 48.0f;
        p.gate = true;
        std::vector<float> osc, out;
        renderPort(v, p, 20.0f, osc, out, [](float t, twm::VoiceParams& q) {
            q.a.table = std::min(9, (int) (t / 2.0f));
            q.a.position = std::fmod(t, 2.0f) / 2.0f;
        });
        for (auto& x : osc)
            x *= 0.6f;
        writeWav(dir + "/02_alle_tabellen.wav", osc, osc);
    }
    // 3: a slow pad: oscillator B a fifth up, unison, the vowel table moving, the comb filter.
    {
        twm::Voice v;
        v.prepare(kRate);
        twm::VoiceParams p;
        p.a.table = 3;
        p.b.on = true;
        p.b.table = 3;
        p.b.transpose = 7.0f;
        p.b.position = 0.6f;
        p.a.unison = p.b.unison = 4;
        p.a.detune = p.b.detune = 25.0f;
        p.a.level = p.b.level = 0.5f;
        p.filter = twm::FilterMode::low12;
        p.cutoff = 1800.0f;
        p.resonance = 0.3f;
        p.attack = 0.8f;
        p.release = 1.5f;
        std::vector<float> osc, out;
        renderPort(v, p, 10.0f, osc, out, [](float t, twm::VoiceParams& q) {
            q.note = t < 5.0f ? 45.0f : 41.0f;
            q.gate = std::fmod(t, 5.0f) < 4.0f;
            q.a.position = 0.5f + 0.5f * std::sin(0.7f * t);
        });
        writeWav(dir + "/03_vokal_flaeche.wav", out, out);
    }
}
} // namespace

/** The drive keeps a full-level note about as loud as without it: the module's oscillators reach the clipper at
 * full swing, where the plugin's fixed make-up took up to 12 dB away (Marcel's module, 2026-11-01). */
void drive()
{
    struct Case
    {
        const char* name;
        int table;
        float position;
        int unison;
        bool b;
    };
    const Case cases[] = {
        { "saw", 0, 2.0f / 3.0f, 1, false },
        { "square", 0, 1.0f, 1, false },
        { "sine", 0, 0.0f, 1, false },
        { "vowel table", 3, 0.4f, 1, false },
        { "saw, four unison voices", 0, 2.0f / 3.0f, 4, false },
        { "saw with oscillator B", 0, 2.0f / 3.0f, 1, true },
    };
    for (const auto& c : cases)
    {
        std::string line = std::string("drive on a ") + c.name + ":";
        double reference = 0.0, worst = 0.0;
        for (float d : { 0.0f, 0.01f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            twm::VoiceParams p;
            p.note = 48.0f;
            p.a.table = c.table;
            p.a.position = c.position;
            p.a.unison = c.unison;
            p.b.on = c.b;
            p.b.table = c.table;
            p.b.position = c.position;
            p.filter = twm::FilterMode::low24;
            p.cutoff = 20000.0f;
            p.drive = d;
            p.gate = true;
            twm::Voice v;
            v.prepare(kRate);
            std::vector<float> osc, out;
            renderPort(v, p, 0.5f, osc, out, [](float, twm::VoiceParams&) {});
            const std::vector<float> tail(out.begin() + (long) out.size() / 2, out.end());
            const double level = db(rms(tail));
            if (d == 0.0f)
                reference = level;
            worst = std::max(worst, std::abs(level - reference));
            char part[32];
            std::snprintf(part, sizeof part, " %+.1f", level - reference);
            line += part;
        }
        check(worst < 2.0, line + " dB at drive 0, 1, 25, 50, 75, 100 %");
    }
}

/** A whole image streamed in uneven pieces lands where USB would put it: the partition table compared, the instrument
 *  in "factory", the data in "tables", bootloader, boot data and updater left alone. */
void updateImage()
{
    using namespace twm::update;
    Layout layout;
    layout.app = { 0x10000, 0x180000 };
    layout.tables = { 0x310000, 0x600000 };
    // An image as large as Tonwerk Granular's: up to 3.5 MB into the tables.
    const uint32_t size = 0x310000 + 0x380000;
    std::vector<uint8_t> image(size);
    for (uint32_t i = 0; i < size; ++i)
        image[i] = (uint8_t) (i * 2654435761u >> 24);
    std::vector<uint8_t> table(layout.partitionTable.size), app(layout.app.size), tables(layout.tables.size);
    uint32_t skipped = 0, pieces = 0;
    bool inPlace = true;
    uint32_t at = 0, seed = 1;
    while (at < size)
    {
        seed = seed * 1664525u + 1013904223u;
        uint32_t piece = std::min(size - at, 1 + (seed >> 20)); // up to 4 KB, like the updater's reads
        while (piece > 0)
        {
            const Span s = spanAt(layout, at, piece);
            std::vector<uint8_t>* into = s.target == Target::partitionTable ? &table
                                         : s.target == Target::app          ? &app
                                         : s.target == Target::tables       ? &tables
                                                                            : nullptr;
            if (into == nullptr)
                skipped += s.length;
            else if (s.offset + s.length <= into->size())
                std::copy(image.begin() + at, image.begin() + at + s.length, into->begin() + s.offset);
            else
                inPlace = false;
            inPlace = inPlace && s.length > 0;
            at += s.length;
            piece -= s.length;
            ++pieces;
        }
    }
    auto same = [&](const std::vector<uint8_t>& part, uint32_t start, uint32_t length) {
        return std::equal(part.begin(), part.begin() + length, image.begin() + start);
    };
    check(inPlace && same(table, 0x8000, 0xc00) && same(app, 0x10000, 0x180000) && same(tables, 0x310000, 0x380000)
              && skipped == size - 0xc00 - 0x180000 - 0x380000,
          "image over Wi-Fi: partition table, instrument and tables in place, bootloader, boot data and updater "
          "skipped (" + std::to_string(pieces) + " pieces)");
    const Span past = spanAt(layout, 0x910000, 100);
    check(past.target == Target::skip && past.length == 100, "image over Wi-Fi: past the tables nothing is written");
}

int main(int argc, char** argv)
{
    const auto start = std::chrono::steady_clock::now();
    const bool built = twm::Bank::build([](std::size_t bytes) { return std::malloc(bytes); });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(built, "wavetables built");
    std::printf("info  tables: %.1f MB, made in %.0f ms on this computer\n",
                twm::kTables * twm::Table::samples() * 2.0 / 1.0e6, ms);

    comparesWithPlugin();
    tuning();
    panel();
    drive();
    updateImage();
    speed();
    benchTest();
    if (argc > 1)
        examples(argv[1]);
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
