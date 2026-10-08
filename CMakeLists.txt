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

juce_add_plugin(TonwerkSynth
    PRODUCT_NAME "Tonwerk Synth"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://github.com/Marcel-B/Synth"
    BUNDLE_ID de.bvelop.tonwerksynth
    PLUGIN_MANUFACTURER_CODE Bvlp
    PLUGIN_CODE Twsy
    FORMATS ${TONWERK_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    # deploy/install-macos.sh copies and signs; copying here would need write access to the plug-in folders.
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(TONWERK_SOURCES
    src/dsp/Defaults.cpp
    src/EffectsChain.cpp
    src/Parameters.cpp
    src/PatchJson.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/PresetLibrary.cpp)

set(TONWERK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(TonwerkSynth PRIVATE ${TONWERK_SOURCES})
target_compile_definitions(TonwerkSynth PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(TonwerkSynth
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

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
    glitch/src/GlitchProcessor.cpp)

target_sources(ChromeGlitch PRIVATE ${CHROME_GLITCH_SOURCES})
target_compile_definitions(ChromeGlitch PUBLIC ${TONWERK_DEFINITIONS})
target_link_libraries(ChromeGlitch
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# The tests compile the same sources into a console app and run the DSP, the JSON and the parameters without a host.
option(TONWERK_TESTS "Build the tests" ON)
if(TONWERK_TESTS)
    enable_testing()
    juce_add_console_app(TonwerkSynthTests PRODUCT_NAME "Tonwerk Synth Tests")
    target_sources(TonwerkSynthTests PRIVATE
        ${TONWERK_SOURCES}
        tests/Main.cpp
        tests/DspTests.cpp
        tests/PatchTests.cpp
        tests/ProcessorTests.cpp
        ${CHROME_GLITCH_SOURCES}
        tests/GlitchTests.cpp)
    target_include_directories(TonwerkSynthTests PRIVATE src glitch/src)
    target_compile_definitions(TonwerkSynthTests PRIVATE
        ${TONWERK_DEFINITIONS}
        JucePlugin_Name="Tonwerk Synth"
        # Both plugins define the factory a host calls; the tests make their processors themselves.
        CHROME_GLITCH_NO_ENTRY=1
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(TonwerkSynthTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME TonwerkSynthTests COMMAND TonwerkSynthTests)
endif()
