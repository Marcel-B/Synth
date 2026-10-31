// Host test of the Granular firmware (-DTONWERK_GRANULAR=1): builds like HostTest.cpp with a plain C++ compiler.
// Writes the sources the firmware flashes (`grain-sources <file>`), checks the committed copy against the plugin's,
// compares the port with Tonwerk Granular's own voice from this repository for every source, window, pitch grid,
// filter and LFO target, checks the tuning over the V/Oct range, the player's gate and V/Oct strikes, the panel and
// the levels, measures the render time and writes listening examples.

#include "app/GranularModule.h"
#include "dsp/GrainVoice.h"

#include "GranularEngine.h" // Tonwerk Granular, from the Synth repository

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;
constexpr int kBlock = twm::grain::Voice::kControl;
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

/** The frequency of `x` between `from` and `to` from its rising zero crossings, interpolated between samples. */
double frequencyOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (std::size_t i = from + 1; i < std::min(to, x.size()); ++i)
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

const char* sourceName(int i)
{
    static const char* names[] = { "Chor", "Glas", "Streicher", "Zupfen", "Orgel", "Wind", "Metall", "Sinus", "Rhythmus" };
    return names[i];
}

// ------------------------------------------------------------------------------------------------ the sources

using Sources = twm::grain::Sources;

/** The plugin's sources at full rate as the firmware flashes them: 16 bit, one after another. */
std::vector<int16_t> flashedSamples()
{
    std::vector<int16_t> out;
    out.reserve((std::size_t) Sources::kCount * Sources::kSamples);
    const auto& bank = tonwerkgrain::SourceBank::instance();
    for (int i = 0; i < Sources::kCount; ++i)
    {
        const auto& level = bank.source(i).levels[0];
        for (uint32_t n = 0; n < Sources::kSamples; ++n)
        {
            const float x = n < level.size() ? level[n] : 0.0f;
            out.push_back((int16_t) std::clamp(std::lround(x * 32767.0f), -32767L, 32767L));
        }
    }
    return out;
}

/** The block the firmware finds in its partition: a sector with the header, then the samples. */
std::vector<uint8_t> flashedBlock()
{
    const auto samples = flashedSamples();
    std::vector<uint8_t> block(4096, 0xff);
    const uint32_t header[3] = { Sources::kMagic, Sources::kVersion, (uint32_t) Sources::flashedBytes() };
    std::memcpy(block.data(), header, sizeof header);
    const auto* bytes = reinterpret_cast<const uint8_t*>(samples.data());
    block.insert(block.end(), bytes, bytes + samples.size() * sizeof(int16_t));
    return block;
}

/** The firmware's sources as the module builds them in PSRAM, from the flashed samples. */
struct PortSources
{
    std::vector<int16_t> memory;
    Sources sources;
    PortSources() : memory(Sources::bytes() / sizeof(int16_t))
    {
        const auto flashed = flashedSamples();
        std::copy(flashed.begin(), flashed.end(), memory.begin());
        sources.build(memory.data());
    }
};

const PortSources& portSources()
{
    static const PortSources s;
    return s;
}

void testSourceFile(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
    {
        check(false, path + " missing: make grain-sources");
        return;
    }
    std::vector<uint8_t> committed;
    uint8_t buffer[65536];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
        committed.insert(committed.end(), buffer, buffer + n);
    std::fclose(f);
    check(committed == flashedBlock(), "data/grain-sources.bin holds the plugin's sources (else: make grain-sources)");

    // The lower copies the module computes keep within -60 dB of the plugin's (16 bit and float taps).
    double worst = -200.0;
    const auto& bank = tonwerkgrain::SourceBank::instance();
    for (int i = 0; i < Sources::kCount; ++i)
        for (int k = 0; k < twm::grain::Source::kLevels; ++k)
        {
            const auto& plugin = bank.source(i).levels[(std::size_t) k];
            const auto& port = portSources().sources.source(i);
            double error = 0.0, signal = 0.0;
            for (uint32_t n = 0; n < port.length[(std::size_t) k]; ++n)
            {
                const double a = plugin[n], b = port.level[(std::size_t) k][n] * (double) Sources::kScale;
                error += (a - b) * (a - b);
                signal += a * a;
            }
            worst = std::max(worst, db(std::sqrt(error / std::max(signal, 1.0e-12))));
        }
    char text[160];
    std::snprintf(text, sizeof text, "the sources and their lower copies within -60 dB of the plugin's (worst %.1f dB)", worst);
    check(worst < -60.0, text);
}

// --------------------------------------------------------------------------------------- port against plugin

/** The plugin's voices take their dice from a counter; the port is given the same seed. */
int pluginVoices = 0;

struct Sound
{
    const char* name;
    std::function<void(tonwerkgrain::Settings&, twm::grain::Settings&)> set;
};

/** Plays `note` with the gate held for `held` seconds and released for `after`, through both, and returns the
 *  difference over the plugin's level and that level. */
std::pair<double, double> compare(int note, const tonwerkgrain::Settings& plugin, const twm::grain::Settings& port,
                                  double held = 1.2, double after = 0.6)
{
    auto a = std::make_unique<tonwerkgrain::Voice>();
    const uint32_t seed = 0x3c6ef372u * ((uint32_t) ++pluginVoices * 2u + 1u);
    auto b = std::make_unique<twm::grain::Voice>(seed);
    a->prepare(kRate);
    b->prepare(kRate);
    const auto& source = tonwerkgrain::SourceBank::instance().source(plugin.source);
    const auto& portSource = portSources().sources.source(port.source);
    tonwerkgrain::Shared shared;
    shared.sampleRate = kRate;
    a->start(note, 1.0f, plugin, false);
    b->setNote(note);
    b->start(port);
    const int total = (int) ((held + after) * kRate) / kBlock;
    double error = 0.0, signal = 0.0;
    for (int block = 0; block < total; ++block)
    {
        if (block == (int) (held * kRate) / kBlock)
        {
            a->release();
            b->release();
        }
        float al[kBlock] {}, ar[kBlock] {}, bl[kBlock] {}, br[kBlock] {};
        a->render(al, ar, kBlock, plugin, source, shared);
        b->render(bl, br, kBlock, port, portSource);
        for (int i = 0; i < kBlock; ++i)
        {
            error += (double) (al[i] - bl[i]) * (al[i] - bl[i]) + (double) (ar[i] - br[i]) * (ar[i] - br[i]);
            signal += (double) al[i] * al[i] + (double) ar[i] * ar[i];
        }
    }
    const double samples = 2.0 * total * kBlock;
    return { db(std::sqrt(error / std::max(signal, 1.0e-20))), db(std::sqrt(signal / samples)) };
}

/** The plugin's settings with everything the module leaves out held still, and the port's alike. */
void neutral(tonwerkgrain::Settings& p, twm::grain::Settings& q)
{
    p.velocity = { tonwerkgrain::Target::none, 0.0f };
    p.env2Route = {};
    p.wheel = {};
    for (auto& lfo : p.lfo)
    {
        lfo.sync = false;
        lfo.route = {};
    }
    p.lfo[1].rate = 0.0f; // the second LFO never wraps, so it never draws
    p.voiceMode = tonwerkgrain::VoiceMode::mono;
    q = {};
    q.source = p.source;
    q.position = p.position;
    q.spray = p.spray;
    q.scan = p.scan;
    q.size = p.size;
    q.density = p.density;
    q.jitter = p.jitter;
    q.window = (twm::grain::Window) p.window;
    q.reverse = p.reverse;
    q.width = p.width;
    q.fine = p.fine;
    q.scatter = p.scatter;
    q.scatterMode = (twm::grain::Scatter) p.scatterMode;
    q.filterOn = p.filterOn;
    q.filterMode = (twm::grain::FilterMode) p.filterMode;
    q.cutoff = p.cutoff;
    q.resonance = p.resonance;
    q.keytrack = p.keytrack;
    q.env = { p.env1.attack, p.env1.decay, p.env1.sustain, p.env1.release };
    q.lfoShape = (twm::grain::LfoShape) p.lfo[0].shape;
    q.lfoRate = p.lfo[0].rate;
    q.lfoTarget = (twm::grain::Target) p.lfo[0].route.target;
    q.lfoAmount = p.lfo[0].route.amount;
}

void testAgainstPlugin()
{
    using P = tonwerkgrain::Settings;
    std::vector<std::pair<std::string, std::function<void(P&)>>> sounds;
    for (int i = 0; i < Sources::kCount; ++i)
        sounds.push_back({ std::string(sourceName(i)), [i](P& p) { p.source = i; } });
    sounds.push_back({ "Flach, dicht", [](P& p) { p.window = tonwerkgrain::Window::flat; p.density = 120.0f; p.size = 0.25f; } });
    sounds.push_back({ "Schlag, rückwärts", [](P& p) { p.window = tonwerkgrain::Window::struck; p.reverse = 0.5f; p.source = 3; } });
    sounds.push_back({ "Streuung Oktaven", [](P& p) { p.scatter = 2400.0f; p.scatterMode = tonwerkgrain::Scatter::octaves; p.source = 2; } });
    sounds.push_back({ "Streuung Quinten", [](P& p) { p.scatter = 1500.0f; p.scatterMode = tonwerkgrain::Scatter::fifths; p.source = 4; } });
    sounds.push_back({ "Streuung frei", [](P& p) { p.scatter = 300.0f; p.source = 0; } });
    sounds.push_back({ "Scan vorwärts", [](P& p) { p.scan = 1.0f; p.spray = 0.0f; p.source = 8; } });
    sounds.push_back({ "Scan rückwärts", [](P& p) { p.scan = -0.7f; p.source = 5; } });
    sounds.push_back({ "Tiefpass 24", [](P& p) { p.filterOn = true; p.filterMode = tonwerkgrain::FilterMode::low24; p.cutoff = 900.0f; p.resonance = 0.6f; } });
    sounds.push_back({ "Tiefpass 12", [](P& p) { p.filterOn = true; p.filterMode = tonwerkgrain::FilterMode::low12; p.cutoff = 1500.0f; p.keytrack = 1.0f; } });
    sounds.push_back({ "Hochpass", [](P& p) { p.filterOn = true; p.filterMode = tonwerkgrain::FilterMode::high12; p.cutoff = 2000.0f; } });
    sounds.push_back({ "Bandpass", [](P& p) { p.filterOn = true; p.filterMode = tonwerkgrain::FilterMode::band12; p.cutoff = 1000.0f; p.resonance = 0.8f; p.source = 6; } });
    sounds.push_back({ "LFO auf Position", [](P& p) { p.lfo[0].rate = 2.0f; p.lfo[0].route = { tonwerkgrain::Target::position, 0.3f }; } });
    sounds.push_back({ "LFO auf Tonhöhe", [](P& p) { p.lfo[0].shape = tonwerkwave::LfoShape::triangle; p.lfo[0].rate = 5.0f; p.lfo[0].route = { tonwerkgrain::Target::pitch, 0.05f }; } });
    sounds.push_back({ "LFO Zufall auf Cutoff", [](P& p) { p.lfo[0].shape = tonwerkwave::LfoShape::random; p.lfo[0].rate = 8.0f; p.lfo[0].route = { tonwerkgrain::Target::cutoff, 0.3f }; p.filterOn = true; p.cutoff = 2000.0f; } });
    sounds.push_back({ "LFO auf Dichte", [](P& p) { p.lfo[0].shape = tonwerkwave::LfoShape::square; p.lfo[0].rate = 3.0f; p.lfo[0].route = { tonwerkgrain::Target::density, -0.5f }; } });
    sounds.push_back({ "40 Körner", [](P& p) { p.density = 200.0f; p.size = 0.5f; p.jitter = 0.0f; } });

    double worst = -200.0;
    std::string worstName;
    for (const auto& [name, set] : sounds)
        for (const int note : { 48, 60, 84 })
        {
            P p;
            set(p);
            twm::grain::Settings q;
            neutral(p, q);
            const auto [difference, level] = compare(note, p, q);
            if (difference > worst)
            {
                worst = difference;
                worstName = name + " auf " + std::to_string(note);
            }
            if (level < -60.0)
                check(false, name + " is silent");
        }
    char text[200];
    std::snprintf(text, sizeof text, "every source, window, grid, filter and LFO target: within -50 dB of the plugin (worst %.1f dB: %s)",
                  worst, worstName.c_str());
    check(worst < -50.0, text);
}

// ------------------------------------------------------------------------------------------------ player

void play(twm::grain::Player& player, twm::grain::PlayParams& p, double seconds,
          const std::function<void(double, twm::grain::PlayParams&)>& change, std::vector<float>* left = nullptr,
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

std::unique_ptr<twm::grain::Player> makePlayer()
{
    auto player = std::make_unique<twm::grain::Player>();
    player->prepare(kRate);
    player->use(&portSources().sources);
    return player;
}

void testTuning()
{
    // One flat grain of the sine at a time, short enough to fit in the source three octaves up (the source lasts
    // 0.5 s there): its middle is the sine read at the note's rate.
    double worst = 0.0;
    for (int octave = 0; octave <= 5; ++octave)
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
        auto& s = p.settings;
        s.source = 7;
        s.window = twm::grain::Window::flat;
        s.size = 0.4f;
        s.density = 1.0f;
        s.jitter = 0.0f;
        s.spray = 0.0f;
        s.width = 0.0f;
        s.env = { 0.001f, 1.0f, 1.0f, 0.1f };
        const float volts = (float) octave - 2.0f; // C2 to C7
        std::vector<float> left;
        play(*player, p, 0.9, [volts](double, twm::grain::PlayParams& q) {
            q.gate = true;
            q.note = 60.0f + 12.0f * volts;
        }, &left);
        const double expected = 440.0 * std::exp2((60.0 + 12.0 * volts - 69.0) / 12.0);
        const double measured = frequencyOf(left, (std::size_t) (0.1 * kRate), (std::size_t) (0.3 * kRate));
        worst = std::max(worst, std::abs(1200.0 * std::log2(measured / expected)));
    }
    char text[120];
    std::snprintf(text, sizeof text, "-2 to +3 V: C2 to C7 on pitch (worst %.3f cents)", worst);
    check(worst < 0.5, text);
}

void testPlayer()
{
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
        std::vector<float> left;
        play(*player, p, 2.0, [](double t, twm::grain::PlayParams& q) { q.gate = std::fmod(t, 0.25) < 0.12; }, &left);
        check(player->strikes() == 8, "eight gate pulses: eight strikes (" + std::to_string(player->strikes()) + ")");
        check(rms(left) > 0.01, "and the grains sound");
    }
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
        play(*player, p, 1.0, [](double t, twm::grain::PlayParams& q) {
            q.gate = true;
            q.note = 60.0f + 2.0f * (float) (int) (t / 0.2);
        });
        check(player->strikes() == 5, "gate held, V/OCT stepping five times: five strikes (" + std::to_string(player->strikes()) + ")");
    }
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
        play(*player, p, 1.0, [](double t, twm::grain::PlayParams& q) {
            q.gate = true;
            q.note = 48.0f + 24.0f * (float) t;
        });
        check(player->strikes() == 1, "a glide of two octaves: one strike (" + std::to_string(player->strikes()) + ")");
    }
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
        std::vector<float> left;
        play(*player, p, 3.0, [](double t, twm::grain::PlayParams& q) { q.gate = t < 0.5; }, &left);
        check(rms(left, (std::size_t) (2.8 * kRate)) == 0.0, "the cloud dies away once its gate ends (release 0.8 s)");
    }
}

void testLevels()
{
    // Every source as a dense, wide cloud with the envelope open, and as a sparse one: under full scale, and the
    // defaults loud enough for a module (about -20 dBFS RMS or more).
    double loudest = 0.0, quietest = 1.0;
    std::string loudestName, quietestName;
    for (int i = 0; i < Sources::kCount; ++i)
        for (const bool dense : { false, true })
        {
            auto player = makePlayer();
            twm::grain::PlayParams p;
            p.settings.source = i;
            if (dense)
            {
                p.settings.density = 200.0f;
                p.settings.size = 0.4f;
                p.settings.width = 1.0f;
                p.settings.window = twm::grain::Window::flat;
            }
            std::vector<float> left, right;
            play(*player, p, 3.0, [](double, twm::grain::PlayParams& q) { q.gate = true; }, &left, &right);
            const double peak = std::max(peakOf(left), peakOf(right));
            const double level = std::max(rms(left, 24000), rms(right, 24000));
            const std::string name = std::string(sourceName(i)) + (dense ? ", dicht" : "");
            if (peak > loudest)
            {
                loudest = peak;
                loudestName = name;
            }
            if (! dense && level < quietest)
            {
                quietest = level;
                quietestName = name;
            }
        }
    char text[160];
    std::snprintf(text, sizeof text, "every source sparse and dense: peak under full scale (%.2f, %s)", loudest, loudestName.c_str());
    check(loudest < 1.0, text);
    std::snprintf(text, sizeof text, "the defaults at least -26 dBFS RMS (quietest %.1f dB, %s)", db(quietest), quietestName.c_str());
    check(db(quietest) > -26.0, text);
}

// ------------------------------------------------------------------------------------------------- panel

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
    twm::GranularModule module;
    twm::GranularModule::Params out;
    auto in = centred();
    for (int i = 0; i < 10; ++i)
        module.update(in, 2, out);
    check(std::abs(out.note - 60.0f) < 1.0e-4f && out.settings.fine == 0.0f, "0 V, TUNE and FINE in the middle: C4, the sources' own pitch");
    check(out.settings.scan == 0.0f, "SCAN in the middle: the position stands still");

    // The hidden defaults are the plugin's.
    const twm::grain::Settings d;
    check(std::abs(out.settings.env.attack - d.env.attack) < 1.0e-4f && std::abs(out.settings.env.release - d.env.release) < 1.0e-4f
              && std::abs(out.settings.jitter - d.jitter) < 1.0e-4f && std::abs(out.settings.width - d.width) < 1.0e-4f
              && ! out.settings.filterOn && out.settings.lfoTarget == twm::grain::Target::none,
          "defaults: the plugin's envelope, randomness and width, no filter, no LFO");

    // GRÖSSE and DICHTE take their jacks over the whole travel, POSITION and STREUUNG through attenuators.
    in.pot[twm::table] = 0.0f;
    in.cv[twm::tableCv] = 5.0f;
    in.pot[twm::warp] = 1.0f;
    in.cv[twm::warpCv] = -5.0f;
    in.pot[twm::cutoff] = 0.0f;
    in.pot[twm::cutoffAmount] = 1.0f;
    in.cv[twm::cutoffCv] = 2.5f;
    module.update(in, 2, out);
    check(std::abs(out.settings.size - twm::GranularModule::kSizeHigh) < 1.0e-4f, "GRÖSSE left plus 5 V: the longest grains");
    check(std::abs(out.settings.density - twm::GranularModule::kDensityLow) < 1.0e-4f, "DICHTE right minus 5 V: the sparsest");
    check(std::abs(out.settings.spray - 0.5f) < 1.0e-4f, "STREUUNG left plus 2.5 V at full CV: half the source");

    // Taps: the next source, the next window; nine taps come round again.
    module.dirty = false;
    in.warpButton = true;
    module.update(in, 2, out);
    in.warpButton = false;
    module.update(in, 2, out);
    check(out.settings.source == 1 && module.dirty, "tap on QUELLE: Glas, to be saved");
    for (int i = 0; i < 8; ++i)
    {
        in.warpButton = true;
        module.update(in, 2, out);
        in.warpButton = false;
        module.update(in, 2, out);
    }
    check(out.settings.source == 0, "eight more taps: round to Chor");
    in.filterButton = true;
    module.update(in, 2, out);
    in.filterButton = false;
    module.update(in, 2, out);
    check(out.settings.window == twm::grain::Window::flat, "tap on FENSTER: Flach");

    // Hold FENSTER and turn TUNE right: the bandpass, the other values unchanged.
    const float sizeBefore = out.settings.size;
    in.filterButton = true;
    in.pot[twm::tune] = 0.0f;
    for (int i = 0; i < 200; ++i)
        module.update(in, 2, out);
    for (float v = 0.0f; v <= 1.0f; v += 0.01f)
    {
        in.pot[twm::tune] = v;
        module.update(in, 2, out);
    }
    in.pot[twm::tune] = 1.0f;
    module.update(in, 2, out);
    in.filterButton = false;
    module.update(in, 2, out);
    check(out.settings.filterOn && out.settings.filterMode == twm::grain::FilterMode::band12
              && std::abs(out.settings.size - sizeBefore) < 1.0e-6f && std::abs(out.note - 60.0f) < 1.0e-4f,
          "FENSTER held, TUNE right: bandpass, the pitch and GRÖSSE unchanged");
}

// ------------------------------------------------------------------------------------------------ CPU

void testSpeed()
{
    // The heaviest case: forty grains at once, three octaves up (the lowest copy), the 24 dB filter, a moving pitch.
    auto player = makePlayer();
    twm::grain::PlayParams p;
    auto& s = p.settings;
    s.density = 200.0f;
    s.size = 1.0f;
    s.filterOn = true;
    s.filterMode = twm::grain::FilterMode::low24;
    s.cutoff = 3000.0f;
    s.scatter = 1200.0f;
    int most = 0;
    const auto begin = std::chrono::steady_clock::now();
    play(*player, p, 10.0, [&](double t, twm::grain::PlayParams& q) {
        q.gate = true;
        q.note = 96.0f + 0.01f * (float) std::sin(t * 3000.0);
        most = std::max(most, player->grains());
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    check(most == twm::grain::kMaxGrains, "the dense cloud reaches forty grains (" + std::to_string(most) + ")");
    std::printf("info  forty grains with the 24 dB filter: %.2f %% of real time on this computer (%.0f ns per sample)\n",
                10.0 * seconds, seconds / 10.0 / kRate * 1.0e9);
}

// ---------------------------------------------------------------------------------------------- examples

void writeExamples(const std::string& dir)
{
    struct Example
    {
        const char* name;
        std::function<void(twm::grain::Settings&)> set;
        std::function<void(double, twm::grain::PlayParams&)> play;
    };
    const auto drone = [](double, twm::grain::PlayParams& q) {
        q.gate = true;
        q.note = 55.0f;
    };
    const Example examples[] = {
        { "01-chor-wolke", [](twm::grain::Settings& s) { s.source = 0; s.size = 0.25f; s.density = 40.0f; s.spray = 0.4f; s.scan = 0.15f; s.width = 1.0f; }, drone },
        { "02-glas-funken", [](twm::grain::Settings& s) { s.source = 1; s.size = 0.04f; s.density = 25.0f; s.spray = 1.0f; s.scatter = 2400.0f; s.scatterMode = twm::grain::Scatter::octaves; s.window = twm::grain::Window::struck; s.width = 1.0f; }, drone },
        { "03-streicher-scan", [](twm::grain::Settings& s) { s.source = 2; s.size = 0.15f; s.density = 60.0f; s.spray = 0.05f; s.scan = 0.5f; s.filterOn = true; s.cutoff = 3000.0f; s.lfoTarget = twm::grain::Target::cutoff; s.lfoRate = 0.2f; s.lfoAmount = 0.2f; }, drone },
        { "04-rhythmus-stottern", [](twm::grain::Settings& s) { s.source = 8; s.size = 0.125f; s.density = 8.0f; s.jitter = 0.0f; s.spray = 0.0f; s.window = twm::grain::Window::flat; s.lfoTarget = twm::grain::Target::position; s.lfoShape = twm::grain::LfoShape::random; s.lfoRate = 2.0f; s.lfoAmount = 0.5f; }, [](double, twm::grain::PlayParams& q) { q.gate = true; q.note = 60.0f; } },
        { "05-zupfen-sequenz", [](twm::grain::Settings& s) { s.source = 3; s.size = 0.08f; s.density = 50.0f; s.spray = 0.02f; s.position = 0.0f; s.scan = 1.0f; s.env = { 0.002f, 0.4f, 0.0f, 0.3f }; },
          [](double t, twm::grain::PlayParams& q) {
              static const float riff[] = { 48, 55, 60, 63, 58, 55, 51, 53 };
              q.note = riff[(int) (t / 0.25) % 8];
              q.gate = std::fmod(t, 0.25) < 0.2;
          } },
        { "06-orgel-quinten", [](twm::grain::Settings& s) { s.source = 4; s.size = 0.3f; s.density = 30.0f; s.spray = 0.3f; s.scatter = 1500.0f; s.scatterMode = twm::grain::Scatter::fifths; s.reverse = 0.5f; s.width = 1.0f; }, drone },
    };
    for (const auto& e : examples)
    {
        auto player = makePlayer();
        twm::grain::PlayParams p;
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
    if (argc > 2 && std::string(argv[1]) == "grain-sources")
    {
        const auto block = flashedBlock();
        FILE* f = std::fopen(argv[2], "wb");
        if (f == nullptr || std::fwrite(block.data(), 1, block.size(), f) != block.size())
            return 1;
        std::fclose(f);
        std::printf("wrote %s (%zu bytes)\n", argv[2], block.size());
        return 0;
    }
    testSourceFile("../data/grain-sources.bin");
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
