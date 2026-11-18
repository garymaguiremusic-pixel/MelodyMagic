cmake_minimum_required(VERSION 3.22)

project(MelodyMagic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(MELODYMAGIC_BUILD_PLUGIN      "Build the VST3 plugin (downloads JUCE)"                      ON)
option(MELODYMAGIC_BUILD_STANDALONE  "Also build a standalone app for testing"                     OFF)
option(MELODYMAGIC_BUILD_TESTS       "Build the core unit tests (downloads Catch2)"                ON)
option(MELODYMAGIC_COPY_AFTER_BUILD  "Install the plugin into the user plugin folder after building" OFF)
option(MELODYMAGIC_BUILD_SCREENSHOT  "Build a tool that renders the editor to a PNG (docs/screenshot.png)" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Build number shown in the plugin's bottom bar (the git commit), so it's clear which build is loaded
execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE MELODYMAGIC_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MELODYMAGIC_BUILD_ID)
    set(MELODYMAGIC_BUILD_ID "dev")
endif()

# ------------------------------------------------------------------------------
# Core: all musical logic, no JUCE (shared by the plugin and the tests)
add_library(mmcore STATIC
    core/src/ArpProcessor.cpp
    core/src/ClipPlayer.cpp
    core/src/Formats.cpp
    core/src/Json.cpp
    core/src/Library.cpp
    core/src/Melody.cpp
    core/src/MidiFile.cpp
    core/src/Pattern.cpp
    core/src/Scales.cpp
    core/src/ValueTreeCodec.cpp)
target_include_directories(mmcore PUBLIC core/include)
set_target_properties(mmcore PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER Core)
if(MSVC)
    target_compile_options(mmcore PRIVATE /W4 /wd4244 /wd4267)
else()
    target_compile_options(mmcore PRIVATE -Wall -Wextra -Wno-sign-compare)
endif()

# ------------------------------------------------------------------------------
# Tests
if(MELODYMAGIC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    file(GLOB MELODYMAGIC_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(MelodyMagicTests ${MELODYMAGIC_TEST_SOURCES})
    target_link_libraries(MelodyMagicTests PRIVATE mmcore Catch2::Catch2WithMain)
    set_target_properties(MelodyMagicTests PROPERTIES FOLDER Tests)
    add_test(NAME MelodyMagicTests COMMAND MelodyMagicTests)
endif()

# ------------------------------------------------------------------------------
# Plugin
if(MELODYMAGIC_BUILD_PLUGIN)
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.4
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(MELODYMAGIC_FORMATS VST3)
    if(MELODYMAGIC_BUILD_STANDALONE)
        list(APPEND MELODYMAGIC_FORMATS Standalone)
    endif()

    # The VST3 manifest helper has to run on the build machine, which isn't possible
    # when cross-compiling.
    if(CMAKE_CROSSCOMPILING)
        set(MELODYMAGIC_VST3_MANIFEST FALSE)
    else()
        set(MELODYMAGIC_VST3_MANIFEST TRUE)
    endif()

    # A MIDI generator: it takes MIDI in and sends MIDI out. It registers as an instrument
    # (with a silent stereo output) because that is the slot every major host lets send
    # MIDI on to other tracks. Same manufacturer as Virus Delay and ArpSampler.
    juce_add_plugin(MelodyMagic
        PRODUCT_NAME                "MelodyMagic"
        COMPANY_NAME                "VirusDelay Project"
        BUNDLE_ID                   com.virusdelayproject.melodymagic
        PLUGIN_MANUFACTURER_CODE    Vdpj
        PLUGIN_CODE                 Mmgc
        FORMATS                     ${MELODYMAGIC_FORMATS}
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES             Instrument Generator
        VST3_AUTO_MANIFEST          ${MELODYMAGIC_VST3_MANIFEST}
        COPY_PLUGIN_AFTER_BUILD     ${MELODYMAGIC_COPY_AFTER_BUILD})

    set(MELODYMAGIC_PLUGIN_SOURCES
        plugin/Source/PluginProcessor.cpp
        plugin/Source/PluginEditor.cpp
        plugin/Source/FactoryPatterns.cpp
        plugin/Source/Shared/CustomLookAndFeel.cpp
        plugin/Source/Shared/VirusComponents.cpp
        plugin/Source/UI/PatternGrid.cpp
        plugin/Source/UI/LibraryPanel.cpp
        plugin/Source/UI/MidiMonitor.cpp
        plugin/Source/UI/DragMidiHandle.cpp)
    set(MELODYMAGIC_DEFINITIONS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        MELODYMAGIC_BUILD_ID="${MELODYMAGIC_BUILD_ID}")

    target_sources(MelodyMagic PRIVATE ${MELODYMAGIC_PLUGIN_SOURCES})
    target_include_directories(MelodyMagic PRIVATE plugin/Source)
    target_compile_definitions(MelodyMagic PUBLIC ${MELODYMAGIC_DEFINITIONS})

    target_link_libraries(MelodyMagic
        PRIVATE
            mmcore
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Windows release builds also write debug symbols to a separate .pdb (never shipped
    # inside the plugin), so crash dumps from hosts can be turned into readable stacks.
    if(MSVC)
        foreach(target MelodyMagic MelodyMagic_VST3 MelodyMagic_Standalone)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/Zi>")
                target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
            endif()
        endforeach()
    endif()

    if(MELODYMAGIC_BUILD_SCREENSHOT)
        juce_add_console_app(MelodyMagicScreenshot PRODUCT_NAME "MelodyMagicScreenshot")
        target_sources(MelodyMagicScreenshot PRIVATE tools/screenshot_main.cpp ${MELODYMAGIC_PLUGIN_SOURCES})
        target_include_directories(MelodyMagicScreenshot PRIVATE plugin/Source)
        target_compile_definitions(MelodyMagicScreenshot PRIVATE ${MELODYMAGIC_DEFINITIONS}
            JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="MelodyMagic")
        target_link_libraries(MelodyMagicScreenshot PRIVATE mmcore juce::juce_audio_utils juce::juce_recommended_config_flags)
    endif()
endif()
