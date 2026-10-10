#include "FactorySounds.h"

namespace tonwerk
{
namespace
{
/** Index of the eighth in `kDivisions`, beside the quarter, dotted eighth and sixteenth there. */
constexpr int kEighth = 10;

Envelope env(float attack, float decay, float sustain, float release) { return { attack, decay, sustain, release }; }

Operator op(float ratio, float level, Envelope envelope, float velocity = 0.7f, float detune = 0.0f)
{
    return { ratio, detune, level, velocity, envelope };
}

/** Collects sounds that start from `base` with nothing moving; each one sets what makes it. */
class Sounds
{
public:
    explicit Sounds(Patch start) : base(std::move(start)) {}

    template <typename Shape>
    void add(const char* name, const char* category, Shape shape)
    {
        Patch p = base;
        shape(p);
        list.push_back({ name, category, p });
    }

    std::vector<FactorySound> list;

private:
    Patch base;
};

std::vector<FactorySound> analogSounds()
{
    Patch start;
    start.engine = Engine::analog;
    start.analog = defaultAnalog(Kind::bass);
    start.analog.lfo.depth = 0.0f;
    Sounds s(start);

    s.add("Analog Acid", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.8f };
        a.osc2.level = 0.0f;
        a.filter = { FilterType::lowpass, 320.0f, 14.0f, 3.5f, 0.5f };
        a.filterEnv = env(0.002f, 0.18f, 0.0f, 0.1f);
        a.ampEnv = env(0.002f, 0.25f, 0.8f, 0.06f);
        a.volume = 0.41f;
        p.fx.delay = { 0.2f, 0.35f, 0.3f, 3000.0f, true, 10 };
    });
    s.add("Analog Wobble", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        a.osc2 = { Wave::square, -1, 0.0f, 0.6f };
        a.filter = { FilterType::lowpass, 500.0f, 8.0f, 0.5f, 0.3f };
        a.ampEnv = env(0.005f, 0.3f, 0.9f, 0.1f);
        a.lfo = { Wave::sine, 4.0f, AnalogLfoTarget::filter, 0.9f, true, 10 };
        a.fold.amount = 0.25f;
        a.volume = 0.38f;
    });
    s.add("Analog Falt-Lead", "Leads", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::triangle, 0, 0.0f, 0.8f };
        a.osc2 = { Wave::sine, 1, 0.0f, 0.3f };
        a.fold = { 0.6f, 0.2f, 0.4f };
        a.filter = { FilterType::lowpass, 5000.0f, 2.0f, 1.0f, 0.5f };
        a.filterEnv = env(0.005f, 0.6f, 0.3f, 0.3f);
        a.ampEnv = env(0.01f, 0.4f, 0.8f, 0.25f);
        a.lfo = { Wave::sine, 5.5f, AnalogLfoTarget::pitch, 0.1f };
        a.volume = 0.29f;
        p.fx.delay = { 0.25f, 0.35f, 0.35f, 4000.0f, true, kDottedEighth };
        p.fx.reverb = { 0.04f, 2.5f };
    });
    s.add("Analog Zufall", "Effekte", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::square, 0, 0.0f, 0.7f, 0.3f };
        a.osc2 = { Wave::sawtooth, 0, 7.0f, 0.4f };
        a.filter = { FilterType::lowpass, 1200.0f, 10.0f, 1.0f, 0.5f };
        a.filterEnv = env(0.005f, 0.2f, 0.2f, 0.2f);
        a.ampEnv = env(0.005f, 0.2f, 0.7f, 0.2f);
        a.sampleHold.sync = true;
        a.sampleHold.division = kSixteenth;
        a.sampleHold.filter = 0.6f;
        // The random steps differ with each voice's seed; on unlucky ones resonance and steps peaked near 1.4 at 0.55.
        a.volume = 0.45f;
        p.fx.reverb = { 0.05f, 2.0f };
    });
    s.add("Analog Streicher", "Bläser & Streicher", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::square, 0, 0.0f, 0.6f, 0.5f, 0.4f, true };
        a.osc2 = { Wave::sawtooth, 0, 6.0f, 0.5f };
        a.filter = { FilterType::lowpass, 2200.0f, 1.0f, 0.5f, 0.4f };
        a.filterEnv = env(0.6f, 1.5f, 0.7f, 1.2f);
        a.ampEnv = env(0.5f, 1.0f, 0.85f, 1.2f);
        a.lfo = { Wave::triangle, 0.8f, AnalogLfoTarget::pitch, 0.08f };
        a.volume = 0.27f;
        p.fx.reverb = { 0.07f, 3.5f };
    });

    // Since 0.4.0.
    s.add("Analog Sub", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        // A sine for the low end and a quiet triangle, so the note is still heard on small speakers.
        a.osc1 = { Wave::sine, 0, 0.0f, 0.9f };
        a.osc2 = { Wave::triangle, 0, 0.0f, 0.25f };
        a.filter = { FilterType::lowpass, 900.0f, 0.0f, 0.0f, 0.0f };
        a.ampEnv = env(0.004f, 0.3f, 1.0f, 0.12f);
        a.volume = 0.37f;
    });
    s.add("Analog Druckbass", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::square, -1, 0.0f, 0.6f };
        a.filter = { FilterType::lowpass, 220.0f, 6.0f, 3.0f, 0.5f };
        a.filterEnv = env(0.002f, 0.22f, 0.25f, 0.1f);
        a.ampEnv = env(0.002f, 0.4f, 0.9f, 0.08f);
        a.volume = 0.42f;
    });
    s.add("Analog Pulsbass", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        // The filter envelope also narrows the pulse, so each note starts hollow and turns reedy.
        a.osc1 = { Wave::square, 0, 0.0f, 0.9f, 0.3f, 0.0f, true, 0.0f, -0.5f };
        a.osc2 = { Wave::sine, -1, 0.0f, 0.5f };
        a.filter = { FilterType::lowpass, 400.0f, 4.0f, 2.5f, 0.4f };
        a.filterEnv = env(0.002f, 0.3f, 0.2f, 0.15f);
        a.ampEnv = env(0.002f, 0.5f, 0.85f, 0.1f);
        a.volume = 0.6f;
    });
    s.add("Analog Reese", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        // Two saws 28 cents apart beat against each other; a slow LFO lets the filter breathe.
        a.osc1 = { Wave::sawtooth, 0, -14.0f, 0.6f };
        a.osc2 = { Wave::sawtooth, 0, 14.0f, 0.6f };
        a.fold.amount = 0.12f;
        a.filter = { FilterType::lowpass, 650.0f, 3.0f, 0.5f, 0.3f };
        a.filterEnv = env(0.01f, 0.5f, 0.6f, 0.2f);
        a.ampEnv = env(0.005f, 0.3f, 1.0f, 0.15f);
        a.lfo = { Wave::triangle, 0.25f, AnalogLfoTarget::filter, 0.15f };
        a.volume = 0.42f;
    });
    s.add("Analog Gummibass", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        // A resonant lowpass swept down fast from high up: the "bow" of a rubber band.
        a.osc1 = { Wave::square, 0, 0.0f, 0.5f };
        a.osc2 = { Wave::sine, -1, 0.0f, 0.6f };
        a.filter = { FilterType::lowpass, 160.0f, 12.0f, 3.5f, 0.5f };
        a.filterEnv = env(0.002f, 0.14f, 0.0f, 0.1f);
        a.ampEnv = env(0.002f, 0.4f, 0.6f, 0.1f);
        a.volume = 0.29f;
    });
    s.add("Analog Synthwave-Bass", "Bässe", [](Patch& p) {
        auto& a = p.analog;
        // Short and plucked, for the eighths and sixteenths of an eighties bassline.
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::sawtooth, 0, 7.0f, 0.5f };
        a.filter = { FilterType::lowpass, 300.0f, 3.0f, 2.5f, 0.5f };
        a.filterEnv = env(0.002f, 0.15f, 0.1f, 0.1f);
        a.ampEnv = env(0.002f, 0.25f, 0.35f, 0.1f);
        a.volume = 0.64f;
    });
    s.add("Analog Sägen-Lead", "Leads", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, -6.0f, 0.6f };
        a.osc2 = { Wave::sawtooth, 0, 6.0f, 0.6f };
        a.filter = { FilterType::lowpass, 2500.0f, 3.0f, 1.0f, 0.6f };
        a.filterEnv = env(0.01f, 0.4f, 0.6f, 0.3f);
        a.ampEnv = env(0.005f, 0.2f, 0.9f, 0.25f);
        a.lfo = { Wave::sine, 5.5f, AnalogLfoTarget::pitch, 0.12f };
        a.volume = 0.38f;
        p.fx.delay = { 0.18f, 0.35f, 0.3f, 3500.0f, true, kDottedEighth };
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("Analog Chiptune", "Leads", [](Patch& p) {
        auto& a = p.analog;
        // Plain squares an octave apart and no filter: the sound of an old console.
        a.osc1 = { Wave::square, 0, 0.0f, 0.5f };
        a.osc2 = { Wave::square, 1, 0.0f, 0.2f };
        a.filter = { FilterType::lowpass, 12000.0f, 0.0f, 0.0f, 0.0f };
        a.ampEnv = env(0.002f, 0.1f, 0.8f, 0.05f);
        a.lfo = { Wave::triangle, 6.0f, AnalogLfoTarget::pitch, 0.15f };
        a.volume = 0.47f;
        p.fx.delay = { 0.15f, 0.25f, 0.2f, 5000.0f, true, kEighth };
    });
    s.add("Analog Pfeife", "Leads", [](Patch& p) {
        auto& a = p.analog;
        // A sine with a little breath, sung with vibrato.
        a.osc1 = { Wave::sine, 0, 0.0f, 0.8f };
        a.osc2 = { Wave::triangle, 1, 0.0f, 0.12f };
        a.noise = 0.04f;
        a.filter = { FilterType::lowpass, 3500.0f, 1.0f, 0.5f, 0.5f };
        a.filterEnv = env(0.04f, 0.3f, 0.6f, 0.2f);
        a.ampEnv = env(0.04f, 0.3f, 0.9f, 0.2f);
        a.lfo = { Wave::sine, 5.2f, AnalogLfoTarget::pitch, 0.18f };
        a.volume = 0.41f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("Analog Schrei-Lead", "Leads", [](Patch& p) {
        auto& a = p.analog;
        // A resonant lowpass that tracks the keys, opened by a slow filter envelope, folded a little: it screams.
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::square, 1, 0.0f, 0.3f };
        a.fold.amount = 0.2f;
        a.filter = { FilterType::lowpass, 900.0f, 15.0f, 2.5f, 0.8f };
        a.filterEnv = env(0.05f, 0.8f, 0.5f, 0.3f);
        a.ampEnv = env(0.005f, 0.3f, 0.9f, 0.2f);
        a.lfo = { Wave::sine, 5.5f, AnalogLfoTarget::pitch, 0.1f };
        a.volume = 0.21f;
        p.fx.delay = { 0.2f, 0.35f, 0.35f, 3000.0f, true, kDottedEighth };
    });
    s.add("Analog Warme Fläche", "Flächen", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, -9.0f, 0.5f };
        a.osc2 = { Wave::sawtooth, 0, 9.0f, 0.5f };
        a.filter = { FilterType::lowpass, 700.0f, 1.0f, 1.2f, 0.4f };
        a.filterEnv = env(0.8f, 2.0f, 0.4f, 1.5f);
        a.ampEnv = env(0.6f, 1.0f, 0.9f, 1.5f);
        a.lfo = { Wave::triangle, 0.3f, AnalogLfoTarget::filter, 0.12f };
        a.volume = 0.23f;
        p.fx.reverb = { 0.07f, 3.5f };
    });
    s.add("Analog PWM-Fläche", "Flächen", [](Patch& p) {
        auto& a = p.analog;
        // Two pulses, the LFO sweeping their widths; it moves nothing else.
        a.osc1 = { Wave::square, 0, 0.0f, 0.6f, 0.5f, 0.7f, true };
        a.osc2 = { Wave::square, -1, 5.0f, 0.5f, 0.5f, 0.5f, true };
        a.filter = { FilterType::lowpass, 1500.0f, 2.0f, 0.5f, 0.4f };
        a.filterEnv = env(0.5f, 2.0f, 0.6f, 1.5f);
        a.ampEnv = env(0.4f, 1.0f, 0.85f, 1.8f);
        a.lfo = { Wave::triangle, 0.5f, AnalogLfoTarget::pitch, 0.0f };
        a.volume = 0.3f;
        p.fx.reverb = { 0.07f, 4.0f };
    });
    s.add("Analog Nebel", "Flächen", [](Patch& p) {
        auto& a = p.analog;
        // Noise and a triangle through a resonant lowpass, which the LFO moves slowly: a fog more than a chord.
        a.osc1 = { Wave::triangle, 0, 0.0f, 0.5f };
        a.osc2.level = 0.0f;
        a.noise = 0.4f;
        a.filter = { FilterType::lowpass, 800.0f, 9.0f, 1.0f, 0.8f };
        a.filterEnv = env(1.5f, 3.0f, 0.3f, 2.0f);
        a.ampEnv = env(0.6f, 2.0f, 0.8f, 3.0f);
        a.lfo = { Wave::sine, 0.15f, AnalogLfoTarget::filter, 0.4f };
        a.volume = 0.22f;
        p.fx.reverb = { 0.08f, 6.0f };
    });
    s.add("Analog Glasfläche", "Flächen", [](Patch& p) {
        auto& a = p.analog;
        // A triangle and a sine two octaves up through the wavefolder, which the filter envelope opens slowly.
        a.osc1 = { Wave::triangle, 0, 0.0f, 0.6f };
        a.osc2 = { Wave::sine, 2, 0.0f, 0.25f };
        a.fold = { 0.15f, 0.0f, 0.35f };
        a.filter = { FilterType::lowpass, 4000.0f, 1.0f, 0.5f, 0.3f };
        a.filterEnv = env(1.0f, 2.5f, 0.5f, 2.0f);
        a.ampEnv = env(0.3f, 1.0f, 0.9f, 2.0f);
        a.lfo = { Wave::sine, 4.5f, AnalogLfoTarget::pitch, 0.05f };
        a.volume = 0.12f;
        p.fx.reverb = { 0.08f, 4.5f };
    });
    s.add("Analog Sweep-Fläche", "Flächen", [](Patch& p) {
        auto& a = p.analog;
        // The filter envelope takes three seconds to open a resonant lowpass four octaves.
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.6f };
        a.osc2 = { Wave::sawtooth, -1, 5.0f, 0.4f };
        a.filter = { FilterType::lowpass, 250.0f, 8.0f, 4.0f, 0.3f };
        a.filterEnv = env(3.0f, 3.0f, 0.4f, 2.0f);
        a.ampEnv = env(0.8f, 1.0f, 0.9f, 2.5f);
        a.volume = 0.2f;
        p.fx.reverb = { 0.07f, 4.0f };
    });
    s.add("Analog Combo-Orgel", "Tasten", [](Patch& p) {
        auto& a = p.analog;
        // Squares an octave apart, the hollow buzz of a sixties combo organ, with its quick vibrato.
        a.osc1 = { Wave::square, 0, 0.0f, 0.4f };
        a.osc2 = { Wave::square, 1, 0.0f, 0.3f };
        a.filter = { FilterType::lowpass, 3000.0f, 0.0f, 0.0f, 0.3f };
        a.ampEnv = env(0.005f, 0.1f, 1.0f, 0.06f);
        a.lfo = { Wave::sine, 6.5f, AnalogLfoTarget::pitch, 0.08f };
        a.volume = 0.22f;
        p.fx.reverb = { 0.04f, 1.5f };
    });
    s.add("Analog Clavi", "Tasten", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::square, 0, 0.0f, 0.9f, 0.35f };
        a.osc2 = { Wave::sawtooth, 1, 0.0f, 0.15f };
        a.filter = { FilterType::lowpass, 1200.0f, 3.0f, 2.5f, 0.6f };
        a.filterEnv = env(0.001f, 0.15f, 0.1f, 0.08f);
        a.ampEnv = env(0.001f, 0.5f, 0.2f, 0.08f);
        a.volume = 0.49f;
    });
    s.add("Analog Polysynth", "Tasten", [](Patch& p) {
        auto& a = p.analog;
        // The eighties' polyphonic synth for chords: two saws, a filter that closes after the attack.
        a.osc1 = { Wave::sawtooth, 0, -5.0f, 0.5f };
        a.osc2 = { Wave::sawtooth, 0, 5.0f, 0.5f };
        a.filter = { FilterType::lowpass, 1200.0f, 2.0f, 2.0f, 0.5f };
        a.filterEnv = env(0.005f, 0.5f, 0.3f, 0.4f);
        a.ampEnv = env(0.005f, 0.6f, 0.7f, 0.4f);
        a.volume = 0.3f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("Analog Pluck", "Plucks", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.6f };
        a.osc2 = { Wave::square, 0, 5.0f, 0.4f };
        a.filter = { FilterType::lowpass, 500.0f, 5.0f, 3.5f, 0.5f };
        a.filterEnv = env(0.001f, 0.18f, 0.0f, 0.15f);
        a.ampEnv = env(0.001f, 0.45f, 0.0f, 0.3f);
        a.volume = 0.39f;
        p.fx.delay = { 0.22f, 0.35f, 0.35f, 3500.0f, true, kDottedEighth };
        p.fx.reverb = { 0.05f, 2.0f };
    });
    s.add("Analog Berlin-Sequenz", "Plucks", [](Patch& p) {
        auto& a = p.analog;
        // A resonant blip for long sequences, with the echo on the eighths of the song.
        a.osc1 = { Wave::sawtooth, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::square, -1, 0.0f, 0.3f };
        a.filter = { FilterType::lowpass, 400.0f, 12.0f, 2.5f, 0.5f };
        a.filterEnv = env(0.001f, 0.12f, 0.05f, 0.1f);
        a.ampEnv = env(0.001f, 0.25f, 0.0f, 0.1f);
        a.volume = 0.55f;
        p.fx.delay = { 0.25f, 0.25f, 0.4f, 2500.0f, true, kEighth };
    });
    s.add("Analog Kristall", "Glocken", [](Patch& p) {
        auto& a = p.analog;
        // A sine and its double octave, folded hard at the strike and less as the envelope falls: bright, then pure.
        a.osc1 = { Wave::sine, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::sine, 2, 0.0f, 0.3f };
        a.fold = { 0.05f, 0.1f, 0.6f };
        a.filter = { FilterType::lowpass, 6000.0f, 0.0f, 0.0f, 0.3f };
        a.filterEnv = env(0.001f, 0.6f, 0.0f, 0.4f);
        a.ampEnv = env(0.001f, 1.6f, 0.0f, 1.2f);
        a.volume = 0.27f;
        p.fx.delay = { 0.15f, 0.375f, 0.3f, 4000.0f, true, kDottedEighth };
        p.fx.reverb = { 0.06f, 3.0f };
    });
    s.add("Analog Blechsatz", "Bläser & Streicher", [](Patch& p) {
        auto& a = p.analog;
        // The filter opens a little after the note, as a brass section swells into it.
        a.osc1 = { Wave::sawtooth, 0, -4.0f, 0.6f };
        a.osc2 = { Wave::sawtooth, 0, 4.0f, 0.6f };
        a.filter = { FilterType::lowpass, 600.0f, 2.0f, 2.5f, 0.5f };
        a.filterEnv = env(0.08f, 0.6f, 0.5f, 0.3f);
        a.ampEnv = env(0.05f, 0.3f, 0.9f, 0.3f);
        a.lfo = { Wave::sine, 5.0f, AnalogLfoTarget::pitch, 0.05f };
        a.volume = 0.24f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("Analog Flöte", "Bläser & Streicher", [](Patch& p) {
        auto& a = p.analog;
        a.osc1 = { Wave::triangle, 0, 0.0f, 0.7f };
        a.osc2 = { Wave::sine, 1, 0.0f, 0.1f };
        a.noise = 0.08f;
        a.filter = { FilterType::lowpass, 2500.0f, 2.0f, 0.8f, 0.6f };
        a.filterEnv = env(0.06f, 0.3f, 0.7f, 0.2f);
        a.ampEnv = env(0.06f, 0.3f, 0.9f, 0.2f);
        a.lfo = { Wave::sine, 5.0f, AnalogLfoTarget::pitch, 0.12f };
        a.volume = 0.35f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("Analog Wind", "Effekte", [](Patch& p) {
        auto& a = p.analog;
        // Noise through a resonant lowpass that follows the keys and drifts: higher notes, higher wind.
        a.osc1.level = 0.0f;
        a.osc2.level = 0.0f;
        a.noise = 1.0f;
        a.filter = { FilterType::lowpass, 700.0f, 12.0f, 0.0f, 1.0f };
        a.ampEnv = env(1.0f, 1.0f, 1.0f, 2.5f);
        a.lfo = { Wave::sine, 0.2f, AnalogLfoTarget::filter, 0.5f };
        a.volume = 0.46f;
        p.fx.reverb = { 0.08f, 5.0f };
    });
    s.add("Analog Computer", "Effekte", [](Patch& p) {
        auto& a = p.analog;
        // Sample and hold on the pitch in sixteenths: a computer thinking out loud.
        a.osc1 = { Wave::square, 0, 0.0f, 0.5f };
        a.osc2 = { Wave::sine, 1, 0.0f, 0.3f };
        a.filter = { FilterType::lowpass, 4000.0f, 2.0f, 0.0f, 0.5f };
        a.ampEnv = env(0.002f, 0.2f, 1.0f, 0.1f);
        a.sampleHold.sync = true;
        a.sampleHold.division = kSixteenth;
        a.sampleHold.pitch = 1.0f;
        a.volume = 0.4f;
        p.fx.delay = { 0.2f, 0.25f, 0.3f, 4000.0f, true, kEighth };
    });
    return s.list;
}

std::vector<FactorySound> fmSounds()
{
    Patch start;
    start.engine = Engine::fm;
    start.fm.lfo.depth = 0.0f;
    Sounds s(start);

    s.add("FM Glocke", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        p.fm.ops = { op(1.0f, 0.7f, env(0.002f, 4.0f, 0.0f, 3.0f)), op(3.5f, 0.45f, env(0.002f, 2.0f, 0.0f, 2.0f)),
                     op(2.0f, 0.35f, env(0.002f, 3.0f, 0.0f, 2.5f)), op(7.0f, 0.25f, env(0.002f, 1.0f, 0.0f, 1.0f)) };
        p.fm.volume = 0.32f;
        p.fx.reverb = { 0.06f, 3.0f };
    });
    s.add("FM Marimba", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        p.fm.ops = { op(1.0f, 0.8f, env(0.001f, 0.6f, 0.0f, 0.4f)), op(4.0f, 0.35f, env(0.001f, 0.08f, 0.0f, 0.08f)),
                     op(3.0f, 0.25f, env(0.001f, 0.35f, 0.0f, 0.3f)),
                     op(10.0f, 0.2f, env(0.001f, 0.05f, 0.0f, 0.05f)) };
        p.fm.volume = 0.81f;
        p.fx.reverb = { 0.04f, 1.5f };
    });
    s.add("FM Orgel", "Tasten", [](Patch& p) {
        p.fm.algorithm = 8;
        p.fm.feedback = 0.2f;
        const auto held = env(0.005f, 0.1f, 1.0f, 0.08f);
        p.fm.ops = { op(0.5f, 0.5f, held, 0.2f), op(1.0f, 0.6f, held, 0.2f), op(2.0f, 0.35f, held, 0.2f),
                     op(3.0f, 0.25f, held, 0.2f) };
        p.fm.lfo = { Wave::sine, 6.0f, FmLfoTarget::pitch, 0.06f };
        p.fm.volume = 0.3f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Zupf", "Plucks", [](Patch& p) {
        p.fm.algorithm = 4;
        p.fm.feedback = 0.3f;
        p.fm.ops = { op(1.0f, 0.85f, env(0.001f, 0.9f, 0.0f, 0.3f)), op(2.0f, 0.4f, env(0.001f, 0.3f, 0.0f, 0.2f)),
                     op(1.0f, 0.3f, env(0.001f, 0.2f, 0.0f, 0.1f)), op(5.0f, 0.2f, env(0.001f, 0.1f, 0.0f, 0.1f)) };
        p.fm.volume = 0.4f;
        p.fx.delay = { 0.2f, 0.35f, 0.35f, 4000.0f, true, kDottedEighth };
    });

    // Since 0.4.0. A modulator's level L bends its carrier by an index of 8 L squared: 0.35 is about 1, 0.5 is 2.
    s.add("FM Slapbass", "Bässe", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.3f;
        // The thumb: a bright snap that dies in a tenth of a second over a body with a sub octave.
        p.fm.ops = { op(1.0f, 0.85f, env(0.001f, 0.8f, 0.5f, 0.08f), 0.5f),
                     op(1.0f, 0.55f, env(0.001f, 0.12f, 0.15f, 0.08f), 0.9f),
                     op(0.5f, 0.45f, env(0.001f, 1.0f, 0.6f, 0.08f), 0.5f),
                     op(3.0f, 0.3f, env(0.001f, 0.06f, 0.0f, 0.05f), 0.9f) };
        p.fm.volume = 0.7f;
    });
    s.add("FM Holzbass", "Bässe", [](Patch& p) {
        p.fm.algorithm = 1;
        p.fm.feedback = 0.2f;
        // A round body plucked with a short, woody attack.
        p.fm.ops = { op(1.0f, 0.9f, env(0.004f, 1.5f, 0.3f, 0.15f), 0.4f),
                     op(1.0f, 0.35f, env(0.002f, 0.25f, 0.1f, 0.1f), 0.8f),
                     op(2.0f, 0.25f, env(0.001f, 0.1f, 0.0f, 0.1f)), op(1.0f, 0.2f, env(0.001f, 0.05f, 0.0f, 0.05f)) };
        p.fm.volume = 0.47f;
    });
    s.add("FM Wobble-Bass", "Bässe", [](Patch& p) {
        p.fm.algorithm = 3;
        p.fm.feedback = 0.7f;
        // The LFO opens and closes the modulation on the song's eighths: from a sine to a growl and back.
        const auto held = env(0.003f, 0.3f, 1.0f, 0.1f);
        p.fm.ops = { op(1.0f, 0.9f, held, 0.3f), op(1.0f, 0.5f, held, 0.3f), op(0.5f, 0.3f, held, 0.3f),
                     op(1.0f, 0.55f, held, 0.3f) };
        p.fm.lfo = { Wave::sine, 4.0f, FmLfoTarget::index, 1.0f, true, kEighth };
        p.fm.volume = 0.39f;
    });
    s.add("FM Synthbass", "Bässe", [](Patch& p) {
        p.fm.algorithm = 4;
        p.fm.feedback = 0.85f;
        p.fm.ops = { op(1.0f, 0.9f, env(0.001f, 0.6f, 0.6f, 0.08f), 0.4f),
                     op(1.0f, 0.6f, env(0.001f, 0.25f, 0.25f, 0.08f)), op(1.0f, 0.4f, env(0.001f, 0.4f, 0.3f, 0.1f)),
                     op(1.0f, 0.45f, env(0.001f, 0.3f, 0.2f, 0.1f)) };
        p.fm.volume = 0.49f;
    });
    s.add("FM Zungen-Piano", "Tasten", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // A reed piano: it barks when struck hard, and its tremolo wobbles the chords.
        p.fm.ops = { op(1.0f, 0.8f, env(0.002f, 2.5f, 0.0f, 0.4f), 0.6f),
                     op(1.0f, 0.5f, env(0.002f, 0.8f, 0.15f, 0.3f), 0.95f),
                     op(1.0f, 0.4f, env(0.002f, 1.8f, 0.0f, 0.4f), 0.6f, 3.0f),
                     op(3.0f, 0.3f, env(0.002f, 0.3f, 0.05f, 0.2f), 0.9f) };
        p.fm.lfo = { Wave::sine, 5.0f, FmLfoTarget::amp, 0.25f };
        p.fm.volume = 0.28f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Cembalo", "Tasten", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.2f;
        // Bright and plucked, hardly touched by the velocity, as a harpsichord's quill is.
        p.fm.ops = { op(1.0f, 0.7f, env(0.001f, 2.0f, 0.0f, 0.25f), 0.2f),
                     op(3.0f, 0.55f, env(0.001f, 1.5f, 0.0f, 0.25f), 0.3f),
                     op(2.0f, 0.5f, env(0.001f, 1.6f, 0.0f, 0.25f), 0.2f),
                     op(7.0f, 0.4f, env(0.001f, 1.0f, 0.0f, 0.2f), 0.3f) };
        p.fm.volume = 0.29f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Clavi", "Tasten", [](Patch& p) {
        p.fm.algorithm = 2;
        p.fm.feedback = 0.6f;
        p.fm.ops = { op(1.0f, 0.85f, env(0.001f, 0.8f, 0.3f, 0.06f), 0.5f),
                     op(1.0f, 0.5f, env(0.001f, 0.3f, 0.2f, 0.06f), 0.8f),
                     op(3.0f, 0.3f, env(0.001f, 0.2f, 0.1f, 0.05f)), op(1.0f, 0.3f, env(0.001f, 0.15f, 0.1f, 0.05f)) };
        p.fm.volume = 0.31f;
    });
    s.add("FM Spieluhr", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // Two struck tines, the second two octaves up.
        p.fm.ops = { op(1.0f, 0.7f, env(0.001f, 1.8f, 0.0f, 1.2f), 0.5f),
                     op(5.0f, 0.35f, env(0.001f, 0.4f, 0.0f, 0.3f)),
                     op(4.0f, 0.25f, env(0.001f, 0.9f, 0.0f, 0.6f), 0.5f),
                     op(7.0f, 0.25f, env(0.001f, 0.2f, 0.0f, 0.2f)) };
        p.fm.volume = 0.49f;
        p.fx.delay = { 0.12f, 0.375f, 0.3f, 5000.0f, true, kDottedEighth };
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("FM Vibrafon", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // The bar's fundamental and its fourth harmonic, the mallet's knock, and the motor's tremolo.
        p.fm.ops = { op(1.0f, 0.8f, env(0.001f, 3.0f, 0.0f, 1.0f), 0.5f),
                     op(4.0f, 0.3f, env(0.001f, 0.15f, 0.0f, 0.15f)),
                     op(4.0f, 0.2f, env(0.001f, 0.8f, 0.0f, 0.5f), 0.5f),
                     op(1.0f, 0.1f, env(0.001f, 0.1f, 0.0f, 0.1f)) };
        p.fm.lfo = { Wave::sine, 5.0f, FmLfoTarget::amp, 0.35f };
        p.fm.volume = 0.49f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("FM Kalimba", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // The thumb's click, then the tine and its high, quickly fading partial.
        p.fm.ops = { op(1.0f, 0.8f, env(0.001f, 1.0f, 0.0f, 0.5f), 0.5f),
                     op(2.0f, 0.3f, env(0.001f, 0.08f, 0.0f, 0.08f)),
                     op(6.0f, 0.15f, env(0.001f, 0.25f, 0.0f, 0.2f), 0.5f),
                     op(1.5f, 0.2f, env(0.001f, 0.1f, 0.0f, 0.1f)) };
        p.fm.volume = 0.69f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Steeldrum", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // A modulator at one and a half times the note puts partials between the harmonics: the pan's ring.
        p.fm.ops = { op(1.0f, 0.75f, env(0.001f, 1.2f, 0.0f, 0.5f), 0.5f),
                     op(1.5f, 0.4f, env(0.001f, 0.3f, 0.0f, 0.3f)),
                     op(2.0f, 0.35f, env(0.001f, 0.8f, 0.0f, 0.4f), 0.5f),
                     op(3.0f, 0.2f, env(0.001f, 0.2f, 0.0f, 0.2f)) };
        p.fm.volume = 0.6f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Gong", "Glocken", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.3f;
        // Detuned modulators beat against the low carriers, and the second pair swells after the strike.
        p.fm.ops = { op(0.5f, 0.8f, env(0.005f, 6.0f, 0.0f, 4.0f), 0.5f),
                     op(3.5f, 0.5f, env(0.01f, 4.0f, 0.0f, 3.0f), 0.7f, 30.0f),
                     op(1.0f, 0.5f, env(0.01f, 5.0f, 0.0f, 3.5f), 0.5f, -20.0f),
                     op(2.5f, 0.45f, env(0.3f, 3.0f, 0.0f, 3.0f), 0.7f, 45.0f) };
        p.fm.volume = 0.25f;
        p.fx.reverb = { 0.06f, 5.0f };
    });
    s.add("FM Glasfläche", "Flächen", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // Two slow pairs a few cents apart; the LFO lets the modulation breathe.
        p.fm.ops = { op(1.0f, 0.7f, env(0.8f, 2.0f, 0.85f, 2.0f), 0.3f),
                     op(3.0f, 0.25f, env(1.5f, 3.0f, 0.6f, 2.0f), 0.3f),
                     op(1.0f, 0.6f, env(0.9f, 2.0f, 0.85f, 2.0f), 0.3f, 7.0f),
                     op(7.0f, 0.15f, env(2.0f, 3.0f, 0.5f, 2.0f), 0.3f) };
        p.fm.lfo = { Wave::triangle, 0.3f, FmLfoTarget::index, 0.4f };
        p.fm.volume = 0.2f;
        p.fx.reverb = { 0.07f, 4.0f };
    });
    s.add("FM Atemfläche", "Flächen", [](Patch& p) {
        p.fm.algorithm = 8;
        p.fm.feedback = 1.0f;
        // Four carriers; operator 4, high and at full feedback, is the air on top.
        const auto slow = env(0.8f, 1.5f, 0.9f, 2.0f);
        p.fm.ops = { op(1.0f, 0.6f, slow, 0.3f, -6.0f), op(1.0f, 0.6f, slow, 0.3f, 6.0f),
                     op(2.0f, 0.2f, env(1.0f, 1.5f, 0.9f, 2.0f), 0.3f),
                     op(4.0f, 0.08f, env(1.2f, 2.0f, 0.8f, 2.0f), 0.3f) };
        p.fm.lfo = { Wave::sine, 4.5f, FmLfoTarget::pitch, 0.04f };
        p.fm.volume = 0.28f;
        p.fx.reverb = { 0.07f, 4.0f };
    });
    s.add("FM Dunkle Fläche", "Flächen", [](Patch& p) {
        p.fm.algorithm = 6;
        p.fm.feedback = 0.3f;
        // One slow modulator over three carriers, the lowest an octave down.
        const auto slow = env(1.0f, 2.0f, 0.9f, 2.5f);
        p.fm.ops = { op(0.5f, 0.6f, slow, 0.3f), op(1.0f, 0.5f, slow, 0.3f, 5.0f), op(1.0f, 0.5f, slow, 0.3f, -5.0f),
                     op(1.0f, 0.3f, env(2.0f, 2.0f, 0.8f, 2.5f), 0.3f) };
        p.fm.lfo = { Wave::sine, 0.2f, FmLfoTarget::index, 0.6f };
        p.fm.volume = 0.21f;
        p.fx.reverb = { 0.07f, 4.0f };
    });
    s.add("FM Sägezahn-Lead", "Leads", [](Patch& p) {
        p.fm.algorithm = 8;
        p.fm.feedback = 0.8f;
        // Operator 4 fed back on itself is close to a sawtooth; the others add body under it.
        const auto held = env(0.005f, 0.3f, 0.9f, 0.2f);
        p.fm.ops = { op(1.0f, 0.4f, held, 0.4f), op(0.5f, 0.25f, held, 0.4f), op(2.0f, 0.15f, held, 0.4f),
                     op(1.0f, 0.7f, held, 0.4f) };
        p.fm.lfo = { Wave::sine, 5.5f, FmLfoTarget::pitch, 0.12f };
        p.fm.volume = 0.51f;
        p.fx.delay = { 0.18f, 0.375f, 0.3f, 3500.0f, true, kDottedEighth };
    });
    s.add("FM Sync-Lead", "Leads", [](Patch& p) {
        p.fm.algorithm = 1;
        p.fm.feedback = 0.5f;
        // A chain whose modulation falls after the attack, sweeping like a synced oscillator.
        p.fm.ops = { op(1.0f, 0.85f, env(0.003f, 0.4f, 0.9f, 0.2f), 0.4f),
                     op(2.0f, 0.6f, env(0.002f, 0.8f, 0.4f, 0.3f), 0.6f), op(3.0f, 0.3f, env(0.002f, 0.5f, 0.3f, 0.3f)),
                     op(1.0f, 0.25f, env(0.002f, 0.5f, 0.3f, 0.3f)) };
        p.fm.lfo = { Wave::sine, 5.5f, FmLfoTarget::pitch, 0.1f };
        p.fm.volume = 0.27f;
        p.fx.delay = { 0.18f, 0.375f, 0.3f, 3500.0f, true, kDottedEighth };
        p.fx.reverb = { 0.03f, 2.0f };
    });
    s.add("FM Panflöte", "Bläser & Streicher", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 1.0f;
        // A soft tone and, from operator 4 at full feedback, a breath that is loudest as the note starts.
        p.fm.ops = { op(1.0f, 0.8f, env(0.08f, 0.3f, 0.9f, 0.2f), 0.4f), op(1.0f, 0.15f, env(0.05f, 0.3f, 0.8f, 0.2f)),
                     op(4.0f, 0.08f, env(0.02f, 0.15f, 0.3f, 0.1f), 0.4f),
                     op(7.0f, 0.6f, env(0.02f, 0.3f, 0.6f, 0.2f)) };
        p.fm.lfo = { Wave::sine, 5.0f, FmLfoTarget::pitch, 0.1f };
        p.fm.volume = 0.31f;
        p.fx.reverb = { 0.05f, 2.5f };
    });
    s.add("FM Klarinette", "Bläser & Streicher", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.0f;
        // A modulator at twice the note puts only odd harmonics on the carrier: the clarinet's hollow tone.
        p.fm.ops = { op(1.0f, 0.85f, env(0.04f, 0.3f, 0.9f, 0.15f), 0.4f),
                     op(2.0f, 0.42f, env(0.06f, 0.4f, 0.8f, 0.15f), 0.6f), op(1.0f, 0.0f, env(0.01f, 0.3f, 0.8f, 0.2f)),
                     op(1.0f, 0.0f, env(0.01f, 0.3f, 0.8f, 0.2f)) };
        p.fm.lfo = { Wave::sine, 5.0f, FmLfoTarget::pitch, 0.06f };
        p.fm.volume = 0.2f;
        p.fx.reverb = { 0.04f, 2.0f };
    });
    s.add("FM Streicher", "Bläser & Streicher", [](Patch& p) {
        p.fm.algorithm = 6;
        p.fm.feedback = 0.0f;
        // One modulator over three carriers spread by detune: a section, not a soloist.
        const auto bow = env(0.35f, 1.0f, 0.9f, 0.8f);
        p.fm.ops = { op(1.0f, 0.6f, bow, 0.4f, -7.0f), op(1.0f, 0.6f, bow, 0.4f, 7.0f), op(2.0f, 0.3f, bow, 0.4f),
                     op(1.0f, 0.35f, env(0.5f, 1.0f, 0.8f, 0.8f), 0.4f) };
        p.fm.lfo = { Wave::sine, 5.5f, FmLfoTarget::pitch, 0.07f };
        p.fm.volume = 0.28f;
        p.fx.reverb = { 0.06f, 3.0f };
    });
    s.add("FM Metall", "Effekte", [](Patch& p) {
        p.fm.algorithm = 5;
        p.fm.feedback = 0.4f;
        // Deep, detuned modulation at odd ratios: a struck sheet of metal.
        p.fm.ops = { op(1.0f, 0.7f, env(0.001f, 1.5f, 0.0f, 1.0f), 0.5f),
                     op(3.5f, 0.8f, env(0.001f, 0.8f, 0.0f, 0.6f), 0.7f, 13.0f),
                     op(1.5f, 0.6f, env(0.001f, 1.2f, 0.0f, 0.8f), 0.5f, -21.0f),
                     op(6.5f, 0.7f, env(0.001f, 0.6f, 0.0f, 0.5f)) };
        p.fm.volume = 0.55f;
        p.fx.reverb = { 0.05f, 3.0f };
    });
    s.add("FM Roboter", "Effekte", [](Patch& p) {
        p.fm.algorithm = 1;
        p.fm.feedback = 0.8f;
        // The LFO switches the modulation on and off in sixteenths: a machine that talks.
        const auto held = env(0.002f, 0.2f, 1.0f, 0.1f);
        p.fm.ops = { op(1.0f, 0.8f, held, 0.3f), op(1.0f, 0.6f, held, 0.3f), op(2.0f, 0.4f, held, 0.3f),
                     op(0.5f, 0.5f, held, 0.3f) };
        p.fm.lfo = { Wave::square, 8.0f, FmLfoTarget::index, 1.0f, true, kSixteenth };
        p.fm.volume = 0.37f;
    });
    return s.list;
}
} // namespace

const std::vector<FactorySound>& factorySounds(Engine engine)
{
    static const std::vector<FactorySound> analog = analogSounds();
    static const std::vector<FactorySound> fm = fmSounds();
    return engine == Engine::fm ? fm : analog;
}
} // namespace tonwerk
