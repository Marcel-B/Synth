cmake_minimum_required(VERSION 3.22)

# A release passes its tag's version (-DTONWERK_VERSION=0.2.0); Logic takes a higher version as a new plugin.
set(TONWERK_VERSION "0.3.0" CACHE STRING "Plugin version")
project(TonwerkSynth VERSION ${TONWERK_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # The iPad (scripts/build-ios.sh): every iPad that runs iPadOS 15 has an arm64 chip, as do the simulators on
    # Apple Silicon.
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "")
else()
    # One build for Apple Silicon and Intel Macs, from macOS 11 on.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Link-time optimisation for the plugins people install. CI turns it off (-DTONWERK_LTO=OFF): with LTO the compiler
# only stores bitcode and all code is generated again at every link, which ccache cannot keep, so each CI run took
# as long as a fresh build. The release builds and tests with LTO. GCC gets -flto=auto so the link-time code
# generation uses every core instead of running its jobs one after the other.
option(TONWERK_LTO "Link-time optimisation in Release builds" ON)
add_library(tonwerk_lto_flags INTERFACE)
if(TONWERK_LTO)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tonwerk_lto_flags INTERFACE $<$<CONFIG:Release>:-flto=auto>)
        target_link_options(tonwerk_lto_flags INTERFACE $<$<CONFIG:Release>:-flto=auto>)
    else()
        target_link_libraries(tonwerk_lto_flags INTERFACE juce::juce_recommended_lto_flags)
    endif()
endif()

# Audio Units exist only on the Mac; elsewhere the VST3 and the standalone app are built, which is enough for the tests.
# On the iPad every plugin is an AUv3 inside the one Tonwerk app (app/, below; Logic Pro for iPad, GarageBand, AUM load
# the AUv3s, the app plays them on its own). iOS needs the Xcode generator (its SDK and the app extension's embedding come only with Xcode); the Mac's own
# build stays on the Command Line Tools.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TONWERK_FORMATS AUv3)
elseif(APPLE)
    set(TONWERK_FORMATS AU VST3 Standalone)
else()
    set(TONWERK_FORMATS VST3 Standalone)
endif()

# The iPad apps: iPad only (the editors are too wide for a phone), landscape, full screen, playing on in the background
# so a key on a MIDI keyboard still sounds when another app is in front. Elsewhere JUCE ignores these.
set(TONWERK_IOS_OPTIONS
    TARGETED_DEVICE_FAMILY 2
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    REQUIRES_FULL_SCREEN TRUE
    STATUS_BAR_HIDDEN TRUE
    BACKGROUND_AUDIO_ENABLED TRUE)

# An app extension's bundle id must be its app's plus one more part, so on the iPad the plugins' ids sit under the
# app's (de.bvelop.tonwerk.tonwerkdx, which the AUv3 takes as it is, below). The Mac keeps its ids.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TONWERK_BUNDLE_PREFIX de.bvelop.tonwerk)
    # TestFlight takes every upload only with a build number of its own (CFBundleVersion); the app and its
    # extensions must carry the same one. The upload workflow passes its run number.
    set(TONWERK_BUILD_NUMBER "${TONWERK_VERSION}" CACHE STRING "CFBundleVersion of the iPad app and its AUv3s")
    list(APPEND TONWERK_IOS_OPTIONS BUILD_VERSION "${TONWERK_BUILD_NUMBER}")
else()
    set(TONWERK_BUNDLE_PREFIX de.bvelop)
endif()

# The effects' standalone apps read the audio interface's inputs. macOS hands an app without a microphone usage text in
# its Info.plist nothing but silence there, without asking (the plugins live in the host, which has asked already).
set(TONWERK_INPUT_PERMISSION
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Liest die Eingänge des Audio-Interfaces, um sie zu bearbeiten oder anzuzeigen.")

set(TONWERK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# The Night City look the plugins share: colours, panels, knobs, the oscilloscope (common/).
set(TONWERK_UI_SOURCES common/NightCity.cpp common/MidiLearn.cpp)

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
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/${target}.png"
        COMPANY_NAME "b-velop"
        COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
        BUNDLE_ID ${bundle}
        PLUGIN_MANUFACTURER_CODE Bvlp
        PLUGIN_CODE ${code}
        FORMATS ${TONWERK_FORMATS}
        ${TONWERK_IOS_OPTIONS}
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
        PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)
endfunction()

tonwerk_add_synth(TonwerkAnalog "Tonwerk Analog" ${TONWERK_BUNDLE_PREFIX}.tonwerkanalog TwAn analog)
tonwerk_add_synth(TonwerkFM "Tonwerk FM" ${TONWERK_BUNDLE_PREFIX}.tonwerkfm TwFm fm)
tonwerk_add_synth(TonwerkSynth "Tonwerk Synth" ${TONWERK_BUNDLE_PREFIX}.tonwerksynth Twsy combined)

# Chrome Glitch: an audio effect from the same repository, for a voice whose speech chrome fails (glitch/).
juce_add_plugin(ChromeGlitch
    PRODUCT_NAME "Chrome Glitch"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/ChromeGlitch.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.chromeglitch
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE ChGl
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Distortion: a classic guitar distortion pedal's circuit as an audio effect (distortion/).
juce_add_plugin(TonwerkDistortion
    PRODUCT_NAME "Tonwerk Distortion"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkDistortion.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkdistortion
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwDs
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Wavetable: a wavetable synthesizer for basses and leads (wavetable/).
juce_add_plugin(TonwerkWavetable
    PRODUCT_NAME "Tonwerk Wavetable"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkWavetable.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkwavetable
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwWt
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk DX: six-operator FM with 32 algorithms, with Tonwerk's delay and hall (dx/).
juce_add_plugin(TonwerkDX
    PRODUCT_NAME "Tonwerk DX"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkDX.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkdx
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwDx
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Granular: clouds of grains from its own sources or a file of the user's, with Tonwerk's delay and hall (granular/).
juce_add_plugin(TonwerkGranular
    PRODUCT_NAME "Tonwerk Granular"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkGranular.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkgranular
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwGr
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Physical: strings, tubes and struck bodies built from their physics, with Tonwerk's delay and hall (physical/).
juce_add_plugin(TonwerkPhysical
    PRODUCT_NAME "Tonwerk Physical"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkPhysical.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkphysical
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwPh
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Additiv: sounds built from up to 64 sine partials, with Tonwerk's delay and hall (additive/).
juce_add_plugin(TonwerkAdditiv
    PRODUCT_NAME "Tonwerk Additiv"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkAdditiv.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkadditiv
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwAd
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Groovebox: a drum machine of synthesised drums with a step sequencer in the host's tempo (groovebox/).
juce_add_plugin(TonwerkGroovebox
    PRODUCT_NAME "Tonwerk Groovebox"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkGroovebox.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkgroovebox
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwGb
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Robovox: a voice sung through a synth, the robot voice of AI songs (robovox/). A music effect (aumf), so
# Logic can send it MIDI notes for its carrier through the side chain.
juce_add_plugin(TonwerkRobovox
    PRODUCT_NAME "Tonwerk Robovox"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkRobovox.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkrobovox
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwRv
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Wobble: a filter and a volume shaper moved by drawn, tempo-synced LFO curves, for dubstep wobbles (wobble/).
juce_add_plugin(TonwerkWobble
    PRODUCT_NAME "Tonwerk Wobble"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkWobble.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkwobble
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwWb
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk PD: phase distortion as on Casio's CZ synths, with a chorus and Tonwerk's delay and hall (pd/).
juce_add_plugin(TonwerkPD
    PRODUCT_NAME "Tonwerk PD"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkPD.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkpd
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwPd
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Tape: a worn tape machine as an audio effect, saturation, wow and flutter, hiss, dropouts (tape/).
juce_add_plugin(TonwerkTape
    PRODUCT_NAME "Tonwerk Tape"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkTape.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerktape
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwTp
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Freeze: a spectral freeze that holds a moment of the input as an endless pad (freeze/).
juce_add_plugin(TonwerkFreeze
    PRODUCT_NAME "Tonwerk Freeze"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkFreeze.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkfreeze
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwFz
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Arp: an arpeggiator and step sequencer for the synths (arp/). A MIDI effect (aumi), the kind Logic offers in
# a software instrument track's MIDI FX slot; no audio buses.
juce_add_plugin(TonwerkArp
    PRODUCT_NAME "Tonwerk Arp"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkArp.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkarp
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwAr
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Sampler: a sampler for chromatic samples, loops and slices, from sounds built in or the user's own file (sampler/).
juce_add_plugin(TonwerkSampler
    PRODUCT_NAME "Tonwerk Sampler"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkSampler.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerksampler
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwSm
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Bass: an acid bass in the manner of the TB-303, one voice with slide and accent, with Tonwerk's delay and hall (bass/).
juce_add_plugin(TonwerkBass
    PRODUCT_NAME "Tonwerk Bass"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkBass.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkbass
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwBs
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
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
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Vocoder: a classic channel vocoder, the voice on the side chain and an eight-voice synth played by MIDI as the carrier (vocoder/).
juce_add_plugin(TonwerkVocoder
    PRODUCT_NAME "Tonwerk Vocoder"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkVocoder.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkvocoder
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwVc
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_VOCODER_SOURCES
    vocoder/src/VocoderEditor.cpp
    vocoder/src/VocoderPresets.cpp
    vocoder/src/VocoderProcessor.cpp)

target_sources(TonwerkVocoder PRIVATE ${TONWERK_VOCODER_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkVocoder PRIVATE src common)
target_compile_definitions(TonwerkVocoder PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkVocoder
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Delay: ping-pong, stereo or mono echoes in the host tempo with filters and saturation in the feedback and ducking (delay/).
juce_add_plugin(TonwerkDelay
    PRODUCT_NAME "Tonwerk Delay"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkDelay.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkdelay
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwDl
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Delay
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_DELAY_SOURCES
    delay/src/DelayEditor.cpp
    delay/src/DelayPresets.cpp
    delay/src/DelayProcessor.cpp)

target_sources(TonwerkDelay PRIVATE ${TONWERK_DELAY_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkDelay PRIVATE src common)
target_compile_definitions(TonwerkDelay PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkDelay
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Chorus: chorus, ensemble, flanger and phaser on one LFO, free or in the host tempo (chorus/).
juce_add_plugin(TonwerkChorus
    PRODUCT_NAME "Tonwerk Chorus"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkChorus.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkchorus
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwCh
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Modulation
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_CHORUS_SOURCES
    chorus/src/ChorusEditor.cpp
    chorus/src/ChorusPresets.cpp
    chorus/src/ChorusProcessor.cpp)

target_sources(TonwerkChorus PRIVATE ${TONWERK_CHORUS_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkChorus PRIVATE src common)
target_compile_definitions(TonwerkChorus PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkChorus
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Glue: a bus compressor with a side chain and a pump in the host tempo (glue/).
juce_add_plugin(TonwerkGlue
    PRODUCT_NAME "Tonwerk Glue"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkGlue.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkglue
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwGl
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_GLUE_SOURCES
    glue/src/GlueEditor.cpp
    glue/src/GluePresets.cpp
    glue/src/GlueProcessor.cpp)

target_sources(TonwerkGlue PRIVATE ${TONWERK_GLUE_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkGlue PRIVATE src common)
target_compile_definitions(TonwerkGlue PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkGlue
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Shimmer: a reverb whose tail rises by an interval on each round trip (shimmer/).
juce_add_plugin(TonwerkShimmer
    PRODUCT_NAME "Tonwerk Shimmer"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkShimmer.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkshimmer
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwSh
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_SHIMMER_SOURCES
    shimmer/src/ShimmerEditor.cpp
    shimmer/src/ShimmerPresets.cpp
    shimmer/src/ShimmerProcessor.cpp)

target_sources(TonwerkShimmer PRIVATE ${TONWERK_SHIMMER_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkShimmer PRIVATE src common)
target_compile_definitions(TonwerkShimmer PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkShimmer
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Filter: a multimode filter moved by an envelope follower and a tempo-synced LFO (filter/).
juce_add_plugin(TonwerkFilter
    PRODUCT_NAME "Tonwerk Filter"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkFilter.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkfilter
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwFl
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Filter
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_FILTER_SOURCES
    filter/src/FilterEditor.cpp
    filter/src/FilterPresets.cpp
    filter/src/FilterProcessor.cpp)

target_sources(TonwerkFilter PRIVATE ${TONWERK_FILTER_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
# Borrows the divisions and the state variable filter from Tonwerk Wobble.
target_include_directories(TonwerkFilter PRIVATE common wobble/src)
target_compile_definitions(TonwerkFilter PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkFilter
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Neuro: a mono bass for Reese and neuro sounds, detuned saws, FM and a clean sub, wobbled in tempo (neuro/).
juce_add_plugin(TonwerkNeuro
    PRODUCT_NAME "Tonwerk Neuro"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkNeuro.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkneuro
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwNr
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_NEURO_SOURCES
    neuro/src/NeuroEditor.cpp
    neuro/src/NeuroPresets.cpp
    neuro/src/NeuroProcessor.cpp)

target_sources(TonwerkNeuro PRIVATE ${TONWERK_NEURO_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkNeuro PRIVATE src common wavetable/src bass/src)
target_compile_definitions(TonwerkNeuro PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkNeuro
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Stutter: a beat repeat on the song's grid, its repeats pitched, reversed, faded and filtered (stutter/).
juce_add_plugin(TonwerkStutter
    PRODUCT_NAME "Tonwerk Stutter"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkStutter.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkstutter
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwSt
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_STUTTER_SOURCES
    stutter/src/StutterEditor.cpp
    stutter/src/StutterPresets.cpp
    stutter/src/StutterProcessor.cpp)

target_sources(TonwerkStutter PRIVATE ${TONWERK_STUTTER_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
# Borrows the divisions and the state variable filter from Tonwerk Wobble.
target_include_directories(TonwerkStutter PRIVATE common wobble/src)
target_compile_definitions(TonwerkStutter PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkStutter
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Multiband: a three-band compressor, downward and upward (multiband/).
juce_add_plugin(TonwerkMultiband
    PRODUCT_NAME "Tonwerk Multiband"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkMultiband.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkmultiband
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwMb
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_MULTIBAND_SOURCES
    multiband/src/MultibandEditor.cpp
    multiband/src/MultibandPresets.cpp
    multiband/src/MultibandProcessor.cpp)

target_sources(TonwerkMultiband PRIVATE ${TONWERK_MULTIBAND_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
# Borrows the state variable filter from Tonwerk Wobble.
target_include_directories(TonwerkMultiband PRIVATE common wobble/src)
target_compile_definitions(TonwerkMultiband PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkMultiband
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Zone: a compressor on one range of the spectrum, a dynamic EQ with a side chain (zone/).
juce_add_plugin(TonwerkZone
    PRODUCT_NAME "Tonwerk Zone"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkZone.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkzone
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwZn
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Dynamics EQ
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_ZONE_SOURCES
    zone/src/ZoneEditor.cpp
    zone/src/ZonePresets.cpp
    zone/src/ZoneProcessor.cpp)

target_sources(TonwerkZone PRIVATE ${TONWERK_ZONE_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
# Borrows the state variable filter from Tonwerk Wobble.
target_include_directories(TonwerkZone PRIVATE common wobble/src filter/src)
target_compile_definitions(TonwerkZone PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkZone
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Scope: an oscilloscope that passes the audio through untouched (scope/).
juce_add_plugin(TonwerkScope
    PRODUCT_NAME "Tonwerk Scope"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkScope.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkscope
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwSc
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    ${TONWERK_INPUT_PERMISSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_SCOPE_SOURCES
    scope/src/ScopeEditor.cpp
    scope/src/ScopePresets.cpp
    scope/src/ScopeProcessor.cpp)

target_sources(TonwerkScope PRIVATE ${TONWERK_SCOPE_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkScope PRIVATE common)
target_compile_definitions(TonwerkScope PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkScope
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk Expander: eight voices of two oscillators into a 12 dB filter from lowpass through notch to highpass (expander/).
juce_add_plugin(TonwerkExpander
    PRODUCT_NAME "Tonwerk Expander"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkExpander.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerkexpander
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwEx
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_EXPANDER_SOURCES
    expander/src/ExpanderEditor.cpp
    expander/src/ExpanderPresets.cpp
    expander/src/ExpanderProcessor.cpp)

target_sources(TonwerkExpander PRIVATE ${TONWERK_EXPANDER_SOURCES} src/EffectsChain.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkExpander PRIVATE src common wavetable/src)
target_compile_definitions(TonwerkExpander PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkExpander
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk LFO: an LFO that sends a MIDI controller to the instrument after it, whose knob learnt it (lfo/). A MIDI
# effect (aumi) like Tonwerk Arp; no audio buses.
juce_add_plugin(TonwerkLFO
    PRODUCT_NAME "Tonwerk LFO"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/TonwerkLFO.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID ${TONWERK_BUNDLE_PREFIX}.tonwerklfo
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE TwLf
    FORMATS ${TONWERK_FORMATS}
    ${TONWERK_IOS_OPTIONS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_LFO_SOURCES
    lfo/src/LfoEditor.cpp
    lfo/src/LfoPresets.cpp
    lfo/src/LfoProcessor.cpp)

target_sources(TonwerkLFO PRIVATE ${TONWERK_LFO_SOURCES} common/EffectPresets.cpp ${TONWERK_UI_SOURCES})
target_include_directories(TonwerkLFO PRIVATE common wobble/src filter/src)
target_compile_definitions(TonwerkLFO PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkLFO
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

# Tonwerk, the iPad app (app/): every plugin's AUv3 inside one app, which also plays the instruments and effects on its
# own from the same sources the tests compile. It builds on the desktop too (-DTONWERK_APP=ON) to try it there.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TONWERK_APP_DEFAULT ON)
else()
    set(TONWERK_APP_DEFAULT OFF)
endif()
option(TONWERK_APP "Build the Tonwerk app with every plugin" ${TONWERK_APP_DEFAULT})
set(TONWERK_ALL_SOURCES
    ${TONWERK_SOURCES}
    ${TONWERK_UI_SOURCES}
    common/EffectPresets.cpp
    ${CHROME_GLITCH_SOURCES}
    ${TONWERK_DISTORTION_SOURCES}
    ${TONWERK_WAVETABLE_SOURCES}
    ${TONWERK_DX_SOURCES}
    ${TONWERK_GRANULAR_SOURCES}
    ${TONWERK_PHYSICAL_SOURCES}
    ${TONWERK_ADDITIVE_SOURCES}
    ${TONWERK_GROOVEBOX_SOURCES}
    ${TONWERK_ROBOVOX_SOURCES}
    ${TONWERK_WOBBLE_SOURCES}
    ${TONWERK_PD_SOURCES}
    ${TONWERK_TAPE_SOURCES}
    ${TONWERK_FREEZE_SOURCES}
    ${TONWERK_SAMPLER_SOURCES}
    ${TONWERK_BASS_SOURCES}
    ${TONWERK_VOCODER_SOURCES}
    ${TONWERK_DELAY_SOURCES}
    ${TONWERK_CHORUS_SOURCES}
    ${TONWERK_GLUE_SOURCES}
    ${TONWERK_SHIMMER_SOURCES}
    ${TONWERK_FILTER_SOURCES}
    ${TONWERK_NEURO_SOURCES}
    ${TONWERK_STUTTER_SOURCES}
    ${TONWERK_MULTIBAND_SOURCES}
    ${TONWERK_ZONE_SOURCES}
    ${TONWERK_SCOPE_SOURCES}
    ${TONWERK_EXPANDER_SOURCES})
set(TONWERK_ALL_INCLUDES src common glitch/src distortion/src wavetable/src dx/src granular/src physical/src additive/src
    groovebox/src robovox/src wobble/src pd/src tape/src freeze/src arp/src sampler/src bass/src vocoder/src delay/src
    chorus/src glue/src shimmer/src filter/src neuro/src stutter/src multiband/src zone/src scope/src expander/src lfo/src)
# Each plugin defines the factory a host calls; the app and the tests make their processors themselves.
set(TONWERK_NO_ENTRIES
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
    TONWERK_VOCODER_NO_ENTRY=1
    TONWERK_DELAY_NO_ENTRY=1
    TONWERK_CHORUS_NO_ENTRY=1
    TONWERK_GLUE_NO_ENTRY=1
    TONWERK_SHIMMER_NO_ENTRY=1
    TONWERK_FILTER_NO_ENTRY=1
    TONWERK_NEURO_NO_ENTRY=1
    TONWERK_STUTTER_NO_ENTRY=1
    TONWERK_MULTIBAND_NO_ENTRY=1
    TONWERK_ZONE_NO_ENTRY=1
    TONWERK_SCOPE_NO_ENTRY=1
    TONWERK_EXPANDER_NO_ENTRY=1
    TONWERK_LFO_NO_ENTRY=1)

if(TONWERK_APP)
    juce_add_gui_app(Tonwerk
        PRODUCT_NAME "Tonwerk"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/Tonwerk.png"
        COMPANY_NAME "b-velop"
        COMPANY_WEBSITE "https://tonwerk.studio"
        BUNDLE_ID de.bvelop.tonwerk
        ${TONWERK_IOS_OPTIONS}
        ${TONWERK_INPUT_PERMISSION}
        BLUETOOTH_PERMISSION_ENABLED TRUE
        BLUETOOTH_PERMISSION_TEXT "Verbindet MIDI-Keyboards über Bluetooth."
        # No encryption beyond the system's, so TestFlight asks no export questions.
        PLIST_TO_MERGE "<plist><dict><key>ITSAppUsesNonExemptEncryption</key><false/></dict></plist>")
    target_sources(Tonwerk PRIVATE app/src/TonwerkApp.cpp app/src/Catalogue.cpp ${TONWERK_ALL_SOURCES})
    target_include_directories(Tonwerk PRIVATE ${TONWERK_ALL_INCLUDES})
    target_compile_definitions(Tonwerk PRIVATE
        ${TONWERK_DEFINITIONS}
        ${TONWERK_NO_ENTRIES}
        JUCE_APPLICATION_NAME_STRING="Tonwerk"
        JUCE_APPLICATION_VERSION_STRING="${TONWERK_VERSION}")
    target_link_libraries(Tonwerk
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags tonwerk_lto_flags juce::juce_recommended_warning_flags)

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # Every plugin's AUv3 goes into the app's PlugIns folder.
        set(tonwerk_extensions
            TonwerkAnalog_AUv3 TonwerkFM_AUv3 TonwerkSynth_AUv3 ChromeGlitch_AUv3 TonwerkDistortion_AUv3
            TonwerkWavetable_AUv3 TonwerkDX_AUv3 TonwerkGranular_AUv3 TonwerkPhysical_AUv3 TonwerkAdditiv_AUv3
            TonwerkGroovebox_AUv3 TonwerkRobovox_AUv3 TonwerkWobble_AUv3 TonwerkPD_AUv3 TonwerkTape_AUv3
            TonwerkFreeze_AUv3 TonwerkArp_AUv3 TonwerkSampler_AUv3 TonwerkBass_AUv3 TonwerkVocoder_AUv3
            TonwerkDelay_AUv3 TonwerkChorus_AUv3 TonwerkGlue_AUv3 TonwerkShimmer_AUv3 TonwerkFilter_AUv3
            TonwerkNeuro_AUv3 TonwerkStutter_AUv3 TonwerkMultiband_AUv3 TonwerkZone_AUv3 TonwerkScope_AUv3
            TonwerkExpander_AUv3 TonwerkLFO_AUv3)
        add_dependencies(Tonwerk ${tonwerk_extensions})
        # App Store Connect wants a privacy manifest in the app and in each extension.
        set(tonwerk_privacy "${CMAKE_CURRENT_SOURCE_DIR}/app/PrivacyInfo.xcprivacy")
        foreach(bundle Tonwerk ${tonwerk_extensions})
            target_sources(${bundle} PRIVATE "${tonwerk_privacy}")
            set_property(TARGET ${bundle} APPEND PROPERTY RESOURCE "${tonwerk_privacy}")
        endforeach()
        # JUCE names an AUv3 <plugin's id>.<its last part>AUv3, two parts past the app's id; App Store Connect takes
        # only one (ITMS-90347), so each extension gets its plugin's id, de.bvelop.tonwerk.tonwerkdx.
        foreach(extension ${tonwerk_extensions})
            string(REGEX REPLACE "_AUv3$" "" plugin "${extension}")
            get_target_property(plugin_id ${plugin} JUCE_BUNDLE_ID)
            set_target_properties(${extension} PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${plugin_id}")
        endforeach()
        set_target_properties(Tonwerk PROPERTIES
            XCODE_EMBED_APP_EXTENSIONS "${tonwerk_extensions}"
            XCODE_EMBED_APP_EXTENSIONS_REMOVE_HEADERS_ON_COPY ON
            XCODE_EMBED_APP_EXTENSIONS_CODE_SIGN_ON_COPY ON
            # The app is what an archive for TestFlight holds, even when the upload sets SKIP_INSTALL for the rest.
            XCODE_ATTRIBUTE_SKIP_INSTALL NO
            XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)")
    endif()
endif()

# The tests compile the same sources into a console app and run the DSP, the JSON and the parameters without a host.
option(TONWERK_TESTS "Build the tests" ON)
# The tests run on the Mac and Linux; the iPad build has the same DSP.
if(TONWERK_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
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
        ${TONWERK_VOCODER_SOURCES}
        tests/VocoderTests.cpp
        ${TONWERK_DELAY_SOURCES}
        tests/DelayTests.cpp
        ${TONWERK_CHORUS_SOURCES}
        tests/ChorusTests.cpp
        ${TONWERK_GLUE_SOURCES}
        tests/GlueTests.cpp
        ${TONWERK_SHIMMER_SOURCES}
        tests/ShimmerTests.cpp
        ${TONWERK_FILTER_SOURCES}
        tests/FilterTests.cpp
        ${TONWERK_NEURO_SOURCES}
        tests/NeuroTests.cpp
        ${TONWERK_STUTTER_SOURCES}
        tests/StutterTests.cpp
        ${TONWERK_MULTIBAND_SOURCES}
        tests/MultibandTests.cpp
        ${TONWERK_ZONE_SOURCES}
        tests/ZoneTests.cpp
        ${TONWERK_SCOPE_SOURCES}
        tests/ScopeTests.cpp
        ${TONWERK_EXPANDER_SOURCES}
        tests/ExpanderTests.cpp
        ${TONWERK_LFO_SOURCES}
        tests/LfoTests.cpp
        tests/MidiLearnTests.cpp
        tests/PresetTests.cpp
        tests/EffectPresetTests.cpp)
    target_include_directories(TonwerkSynthTests PRIVATE ${TONWERK_ALL_INCLUDES})
    target_compile_definitions(TonwerkSynthTests PRIVATE
        ${TONWERK_DEFINITIONS}
        JucePlugin_Name="Tonwerk Synth"
        ${TONWERK_NO_ENTRIES}
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(TonwerkSynthTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME TonwerkSynthTests COMMAND TonwerkSynthTests)
endif()
