#include "Parameters.h"

#include "dsp/Ranges.h"

namespace tonwerk
{
namespace
{
using Type = ParameterDescriptor::Type;

/** Names with umlauts; juce::String takes a plain `const char*` as ASCII only. */
juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

const juce::StringArray kWaveNames { "Sinus", "Dreieck", utf8("Sägezahn"), "Rechteck/Puls" };

/** A field of the patch, reached through a function so nested fields need no member-pointer chains. */
template <typename T>
using Field = std::function<T&(Patch&)>;

class Builder
{
public:
    std::vector<ParameterDescriptor> list;

    void number(const juce::String& id, const juce::String& name, Range range, Field<float> field,
                float centre = 0.0f, const juce::String& unit = {})
    {
        list.push_back({ id, name, Type::number, range.min, range.max, centre, {}, unit,
                         [field](const Patch& p) { return field(const_cast<Patch&>(p)); },
                         [field](Patch& p, float v) { field(p) = v; } });
    }

    void integer(const juce::String& id, const juce::String& name, Range range, Field<int> field)
    {
        list.push_back({ id, name, Type::integer, range.min, range.max, 0.0f, {}, {},
                         [field](const Patch& p) { return (float) field(const_cast<Patch&>(p)); },
                         [field](Patch& p, float v) { field(p) = juce::roundToInt(v); } });
    }

    template <typename Enum>
    void choice(const juce::String& id, const juce::String& name, const juce::StringArray& choices, Field<Enum> field)
    {
        list.push_back({ id, name, Type::choice, 0.0f, (float) choices.size() - 1, 0.0f, choices, {},
                         [field](const Patch& p) { return (float) (int) field(const_cast<Patch&>(p)); },
                         [field](Patch& p, float v) { field(p) = (Enum) juce::roundToInt(v); } });
    }

    void toggle(const juce::String& id, const juce::String& name, Field<bool> field)
    {
        list.push_back({ id, name, Type::toggle, 0.0f, 1.0f, 0.0f, {}, {},
                         [field](const Patch& p) { return field(const_cast<Patch&>(p)) ? 1.0f : 0.0f; },
                         [field](Patch& p, float v) { field(p) = v > 0.5f; } });
    }

    void envelope(const juce::String& prefix, const juce::String& name, Field<Envelope> env)
    {
        number(prefix + ".attack", name + " Attack", ranges::attack, [env](Patch& p) -> float& { return env(p).attack; }, 0.3f, "s");
        number(prefix + ".decay", name + " Decay", ranges::decay, [env](Patch& p) -> float& { return env(p).decay; }, 0.6f, "s");
        number(prefix + ".sustain", name + " Sustain", ranges::sustain, [env](Patch& p) -> float& { return env(p).sustain; });
        number(prefix + ".release", name + " Release", ranges::release, [env](Patch& p) -> float& { return env(p).release; }, 0.6f, "s");
    }

    void oscillator(const juce::String& prefix, const juce::String& name, Field<Oscillator> osc)
    {
        choice<Wave>(prefix + ".wave", name + " Welle", kWaveNames, [osc](Patch& p) -> Wave& { return osc(p).wave; });
        integer(prefix + ".octave", name + " Oktave", ranges::octave, [osc](Patch& p) -> int& { return osc(p).octave; });
        number(prefix + ".detune", name + " Verstimmung", ranges::detune, [osc](Patch& p) -> float& { return osc(p).detune; }, 0.0f, "ct");
        number(prefix + ".level", name + " Pegel", ranges::level, [osc](Patch& p) -> float& { return osc(p).level; });
        number(prefix + ".width", name + " Pulsbreite", ranges::width, [osc](Patch& p) -> float& { return osc(p).width; });
        number(prefix + ".pwm", name + " PWM LFO", ranges::pwm, [osc](Patch& p) -> float& { return osc(p).pwm; });
        toggle(prefix + ".pwmLfo", name + " PWM LFO an", [osc](Patch& p) -> bool& { return osc(p).pwmLfo; });
        number(prefix + ".pwmAmpEnv", name + utf8(" PWM Lautst.-Hüllk."), ranges::pwmEnv, [osc](Patch& p) -> float& { return osc(p).pwmAmpEnv; });
        number(prefix + ".pwmFilterEnv", name + utf8(" PWM Filter-Hüllk."), ranges::pwmEnv, [osc](Patch& p) -> float& { return osc(p).pwmFilterEnv; });
    }
};

std::vector<ParameterDescriptor> build()
{
    Builder b;
    b.choice<Engine>("engine", "Engine", { "Analog", "FM" }, [](Patch& p) -> Engine& { return p.engine; });

    // Analog.
    b.oscillator("osc1", "Osz 1", [](Patch& p) -> Oscillator& { return p.analog.osc1; });
    b.oscillator("osc2", "Osz 2", [](Patch& p) -> Oscillator& { return p.analog.osc2; });
    b.number("noise", "Rauschen", ranges::level, [](Patch& p) -> float& { return p.analog.noise; });
    b.choice<FilterType>("filter.type", "Filtertyp", { "Tiefpass", "Hochpass", "Bandpass" },
                         [](Patch& p) -> FilterType& { return p.analog.filter.type; });
    b.number("filter.cutoff", "Cutoff", ranges::cutoff, [](Patch& p) -> float& { return p.analog.filter.cutoff; }, 1000.0f, "Hz");
    b.number("filter.resonance", "Resonanz", ranges::resonance, [](Patch& p) -> float& { return p.analog.filter.resonance; });
    b.number("filter.envAmount", utf8("Filter-Hüllk. Menge"), ranges::envAmount, [](Patch& p) -> float& { return p.analog.filter.envAmount; }, 0.0f, "Okt");
    b.number("filter.keyTrack", "Keytracking", ranges::keyTrack, [](Patch& p) -> float& { return p.analog.filter.keyTrack; });
    b.envelope("filterEnv", utf8("Filter-Hüllk."), [](Patch& p) -> Envelope& { return p.analog.filterEnv; });
    b.envelope("ampEnv", utf8("Lautst.-Hüllk."), [](Patch& p) -> Envelope& { return p.analog.ampEnv; });
    b.choice<Wave>("lfo.wave", "LFO Welle", kWaveNames, [](Patch& p) -> Wave& { return p.analog.lfo.wave; });
    b.number("lfo.rate", "LFO Tempo", ranges::rate, [](Patch& p) -> float& { return p.analog.lfo.rate; }, 3.0f, "Hz");
    b.choice<AnalogLfoTarget>("lfo.target", "LFO Ziel", { utf8("Tonhöhe"), "Filter", utf8("Lautstärke") },
                              [](Patch& p) -> AnalogLfoTarget& { return p.analog.lfo.target; });
    b.number("lfo.depth", "LFO Tiefe", ranges::depth, [](Patch& p) -> float& { return p.analog.lfo.depth; });
    b.number("volume", utf8("Lautstärke"), ranges::volume, [](Patch& p) -> float& { return p.analog.volume; });

    // FM.
    juce::StringArray algorithms;
    for (size_t i = 0; i < kAlgorithms.size(); ++i)
        algorithms.add(juce::String((int) i + 1) + ": " + juce::String::fromUTF8(kAlgorithms[i].label));
    // A choice holds 0 to 7; the patch counts 1 to 8.
    b.list.push_back({ "fm.algorithm", "FM Algorithmus", Type::choice, 0.0f, 7.0f, 0.0f, algorithms, {},
                       [](const Patch& p) { return (float) (p.fm.algorithm - 1); },
                       [](Patch& p, float v) { p.fm.algorithm = juce::roundToInt(v) + 1; } });
    b.number("fm.feedback", "FM Feedback", ranges::feedback, [](Patch& p) -> float& { return p.fm.feedback; });
    for (int i = 0; i < 4; ++i)
    {
        const auto prefix = "fm.op" + juce::String(i + 1);
        const auto name = "Op " + juce::String(i + 1);
        const Field<Operator> op = [i](Patch& p) -> Operator& { return p.fm.ops[(size_t) i]; };
        b.number(prefix + ".ratio", name + " Ratio", ranges::ratio, [op](Patch& p) -> float& { return op(p).ratio; });
        b.number(prefix + ".detune", name + " Verstimmung", ranges::detune, [op](Patch& p) -> float& { return op(p).detune; }, 0.0f, "ct");
        b.number(prefix + ".level", name + " Pegel", ranges::level, [op](Patch& p) -> float& { return op(p).level; });
        b.number(prefix + ".velocity", name + " Anschlag", ranges::velocity, [op](Patch& p) -> float& { return op(p).velocity; });
        b.envelope(prefix + ".env", name, [op](Patch& p) -> Envelope& { return op(p).env; });
    }
    b.choice<Wave>("fm.lfo.wave", "FM LFO Welle", kWaveNames, [](Patch& p) -> Wave& { return p.fm.lfo.wave; });
    b.number("fm.lfo.rate", "FM LFO Tempo", ranges::rate, [](Patch& p) -> float& { return p.fm.lfo.rate; }, 3.0f, "Hz");
    b.choice<FmLfoTarget>("fm.lfo.target", "FM LFO Ziel", { utf8("Tonhöhe"), "Index", utf8("Lautstärke") },
                          [](Patch& p) -> FmLfoTarget& { return p.fm.lfo.target; });
    b.number("fm.lfo.depth", "FM LFO Tiefe", ranges::depth, [](Patch& p) -> float& { return p.fm.lfo.depth; });
    b.number("fm.volume", utf8("FM Lautstärke"), ranges::volume, [](Patch& p) -> float& { return p.fm.volume; });

    // Effects.
    b.number("fx.delay.mix", "Delay Mix", ranges::mix, [](Patch& p) -> float& { return p.fx.delay.mix; });
    b.number("fx.delay.time", "Delay Zeit", ranges::delayTime, [](Patch& p) -> float& { return p.fx.delay.time; }, 0.35f, "s");
    b.number("fx.delay.feedback", "Delay Feedback", ranges::delayFeedback, [](Patch& p) -> float& { return p.fx.delay.feedback; });
    b.number("fx.delay.tone", "Delay Ton", ranges::tone, [](Patch& p) -> float& { return p.fx.delay.tone; }, 3000.0f, "Hz");
    b.number("fx.reverb.mix", "Hall Mix", ranges::mix, [](Patch& p) -> float& { return p.fx.reverb.mix; });
    b.number("fx.reverb.decay", utf8("Hall Länge"), ranges::reverbDecay, [](Patch& p) -> float& { return p.fx.reverb.decay; }, 2.0f, "s");

    // Added after the first release: appended, so the parameters before them keep their places in saved projects.
    const auto first = b.list.size();
    juce::StringArray divisions;
    for (const auto& division : kDivisions)
        divisions.add(division.label);
    b.toggle("lfo.sync", "LFO Sync", [](Patch& p) -> bool& { return p.analog.lfo.sync; });
    b.choice<int>("lfo.division", "LFO Teilung", divisions, [](Patch& p) -> int& { return p.analog.lfo.division; });
    b.toggle("fm.lfo.sync", "FM LFO Sync", [](Patch& p) -> bool& { return p.fm.lfo.sync; });
    b.choice<int>("fm.lfo.division", "FM LFO Teilung", divisions, [](Patch& p) -> int& { return p.fm.lfo.division; });
    b.toggle("fx.delay.sync", "Delay Sync", [](Patch& p) -> bool& { return p.fx.delay.sync; });
    b.choice<int>("fx.delay.division", "Delay Teilung", divisions, [](Patch& p) -> int& { return p.fx.delay.division; });
    for (auto i = first; i < b.list.size(); ++i)
        b.list[i].version = 2;

    // Added in 0.3.0.
    const auto third = b.list.size();
    b.number("fold.amount", "Wavefolder Menge", ranges::level, [](Patch& p) -> float& { return p.analog.fold.amount; });
    b.number("fold.symmetry", "Wavefolder Symmetrie", ranges::bipolar, [](Patch& p) -> float& { return p.analog.fold.symmetry; });
    b.number("fold.env", utf8("Wavefolder Filter-Hüllk."), ranges::bipolar, [](Patch& p) -> float& { return p.analog.fold.env; });
    b.number("sh.rate", "S&H Tempo", ranges::rate, [](Patch& p) -> float& { return p.analog.sampleHold.rate; }, 3.0f, "Hz");
    b.toggle("sh.sync", "S&H Sync", [](Patch& p) -> bool& { return p.analog.sampleHold.sync; });
    b.choice<int>("sh.division", "S&H Teilung", divisions, [](Patch& p) -> int& { return p.analog.sampleHold.division; });
    b.number("sh.filter", "S&H Filter", ranges::depth, [](Patch& p) -> float& { return p.analog.sampleHold.filter; });
    b.number("sh.pitch", utf8("S&H Tonhöhe"), ranges::depth, [](Patch& p) -> float& { return p.analog.sampleHold.pitch; });
    for (auto i = third; i < b.list.size(); ++i)
        b.list[i].version = 3;
    return std::move(b.list);
}
} // namespace

const std::vector<ParameterDescriptor>& descriptors()
{
    static const std::vector<ParameterDescriptor> list = build();
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const Patch defaults;
    for (const auto& d : descriptors())
    {
        // Logic's Audio Units need the version hint; parameters added after the first release have 2.
        const juce::ParameterID id { d.id, d.version };
        const float value = d.get(defaults);
        switch (d.type)
        {
            case Type::number:
            {
                juce::NormalisableRange<float> range(d.min, d.max);
                if (d.centre > d.min && d.centre < d.max && ! juce::approximatelyEqual(d.centre, (d.min + d.max) / 2.0f))
                    range.setSkewForCentre(d.centre);
                if (d.id.endsWith(".ratio"))
                    range.interval = 0.5f;
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, d.name, range, value, juce::AudioParameterFloatAttributes().withLabel(d.unit)));
                break;
            }
            case Type::integer:
                layout.add(std::make_unique<juce::AudioParameterInt>(id, d.name, (int) d.min, (int) d.max, (int) value));
                break;
            case Type::choice:
                layout.add(std::make_unique<juce::AudioParameterChoice>(id, d.name, d.choices, (int) value));
                break;
            case Type::toggle:
                layout.add(std::make_unique<juce::AudioParameterBool>(id, d.name, value > 0.5f));
                break;
        }
    }
    return layout;
}

ParameterReader::ParameterReader(juce::AudioProcessorValueTreeState& state)
{
    for (const auto& d : descriptors())
    {
        values.push_back(state.getRawParameterValue(d.id));
        jassert(values.back() != nullptr);
    }
}

Patch ParameterReader::read() const
{
    Patch patch;
    const auto& list = descriptors();
    for (size_t i = 0; i < list.size(); ++i)
        list[i].set(patch, values[i]->load(std::memory_order_relaxed));
    return patch;
}

void writePatch(juce::AudioProcessorValueTreeState& state, const Patch& patch)
{
    for (const auto& d : descriptors())
    {
        if (auto* parameter = state.getParameter(d.id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(d.get(patch)));
            parameter->endChangeGesture();
        }
    }
}
} // namespace tonwerk
