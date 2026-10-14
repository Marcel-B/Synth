cmake_minimum_required(VERSION 3.22)

# A release passes its tag's version (-DTONWERK_VERSION=0.2.0); Logic takes a higher version as a new plugin.
set(TONWERK_VERSION "0.3.0" CACHE STRING "Plugin version")
project(TonwerkSynth VERSION ${TONWERK_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# One build for Apple Silicon and Intel Macs, from macOS 11 on.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Audio Units exist only on the Mac; elsewhere the VST3 and the standalone app are built, which is enough for the tests.
if(APPLE)
    set(TONWERK_FORMATS AU VST3 Standalone)
else()
    set(TONWERK_FORMATS VST3 Standalone)
endif()

set(TONWERK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# The Night City look the plugins share: colours, panels, knobs, the oscilloscope (common/).
set(TONWERK_UI_SOURCES common/NightCity.cpp)

set(TONWERK_SOURCES
    src/dsp/Defaults.cpp
    src/EffectsChain.cpp
    src/FactorySounds.cpp
    src/Parameters.cpp
    src/PatchJson.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/PresetLibrary.cpp)

# Tonwerk's two browser synthesizers, each its own instrument (src/, built twice with a different edition), and the
# first plugin, which plays both and stays for the Logic projects made with it.
function(tonwerk_add_synth target product bundle code edition)
    juce_add_plugin(${target}
        PRODUCT_NAME "${product}"
        COMPANY_NAME "b-velop"
        COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
        BUNDLE_ID ${bundle}
        PLUGIN_MANUFACTURER_CODE Bvlp
        PLUGIN_CODE ${code}
        FORMATS ${TONWERK_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Synth
        # scripts/install-macos.sh copies and signs; copying here would need write access to the plug-in folders.
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${target} PRIVATE ${TONWERK_SOURCES} ${TONWERK_UI_SOURCES})
    target_include_directories(${target} PRIVATE common)
    target_compile_definitions(${target} PUBLIC ${TONWERK_DEFINITIONS} TONWERK_EDITION=${edition})
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

tonwerk_add_synth(TonwerkAnalog "Tonwerk Analog" de.bvelop.tonwerkanalog TwAn analog)
tonwerk_add_synth(TonwerkFM "Tonwerk FM" de.bvelop.tonwerkfm TwFm fm)
tonwerk_add_synth(TonwerkSynth "Tonwerk Synth" de.bvelop.tonwerksynth Twsy combined)

# Chrome Glitch: an audio effect from the same repository, for a voice whose speech chrome fails (glitch/).
juce_add_plugin(ChromeGlitch
    PRODUCT_NAME "Chrome Glitch"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.chromeglitch
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE ChGl
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(CHROME_GLITCH_SOURCES
    glitch/src/GlitchEditor.cpp
    glitch/src/GlitchPresets.cpp
    glitch/src/GlitchProcessor.cpp
    common/EffectPresets.cpp)

target_sources(ChromeGlitch PRIVATE ${CHROME_GLITCH_SOURCES})
target_include_directories(ChromeGlitch PRIVATE common)
target_compile_definitions(ChromeGlitch PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(ChromeGlitch
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Distortion: a classic guitar distortion pedal's circuit as an audio effect (distortion/).
juce_add_plugin(TonwerkDistortion
    PRODUCT_NAME "Tonwerk Distortion"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkdistortion
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwDs
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_DISTORTION_SOURCES
    distortion/src/DistortionEditor.cpp
    distortion/src/DistortionPresets.cpp
    distortion/src/DistortionProcessor.cpp
    common/EffectPresets.cpp)

target_sources(TonwerkDistortion PRIVATE ${TONWERK_DISTORTION_SOURCES} ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkDistortion PRIVATE common)
target_compile_definitions(TonwerkDistortion PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkDistortion
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Wavetable: a wavetable synthesizer for basses and leads (wavetable/).
juce_add_plugin(TonwerkWavetable
    PRODUCT_NAME "Tonwerk Wavetable"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkwavetable
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwWt
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_WAVETABLE_SOURCES
    wavetable/src/Wavetables.cpp
    wavetable/src/WavetableEditor.cpp
    wavetable/src/WavetablePresets.cpp
    wavetable/src/WavetableProcessor.cpp)

target_sources(TonwerkWavetable PRIVATE ${TONWERK_WAVETABLE_SOURCES} ${TONWERK_UI_SOURCES})
# The sub oscillator and the noise come from the synth's DSP (src/dsp/Oscillators.h).
target_include_directories(TonwerkWavetable PRIVATE src common)
target_compile_definitions(TonwerkWavetable PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkWavetable
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk DX: six-operator FM with 32 algorithms, with Tonwerk's delay and hall (dx/).
juce_add_plugin(TonwerkDX
    PRODUCT_NAME "Tonwerk DX"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkdx
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwDx
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_DX_SOURCES
    dx/src/DxEditor.cpp
    dx/src/DxPresets.cpp
    dx/src/DxProcessor.cpp)

# The effects are the synth's own (src/EffectsChain.cpp); the tests have them already.
target_sources(TonwerkDX PRIVATE ${TONWERK_DX_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkDX PRIVATE src common)
target_compile_definitions(TonwerkDX PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkDX
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Granular: clouds of grains from its own sources or a file of the user's, with Tonwerk's delay and hall (granular/).
juce_add_plugin(TonwerkGranular
    PRODUCT_NAME "Tonwerk Granular"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkgranular
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwGr
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_GRANULAR_SOURCES
    granular/src/GrainSources.cpp
    granular/src/GranularEditor.cpp
    granular/src/GranularPresets.cpp
    granular/src/GranularProcessor.cpp)

# The envelopes, LFO shapes and filter come from Tonwerk Wavetable's DSP (wavetable/src/WaveDsp.h), the effects from the synth.
target_sources(TonwerkGranular PRIVATE ${TONWERK_GRANULAR_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkGranular PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkGranular PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkGranular
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Physical: strings, tubes and struck bodies built from their physics, with Tonwerk's delay and hall (physical/).
juce_add_plugin(TonwerkPhysical
    PRODUCT_NAME "Tonwerk Physical"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkphysical
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwPh
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_PHYSICAL_SOURCES
    physical/src/PhysicalEditor.cpp
    physical/src/PhysicalPresets.cpp
    physical/src/PhysicalProcessor.cpp)

# The envelopes, LFO shapes and filter come from Tonwerk Wavetable's DSP (wavetable/src/WaveDsp.h), the effects from the synth.
target_sources(TonwerkPhysical PRIVATE ${TONWERK_PHYSICAL_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkPhysical PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkPhysical PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkPhysical
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Additiv: sounds built from up to 64 sine partials, with Tonwerk's delay and hall (additive/).
juce_add_plugin(TonwerkAdditiv
    PRODUCT_NAME "Tonwerk Additiv"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkadditiv
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwAd
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_ADDITIVE_SOURCES
    additive/src/AdditiveEditor.cpp
    additive/src/AdditivePresets.cpp
    additive/src/AdditiveProcessor.cpp)

# The envelopes and LFO shapes come from Tonwerk Wavetable's DSP (wavetable/src/WaveDsp.h), the effects from the synth.
target_sources(TonwerkAdditiv PRIVATE ${TONWERK_ADDITIVE_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkAdditiv PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkAdditiv PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkAdditiv
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Groovebox: a drum machine of synthesised drums with a step sequencer in the host's tempo (groovebox/).
juce_add_plugin(TonwerkGroovebox
    PRODUCT_NAME "Tonwerk Groovebox"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkgroovebox
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwGb
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Drum
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_GROOVEBOX_SOURCES
    groovebox/src/GrooveEditor.cpp
    groovebox/src/GroovePresets.cpp
    groovebox/src/GrooveProcessor.cpp)

# The noise, oscillators and filters come from the synth's DSP (src/dsp/Oscillators.h), the effects from the synth.
target_sources(TonwerkGroovebox PRIVATE ${TONWERK_GROOVEBOX_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkGroovebox PRIVATE src common)
target_compile_definitions(TonwerkGroovebox PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkGroovebox
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Robovox: a voice sung through a synth, the robot voice of AI songs (robovox/). A music effect (aumf), so
# Logic can send it MIDI notes for its carrier through the side chain.
juce_add_plugin(TonwerkRobovox
    PRODUCT_NAME "Tonwerk Robovox"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkrobovox
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwRv
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_ROBOVOX_SOURCES
    robovox/src/RobovoxEditor.cpp
    robovox/src/RobovoxPresets.cpp
    robovox/src/RobovoxProcessor.cpp
    common/EffectPresets.cpp)

# The carrier's oscillator and noise come from the synth's DSP (src/dsp/Oscillators.h).
target_sources(TonwerkRobovox PRIVATE ${TONWERK_ROBOVOX_SOURCES} ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkRobovox PRIVATE src common)
target_compile_definitions(TonwerkRobovox PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkRobovox
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Wobble: a filter and a volume shaper moved by drawn, tempo-synced LFO curves, for dubstep wobbles (wobble/).
juce_add_plugin(TonwerkWobble
    PRODUCT_NAME "Tonwerk Wobble"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkwobble
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwWb
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Filter
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_WOBBLE_SOURCES
    wobble/src/WobbleEditor.cpp
    wobble/src/WobblePresets.cpp
    wobble/src/WobbleProcessor.cpp
    common/EffectPresets.cpp)

target_sources(TonwerkWobble PRIVATE ${TONWERK_WOBBLE_SOURCES} ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkWobble PRIVATE common)
target_compile_definitions(TonwerkWobble PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkWobble
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk PD: phase distortion as on Casio's CZ synths, with a chorus and Tonwerk's delay and hall (pd/).
juce_add_plugin(TonwerkPD
    PRODUCT_NAME "Tonwerk PD"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkpd
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwPd
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_PD_SOURCES
    pd/src/PdEditor.cpp
    pd/src/PdPresets.cpp
    pd/src/PdProcessor.cpp)

# The envelopes and LFO shapes come from Tonwerk Wavetable's DSP (wavetable/src/WaveDsp.h), the effects from the synth.
target_sources(TonwerkPD PRIVATE ${TONWERK_PD_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkPD PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkPD PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkPD
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Tape: a worn tape machine as an audio effect, saturation, wow and flutter, hiss, dropouts (tape/).
juce_add_plugin(TonwerkTape
    PRODUCT_NAME "Tonwerk Tape"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerktape
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwTp
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_TAPE_SOURCES
    tape/src/TapeEditor.cpp
    tape/src/TapePresets.cpp
    tape/src/TapeProcessor.cpp
    common/EffectPresets.cpp)

target_sources(TonwerkTape PRIVATE ${TONWERK_TAPE_SOURCES} ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkTape PRIVATE common)
target_compile_definitions(TonwerkTape PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkTape
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Freeze: a spectral freeze that holds a moment of the input as an endless pad (freeze/).
juce_add_plugin(TonwerkFreeze
    PRODUCT_NAME "Tonwerk Freeze"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkfreeze
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwFz
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_FREEZE_SOURCES
    freeze/src/FreezeEditor.cpp
    freeze/src/FreezePresets.cpp
    freeze/src/FreezeProcessor.cpp)

target_sources(TonwerkFreeze PRIVATE ${TONWERK_FREEZE_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkFreeze PRIVATE common)
target_compile_definitions(TonwerkFreeze PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkFreeze
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Arp: an arpeggiator and step sequencer for the synths (arp/). A MIDI effect (aumi), the kind Logic offers in
# a software instrument track's MIDI FX slot; no audio buses.
juce_add_plugin(TonwerkArp
    PRODUCT_NAME "Tonwerk Arp"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkarp
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwAr
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_ARP_SOURCES
    arp/src/ArpEditor.cpp
    arp/src/ArpPresets.cpp
    arp/src/ArpProcessor.cpp
    common/EffectPresets.cpp)

target_sources(TonwerkArp PRIVATE ${TONWERK_ARP_SOURCES} ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkArp PRIVATE common)
target_compile_definitions(TonwerkArp PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkArp
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Sampler: a sampler for chromatic samples, loops and slices, from sounds built in or the user's own file (sampler/).
juce_add_plugin(TonwerkSampler
    PRODUCT_NAME "Tonwerk Sampler"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerksampler
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwSm
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_SAMPLER_SOURCES
    sampler/src/SampleData.cpp
    sampler/src/SamplerEditor.cpp
    sampler/src/SamplerPresets.cpp
    sampler/src/SamplerProcessor.cpp)

target_sources(TonwerkSampler PRIVATE ${TONWERK_SAMPLER_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkSampler PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkSampler PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkSampler
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Bass: an acid bass in the manner of the TB-303, one voice with slide and accent, with Tonwerk's delay and hall (bass/).
juce_add_plugin(TonwerkBass
    PRODUCT_NAME "Tonwerk Bass"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerkbass
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwBs
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_BASS_SOURCES
    bass/src/BassEditor.cpp
    bass/src/BassPresets.cpp
    bass/src/BassProcessor.cpp)

target_sources(TonwerkBass PRIVATE ${TONWERK_BASS_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkBass PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkBass PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkBass
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# The tests compile the same sources into a console app and run the DSP, the JSON and the parameters without a host.
option(TONWERK_TESTS "Build the tests" ON)
if(TONWERK_TESTS)
    enable_testing()
    juce_add_console_app(TonwerkSynthTests PRODUCT_NAME "Tonwerk Synth Tests")
    target_sources(TonwerkSynthTests PRIVATE
        ${TONWERK_SOURCES}
        ${TONWERK_UI_SOURCES}
        tests/Main.cpp
        tests/DspTests.cpp
        tests/PatchTests.cpp
        tests/ProcessorTests.cpp
        tests/EditionTests.cpp
        ${CHROME_GLITCH_SOURCES}
        tests/GlitchTests.cpp
        ${TONWERK_DISTORTION_SOURCES}
        tests/DistortionTests.cpp
        ${TONWERK_WAVETABLE_SOURCES}
        tests/WavetableTests.cpp
        ${TONWERK_DX_SOURCES}
        tests/DxTests.cpp
        ${TONWERK_GRANULAR_SOURCES}
        tests/GranularTests.cpp
        ${TONWERK_PHYSICAL_SOURCES}
        tests/PhysicalTests.cpp
        ${TONWERK_ADDITIVE_SOURCES}
        tests/AdditiveTests.cpp
        ${TONWERK_GROOVEBOX_SOURCES}
        tests/GrooveboxTests.cpp
        ${TONWERK_ROBOVOX_SOURCES}
        tests/RobovoxTests.cpp
        ${TONWERK_WOBBLE_SOURCES}
        tests/WobbleTests.cpp
        ${TONWERK_PD_SOURCES}
        tests/PdTests.cpp
        ${TONWERK_TAPE_SOURCES}
        tests/TapeTests.cpp
        ${TONWERK_FREEZE_SOURCES}
        tests/FreezeTests.cpp
        arp/src/ArpEditor.cpp
        arp/src/ArpPresets.cpp
        arp/src/ArpProcessor.cpp
        tests/ArpTests.cpp
        ${TONWERK_SAMPLER_SOURCES}
        tests/SamplerTests.cpp
        ${TONWERK_BASS_SOURCES}
        tests/BassTests.cpp
        tests/PresetTests.cpp
        tests/EffectPresetTests.cpp)
    target_include_directories(TonwerkSynthTests PRIVATE src common glitch/src distortion/src wavetable/src dx/src granular/src physical/src additive/src groovebox/src robovox/src wobble/src pd/src tape/src freeze/src arp/src sampler/src bass/src)
    target_compile_definitions(TonwerkSynthTests PRIVATE
        ${TONWERK_DEFINITIONS}
        JucePlugin_Name="Tonwerk Synth"
        # Each plugin defines the factory a host calls; the tests make their processors themselves.
        CHROME_GLITCH_NO_ENTRY=1
        TONWERK_DISTORTION_NO_ENTRY=1
        TONWERK_WAVETABLE_NO_ENTRY=1
        TONWERK_DX_NO_ENTRY=1
        TONWERK_GRANULAR_NO_ENTRY=1
        TONWERK_PHYSICAL_NO_ENTRY=1
        TONWERK_ADDITIVE_NO_ENTRY=1
        TONWERK_GROOVEBOX_NO_ENTRY=1
        TONWERK_ROBOVOX_NO_ENTRY=1
        TONWERK_WOBBLE_NO_ENTRY=1
        TONWERK_PD_NO_ENTRY=1
        TONWERK_TAPE_NO_ENTRY=1
        TONWERK_FREEZE_NO_ENTRY=1
        TONWERK_ARP_NO_ENTRY=1
        TONWERK_SAMPLER_NO_ENTRY=1
        TONWERK_BASS_NO_ENTRY=1
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(TonwerkSynthTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME TonwerkSynthTests COMMAND TonwerkSynthTests)
endif()
