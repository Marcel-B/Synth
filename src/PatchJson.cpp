#include "PatchJson.h"

#include "dsp/Ranges.h"

namespace tonwerk
{
namespace
{
const char* const kWaves[] = { "sine", "triangle", "sawtooth", "square" };
const char* const kFilterTypes[] = { "lowpass", "highpass", "bandpass" };
const char* const kAnalogTargets[] = { "pitch", "filter", "amp" };
const char* const kFmTargets[] = { "pitch", "index", "amp" };

float number(const juce::var& value, Range range, float fallback)
{
    if (! (value.isDouble() || value.isInt() || value.isInt64()))
        return fallback;
    const double number = value;
    if (! std::isfinite(number))
        return fallback;
    return (float) juce::jlimit((double) range.min, (double) range.max, number);
}

template <typename Enum, size_t Count>
Enum choice(const juce::var& value, const char* const (&names)[Count], Enum fallback)
{
    if (value.isString())
        for (size_t i = 0; i < Count; ++i)
            if (value.toString() == names[i])
                return (Enum) i;
    return fallback;
}

juce::var property(const juce::var& object, const char* name)
{
    return object.isObject() ? object.getProperty(name, {}) : juce::var();
}

bool flag(const juce::var& value, bool fallback) { return value.isBool() ? (bool) value : fallback; }

/** A note value by its label ("1/8."), as written by `divisionJson`. */
int division(const juce::var& value, int fallback)
{
    for (size_t i = 0; i < kDivisions.size(); ++i)
        if (value.toString() == kDivisions[i].label)
            return (int) i;
    return fallback;
}

/** Tempo sync is this plugin's own; Tonwerk ignores the two fields, so they are only written when sync is on. */
void writeSync(juce::DynamicObject& json, bool sync, int index)
{
    if (! sync)
        return;
    json.setProperty("sync", true);
    json.setProperty("division", kDivisions[(size_t) juce::jlimit(0, (int) kDivisions.size() - 1, index)].label);
}

Envelope envelopeOf(const juce::var& raw, const Envelope& fallback)
{
    return {
        number(property(raw, "attack"), ranges::attack, fallback.attack),
        number(property(raw, "decay"), ranges::decay, fallback.decay),
        number(property(raw, "sustain"), ranges::sustain, fallback.sustain),
        number(property(raw, "release"), ranges::release, fallback.release),
    };
}

Oscillator oscillatorOf(const juce::var& raw, const Oscillator& fallback)
{
    Oscillator osc;
    osc.wave = choice(property(raw, "wave"), kWaves, fallback.wave);
    osc.octave = juce::roundToInt(number(property(raw, "octave"), ranges::octave, (float) fallback.octave));
    osc.detune = number(property(raw, "detune"), ranges::detune, fallback.detune);
    osc.level = number(property(raw, "level"), ranges::level, fallback.level);
    osc.width = number(property(raw, "width"), ranges::width, fallback.width);
    osc.pwm = number(property(raw, "pwm"), ranges::pwm, fallback.pwm);
    // Sounds saved before the switch existed had the LFO on whenever PWM was set.
    const auto pwmLfo = property(raw, "pwmLfo");
    osc.pwmLfo = pwmLfo.isBool() ? (bool) pwmLfo : fallback.pwmLfo;
    osc.pwmAmpEnv = number(property(raw, "pwmAmpEnv"), ranges::pwmEnv, fallback.pwmAmpEnv);
    osc.pwmFilterEnv = number(property(raw, "pwmFilterEnv"), ranges::pwmEnv, fallback.pwmFilterEnv);
    return osc;
}

AnalogPatch analogOf(const juce::var& raw)
{
    const AnalogPatch fallback = defaultAnalog(Kind::melody);
    AnalogPatch patch;
    patch.osc1 = oscillatorOf(property(raw, "osc1"), fallback.osc1);
    patch.osc2 = oscillatorOf(property(raw, "osc2"), fallback.osc2);
    patch.noise = number(property(raw, "noise"), ranges::level, fallback.noise);
    const auto filter = property(raw, "filter");
    patch.filter.type = choice(property(filter, "type"), kFilterTypes, fallback.filter.type);
    patch.filter.cutoff = number(property(filter, "cutoff"), ranges::cutoff, fallback.filter.cutoff);
    patch.filter.resonance = number(property(filter, "resonance"), ranges::resonance, fallback.filter.resonance);
    patch.filter.envAmount = number(property(filter, "envAmount"), ranges::envAmount, fallback.filter.envAmount);
    patch.filter.keyTrack = number(property(filter, "keyTrack"), ranges::keyTrack, fallback.filter.keyTrack);
    patch.filterEnv = envelopeOf(property(raw, "filterEnv"), fallback.filterEnv);
    patch.ampEnv = envelopeOf(property(raw, "ampEnv"), fallback.ampEnv);
    const auto lfo = property(raw, "lfo");
    patch.lfo.wave = choice(property(lfo, "wave"), kWaves, fallback.lfo.wave);
    patch.lfo.rate = number(property(lfo, "rate"), ranges::rate, fallback.lfo.rate);
    patch.lfo.target = choice(property(lfo, "target"), kAnalogTargets, fallback.lfo.target);
    patch.lfo.depth = number(property(lfo, "depth"), ranges::depth, fallback.lfo.depth);
    patch.lfo.sync = flag(property(lfo, "sync"), fallback.lfo.sync);
    patch.lfo.division = division(property(lfo, "division"), fallback.lfo.division);
    patch.volume = number(property(raw, "volume"), ranges::volume, fallback.volume);
    const auto fold = property(raw, "fold");
    patch.fold.amount = number(property(fold, "amount"), ranges::level, fallback.fold.amount);
    patch.fold.symmetry = number(property(fold, "symmetry"), ranges::bipolar, fallback.fold.symmetry);
    patch.fold.env = number(property(fold, "env"), ranges::bipolar, fallback.fold.env);
    const auto sh = property(raw, "sampleHold");
    patch.sampleHold.rate = number(property(sh, "rate"), ranges::rate, fallback.sampleHold.rate);
    patch.sampleHold.sync = flag(property(sh, "sync"), fallback.sampleHold.sync);
    patch.sampleHold.division = division(property(sh, "division"), fallback.sampleHold.division);
    patch.sampleHold.filter = number(property(sh, "filter"), ranges::depth, fallback.sampleHold.filter);
    patch.sampleHold.pitch = number(property(sh, "pitch"), ranges::depth, fallback.sampleHold.pitch);
    return patch;
}

FmPatch fmOf(const juce::var& raw)
{
    const FmPatch fallback = defaultFm(Kind::melody);
    FmPatch patch;
    patch.algorithm = juce::roundToInt(number(property(raw, "algorithm"), ranges::algorithm, (float) fallback.algorithm));
    patch.feedback = number(property(raw, "feedback"), ranges::feedback, fallback.feedback);
    const auto ops = property(raw, "ops");
    for (int i = 0; i < 4; ++i)
    {
        const auto opRaw = ops.isArray() && i < ops.size() ? ops[i] : juce::var();
        const Operator& opFallback = fallback.ops[(size_t) i];
        Operator& op = patch.ops[(size_t) i];
        // Halves, as Tonwerk's slider sets them.
        op.ratio = std::round(number(property(opRaw, "ratio"), ranges::ratio, opFallback.ratio) * 2.0f) / 2.0f;
        op.detune = number(property(opRaw, "detune"), ranges::detune, opFallback.detune);
        op.level = number(property(opRaw, "level"), ranges::level, opFallback.level);
        op.velocity = number(property(opRaw, "velocity"), ranges::velocity, opFallback.velocity);
        op.env = envelopeOf(property(opRaw, "env"), opFallback.env);
    }
    const auto lfo = property(raw, "lfo");
    patch.lfo.wave = choice(property(lfo, "wave"), kWaves, fallback.lfo.wave);
    patch.lfo.rate = number(property(lfo, "rate"), ranges::rate, fallback.lfo.rate);
    patch.lfo.target = choice(property(lfo, "target"), kFmTargets, fallback.lfo.target);
    patch.lfo.depth = number(property(lfo, "depth"), ranges::depth, fallback.lfo.depth);
    patch.lfo.sync = flag(property(lfo, "sync"), fallback.lfo.sync);
    patch.lfo.division = division(property(lfo, "division"), fallback.lfo.division);
    patch.volume = number(property(raw, "volume"), ranges::volume, fallback.volume);
    return patch;
}

Effects effectsOf(const juce::var& raw)
{
    const Effects fallback;
    Effects fx;
    const auto delay = property(raw, "delay");
    const auto reverb = property(raw, "reverb");
    fx.delay.mix = number(property(delay, "mix"), ranges::mix, fallback.delay.mix);
    fx.delay.time = number(property(delay, "time"), ranges::delayTime, fallback.delay.time);
    fx.delay.feedback = number(property(delay, "feedback"), ranges::delayFeedback, fallback.delay.feedback);
    fx.delay.tone = number(property(delay, "tone"), ranges::tone, fallback.delay.tone);
    fx.delay.sync = flag(property(delay, "sync"), fallback.delay.sync);
    fx.delay.division = division(property(delay, "division"), fallback.delay.division);
    fx.reverb.mix = number(property(reverb, "mix"), ranges::mix, fallback.reverb.mix);
    fx.reverb.decay = number(property(reverb, "decay"), ranges::reverbDecay, fallback.reverb.decay);
    return fx;
}

juce::DynamicObject::Ptr object() { return new juce::DynamicObject(); }

juce::var envelopeJson(const Envelope& env)
{
    auto json = object();
    json->setProperty("attack", env.attack);
    json->setProperty("decay", env.decay);
    json->setProperty("sustain", env.sustain);
    json->setProperty("release", env.release);
    return json.get();
}

juce::var oscillatorJson(const Oscillator& osc)
{
    auto json = object();
    json->setProperty("wave", kWaves[(int) osc.wave]);
    json->setProperty("octave", osc.octave);
    json->setProperty("detune", osc.detune);
    json->setProperty("level", osc.level);
    json->setProperty("width", osc.width);
    json->setProperty("pwm", osc.pwm);
    json->setProperty("pwmLfo", osc.pwmLfo);
    json->setProperty("pwmAmpEnv", osc.pwmAmpEnv);
    json->setProperty("pwmFilterEnv", osc.pwmFilterEnv);
    return json.get();
}

template <typename Lfo, size_t Count>
juce::var lfoJson(const Lfo& lfo, const char* const (&targets)[Count])
{
    auto json = object();
    json->setProperty("wave", kWaves[(int) lfo.wave]);
    json->setProperty("rate", lfo.rate);
    json->setProperty("target", targets[(int) lfo.target]);
    json->setProperty("depth", lfo.depth);
    writeSync(*json, lfo.sync, lfo.division);
    return json.get();
}
} // namespace

Patch patchFromJson(const juce::var& json, const Patch& base)
{
    Patch patch = base;
    if (property(json, "engine").toString() == "fm")
    {
        patch.engine = Engine::fm;
        patch.fm = fmOf(json);
    }
    else
    {
        // Sounds saved before Tonwerk had a second engine name none.
        patch.engine = Engine::analog;
        patch.analog = analogOf(json);
    }
    patch.fx = effectsOf(property(json, "fx"));
    return patch;
}

juce::var patchToJson(const Patch& patch)
{
    auto json = object();
    if (patch.engine == Engine::fm)
    {
        const FmPatch& fm = patch.fm;
        json->setProperty("engine", "fm");
        json->setProperty("algorithm", fm.algorithm);
        json->setProperty("feedback", fm.feedback);
        juce::Array<juce::var> ops;
        for (const auto& op : fm.ops)
        {
            auto opJson = object();
            opJson->setProperty("ratio", op.ratio);
            opJson->setProperty("detune", op.detune);
            opJson->setProperty("level", op.level);
            opJson->setProperty("velocity", op.velocity);
            opJson->setProperty("env", envelopeJson(op.env));
            ops.add(opJson.get());
        }
        json->setProperty("ops", ops);
        json->setProperty("lfo", lfoJson(fm.lfo, kFmTargets));
        json->setProperty("volume", fm.volume);
    }
    else
    {
        const AnalogPatch& analog = patch.analog;
        json->setProperty("engine", "analog");
        json->setProperty("osc1", oscillatorJson(analog.osc1));
        json->setProperty("osc2", oscillatorJson(analog.osc2));
        json->setProperty("noise", analog.noise);
        auto filter = object();
        filter->setProperty("type", kFilterTypes[(int) analog.filter.type]);
        filter->setProperty("cutoff", analog.filter.cutoff);
        filter->setProperty("resonance", analog.filter.resonance);
        filter->setProperty("envAmount", analog.filter.envAmount);
        filter->setProperty("keyTrack", analog.filter.keyTrack);
        json->setProperty("filter", filter.get());
        json->setProperty("filterEnv", envelopeJson(analog.filterEnv));
        json->setProperty("ampEnv", envelopeJson(analog.ampEnv));
        json->setProperty("lfo", lfoJson(analog.lfo, kAnalogTargets));
        json->setProperty("volume", analog.volume);
        // The folder and the sample and hold are this plugin's own, written only when they sound, like tempo sync.
        if (analog.fold.amount > 0.0f || ! isZero(analog.fold.symmetry) || ! isZero(analog.fold.env))
        {
            auto fold = object();
            fold->setProperty("amount", analog.fold.amount);
            fold->setProperty("symmetry", analog.fold.symmetry);
            fold->setProperty("env", analog.fold.env);
            json->setProperty("fold", fold.get());
        }
        if (analog.sampleHold.filter > 0.0f || analog.sampleHold.pitch > 0.0f)
        {
            auto sh = object();
            sh->setProperty("rate", analog.sampleHold.rate);
            writeSync(*sh, analog.sampleHold.sync, analog.sampleHold.division);
            sh->setProperty("filter", analog.sampleHold.filter);
            sh->setProperty("pitch", analog.sampleHold.pitch);
            json->setProperty("sampleHold", sh.get());
        }
    }
    auto fx = object();
    auto delay = object();
    delay->setProperty("mix", patch.fx.delay.mix);
    delay->setProperty("time", patch.fx.delay.time);
    delay->setProperty("feedback", patch.fx.delay.feedback);
    delay->setProperty("tone", patch.fx.delay.tone);
    writeSync(*delay, patch.fx.delay.sync, patch.fx.delay.division);
    auto reverb = object();
    reverb->setProperty("mix", patch.fx.reverb.mix);
    reverb->setProperty("decay", patch.fx.reverb.decay);
    fx->setProperty("delay", delay.get());
    fx->setProperty("reverb", reverb.get());
    json->setProperty("fx", fx.get());
    return json.get();
}

juce::Array<NamedPatch> namedPatchesFromJson(const juce::String& text, const juce::String& fallbackName)
{
    juce::Array<NamedPatch> result;
    const auto json = juce::JSON::parse(text);
    auto addEntry = [&result](const juce::var& entry, const juce::String& name) {
        const auto patch = property(entry, "patch");
        if (patch.isObject() && name.trim().isNotEmpty())
            result.add({ name.trim(), patch });
    };
    if (json.isArray())
    {
        for (const auto& entry : *json.getArray())
            addEntry(entry, property(entry, "name").toString());
    }
    else if (json.isObject() && property(json, "patch").isObject())
    {
        const auto name = property(json, "name").toString();
        addEntry(json, name.isNotEmpty() ? name : fallbackName);
    }
    else if (json.isObject())
    {
        result.add({ fallbackName, json });
    }
    return result;
}
} // namespace tonwerk
