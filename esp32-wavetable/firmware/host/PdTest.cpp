// Host test of the PD firmware (-DTONWERK_PD=1): builds like HostTest.cpp with a plain C++ compiler. Compares the
// port with Tonwerk PD's own voice and chorus from this repository for every wave and every way of combining the
// lines, checks the tuning over the V/Oct range, the player's gate and V/Oct strikes, the panel and the levels,
// measures the render time and writes listening examples.

#include "app/PdModule.h"
#include "dsp/PdVoice.h"

#include "PdEngine.h" // Tonwerk PD, from the Synth repository

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;
constexpr int kBlock = twm::pd::Voice::kControl;
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

double rms(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size())
        to = x.size();
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt(sum / (double) std::max<std::size_t>(1, to - from));
}

double peakOf(const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max(p, (double) std::abs(v));
    return p;
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

/** The frequency of `x` from its rising zero crossings after `from`, interpolated between samples. */
double frequencyOf(const std::vector<float>& x, std::size_t from)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = from + 1; i < x.size(); ++i)
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
    for (std::size_t i = 0; i < left.size(); ++i)
        for (float v : { left[i], right[i] })
        {
            const auto s = (int16_t) std::lround(std::clamp(twm::softClip(v), -1.0f, 1.0f) * 32000.0f);
            std::fwrite(&s, 2, 1, f);
        }
    std::fclose(f);
}

const char* waveName(int w)
{
    static const char* names[] = { "Säge", "Rechteck", "Puls", "Doppelsinus", "Säge-Puls", "Resonanz Säge",
                                   "Resonanz Dreieck", "Resonanz Trapez" };
    return names[w];
}

// --------------------------------------------------------------------------------------- port against plugin

/** The plugin's voices take their noise seeds from a counter; the port is given the same one. */
int pluginVoices = 0;

/** A sound in both forms: the plugin's DCW offset comes from the wheel routed to the DCW, as the module's DCW knob. */
struct Sound
{
    tonwerkpd::Settings plugin;
    twm::pd::Settings port;
    float bend = 0.0f;
};

Sound makeSound(int wave1, int wave2, int lines, int modulation, float depth, float bend)
{
    Sound s;
    s.bend = bend;
    auto& p = s.plugin;
    p.lines = (tonwerkpd::LineSelect) lines;
    p.modulation = (tonwerkpd::Modulation) modulation;
    p.wheel = { tonwerkpd::Target::dcw, bend };
    p.velocity = { tonwerkpd::Target::none, 0.0f };
    for (auto& lfo : p.lfo)
    {
        // The plugin's LFOs draw noise when they wrap; held still, both draw the same sequence.
        lfo.sync = false;
        lfo.rate = 0.0f;
    }
    p.width = 0.5f;
    p.line[0].first = (tonwerkpd::Wave) wave1;
    p.line[1].first = (tonwerkpd::Wave) wave2;
    p.line[0].depth = p.line[1].depth = depth;
    auto& m = s.port;
    m.lines = (twm::pd::LineSelect) lines;
    m.modulation = (twm::pd::Modulation) modulation;
    m.bend = bend;
    m.width = 0.5f;
    for (std::size_t k = 0; k < 2; ++k)
    {
        const auto& pl = p.line[k];
        auto& ml = m.line[k];
        ml.first = (twm::pd::Wave) pl.first;
        ml.alternate = pl.alternate;
        ml.second = (twm::pd::Wave) pl.second;
        ml.depth = pl.depth;
        ml.keyFollow = pl.keyFollow;
        ml.level = pl.level;
        ml.octave = pl.octave;
        ml.semitones = (float) pl.semitones + pl.fine / 100.0f;
        ml.dcw = { pl.dcw.attack, pl.dcw.decay, pl.dcw.sustain, pl.dcw.release };
        ml.amp = { pl.amp.attack, pl.amp.decay, pl.amp.sustain, pl.amp.release };
    }
    return s;
}

struct Pair
{
    std::vector<float> pluginLeft, pluginRight, portLeft, portRight;
};

/** One note on both voices: held for `held` seconds, then let go, `seconds` in all. */
Pair renderBoth(const Sound& sound, int note, double seconds, double held)
{
    auto plugin = std::make_unique<tonwerkpd::Voice>();
    ++pluginVoices;
    auto port = std::make_unique<twm::pd::Voice>(0x2545f491u * (uint32_t) (pluginVoices * 2 + 1));
    plugin->prepare(kRate);
    port->prepare(kRate);
    tonwerkpd::Shared shared;
    shared.sampleRate = kRate;
    shared.modWheel = 1.0f;
    plugin->start(note, 1.0f, sound.plugin, false);
    port->start((float) note, sound.port);

    const int total = (int) (seconds * kRate) / kBlock * kBlock;
    const int heldFor = (int) (held * kRate);
    Pair out;
    for (auto* v : { &out.pluginLeft, &out.pluginRight, &out.portLeft, &out.portRight })
        v->assign((std::size_t) total, 0.0f);
    bool released = false;
    for (int i = 0; i < total; i += kBlock)
    {
        if (! released && i >= heldFor)
        {
            plugin->release();
            port->release();
            released = true;
        }
        plugin->render(out.pluginLeft.data() + i, out.pluginRight.data() + i, kBlock, sound.plugin, shared);
        port->render(out.portLeft.data() + i, out.portRight.data() + i, kBlock, sound.port);
    }
    return out;
}

/** The port's difference from the plugin on both sides, in dB of the plugin's level. */
double difference(const Pair& p)
{
    double sum = 0.0, level = 0.0;
    for (std::size_t i = 0; i < p.pluginLeft.size(); ++i)
    {
        const double l = p.portLeft[i] - p.pluginLeft[i], r = p.portRight[i] - p.pluginRight[i];
        sum += l * l + r * r;
        level += (double) p.pluginLeft[i] * p.pluginLeft[i] + (double) p.pluginRight[i] * p.pluginRight[i];
    }
    return db(std::sqrt(sum / std::max(1.0e-20, level)));
}

void testAgainstPlugin()
{
    // float phases against the plugin's double: the error grows over a note and with the resonances' ratio, so every
    // wave is compared over a whole held note of 1.5 s with its release, at three pitches.
    double worst = -200.0;
    std::string worstName;
    auto compare = [&](const Sound& sound, int note, const std::string& name) {
        const auto pair = renderBoth(sound, note, 1.5, 1.0);
        if (rms(pair.pluginLeft) < 1.0e-4)
            check(false, name + ": silent");
        const double d = difference(pair);
        if (d > -50.0)
            std::printf("info  %s: %.1f dB\n", name.c_str(), d);
        if (d > worst)
        {
            worst = d;
            worstName = name;
        }
    };
    for (int w = 0; w < (int) twm::pd::Wave::count; ++w)
        for (int note : { 36, 60, 84 })
            for (float depth : { 0.3f, 1.0f })
                compare(makeSound(w, 1, 0, 0, depth, 0.1f), note,
                        std::string(waveName(w)) + " C" + std::to_string(note / 12 - 1) + " DCW " + std::to_string((int) (depth * 100)));
    const char* lineNames[] = { "Linie 1", "Linie 2", "1+1'", "1+2" };
    const char* modulationNames[] = { "", " mit Ring", " mit Rauschen" };
    for (int lines = 1; lines < 4; ++lines)
        for (int modulation = 0; modulation < 3; ++modulation)
            compare(makeSound(0, 3, lines, modulation, 0.6f, 0.0f), 48,
                    std::string(lineNames[lines]) + modulationNames[modulation]);
    // Alternating waves, cycle by cycle.
    Sound alternating = makeSound(1, 6, 3, 0, 0.8f, 0.0f);
    alternating.plugin.line[0].alternate = alternating.port.line[0].alternate = true;
    alternating.plugin.line[0].second = tonwerkpd::Wave::doubleSine;
    alternating.port.line[0].second = twm::pd::Wave::doubleSine;
    compare(alternating, 48, "Rechteck im Wechsel mit Doppelsinus");

    char text[200];
    std::snprintf(text, sizeof text, "every wave, every way of combining the lines: within -50 dB of the plugin "
                  "(worst %.1f dB: %s)", worst, worstName.c_str());
    check(worst <= -50.0, text);
}

void testChorusAgainstPlugin()
{
    tonwerkpd::Chorus plugin;
    twm::pd::Chorus port;
    plugin.prepare(kRate);
    port.prepare(kRate);
    tonwerk::Noise noise(7);
    std::vector<float> pl(48000), pr(48000), ml(48000), mr(48000);
    for (std::size_t i = 0; i < pl.size(); ++i)
    {
        pl[i] = ml[i] = (float) std::sin(2.0 * 3.14159265 * 220.0 * (double) i / kRate);
        pr[i] = mr[i] = 0.5f * (float) noise.next();
    }
    for (std::size_t i = 0; i < pl.size(); i += 32)
    {
        plugin.process(pl.data() + i, pr.data() + i, 32, 0.7f, 0.6f, 0.8f);
        port.process(ml.data() + i, mr.data() + i, 32, 0.7f, 0.6f, 0.8f);
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < pl.size(); ++i)
        sum += (double) (ml[i] - pl[i]) * (ml[i] - pl[i]) + (double) (mr[i] - pr[i]) * (mr[i] - pr[i]);
    const double d = db(std::sqrt(sum / (double) pl.size()) / std::sqrt(0.5 * (rms(pl) * rms(pl) + rms(pr) * rms(pr))));
    char text[120];
    std::snprintf(text, sizeof text, "chorus within -60 dB of the plugin's (%.1f dB)", d);
    check(d <= -60.0, text);
}

// ------------------------------------------------------------------------------------------------ player

/** Plays `seconds` through a player, `change` setting the parameters at each block. */
void play(twm::pd::Player& player, twm::pd::PlayParams& p, double seconds,
          const std::function<void(double, twm::pd::PlayParams&)>& change, std::vector<float>* left = nullptr,
          std::vector<float>* right = nullptr)
{
    const int blocks = (int) (seconds * kRate) / kBlock;
    float l[kBlock], r[kBlock];
    for (int b = 0; b < blocks; ++b)
    {
        change((double) b * kBlock / kRate, p);
        player.render(l, r, kBlock, p);
        if (left != nullptr)
            left->insert(left->end(), l, l + kBlock);
        if (right != nullptr)
            right->insert(right->end(), r, r + kBlock);
    }
}

void testTuning()
{
    // A plain cosine (no bend) on line 1 alone, at 0 to 5 V: on pitch to a hundredth of a cent.
    double worst = 0.0;
    for (int volts = 0; volts <= 5; ++volts)
    {
        auto player = std::make_unique<twm::pd::Player>();
        player->prepare(kRate);
        twm::pd::PlayParams p;
        p.settings.line[0].depth = 0.0f;
        p.settings.chorusMix = 0.0f;
        p.note = 36.0f + 12.0f * (float) volts;
        p.gate = true;
        std::vector<float> left;
        play(*player, p, 1.0, [](double, twm::pd::PlayParams&) {}, &left);
        const double expected = 440.0 * std::pow(2.0, (p.note - 69.0) / 12.0);
        worst = std::max(worst, std::abs(1200.0 * std::log2(frequencyOf(left, 4800) / expected)));
    }
    char text[120];
    std::snprintf(text, sizeof text, "0 to 5 V: C2 to C7 on pitch (worst %.3f cents)", worst);
    check(worst < 0.05, text);
}

void testPlayer()
{
    // A pulse at GATE starts a note, its end lets the note go.
    {
        auto player = std::make_unique<twm::pd::Player>();
        player->prepare(kRate);
        twm::pd::PlayParams p;
        std::vector<float> left;
        play(*player, p, 2.0, [](double t, twm::pd::PlayParams& q) { q.gate = std::fmod(t, 0.25) < 0.1; }, &left);
        check(player->strikes() == 8, "eight gate pulses: eight notes (" + std::to_string(player->strikes()) + ")");
        // The DCA's release is 0.3 s: 150 ms after the last pulse it has fallen, but not to silence.
        const double late = rms(left, (std::size_t) (1.85 * kRate), (std::size_t) (1.95 * kRate));
        const double held = rms(left, (std::size_t) (1.76 * kRate), (std::size_t) (1.84 * kRate));
        check(late < held, "the note falls once its gate ends");
    }
    // Without a gate cable the gate is on: a jump at V/OCT starts a note, a glide does not.
    {
        auto player = std::make_unique<twm::pd::Player>();
        player->prepare(kRate);
        twm::pd::PlayParams p;
        play(*player, p, 2.0, [](double t, twm::pd::PlayParams& q) {
            q.gate = true;
            q.note = 48.0f + 5.0f * (float) ((int) (t / 0.4));
        });
        check(player->strikes() == 5, "gate held, V/OCT stepping five times: five notes (" + std::to_string(player->strikes()) + ")");
        auto glide = std::make_unique<twm::pd::Player>();
        glide->prepare(kRate);
        twm::pd::PlayParams q;
        play(*glide, q, 2.0, [](double t, twm::pd::PlayParams& r) {
            r.gate = true;
            r.note = 48.0f + 12.0f * (float) t;
        });
        check(glide->strikes() == 1, "a glide of two octaves: one note (" + std::to_string(glide->strikes()) + ")");
    }
    // The newest note follows V/OCT while it sounds.
    {
        auto player = std::make_unique<twm::pd::Player>();
        player->prepare(kRate);
        twm::pd::PlayParams p;
        p.settings.line[0].depth = 0.0f;
        std::vector<float> left;
        play(*player, p, 1.0, [](double t, twm::pd::PlayParams& q) {
            q.gate = true;
            q.note = t < 0.5 ? 48.0f : 48.2f;
        }, &left);
        const double hz = frequencyOf(left, (std::size_t) (0.6 * kRate));
        check(std::abs(1200.0 * std::log2(hz / (440.0 * std::pow(2.0, (48.2 - 69.0) / 12.0)))) < 0.1,
              "a fifth of a semitone at V/OCT moves the sounding note");
    }
}

void testLevels()
{
    // At the module's defaults (both lines, line 2 a square, the full envelope, chorus at half) and with every wave
    // fully bent, the outputs stay under full scale; a single line on its own reaches a fair share of it.
    double worstPeak = 0.0, quietest = 10.0;
    std::string peakName, quietName;
    for (int w = 0; w < (int) twm::pd::Wave::count; ++w)
        for (int lines : { 0, 3 })
            for (float chorus : { 0.0f, 1.0f })
            {
                auto player = std::make_unique<twm::pd::Player>();
                player->prepare(kRate);
                twm::pd::PlayParams p;
                p.settings.lines = (twm::pd::LineSelect) lines;
                p.settings.line[0].first = (twm::pd::Wave) w;
                p.settings.line[0].depth = p.settings.line[1].depth = 1.0f;
                p.settings.chorusMix = chorus;
                p.settings.width = 0.0f;
                std::vector<float> left, right;
                play(*player, p, 1.0, [](double t, twm::pd::PlayParams& q) {
                    q.gate = t < 0.8;
                    q.note = 36.0f;
                }, &left, &right);
                const std::string name = std::string(waveName(w)) + (lines == 3 ? " mit Linie 2" : "")
                                         + (chorus > 0.0f ? ", Chorus" : "");
                const double peak = std::max(peakOf(left), peakOf(right));
                if (peak > worstPeak)
                {
                    worstPeak = peak;
                    peakName = name;
                }
                const double level = rms(left, 0, (std::size_t) (0.8 * kRate));
                if (lines == 0 && chorus == 0.0f && level < quietest)
                {
                    quietest = level;
                    quietName = name;
                }
            }
    char text[160];
    std::snprintf(text, sizeof text, "every wave fully bent, with line 2 and chorus: peak under full scale (%.2f, %s)",
                  worstPeak, peakName.c_str());
    check(worstPeak < 1.0, text);
    std::snprintf(text, sizeof text, "a single line at least -26 dBFS RMS (quietest %.1f dB, %s)", db(quietest),
                  quietName.c_str());
    check(db(quietest) > -26.0, text);
}

// ------------------------------------------------------------------------------------------------ panel

twm::PanelInputs centred()
{
    twm::PanelInputs in;
    for (float& v : in.pot)
        v = 0.5f;
    in.gate = false;
    return in;
}

void testPanel()
{
    twm::PdModule module;
    twm::PdModule::Params out;
    auto in = centred();
    for (int i = 0; i < 10; ++i)
        module.update(in, 2, out);
    check(std::abs(out.note - 36.0f) < 1.0e-4f, "0 V, TUNE and FINE in the middle: C2");

    // The module's defaults: both lines, saw and square, line 2 seven cents up, no ring, the plugin's envelopes.
    const twm::pd::LineSettings d;
    check(out.settings.lines == twm::pd::LineSelect::both && out.settings.line[0].first == twm::pd::Wave::saw
              && out.settings.line[1].first == twm::pd::Wave::square && out.settings.modulation == twm::pd::Modulation::off,
          "defaults: both lines, saw and square, no modulation");
    check(std::abs(out.settings.line[1].semitones - (12.0f + 0.07f)) < 0.002f,
          "LINIE 2 in the middle: an octave and seven cents up");
    check(std::abs(out.settings.line[0].amp.attack - d.amp.attack) < 1.0e-4f
              && std::abs(out.settings.line[0].amp.sustain - d.amp.sustain) < 1.0e-4f
              && std::abs(out.settings.line[0].dcw.sustain - d.dcw.sustain) < 1.0e-4f,
          "defaults: the DCA and DCW envelopes of the plugin");

    // LINIE 2's jack is 1 V/Oct; DCW's and HÜLLK.'s go through their attenuverters, MIX's adds a fifth per volt.
    in.cv[twm::tableCv] = 1.0f;
    in.pot[twm::cutoffAmount] = 1.0f;
    in.cv[twm::cutoffCv] = 2.5f;
    in.pot[twm::warp] = 0.25f;
    in.cv[twm::warpCv] = 2.5f;
    module.update(in, 2, out);
    check(std::abs(out.settings.line[1].semitones - (24.0f + 0.07f)) < 0.002f, "1 V at LINIE 2: another octave");
    check(std::abs(out.settings.bend - 1.0f) < 1.0e-4f, "DCW in the middle plus 2.5 V at full CV: fully bent");
    check(std::abs(out.settings.balance) < 1.0e-4f, "MIX a quarter plus 2.5 V: both lines alike");

    // Taps change the waves and are saved; eight taps come round again.
    module.dirty = false;
    in.warpButton = true;
    module.update(in, 2, out);
    in.warpButton = false;
    module.update(in, 2, out);
    check(out.settings.line[0].first == twm::pd::Wave::square && module.dirty, "tap on WELLE 1: Rechteck, to be saved");
    for (int i = 0; i < 8; ++i)
    {
        in.filterButton = true;
        module.update(in, 2, out);
        in.filterButton = false;
        module.update(in, 2, out);
    }
    check(out.settings.line[1].first == twm::pd::Wave::square, "eight taps on WELLE 2: round to the square");

    // Hold WELLE 2 and turn POSITION right: noise, without a tap and without touching HÜLLK.
    const float depthBefore = out.settings.line[0].depth;
    in.filterButton = true;
    in.pot[twm::position] = 0.0f;
    for (int i = 0; i < 200; ++i)
        module.update(in, 2, out);
    for (float v = 0.0f; v <= 1.0f; v += 0.01f)
    {
        in.pot[twm::position] = v;
        module.update(in, 2, out);
    }
    in.pot[twm::position] = 1.0f;
    module.update(in, 2, out);
    in.filterButton = false;
    module.update(in, 2, out);
    check(out.settings.modulation == twm::pd::Modulation::noise && out.settings.line[1].first == twm::pd::Wave::square
              && std::abs(out.settings.line[0].depth - depthBefore) < 1.0e-4f,
          "WELLE 2 held, POSITION right: noise, the wave and HÜLLK. unchanged");
}

// ------------------------------------------------------------------------------------------------ CPU

void testSpeed()
{
    // The heaviest case: both lines on resonances with ring modulation, alternating waves, the chorus, the pitch
    // moving every block.
    auto player = std::make_unique<twm::pd::Player>();
    player->prepare(kRate);
    twm::pd::PlayParams p;
    p.settings.lines = twm::pd::LineSelect::both;
    p.settings.modulation = twm::pd::Modulation::ring;
    p.settings.line[0].first = twm::pd::Wave::resonanceTrapezoid;
    p.settings.line[0].alternate = true;
    p.settings.line[0].second = twm::pd::Wave::doubleSine;
    p.settings.line[1].first = twm::pd::Wave::resonanceSaw;
    p.settings.chorusMix = 0.5f;
    const auto begin = std::chrono::steady_clock::now();
    play(*player, p, 10.0, [](double t, twm::pd::PlayParams& q) {
        q.gate = std::fmod(t, 0.2) < 0.15;
        q.note = 48.0f + 0.01f * (float) std::sin(t * 3000.0);
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::printf("info  both lines on resonances with ring and chorus: %.2f %% of real time on this computer (%.0f ns "
                "per sample)\n", 10.0 * seconds, seconds / 10.0 / kRate * 1.0e9);
}

// ---------------------------------------------------------------------------------------------- examples

void writeExamples(const std::string& dir)
{
    struct Example
    {
        const char* name;
        int wave1, wave2, lines, modulation;
        float depth, bend, dcwDecay, interval;
        float chorus;
    };
    const Example examples[] = {
        { "01-saege-bass", 0, 1, 0, 0, 0.9f, 0.05f, 0.25f, 12.07f, 0.0f },
        { "02-resonanz-sweep", 5, 1, 0, 0, 1.0f, 0.0f, 0.6f, 12.07f, 0.3f },
        { "03-zwei-linien-chorus", 0, 1, 3, 0, 0.6f, 0.1f, 1.5f, 12.07f, 0.7f },
        { "04-ring-glocke", 3, 6, 3, 1, 0.7f, 0.2f, 2.0f, 19.0f, 0.4f },
        { "05-rauschen-perkussion", 2, 0, 3, 2, 0.8f, 0.0f, 0.12f, 24.0f, 0.0f },
        { "06-puls-flaeche", 2, 7, 2, 0, 0.5f, 0.3f, 3.0f, 0.1f, 0.6f },
    };
    const float riff[] = { 36, 36, 48, 36, 43, 36, 46, 48 };
    for (const auto& e : examples)
    {
        auto player = std::make_unique<twm::pd::Player>();
        player->prepare(kRate);
        twm::pd::PlayParams p;
        auto& s = p.settings;
        s.lines = (twm::pd::LineSelect) e.lines;
        s.modulation = (twm::pd::Modulation) e.modulation;
        s.bend = e.bend;
        s.chorusMix = e.chorus;
        s.line[0].first = (twm::pd::Wave) e.wave1;
        s.line[1].first = (twm::pd::Wave) e.wave2;
        s.line[1].semitones = e.interval;
        for (auto& line : s.line)
        {
            line.depth = e.depth;
            line.dcw.decay = e.dcwDecay;
            line.dcw.sustain = 0.1f;
            line.amp.release = 0.4f;
        }
        std::vector<float> left, right;
        play(*player, p, 8.0, [&riff](double t, twm::pd::PlayParams& q) {
            const int step = (int) (t / 0.25);
            q.note = riff[step % 8] + (step / 16 == 1 ? 5.0f : 0.0f);
            q.gate = std::fmod(t, 0.25) < 0.18;
        }, &left, &right);
        writeWav(dir + "/" + e.name + ".wav", left, right);
        std::printf("info  %s: peak %.2f\n", e.name, std::max(peakOf(left), peakOf(right)));
    }
}
} // namespace

int main(int argc, char** argv)
{
    testAgainstPlugin();
    testChorusAgainstPlugin();
    testTuning();
    testPlayer();
    testLevels();
    testPanel();
    testSpeed();
    if (argc > 1)
        writeExamples(argv[1]);
    std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
