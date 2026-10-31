// Host test of the Physical firmware (-DTONWERK_PHYSICAL=1): builds like HostTest.cpp with a plain C++ compiler.
// Compares the port with Tonwerk Physical's own voice from this repository for every exciter on every resonator,
// checks the tuning over the V/Oct range, the player's gate and V/Oct strikes, the panel, the levels at the extremes,
// measures the render time and writes listening examples.

#include "app/PhysicalModule.h"
#include "dsp/PhysicalVoice.h"

#include "PhysicalEngine.h" // Tonwerk Physical, from the Synth repository

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;
constexpr int kBlock = twm::phys::Voice::kControl;
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

/** The period of `x` near `expected` samples: the normalised autocorrelation's peak, interpolated between lags. */
double period(const std::vector<float>& x, std::size_t from, double expected)
{
    const std::size_t length = std::min<std::size_t>(x.size() - from - (std::size_t) (expected * 1.3) - 2, 9600);
    auto correlation = [&](int lag) {
        double sum = 0.0, a = 0.0, b = 0.0;
        for (std::size_t i = from; i < from + length; ++i)
        {
            sum += (double) x[i] * x[i + (std::size_t) lag];
            a += (double) x[i] * x[i];
            b += (double) x[i + (std::size_t) lag] * x[i + (std::size_t) lag];
        }
        return sum / std::sqrt(std::max(1.0e-20, a * b));
    };
    int best = (int) (expected * 0.8);
    double bestValue = -2.0;
    for (int lag = (int) (expected * 0.8); lag <= (int) (expected * 1.25); ++lag)
    {
        const double c = correlation(lag);
        if (c > bestValue)
        {
            bestValue = c;
            best = lag;
        }
    }
    const double l = correlation(best - 1), c = bestValue, r = correlation(best + 1);
    const double shift = 0.5 * (l - r) / std::min(-1.0e-12, l - 2.0 * c + r);
    return best + shift;
}

double cents(double hz, double reference) { return 1200.0 * std::log2(hz / reference); }

void writeWav(const std::string& path, const std::vector<float>& x)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    const uint32_t frames = (uint32_t) x.size(), bytes = frames * 2;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32((uint32_t) kRate); u32((uint32_t) kRate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    for (float v : x)
    {
        const auto s = (int16_t) std::lround(std::clamp(twm::softClip(v), -1.0f, 1.0f) * 32000.0f);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

const char* exciterName(int e)
{
    static const char* names[] = { "Zupfen", "Schlagen", "Streichen", "Blasen" };
    return names[e];
}
const char* resonatorName(int r)
{
    static const char* names[] = { "Saite", "Rohr geschlossen", "Rohr offen", "Stab", "Glocke", "Fell", "Schale" };
    return names[r];
}

// --------------------------------------------------------------------------------------- port against plugin

/** The plugin's voices take their noise seeds from a counter; the port is given the same one. */
int pluginVoices = 0;

struct Pair
{
    std::vector<float> plugin, port;
};

/** One note on both: held for `held` seconds, then let go, `seconds` in all. */
Pair renderBoth(int exciter, int resonator, int note, float velocity, double seconds, double held,
                void (*adjust)(tonwerkphys::Settings&, twm::phys::Settings&) = nullptr)
{
    tonwerkphys::Settings ps;
    ps.exciter = (tonwerkphys::Exciter) exciter;
    ps.resonator = (tonwerkphys::Resonator) resonator;
    ps.width = 0.0f;
    for (auto& lfo : ps.lfo)
        lfo.rate = 0.0f;   // the plugin's LFOs draw noise when they wrap; held still, the sequences stay the same
    twm::phys::Settings ms;
    ms.exciter = (twm::phys::Exciter) exciter;
    ms.resonator = (twm::phys::Resonator) resonator;
    if (adjust != nullptr)
        adjust(ps, ms);

    auto plugin = std::make_unique<tonwerkphys::Voice>();
    ++pluginVoices;
    auto port = std::make_unique<twm::phys::Voice>(0x2545f491u * (uint32_t) (pluginVoices * 2 + 1));
    plugin->prepare(kRate);
    port->prepare(kRate);
    tonwerkphys::Shared shared;
    shared.sampleRate = kRate;
    plugin->start(note, velocity, ps, false);
    port->start((float) note, velocity, ms, false);

    const int total = (int) (seconds * kRate) / kBlock * kBlock;
    const int heldFor = (int) (held * kRate);
    Pair out;
    out.plugin.assign((std::size_t) total, 0.0f);
    out.port.assign((std::size_t) total, 0.0f);
    std::vector<float> right(kBlock);
    bool released = false;
    for (int i = 0; i < total; i += kBlock)
    {
        if (! released && i >= heldFor)
        {
            plugin->release();
            port->release();
            released = true;
        }
        std::fill(right.begin(), right.end(), 0.0f);
        plugin->render(out.plugin.data() + i, right.data(), kBlock, ps, shared);
        port->render(out.port.data() + i, kBlock, ms);
    }
    return out;
}

/** The port's difference from the plugin over a stretch, in dB of the plugin's level there. */
double difference(const Pair& pair, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += (double) (pair.port[i] - pair.plugin[i]) * (pair.port[i] - pair.plugin[i]);
    return db(std::sqrt(sum / (double) (to - from)) / std::max(1.0e-9, rms(pair.plugin, from, to)));
}

void testAgainstPlugin()
{
    // The waveguides in float follow the plugin's double to the sample. The resonances' coefficients are float in both,
    // rounded once in the plugin and after a few steps in the port; either rounding puts a low mode a few cents off its
    // exact pitch, the port no further than the plugin. Modes rounded apart drift in phase and a bowl's close pairs
    // beat at another phase, which no one hears, so there the waveform is compared over its first 0.2 s, the level in
    // steps of 50 ms over the first half second and over the whole note.
    double worstWave = -200.0, worstLevel = 0.0, worstTotal = 0.0;
    std::string waveName, levelName, totalName;
    for (int e = 0; e < 4; ++e)
        for (int r = 0; r < (int) twm::phys::Resonator::count; ++r)
        {
            const auto pair = renderBoth(e, r, 48, 0.8f, 1.5, 1.0);
            const std::string name = std::string(exciterName(e)) + " auf " + resonatorName(r);
            const bool waveguide = twm::phys::isWaveguide((twm::phys::Resonator) r);
            const double wave = difference(pair, 0, waveguide ? pair.plugin.size() : (std::size_t) (0.2 * kRate));
            if (wave > worstWave)
            {
                worstWave = wave;
                waveName = name;
            }
            const std::size_t window = (std::size_t) (0.05 * kRate);
            const double total = std::abs(db(rms(pair.port) / rms(pair.plugin)));
            if (total > worstTotal)
            {
                worstTotal = total;
                totalName = name;
            }
            for (std::size_t i = 0; i + window <= (std::size_t) (0.5 * kRate); i += window)
            {
                const double plugin = rms(pair.plugin, i, i + window), port = rms(pair.port, i, i + window);
                if (plugin < 1.0e-3)
                    continue;
                const double off = std::abs(db(port / plugin));
                if (off > worstLevel)
                {
                    worstLevel = off;
                    levelName = name;
                }
            }
            if (rms(pair.plugin) < 1.0e-4)
                check(false, name + ": silent");
        }
    char text[200];
    std::snprintf(text, sizeof text, "every exciter on every resonator: waveform within -40 dB of the plugin (worst %.1f "
                  "dB: %s)", worstWave, waveName.c_str());
    check(worstWave <= -40.0, text);
    std::snprintf(text, sizeof text, "its level within 0.5 dB in every 50 ms of the attack (worst %.2f dB: %s)",
                  worstLevel, levelName.c_str());
    check(worstLevel <= 0.5, text);
    std::snprintf(text, sizeof text, "and over the whole note (worst %.2f dB: %s)", worstTotal, totalName.c_str());
    check(worstTotal <= 0.5, text);
}

void testExtremesAgainstPlugin()
{
    // Stiff string, hard pluck, open damping, at the low and high ends of the keyboard.
    double worst = -200.0;
    for (int note : { 24, 48, 84 })
    {
        const auto pair = renderBoth(0, 0, note, 1.0f, 1.0, 1.0, [](tonwerkphys::Settings& p, twm::phys::Settings& m) {
            p.inharmonicity = m.inharmonicity = 0.8f;
            p.hardness = m.hardness = 0.9f;
            p.damping = m.damping = 0.1f;
            p.filterOn = m.filterOn = true;
            p.filterMode = tonwerkphys::FilterMode::low24;
            m.filterMode = twm::phys::FilterMode::low24;
            p.cutoff = m.cutoff = 3000.0f;
            p.resonance = m.resonance = 0.6f;
        });
        std::vector<float> diff(pair.plugin.size());
        for (std::size_t i = 0; i < diff.size(); ++i)
            diff[i] = pair.port[i] - pair.plugin[i];
        worst = std::max(worst, db(rms(diff) / std::max(1.0e-9, rms(pair.plugin))));
    }
    char text[160];
    std::snprintf(text, sizeof text, "stiff string with the filter on, C1 to C6: within -40 dB of the plugin (worst %.1f dB)",
                  worst);
    check(worst <= -40.0, text);
}

// ------------------------------------------------------------------------------------------------- tuning

void testTuning()
{
    // V/Oct 0 to 5 V through the panel's pitch: C2 to C7, measured on the plucked string and on the bar.
    double worstPort = 0.0, worstPlugin = 0.0;
    std::string worstName;
    for (int volts = 0; volts <= 5; ++volts)
    {
        const int note = 36 + 12 * volts;
        for (int r : { 0, 3 })
        {
            const auto pair = renderBoth(0, r, note, 1.0f, 0.6, 0.6, [](tonwerkphys::Settings& p, twm::phys::Settings& m) {
                p.noise = m.noise = 0.0f;
                p.damping = m.damping = 0.2f;
            });
            const double hz = 440.0 * std::exp2((note - 69) / 12.0);
            const double expected = kRate / hz;
            const double port = kRate / period(pair.port, 4800, expected);
            const double plugin = kRate / period(pair.plugin, 4800, expected);
            if (std::abs(cents(port, hz)) > worstPort)
            {
                worstPort = std::abs(cents(port, hz));
                worstName = std::string(resonatorName(r)) + " at " + std::to_string(volts) + " V, plugin "
                            + std::to_string(cents(plugin, hz)) + " cents";
            }
            worstPlugin = std::max(worstPlugin, std::abs(cents(port, plugin)));
        }
    }
    char text[240];
    std::snprintf(text, sizeof text, "string and bar from 0 to 5 V: %.2f cents from the plugin; worst %.2f cents off "
                  "(%s)", worstPlugin, worstPort, worstName.c_str());
    check(worstPort < 5.0 && worstPlugin < 0.5, text);
}

// ------------------------------------------------------------------------------------------------- player

struct Run
{
    std::vector<float> dry, out;
};

/** The player for `seconds`, with `params` changed by `at(time, params)` before each block. */
template <typename At>
Run play(twm::phys::Player& player, twm::phys::PlayParams params, double seconds, At at)
{
    const int total = (int) (seconds * kRate) / kBlock * kBlock;
    Run run;
    run.dry.assign((std::size_t) total, 0.0f);
    run.out.assign((std::size_t) total, 0.0f);
    for (int i = 0; i < total; i += kBlock)
    {
        at((double) i / kRate, params);
        player.render(run.dry.data() + i, run.out.data() + i, kBlock, params);
    }
    return run;
}

void testPlayer()
{
    auto player = std::make_unique<twm::phys::Player>();

    // Gate pulses: one strike each, at most `voices` ringing.
    player->prepare(kRate);
    twm::phys::PlayParams p;
    p.voices = 2;
    p.settings.release = 60.0f;   // the module's default: the damper stays up
    play(*player, p, 2.0, [](double t, twm::phys::PlayParams& q) { q.gate = std::fmod(t, 0.25) < 0.01; });
    check(player->strikes() == 8, "eight gate pulses strike eight notes (" + std::to_string(player->strikes()) + ")");
    check(player->activeVoices() <= 2, "two voices: never more than two ring");

    // One voice: a new strike plays on the ringing string; four: they all ring together.
    for (int voices : { 1, 4 })
    {
        player->prepare(kRate);
        p.voices = voices;
        p.settings.decay = 8.0f;
        play(*player, p, 1.0, [](double t, twm::phys::PlayParams& q) {
            q.gate = std::fmod(t, 0.2) < 0.01;
            q.note = 48.0f + 2.0f * (float) (int) (t / 0.2);
        });
        check(player->activeVoices() == voices, std::to_string(voices) + " voice(s) ring after five strikes ("
                                                   + std::to_string(player->activeVoices()) + ")");
    }
    p.settings.decay = 4.0f;

    // No gate cable (gate high): each new pitch that holds still strikes, a glide only once it arrives.
    player->prepare(kRate);
    p.voices = 2;
    play(*player, p, 2.0, [](double t, twm::phys::PlayParams& q) {
        q.gate = true;
        const int step = (int) (t / 0.25);
        q.note = 48.0f + (float) ((step * 5) % 12);
        if (t > 1.0)
            q.note = 48.0f + (float) std::min(1.0, (t - 1.0) / 0.5) * 7.0f; // half a second's glide, then still
    });
    check(player->strikes() == 5, "V/Oct alone: the first note, three steps and the jump to a glide strike, the glide "
                                  "itself does not (" + std::to_string(player->strikes()) + ")");

    // A sequencer whose V/Oct settles 5 ms after its gate: one strike per note, not two.
    player->prepare(kRate);
    play(*player, p, 1.0, [](double t, twm::phys::PlayParams& q) {
        const double inStep = std::fmod(t, 0.25);
        q.gate = inStep < 0.1;
        if (inStep >= 0.005)
            q.note = 48.0f + 3.0f * (float) (int) (t / 0.25);
    });
    check(player->strikes() == 4, "V/Oct settling 5 ms after the gate: four strikes for four notes ("
                                      + std::to_string(player->strikes()) + ")");

    // The bow plays while the gate is high and falls silent after it: it follows the pitch without new strikes.
    player->prepare(kRate);
    p.settings.exciter = twm::phys::Exciter::bow;
    const auto bowed = play(*player, p, 3.0, [](double t, twm::phys::PlayParams& q) {
        q.gate = t < 1.5;
        q.note = t < 0.75 ? 48.0f : 50.0f;
    });
    const double playing = rms(bowed.out, (std::size_t) (0.5 * kRate), (std::size_t) (1.4 * kRate));
    const double after = rms(bowed.out, (std::size_t) (2.6 * kRate));
    char text[160];
    std::snprintf(text, sizeof text, "bow: one strike, sounds with the gate (%.1f dBFS) and dies after it (%.1f dB down)",
                  db(playing), db(playing) - db(after));
    check(player->strikes() == 1 && playing > 0.02 && db(playing) - db(after) > 40.0, text);

    // The body adds to `out` only.
    player->prepare(kRate);
    p.settings.exciter = twm::phys::Exciter::pluck;
    p.settings.body = twm::phys::Body::guitar;
    const auto bodied = play(*player, p, 1.0, [](double t, twm::phys::PlayParams& q) { q.gate = t < 0.1; });
    std::vector<float> diff(bodied.out.size());
    for (std::size_t i = 0; i < diff.size(); ++i)
        diff[i] = bodied.out[i] - bodied.dry[i];
    std::snprintf(text, sizeof text, "the guitar body is on KLANG only: %.1f dB of it against the dry string",
                  db(rms(diff) / rms(bodied.dry)));
    check(rms(diff) > 0.1 * rms(bodied.dry), text);
}

void testLevels()
{
    // Every pairing at the knobs' ends, four voices struck in a row on one chord: finite and in the DAC's range.
    double loudest = 0.0, quietest = 10.0;
    std::string loudestName, quietestName;
    for (int e = 0; e < 4; ++e)
        for (int r = 0; r < (int) twm::phys::Resonator::count; ++r)
            for (int corner = 0; corner < 4; ++corner)
            {
                auto player = std::make_unique<twm::phys::Player>();
                player->prepare(kRate);
                twm::phys::PlayParams p;
                p.voices = 4;
                p.velocity = 1.0f;
                auto& s = p.settings;
                s.exciter = (twm::phys::Exciter) e;
                s.resonator = (twm::phys::Resonator) r;
                s.hardness = s.pressure = (corner & 1) ? 1.0f : 0.0f;
                s.damping = (corner & 2) ? 0.0f : 1.0f;
                s.inharmonicity = (corner & 2) ? 1.0f : 0.0f;
                s.decay = 20.0f;
                s.position = (corner & 1) ? 0.5f : 0.02f;
                s.noise = (corner & 1) ? 1.0f : 0.0f;
                const auto run = play(*player, p, 2.0, [](double t, twm::phys::PlayParams& q) {
                    const int step = (int) (t / 0.25);
                    q.gate = std::fmod(t, 0.25) < 0.2 && step < 4;
                    static const float chord[] = { 36.0f, 43.0f, 48.0f, 52.0f };
                    q.note = chord[std::min(step, 3)];
                });
                bool finite = true;
                for (float v : run.out)
                    finite = finite && std::isfinite(v);
                const double pk = peakOf(run.out);
                const std::string name = std::string(exciterName(e)) + " auf " + resonatorName(r) + " Ecke "
                                         + std::to_string(corner);
                if (! finite)
                    check(false, name + ": not finite");
                if (pk > loudest)
                {
                    loudest = pk;
                    loudestName = name;
                }
                if (corner == 1 && pk < quietest)
                {
                    quietest = pk;
                    quietestName = name;
                }
            }
    char text[200];
    std::snprintf(text, sizeof text, "every pairing at the extremes, four voices: finite, peak %.2f at most (%s; the DAC's soft clip takes it)",
                  loudest, loudestName.c_str());
    check(loudest < 8.0, text);
    std::snprintf(text, sizeof text, "hard and full, the quietest pairing still peaks at %.3f (%s)", quietest,
                  quietestName.c_str());
    check(quietest > 0.02, text);

    // Every pairing at its defaults, one note at full strength: what a player meets first, at the jack.
    double highest = 0.0;
    std::string highestName;
    for (int e = 0; e < 4; ++e)
        for (int r = 0; r < (int) twm::phys::Resonator::count; ++r)
        {
            auto one = std::make_unique<twm::phys::Player>();
            one->prepare(kRate);
            twm::phys::PlayParams q;
            q.velocity = 1.0f;
            q.settings.exciter = (twm::phys::Exciter) e;
            q.settings.resonator = (twm::phys::Resonator) r;
            const auto run = play(*one, q, 1.5, [](double t, twm::phys::PlayParams& x) { x.gate = t < 1.0; });
            if (peakOf(run.out) > highest)
            {
                highest = peakOf(run.out);
                highestName = std::string(exciterName(e)) + " auf " + resonatorName(r);
            }
            std::printf("info  %-10s %-17s peak %5.2f  rms %6.1f dBFS\n", exciterName(e), resonatorName(r),
                        peakOf(run.out), db(rms(run.out, 0, (std::size_t) kRate)));
        }
    std::snprintf(text, sizeof text, "every pairing at its defaults peaks under full scale (at most %.2f: %s)", highest,
                  highestName.c_str());
    check(highest < 1.02, text);

    // The default pluck at C3: the level a player meets first.
    auto player = std::make_unique<twm::phys::Player>();
    player->prepare(kRate);
    twm::phys::PlayParams p;
    p.velocity = 1.0f;
    const auto run = play(*player, p, 1.0, [](double t, twm::phys::PlayParams& q) { q.gate = t < 0.1; });
    std::snprintf(text, sizeof text, "default pluck at full KRAFT peaks at %.2f (%.1f dBFS, %.1f V at the jack)",
                  peakOf(run.out), db(peakOf(run.out)), 5.0 * peakOf(run.out));
    check(peakOf(run.out) > 0.2 && peakOf(run.out) < 1.0, text);
}

// -------------------------------------------------------------------------------------------------- panel

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
    twm::PhysicalModule module;
    twm::PhysicalModule::Params out;
    auto in = centred();
    for (int i = 0; i < 10; ++i)
        module.update(in, 2, out);
    check(std::abs(out.note - 36.0f) < 1.0e-4f, "0 V, TUNE and FINE in the middle: C2");
    in.cv[twm::pitchCv] = 2.0f;
    module.update(in, 2, out);
    check(std::abs(out.note - 60.0f) < 1.0e-4f, "2 V: C4");

    // KRAFT is the pluck's strength and the bow's force; its jack adds a fifth per volt.
    in.pot[twm::warp] = 0.2f;
    in.cv[twm::warpCv] = 2.5f;
    module.update(in, 2, out);
    check(std::abs(out.velocity - 0.7f) < 1.0e-4f && std::abs(out.settings.pressure - 0.7f) < 1.0e-4f,
          "KRAFT 20 % plus 2.5 V: strength and force 70 %");
    in.pot[twm::position] = 1.0f;
    module.update(in, 2, out);
    check(std::abs(out.settings.position - 0.5f) < 1.0e-4f, "POSITION right: the middle of the string");

    // A tap on ERREGER changes the exciter, a tap on RESONATOR the resonator; both are saved.
    module.dirty = false;
    in.warpButton = true;
    module.update(in, 2, out);
    module.update(in, 2, out);
    in.warpButton = false;
    module.update(in, 2, out);
    check(out.settings.exciter == twm::phys::Exciter::strike && module.dirty, "tap on ERREGER: Schlagen, to be saved");
    for (int i = 0; i < 7; ++i)
    {
        in.filterButton = true;
        module.update(in, 2, out);
        in.filterButton = false;
        module.update(in, 2, out);
    }
    check(out.settings.resonator == twm::phys::Resonator::string, "seven taps on RESONATOR: round to the string");

    // Hold ERREGER and turn DECAY: the number of voices, no tap, and the normal DECAY stays where it was.
    const float decayBefore = out.settings.decay;
    in.warpButton = true;
    in.pot[twm::decay] = 0.2f;
    for (int i = 0; i < 200; ++i)
        module.update(in, 2, out);
    for (float v = 0.2f; v <= 1.0f; v += 0.01f)
    {
        in.pot[twm::decay] = v;
        module.update(in, 2, out);
    }
    in.pot[twm::decay] = 1.0f;
    module.update(in, 2, out);
    in.warpButton = false;
    module.update(in, 2, out);
    check(out.voices == 4 && out.settings.exciter == twm::phys::Exciter::strike
              && std::abs(out.settings.decay - decayBefore) < 1.0e-4f,
          "ERREGER held, DECAY right: four voices, the exciter and DECAY unchanged");

    // Back in the normal layer the pot must pass its value first.
    in.pot[twm::decay] = 0.9f;
    module.update(in, 2, out);
    check(std::abs(out.settings.decay - decayBefore) < 1.0e-4f, "DECAY picks up its value before it moves it");
    in.pot[twm::decay] = 0.5f;
    module.update(in, 2, out);
    in.pot[twm::decay] = 0.3f;
    module.update(in, 2, out);
    check(std::abs(out.settings.decay - twm::exponential(0.3f, 0.02f, 20.0f)) < 1.0e-4f, "and then follows it");

    // The resonator layer's defaults: no filter, no body, the damper up.
    twm::PhysicalModule fresh;
    auto again = centred();
    fresh.update(again, 2, out);
    check(! out.settings.filterOn && out.settings.body == twm::phys::Body::off && out.settings.release > 50.0f
              && out.voices == 2,
          "defaults: filter and body off, the damper up, two voices");
    const twm::phys::Settings d;
    check(std::abs(out.settings.attack - d.attack) < 1.0e-3f && std::abs(out.settings.envRelease - d.envRelease) < 1.0e-3f,
          "defaults: the bow's envelope as in the plugin");
}

// ------------------------------------------------------------------------------------------------ CPU

void testSpeed()
{
    // The heaviest case: four voices on the skin (16 resonances) or the stiff string, the pitch jittering so every
    // step works out new coefficients.
    double worst = 0.0;
    std::string worstName;
    for (int r : { 0, 5 })
    {
        auto player = std::make_unique<twm::phys::Player>();
        player->prepare(kRate);
        twm::phys::PlayParams p;
        p.voices = 4;
        p.settings.resonator = (twm::phys::Resonator) r;
        p.settings.inharmonicity = 0.7f;
        p.settings.decay = 20.0f;
        p.settings.body = twm::phys::Body::soundboard;
        const auto begin = std::chrono::steady_clock::now();
        play(*player, p, 10.0, [](double t, twm::phys::PlayParams& q) {
            q.gate = std::fmod(t, 0.1) < 0.05;
            q.note = 48.0f + 7.0f * (float) ((int) (t / 0.1) % 4) + 0.01f * (float) std::sin(t * 3000.0);
        });
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (seconds / 10.0 > worst)
        {
            worst = seconds / 10.0;
            worstName = resonatorName(r);
        }
    }
    std::printf("info  four voices, %s, pitch moving every step: %.2f %% of real time on this computer (%.0f ns per "
                "sample)\n", worstName.c_str(), 100.0 * worst, worst / kRate * 1.0e9);
}

// ---------------------------------------------------------------------------------------------- examples

void writeExamples(const std::string& dir)
{
    struct Example
    {
        const char* file;
        twm::phys::Exciter exciter;
        twm::phys::Resonator resonator;
        float hardness, position, decay, damping, inharmonicity;
        twm::phys::Body body;
    };
    const Example examples[] = {
        { "01-zupfen-saite", twm::phys::Exciter::pluck, twm::phys::Resonator::string, 0.5f, 0.2f, 4.0f, 0.4f, 0.0f, twm::phys::Body::guitar },
        { "02-zupfen-saite-steif", twm::phys::Exciter::pluck, twm::phys::Resonator::string, 0.8f, 0.12f, 6.0f, 0.3f, 0.6f, twm::phys::Body::soundboard },
        { "03-schlagen-stab", twm::phys::Exciter::strike, twm::phys::Resonator::bar, 0.4f, 0.25f, 1.5f, 0.6f, 0.0f, twm::phys::Body::box },
        { "04-schlagen-glocke", twm::phys::Exciter::strike, twm::phys::Resonator::bell, 0.7f, 0.2f, 8.0f, 0.4f, 0.1f, twm::phys::Body::off },
        { "05-schlagen-fell", twm::phys::Exciter::strike, twm::phys::Resonator::membrane, 0.5f, 0.3f, 0.6f, 0.5f, 0.0f, twm::phys::Body::off },
        { "06-streichen-saite", twm::phys::Exciter::bow, twm::phys::Resonator::string, 0.5f, 0.15f, 4.0f, 0.4f, 0.0f, twm::phys::Body::violin },
        { "07-blasen-rohr", twm::phys::Exciter::blow, twm::phys::Resonator::closedTube, 0.5f, 0.2f, 4.0f, 0.4f, 0.0f, twm::phys::Body::off },
        { "08-streichen-schale", twm::phys::Exciter::bow, twm::phys::Resonator::bowl, 0.4f, 0.2f, 10.0f, 0.3f, 0.0f, twm::phys::Body::off },
    };
    for (const auto& ex : examples)
    {
        auto player = std::make_unique<twm::phys::Player>();
        player->prepare(kRate);
        twm::phys::PlayParams p;
        p.voices = 3;
        auto& s = p.settings;
        s.exciter = ex.exciter;
        s.resonator = ex.resonator;
        s.hardness = ex.hardness;
        s.position = ex.position;
        s.decay = ex.decay;
        s.damping = ex.damping;
        s.inharmonicity = ex.inharmonicity;
        s.body = ex.body;
        const bool sustained = twm::phys::isSustained(ex.exciter);
        // A phrase in A minor at 100 BPM: eighth notes plucked, half notes bowed or blown.
        const auto run = play(*player, p, 8.0, [sustained](double t, twm::phys::PlayParams& q) {
            static const float phrase[] = { 45, 52, 57, 60, 59, 55, 52, 50, 48, 52, 55, 57, 64, 60, 57, 45 };
            const double step = sustained ? 1.2 : 0.3;
            const int i = std::min(15, (int) (t / step));
            q.note = phrase[sustained ? i % 6 * 2 : i] - 12.0f;
            q.gate = std::fmod(t, step) < step * (sustained ? 0.85 : 0.2) && t < 6.5;
            q.velocity = 0.6f + 0.4f * (float) ((i * 7) % 5) / 4.0f;
        });
        writeWav(dir + "/" + ex.file + ".wav", run.out);
    }
    std::printf("info  listening examples written to %s\n", dir.c_str());
}
} // namespace

int main(int argc, char** argv)
{
    testAgainstPlugin();
    testExtremesAgainstPlugin();
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
