#include "RigCatalogue.h"

#include "AdditiveProcessor.h"
#include "BassProcessor.h"
#include "ChorusProcessor.h"
#include "DelayProcessor.h"
#include "DistortionProcessor.h"
#include "DxProcessor.h"
#include "ExpanderProcessor.h"
#include "FilterProcessor.h"
#include "FreezeProcessor.h"
#include "GlitchProcessor.h"
#include "GlueProcessor.h"
#include "GranularProcessor.h"
#include "GrooveProcessor.h"
#include "MultibandProcessor.h"
#include "NeuroProcessor.h"
#include "PdProcessor.h"
#include "PhysicalProcessor.h"
#include "PluginProcessor.h"
#include "RumbleProcessor.h"
#include "SamplerProcessor.h"
#include "ScopeProcessor.h"
#include "ShimmerProcessor.h"
#include "StutterProcessor.h"
#include "TapeProcessor.h"
#include "WavetableProcessor.h"
#include "WobbleProcessor.h"
#include "ZoneProcessor.h"

namespace tonwerkrig
{
namespace
{
template <typename Processor>
Kind kind(const char* id, const char* name)
{
    return { id, juce::String::fromUTF8(name), [] { return std::make_unique<Processor>(); } };
}

Kind synth(const char* id, const char* name, tonwerk::Edition edition)
{
    return { id, name, [edition] { return std::make_unique<tonwerk::TonwerkSynthProcessor>(edition); } };
}
} // namespace

const std::vector<Kind>& instruments()
{
    static const std::vector<Kind> kinds {
        synth("analog", "Tonwerk Analog", tonwerk::Edition::analog),
        synth("fm", "Tonwerk FM", tonwerk::Edition::fm),
        kind<tonwerkdx::DxProcessor>("dx", "Tonwerk DX"),
        kind<tonwerkwave::WavetableProcessor>("wavetable", "Tonwerk Wavetable"),
        kind<tonwerkgrain::GranularProcessor>("granular", "Tonwerk Granular"),
        kind<tonwerkphys::PhysicalProcessor>("physical", "Tonwerk Physical"),
        kind<tonwerkadd::AdditiveProcessor>("additive", "Tonwerk Additiv"),
        kind<tonwerkpd::PdProcessor>("pd", "Tonwerk PD"),
        kind<tonwerksampler::SamplerProcessor>("sampler", "Tonwerk Sampler"),
        kind<tonwerkbass::BassProcessor>("bass", "Tonwerk Bass"),
        kind<tonwerkneuro::NeuroProcessor>("neuro", "Tonwerk Neuro"),
        kind<tonwerkexpander::ExpanderProcessor>("expander", "Tonwerk Expander"),
        kind<tonwerkgroove::GrooveProcessor>(kTracksInstrument, "Tonwerk Groovebox"),
    };
    return kinds;
}

const std::vector<Kind>& effects()
{
    static const std::vector<Kind> kinds {
        kind<tonwerkdistortion::DistortionProcessor>("distortion", "Tonwerk Distortion"),
        kind<tonwerkfilter::FilterProcessor>("filter", "Tonwerk Filter"),
        kind<tonwerkwobble::WobbleProcessor>("wobble", "Tonwerk Wobble"),
        kind<tonwerkchorus::ChorusProcessor>("chorus", "Tonwerk Chorus"),
        kind<tonwerkdelay::DelayProcessor>("delay", "Tonwerk Delay"),
        kind<tonwerkshimmer::ShimmerProcessor>("shimmer", "Tonwerk Shimmer"),
        kind<tonwerkfreeze::FreezeProcessor>("freeze", "Tonwerk Freeze"),
        kind<tonwerktape::TapeProcessor>("tape", "Tonwerk Tape"),
        kind<tonwerkstutter::StutterProcessor>("stutter", "Tonwerk Stutter"),
        kind<chromeglitch::ChromeGlitchProcessor>("glitch", "Chrome Glitch"),
        kind<tonwerkrumble::RumbleProcessor>("rumble", "Tonwerk Rumble"),
        kind<tonwerkglue::GlueProcessor>("glue", "Tonwerk Glue"),
        kind<tonwerkmultiband::MultibandProcessor>("multiband", "Tonwerk Multiband"),
        kind<tonwerkzone::ZoneProcessor>("zone", "Tonwerk Zone"),
        kind<tonwerkscope::ScopeProcessor>("scope", "Tonwerk Scope"),
    };
    return kinds;
}

const Kind* find(const std::vector<Kind>& kinds, const juce::String& id)
{
    for (const auto& k : kinds)
        if (id == k.id)
            return &k;
    return nullptr;
}
} // namespace tonwerkrig
