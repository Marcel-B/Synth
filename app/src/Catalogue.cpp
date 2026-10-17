#include "Catalogue.h"

#include "AdditiveProcessor.h"
#include "BassProcessor.h"
#include "DxProcessor.h"
#include "ExpanderProcessor.h"
#include "GranularProcessor.h"
#include "GrooveProcessor.h"
#include "NeuroProcessor.h"
#include "PdProcessor.h"
#include "PhysicalProcessor.h"
#include "PluginProcessor.h"
#include "SamplerProcessor.h"
#include "ScopeProcessor.h"
#include "WavetableProcessor.h"

namespace tonwerkapp
{
namespace
{
template <typename Processor>
Entry entry(const char* name)
{
    return { juce::String::fromUTF8(name), [] { return std::make_unique<Processor>(); } };
}

Entry synth(const char* name, tonwerk::Edition edition)
{
    return { name, [edition] { return std::make_unique<tonwerk::TonwerkSynthProcessor>(edition); } };
}
} // namespace

const std::vector<Entry>& catalogue()
{
    static const std::vector<Entry> entries {
        synth("Tonwerk Analog", tonwerk::Edition::analog),
        synth("Tonwerk FM", tonwerk::Edition::fm),
        entry<tonwerkdx::DxProcessor>("Tonwerk DX"),
        entry<tonwerkwave::WavetableProcessor>("Tonwerk Wavetable"),
        entry<tonwerkgrain::GranularProcessor>("Tonwerk Granular"),
        entry<tonwerkphys::PhysicalProcessor>("Tonwerk Physical"),
        entry<tonwerkadd::AdditiveProcessor>("Tonwerk Additiv"),
        entry<tonwerkpd::PdProcessor>("Tonwerk PD"),
        entry<tonwerksampler::SamplerProcessor>("Tonwerk Sampler"),
        entry<tonwerkbass::BassProcessor>("Tonwerk Bass"),
        entry<tonwerkneuro::NeuroProcessor>("Tonwerk Neuro"),
        entry<tonwerkexpander::ExpanderProcessor>("Tonwerk Expander"),
        entry<tonwerkgroove::GrooveProcessor>("Tonwerk Groovebox"),
        { "Tonwerk Scope", [] { return std::make_unique<tonwerkscope::ScopeProcessor>(); }, true },
    };
    return entries;
}
} // namespace tonwerkapp
