// Host test of the Additiv firmware (-DTONWERK_ADDITIVE=1): builds like HostTest.cpp with a plain C++ compiler.
// Compares the port with Tonwerk Additiv's own voice from this repository for every spectrum and every way of shaping
// it, checks the tuning over the V/Oct range, the player's gate and V/Oct strikes, the panel and the levels, measures
// the render time and writes listening examples.

#include "app/AdditiveModule.h"
#include "dsp/AdditiveVoice.h"

#include "AdditiveEngine.h" // Tonwerk Additiv, from the Synth repository

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
constexpr int kBlock = twm::add::Voice::kControl;
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

const char* spectrumName(int s)
{
    static const char* names[] = { "Sinus", "Säge", "Rechteck", "Dreieck", "Puls", "Orgel", "Hohl", "Hell", "Oktaven",
                                   "Glas", "Primzahlen", "Zufall" };
    return names[s];
}

// --------------------------------------------------------------------------------------- port against plugin

/** The plugin's voices take their random seeds from a counter; the port is given the same one. */
int pluginVoices = 0;

/** The plugin's settings for the port's: its modulation left at rest. */
tonwerkadd::Settings pluginSettings(const twm::add::Settings& m)
{
    tonwerkadd::Settings p;
    p.spectrumA = (tonwerkadd::Spectrum) m.spectrumA;
    p.spectrumB = (tonwerkadd::Spectrum) m.spectrumB;
    p.morph = m.morph;
    p.partials = m.partials;
    p.tilt = m.tilt;
    p.oddEven = m.oddEven;
    p.stretch = m.stretch;
    p.beating = m.beating;
    p.phases = (tonwerkadd::PhaseMode) m.phases;
    p.width = m.width;
    p.combDepth = m.combDepth;
    p.combSpacing = m.combSpacing;
    p.combShift = m.combShift;
    p.vowel = m.vowel;
    p.formantDepth = m.formantDepth;
    p.formantShift = m.formantShift;
    p.filterOn = m.filterOn;
    p.filterMode = (tonwerkadd::FilterMode) m.filterMode;
    p.cutoff = m.cutoff;
    p.resonance = m.resonance;
    p.keytrack = m.keytrack;
    p.spectralOn = m.spectralOn;
    p.bloom = m.bloom;
    p.spectralDecay = m.spectralDecay;
    p.spectralSustain = m.spectralSustain;
    p.spectralDamping = m.spectralDamping;
    p.env1 = { m.env.attack, m.env.decay, m.env.sustain, m.env.release };
    p.velocity = { tonwerkadd::Target::none, 0.0f };
    return p;
}

struct Pair
{
    std::vector<float> pluginLeft, pluginRight, portLeft, portRight;
};

/** One note on both voices: held for `held` seconds, then let go, `seconds` in all. */
Pair renderBoth(const twm::add::Settings& sound, int note, double seconds, double held)
{
    const tonwerkadd::Settings plugin = pluginSettings(sound);
    auto pluginVoice = std::make_unique<tonwerkadd::Voice>();
    ++pluginVoices;
    auto port = std::make_unique<twm::add::Voice>(0x2545f491u * (uint32_t) (pluginVoices * 2 + 1));
    pluginVoice->prepare(kRate);
    port->prepare(kRate);
    tonwerkadd::Shared shared;
    shared.sampleRate = kRate;
    pluginVoice->start(note, 1.0f, plugin, false);
    port->start((float) note, sound);

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
            pluginVoice->release();
            port->release();
            released = true;
        }
        pluginVoice->render(out.pluginLeft.data() + i, out.pluginRight.data() + i, kBlock, plugin, shared);
        port->render(out.portLeft.data() + i, out.portRight.data() + i, kBlock, sound);
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
    // Both turn float phasors; the port works out the rotations in float where the plugin starts from a double
    // frequency, so the high partials drift apart by a few millionths of a turn per turn. Every spectrum is compared
    // over a whole held note of 1.5 s with its release, at three pitches, then every way of shaping one.
    double worst = -200.0;
    std::string worstName;
    auto compare = [&](const twm::add::Settings& sound, int note, const std::string& name) {
        const auto pair = renderBoth(sound, note, 1.5, 1.0);
        if (rms(pair.pluginLeft) < 1.0e-4)
            check(false, name + ": silent");
        const double d = difference(pair);
        if (d > -60.0)
            std::printf("info  %s: %.1f dB\n", name.c_str(), d);
        if (d > worst)
        {
            worst = d;
            worstName = name;
        }
    };
    for (int sp = 0; sp < (int) twm::add::Spectrum::count; ++sp)
        for (int note : { 36, 60, 84 })
        {
            twm::add::Settings s;
            s.spectrumA = (twm::add::Spectrum) sp;
            compare(s, note, std::string(spectrumName(sp)) + " C" + std::to_string(note / 12 - 1));
        }

    struct Shaping
    {
        const char* name;
        std::function<void(twm::add::Settings&)> set;
    };
    const Shaping shapings[] = {
        { "Morph Säge zu Glas", [](auto& s) { s.spectrumB = twm::add::Spectrum::glass; s.morph = 0.6f; } },
        { "12 Obertöne und ein halber", [](auto& s) { s.partials = 12.5f; } },
        { "Neigung +9 dB/Okt", [](auto& s) { s.tilt = 9.0f; } },
        { "Neigung -12 dB/Okt", [](auto& s) { s.tilt = -12.0f; } },
        { "nur ungerade", [](auto& s) { s.oddEven = 1.0f; } },
        { "gerade stärker", [](auto& s) { s.oddEven = -0.7f; } },
        { "Spreizung +1", [](auto& s) { s.stretch = 1.0f; } },
        { "Spreizung -0.6", [](auto& s) { s.stretch = -0.6f; } },
        { "Schwebung 30 ct", [](auto& s) { s.beating = 30.0f; } },
        { "gleiche Phasen, schmal", [](auto& s) { s.phases = twm::add::PhaseMode::aligned; s.width = 0.0f; } },
        { "Kamm", [](auto& s) { s.combDepth = 0.9f; s.combSpacing = 5.0f; s.combShift = 0.3f; } },
        { "Vokal O", [](auto& s) { s.formantDepth = 1.0f; s.vowel = 3.0f; } },
        { "Vokal E verschoben", [](auto& s) { s.formantDepth = 0.7f; s.vowel = 1.4f; s.formantShift = -5.0f; } },
        { "Tiefpass mit Resonanz", [](auto& s) { s.filterOn = true; s.cutoff = 900.0f; s.resonance = 0.8f; s.keytrack = 0.5f; } },
        { "Hochpass", [](auto& s) { s.filterOn = true; s.filterMode = twm::add::FilterMode::high; s.cutoff = 1500.0f; } },
        { "Bandpass", [](auto& s) { s.filterOn = true; s.filterMode = twm::add::FilterMode::band; s.cutoff = 1200.0f; s.resonance = 0.5f; } },
        { "Kerbe", [](auto& s) { s.filterOn = true; s.filterMode = twm::add::FilterMode::notch; s.cutoff = 700.0f; } },
        { "Obertöne-Hüllkurve", [](auto& s) { s.spectralOn = true; s.bloom = 0.4f; s.spectralDecay = 0.8f; s.spectralSustain = 0.2f; s.spectralDamping = 0.8f; } },
        { "gezupft", [](auto& s) { s.spectralOn = true; s.spectralDecay = 0.3f; s.spectralDamping = 1.0f; s.env = { 0.001f, 1.5f, 0.0f, 0.3f }; } },
    };
    for (const auto& shaping : shapings)
        for (int note : { 36, 72 })
        {
            twm::add::Settings s;
            shaping.set(s);
            compare(s, note, std::string(shaping.name) + " C" + std::to_string(note / 12 - 1));
        }

    char text[200];
    std::snprintf(text, sizeof text, "every spectrum, every way of shaping it: within -50 dB of the plugin "
                  "(worst %.1f dB: %s)", worst, worstName.c_str());
    check(worst <= -50.0, text);
}

// ------------------------------------------------------------------------------------------------ player

/** Plays `seconds` through a player, `change` setting the parameters at each block. */
void play(twm::add::Player& player, twm::add::PlayParams& p, double seconds,
          const std::function<void(double, twm::add::PlayParams&)>& change, std::vector<float>* left = nullptr,
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
    // The sine spectrum (the fundamental alone) at 0 to 5 V: on pitch to a hundredth of a cent.
    double worst = 0.0;
    for (int volts = 0; volts <= 5; ++volts)
    {
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        p.settings.spectrumA = twm::add::Spectrum::sine;
        p.note = 36.0f + 12.0f * (float) volts;
        p.gate = true;
        std::vector<float> left;
        play(*player, p, 1.0, [](double, twm::add::PlayParams&) {}, &left);
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
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        std::vector<float> left;
        play(*player, p, 2.0, [](double t, twm::add::PlayParams& q) { q.gate = std::fmod(t, 0.25) < 0.1; }, &left);
        check(player->strikes() == 8, "eight gate pulses: eight notes (" + std::to_string(player->strikes()) + ")");
        const double late = rms(left, (std::size_t) (1.85 * kRate), (std::size_t) (1.95 * kRate));
        const double held = rms(left, (std::size_t) (1.76 * kRate), (std::size_t) (1.84 * kRate));
        check(late < held, "the note falls once its gate ends");
    }
    // Without a gate cable the gate is on: a jump at V/OCT starts a note, a glide does not.
    {
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        play(*player, p, 2.0, [](double t, twm::add::PlayParams& q) {
            q.gate = true;
            q.note = 48.0f + 5.0f * (float) ((int) (t / 0.4));
        });
        check(player->strikes() == 5, "gate held, V/OCT stepping five times: five notes (" + std::to_string(player->strikes()) + ")");
        auto glide = std::make_unique<twm::add::Player>();
        glide->prepare(kRate);
        twm::add::PlayParams q;
        play(*glide, q, 2.0, [](double t, twm::add::PlayParams& r) {
            r.gate = true;
            r.note = 48.0f + 12.0f * (float) t;
        });
        check(glide->strikes() == 1, "a glide of two octaves: one note (" + std::to_string(glide->strikes()) + ")");
    }
    // The note follows V/OCT while it sounds.
    {
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        p.settings.spectrumA = twm::add::Spectrum::sine;
        std::vector<float> left;
        play(*player, p, 1.0, [](double t, twm::add::PlayParams& q) {
            q.gate = true;
            q.note = t < 0.5 ? 48.0f : 48.2f;
        }, &left);
        const double hz = frequencyOf(left, (std::size_t) (0.6 * kRate));
        check(std::abs(1200.0 * std::log2(hz / (440.0 * std::pow(2.0, (48.2 - 69.0) / 12.0)))) < 0.1,
              "a fifth of a semitone at V/OCT moves the sounding note");
    }
    // ABKLINGEN turned down: a struck note dies away while the gate is still on.
    {
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        p.settings.spectralOn = true;
        p.settings.spectralDecay = 0.4f;
        std::vector<float> left;
        play(*player, p, 1.5, [](double, twm::add::PlayParams& q) { q.gate = true; }, &left);
        const double early = rms(left, 0, (std::size_t) (0.1 * kRate));
        const double late = rms(left, (std::size_t) (1.3 * kRate), (std::size_t) (1.5 * kRate));
        char text[120];
        std::snprintf(text, sizeof text, "ABKLINGEN 0.4 s: a held note falls by %.0f dB within 1.3 s", db(early / late));
        check(db(early / late) > 40.0, text);
    }
}

void testLevels()
{
    // Every spectrum at the module's defaults (random phases, width half) and with aligned phases at full width:
    // under full scale; the quietest still a fair share of it.
    double worstPeak = 0.0, quietest = 10.0;
    std::string peakName, quietName;
    for (int sp = 0; sp < (int) twm::add::Spectrum::count; ++sp)
        for (int phases : { 0, 1 })
            for (int note : { 36, 60 })
            {
                auto player = std::make_unique<twm::add::Player>();
                player->prepare(kRate);
                twm::add::PlayParams p;
                p.settings.spectrumA = (twm::add::Spectrum) sp;
                p.settings.phases = (twm::add::PhaseMode) phases;
                p.settings.width = phases == 0 ? 1.0f : 0.5f;
                std::vector<float> left, right;
                play(*player, p, 1.0, [note](double t, twm::add::PlayParams& q) {
                    q.gate = t < 0.8;
                    q.note = (float) note;
                }, &left, &right);
                const std::string name = std::string(spectrumName(sp)) + (phases == 0 ? ", gleiche Phasen" : "")
                                         + " C" + std::to_string(note / 12 - 1);
                const double peak = std::max(peakOf(left), peakOf(right));
                if (peak > worstPeak)
                {
                    worstPeak = peak;
                    peakName = name;
                }
                const double level = rms(left, 0, (std::size_t) (0.8 * kRate));
                if (level < quietest)
                {
                    quietest = level;
                    quietName = name;
                }
            }
    char text[160];
    std::snprintf(text, sizeof text, "every spectrum: peak under full scale (%.2f, %s)", worstPeak, peakName.c_str());
    check(worstPeak < 1.0, text);
    std::snprintf(text, sizeof text, "every spectrum at least -16 dBFS RMS (quietest %.1f dB, %s)", db(quietest),
                  quietName.c_str());
    check(db(quietest) > -16.0, text);
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

/** Holds `button` and turns `pot` from the left to `to`, so it is caught on the way, then lets go. */
void turnHidden(twm::AdditiveModule& module, twm::PanelInputs& in, bool twm::PanelInputs::*button, int pot, float to,
                twm::AdditiveModule::Params& out)
{
    in.*button = true;
    in.pot[pot] = 0.0f;
    for (int i = 0; i < 200; ++i)
        module.update(in, 2, out);
    for (float v = 0.0f; v <= to; v += 0.01f)
    {
        in.pot[pot] = v;
        module.update(in, 2, out);
    }
    in.pot[pot] = to;
    module.update(in, 2, out);
    in.*button = false;
    in.pot[pot] = 0.5f;
    module.update(in, 2, out);
}

void testPanel()
{
    twm::AdditiveModule module;
    twm::AdditiveModule::Params out;
    auto in = centred();
    for (int i = 0; i < 10; ++i)
        module.update(in, 2, out);
    check(std::abs(out.note - 36.0f) < 1.0e-4f, "0 V, TUNE and FINE in the middle: C2");

    const twm::add::Settings d;
    check(out.settings.spectrumA == twm::add::Spectrum::saw && out.settings.spectrumB == twm::add::Spectrum::square
              && ! out.settings.filterOn && out.settings.phases == twm::add::PhaseMode::random
              && std::abs(out.settings.env.attack - d.env.attack) < 1.0e-4f
              && std::abs(out.settings.env.release - d.env.release) < 1.0e-4f
              && std::abs(out.settings.width - d.width) < 1.0e-4f && out.settings.combDepth == 0.0f
              && out.settings.beating == 0.0f && out.settings.oddEven == 0.0f,
          "defaults: saw and square, filter and comb off, the plugin's envelope, phases and width");
    check(std::abs(out.settings.tilt) < 1.0e-4f && std::abs(out.settings.stretch) < 1.0e-4f
              && std::abs(out.settings.morph - 0.5f) < 1.0e-4f && std::abs(out.settings.partials - 8.0f) < 1.0e-3f,
          "MORPH, NEIGUNG, SPREIZ. in the middle: halfway, flat, harmonic; OBERTÖNE in the middle: 8");

    // OBERTÖNE's ends; ABKLINGEN fully right: the spectral envelope off.
    in.pot[twm::position] = 0.0f;
    in.pot[twm::decay] = 1.0f;
    module.update(in, 2, out);
    const float fewest = out.settings.partials;
    const bool endless = ! out.settings.spectralOn;
    in.pot[twm::position] = 1.0f;
    in.pot[twm::decay] = 0.0f;
    module.update(in, 2, out);
    check(std::abs(fewest - 1.0f) < 1.0e-4f && std::abs(out.settings.partials - 64.0f) < 1.0e-3f,
          "OBERTÖNE: one partial at the left, 64 at the right");
    check(endless && out.settings.spectralOn && std::abs(out.settings.spectralDecay - 0.05f) < 1.0e-4f,
          "ABKLINGEN: endless at the right, 50 ms at the left");

    // MORPH's jack adds a whole travel for 5 V; NEIGUNG's ±24 dB/oct at ±5 V; OBERTÖNE and SPREIZ. through their
    // attenuverters.
    in = centred();
    in.cv[twm::tableCv] = 2.5f;
    in.cv[twm::warpCv] = -5.0f;
    in.pot[twm::cutoffAmount] = 1.0f;
    in.cv[twm::cutoffCv] = 5.0f;
    in.pot[twm::positionAmount] = 0.0f;
    in.cv[twm::positionCv] = 2.5f;
    module.update(in, 2, out);
    check(std::abs(out.settings.morph - 1.0f) < 1.0e-4f, "MORPH in the middle plus 2.5 V: spectrum B");
    check(std::abs(out.settings.tilt + 24.0f) < 1.0e-4f, "-5 V at NEIGUNG: -24 dB/oct");
    check(std::abs(out.settings.stretch - 1.0f) < 1.0e-4f, "5 V at SPREIZ., its CV fully right: fully stretched");
    check(std::abs(out.settings.partials - 1.0f) < 1.0e-3f, "2.5 V at OBERTÖNE, its CV fully left: one partial");

    // VOKAL: off at the left, A once the formants are in, U at the right.
    in = centred();
    in.pot[twm::resonance] = 0.0f;
    module.update(in, 2, out);
    const bool off = out.settings.formantDepth == 0.0f;
    in.pot[twm::resonance] = twm::AdditiveModule::kVowelFade;
    module.update(in, 2, out);
    const bool onA = out.settings.formantDepth == 1.0f && out.settings.vowel == 0.0f;
    in.pot[twm::resonance] = 1.0f;
    module.update(in, 2, out);
    check(off && onA && std::abs(out.settings.vowel - 4.0f) < 1.0e-4f, "VOKAL: off, then A, to U at the right");

    // Taps change the spectra and are saved; twelve taps come round again.
    in = centred();
    module.dirty = false;
    in.warpButton = true;
    module.update(in, 2, out);
    in.warpButton = false;
    module.update(in, 2, out);
    check(out.settings.spectrumA == twm::add::Spectrum::square && module.dirty, "tap on KLANG A: Rechteck, to be saved");
    for (int i = 0; i < 12; ++i)
    {
        in.filterButton = true;
        module.update(in, 2, out);
        in.filterButton = false;
        module.update(in, 2, out);
    }
    check(out.settings.spectrumB == twm::add::Spectrum::square, "twelve taps on KLANG B: round to the square");

    // Hold KLANG B and turn TUNE right: the notch filter, without a tap and without moving the pitch.
    const float noteBefore = out.note;
    turnHidden(module, in, &twm::PanelInputs::filterButton, twm::tune, 1.0f, out);
    check(out.settings.filterOn && out.settings.filterMode == twm::add::FilterMode::notch
              && out.settings.spectrumB == twm::add::Spectrum::square && std::abs(out.note - noteBefore) < 1.0e-4f,
          "KLANG B held, TUNE right: the notch, the spectrum and the pitch unchanged");
    // Hold KLANG A and turn ABKLING.'s knob: the partials come in over time, the spectral envelope on.
    in.pot[twm::decay] = 1.0f;
    module.update(in, 2, out);
    turnHidden(module, in, &twm::PanelInputs::warpButton, twm::decay, 0.6f, out);
    in.pot[twm::decay] = 1.0f;
    for (int i = 0; i < 200; ++i)
        module.update(in, 2, out);
    check(out.settings.spectralOn && out.settings.bloom > 0.1f && out.settings.spectralDecay > 1.0e6f,
          "KLANG A held, ABKLING.'s knob turned: the partials bloom, and still never fall");
}

// ------------------------------------------------------------------------------------------------ CPU

void testSpeed()
{
    // The heaviest case: all 64 partials at C2 with every shaping on, the spectral envelope blooming, the stretch and
    // the tilt moving every block and the pitch jittering as a CV does, so levels, ratios and rotations are all
    // worked out again every step.
    auto player = std::make_unique<twm::add::Player>();
    player->prepare(kRate);
    twm::add::PlayParams p;
    auto& s = p.settings;
    s.spectrumB = twm::add::Spectrum::bright;
    s.morph = 0.5f;
    s.beating = 20.0f;
    s.combDepth = 0.5f;
    s.formantDepth = 1.0f;
    s.filterOn = true;
    s.cutoff = 3000.0f;
    s.resonance = 0.5f;
    s.spectralOn = true;
    s.bloom = 0.5f;
    s.spectralDecay = 5.0f;
    s.spectralSustain = 0.3f;
    int most = 0;
    const auto begin = std::chrono::steady_clock::now();
    play(*player, p, 10.0, [&most, &player](double t, twm::add::PlayParams& q) {
        q.gate = std::fmod(t, 0.5) < 0.45;
        q.note = 36.0f + 0.01f * (float) std::sin(t * 3000.0);
        q.settings.stretch = 0.3f + 0.1f * (float) std::sin(t * 7.0);
        q.settings.tilt = 3.0f * (float) std::sin(t * 5.0);
        most = std::max(most, player->partials());
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::printf("info  %d partials with every shaping, all of it worked out every step: %.2f %% of real time on this "
                "computer (%.0f ns per sample)\n", most, 10.0 * seconds, seconds / 10.0 / kRate * 1.0e9);
}

// ---------------------------------------------------------------------------------------------- examples

void writeExamples(const std::string& dir)
{
    struct Example
    {
        const char* name;
        std::function<void(twm::add::Settings&)> set;
        std::function<void(double, twm::add::PlayParams&)> play;
    };
    const float riff[] = { 36, 36, 48, 36, 43, 36, 46, 48 };
    auto sequence = [riff](double t, twm::add::PlayParams& q) {
        const int step = (int) (t / 0.25);
        q.note = riff[step % 8] + (step / 16 == 1 ? 5.0f : 0.0f);
        q.gate = std::fmod(t, 0.25) < 0.18;
    };
    const Example examples[] = {
        { "01-orgel-morph", [](auto& s) { s.spectrumA = twm::add::Spectrum::organ; s.spectrumB = twm::add::Spectrum::hollow; },
          [](double t, auto& q) {
              q.gate = std::fmod(t, 2.0) < 1.8;
              q.note = 36.0f + (std::fmod(t, 4.0) < 2.0 ? 12.0f : 7.0f);
              q.settings.morph = 0.5f - 0.5f * (float) std::cos(t * 0.8);
          } },
        { "02-obertoene-zaehlen", [](auto& s) { s.phases = twm::add::PhaseMode::aligned; },
          [](double t, auto& q) {
              q.gate = true;
              q.note = 36.0f;
              q.settings.partials = 1.0f + 63.0f * (float) (0.5 - 0.5 * std::cos(t * 0.8));
          } },
        { "03-glocke-gespreizt",
          [](auto& s) {
              s.spectrumA = twm::add::Spectrum::bright;
              s.stretch = 0.7f;
              s.beating = 12.0f;
              s.spectralOn = true;
              s.spectralDecay = 2.5f;
              s.spectralDamping = 0.9f;
              s.env = { 0.001f, 4.0f, 0.0f, 1.5f };
          },
          [](double t, auto& q) {
              const float notes[] = { 60, 67, 63, 70 };
              q.note = notes[(int) (t / 1.0) % 4];
              q.gate = std::fmod(t, 1.0) < 0.1;
          } },
        { "04-vokale",
          [](auto& s) { s.formantDepth = 1.0f; s.width = 0.8f; s.beating = 6.0f; },
          [](double t, auto& q) {
              q.gate = true;
              q.note = 36.0f + (std::fmod(t, 4.0) < 2.0 ? 0.0f : 3.0f);
              q.settings.vowel = 2.0f - 2.0f * (float) std::cos(t * 1.2);
          } },
        { "05-gezupfte-sequenz",
          [](auto& s) {
              s.spectrumA = twm::add::Spectrum::square;
              s.spectralOn = true;
              s.spectralDecay = 0.25f;
              s.spectralSustain = 0.05f;
              s.spectralDamping = 1.0f;
              s.tilt = 3.0f;
          },
          sequence },
        { "06-kamm-sweep",
          [](auto& s) { s.combDepth = 1.0f; s.combSpacing = 6.0f; s.filterOn = true; s.cutoff = 4000.0f; s.resonance = 0.4f; },
          [sequence](double t, auto& q) {
              sequence(t, q);
              q.settings.combShift = (float) (t * 0.15);
          } },
    };
    for (const auto& e : examples)
    {
        auto player = std::make_unique<twm::add::Player>();
        player->prepare(kRate);
        twm::add::PlayParams p;
        e.set(p.settings);
        std::vector<float> left, right;
        play(*player, p, 8.0, e.play, &left, &right);
        writeWav(dir + "/" + e.name + ".wav", left, right);
        std::printf("info  %s: peak %.2f\n", e.name, std::max(peakOf(left), peakOf(right)));
    }
}
} // namespace

int main(int argc, char** argv)
{
    testAgainstPlugin();
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
