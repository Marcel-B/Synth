#include "Catalogue.h"

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
#include "RobovoxProcessor.h"
#include "SamplerProcessor.h"
#include "ScopeProcessor.h"
#include "ShimmerProcessor.h"
#include "StutterProcessor.h"
#include "TapeProcessor.h"
#include "VocoderProcessor.h"
#include "WavetableProcessor.h"
#include "WobbleProcessor.h"
#include "ZoneProcessor.h"

namespace tonwerkapp
{
namespace
{
template <typename Processor>
Entry entry(const char* name, bool instrument)
{
    return { juce::String::fromUTF8(name), instrument, [] { return std::make_unique<Processor>(); } };
}

Entry synth(const char* name, tonwerk::Edition edition)
{
    return { name, true, [edition] { return std::make_unique<tonwerk::TonwerkSynthProcessor>(edition); } };
}
} // namespace

const std::vector<Entry>& catalogue()
{
    static const std::vector<Entry> entries {
        synth("Tonwerk Analog", tonwerk::Edition::analog),
        synth("Tonwerk FM", tonwerk::Edition::fm),
        entry<tonwerkdx::DxProcessor>("Tonwerk DX", true),
        entry<tonwerkwave::WavetableProcessor>("Tonwerk Wavetable", true),
        entry<tonwerkgrain::GranularProcessor>("Tonwerk Granular", true),
        entry<tonwerkphys::PhysicalProcessor>("Tonwerk Physical", true),
        entry<tonwerkadd::AdditiveProcessor>("Tonwerk Additiv", true),
        entry<tonwerkpd::PdProcessor>("Tonwerk PD", true),
        entry<tonwerksampler::SamplerProcessor>("Tonwerk Sampler", true),
        entry<tonwerkbass::BassProcessor>("Tonwerk Bass", true),
        entry<tonwerkneuro::NeuroProcessor>("Tonwerk Neuro", true),
        entry<tonwerkexpander::ExpanderProcessor>("Tonwerk Expander", true),
        entry<tonwerkgroove::GrooveProcessor>("Tonwerk Groovebox", true),
        entry<chromeglitch::ChromeGlitchProcessor>("Chrome Glitch", false),
        entry<tonwerkdistortion::DistortionProcessor>("Tonwerk Distortion", false),
        entry<tonwerkrobovox::RobovoxProcessor>("Tonwerk Robovox", false),
        entry<tonwerkvocoder::VocoderProcessor>("Tonwerk Vocoder", false),
        entry<tonwerkwobble::WobbleProcessor>("Tonwerk Wobble", false),
        entry<tonwerktape::TapeProcessor>("Tonwerk Tape", false),
        entry<tonwerkfreeze::FreezeProcessor>("Tonwerk Freeze", false),
        entry<tonwerkdelay::DelayProcessor>("Tonwerk Delay", false),
        entry<tonwerkchorus::ChorusProcessor>("Tonwerk Chorus", false),
        entry<tonwerkglue::GlueProcessor>("Tonwerk Glue", false),
        entry<tonwerkshimmer::ShimmerProcessor>("Tonwerk Shimmer", false),
        entry<tonwerkfilter::FilterProcessor>("Tonwerk Filter", false),
        entry<tonwerkstutter::StutterProcessor>("Tonwerk Stutter", false),
        entry<tonwerkmultiband::MultibandProcessor>("Tonwerk Multiband", false),
        entry<tonwerkzone::ZoneProcessor>("Tonwerk Zone", false),
        entry<tonwerkscope::ScopeProcessor>("Tonwerk Scope", false),
    };
    return entries;
}
} // namespace tonwerkapp
