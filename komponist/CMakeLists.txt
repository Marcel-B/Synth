cmake_minimum_required(VERSION 3.22)

# Tonwerk Komponist: a desktop app that has Claude write a piece from a description and saves it as a MIDI file with
# one track per instrument. A project of its own beside the plugins' (not in their releases or the iPad app) while it
# is a prototype; JUCE comes the same way and at the same tag as there.
project(TonwerkKomponist VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(KOMPONIST_SOURCES src/Arrangement.cpp src/Composer.cpp)
set(KOMPONIST_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:JUCE_VERSION>")

juce_add_gui_app(TonwerkKomponist
    PRODUCT_NAME "Tonwerk Komponist"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../icons/TonwerkKomponist.png"
    COMPANY_NAME "b-velop"
    COMPANY_WEBSITE "https://tonwerk.studio"
    BUNDLE_ID de.bvelop.tonwerk-komponist
    NEEDS_CURL TRUE)
target_sources(TonwerkKomponist PRIVATE src/Main.cpp ${KOMPONIST_SOURCES})
target_compile_definitions(TonwerkKomponist PRIVATE ${KOMPONIST_DEFINITIONS})
target_link_libraries(TonwerkKomponist
    PRIVATE juce::juce_gui_extra juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(KOMPONIST_TESTS "Build the tests" ON)
if(KOMPONIST_TESTS)
    enable_testing()
    juce_add_console_app(TonwerkKomponistTests PRODUCT_NAME "Tonwerk Komponist Tests" NEEDS_CURL TRUE)
    target_sources(TonwerkKomponistTests PRIVATE tests/KomponistTests.cpp ${KOMPONIST_SOURCES})
    target_include_directories(TonwerkKomponistTests PRIVATE src)
    target_compile_definitions(TonwerkKomponistTests PRIVATE ${KOMPONIST_DEFINITIONS})
    target_link_libraries(TonwerkKomponistTests
        PRIVATE juce::juce_core juce::juce_audio_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME TonwerkKomponistTests COMMAND TonwerkKomponistTests)
endif()
