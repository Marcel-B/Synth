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

# Tonwerk Distortion: the circuit of BOSS's DS-1 as an audio effect (distortion/).
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

# Tonwerk Wavetable: a wavetable synthesizer for basses and leads, in the manner of Serum (wavetable/).
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

# Tonwerk DX: six operators and the DX7's 32 algorithms, with Tonwerk's delay and hall (dx/).
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
        tests/PresetTests.cpp
        tests/EffectPresetTests.cpp)
    target_include_directories(TonwerkSynthTests PRIVATE src common glitch/src distortion/src wavetable/src dx/src)
    target_compile_definitions(TonwerkSynthTests PRIVATE
        ${TONWERK_DEFINITIONS}
        JucePlugin_Name="Tonwerk Synth"
        # Each plugin defines the factory a host calls; the tests make their processors themselves.
        CHROME_GLITCH_NO_ENTRY=1
        TONWERK_DISTORTION_NO_ENTRY=1
        TONWERK_WAVETABLE_NO_ENTRY=1
        TONWERK_DX_NO_ENTRY=1
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(TonwerkSynthTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME TonwerkSynthTests COMMAND TonwerkSynthTests)
endif()
