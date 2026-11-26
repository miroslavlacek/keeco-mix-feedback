cmake_minimum_required(VERSION 3.22)
project(MixSight VERSION 0.5.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: downloaded automatically, or point KMF_JUCE_DIR at a local copy.
set(KMF_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(KMF_JUCE_DIR)
    add_subdirectory(${KMF_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# Manufacturer/plugin codes stay as in the "Keeco Mix Feedback" days so saved projects still find the plugin.
juce_add_plugin(MixSight
    COMPANY_NAME "MixSight"
    PLUGIN_MANUFACTURER_CODE Keco
    PLUGIN_CODE Kmfb
    PRODUCT_NAME "MixSight"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Analyzer Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MixSight PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AiChat.cpp
    Source/AiSetup.cpp
    Source/Updater.cpp)

target_include_directories(MixSight PRIVATE Source)

target_compile_definitions(MixSight PUBLIC
    MIXSIGHT_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MixSight
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# MCP bridge for Antigravity / Claude desktop (stdio server that talks to the plugin on 127.0.0.1).
juce_add_console_app(MixSightBridge PRODUCT_NAME "MixSightBridge")
target_sources(MixSightBridge PRIVATE bridge/BridgeMain.cpp)
target_include_directories(MixSightBridge PRIVATE Source)
target_compile_definitions(MixSightBridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 MIXSIGHT_VERSION="${PROJECT_VERSION}")
target_link_libraries(MixSightBridge PRIVATE juce::juce_core juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Helper used by the installer: registers the bridge in Antigravity, opens the sign-in window.
juce_add_console_app(MixSightHelper PRODUCT_NAME "MixSightHelper")
target_sources(MixSightHelper PRIVATE bridge/HelperMain.cpp Source/AiSetup.cpp)
target_include_directories(MixSightHelper PRIVATE Source)
target_compile_definitions(MixSightHelper PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 MIXSIGHT_VERSION="${PROJECT_VERSION}")
target_link_libraries(MixSightHelper PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# DSP unit tests (no JUCE needed): ctest after building.
enable_testing()
add_executable(kmf_dsp_tests tests/dsp_tests.cpp)
target_include_directories(kmf_dsp_tests PRIVATE Source)
add_test(NAME dsp COMMAND kmf_dsp_tests)

# Optional headless end-to-end check (Linux/CI): -DKMF_BUILD_HARNESS=ON
option(KMF_BUILD_HARNESS "Build the headless plugin harness" OFF)
if(KMF_BUILD_HARNESS)
    juce_add_console_app(kmf_plugin_harness PRODUCT_NAME "kmf_plugin_harness")
    target_sources(kmf_plugin_harness PRIVATE tests/plugin_harness.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AiChat.cpp Source/AiSetup.cpp Source/Updater.cpp)
    target_include_directories(kmf_plugin_harness PRIVATE Source)
    target_compile_definitions(kmf_plugin_harness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="MixSight" JUCE_MODAL_LOOPS_PERMITTED=1 MIXSIGHT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(kmf_plugin_harness PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
endif()

# AI chat tests: stream-json parsing + process runner against a fake agy.
juce_add_console_app(kmf_fake_agy PRODUCT_NAME "kmf_fake_agy")
target_sources(kmf_fake_agy PRIVATE tests/fake_agy.cpp)
target_link_libraries(kmf_fake_agy PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(kmf_chat_tests PRODUCT_NAME "kmf_chat_tests")
target_sources(kmf_chat_tests PRIVATE tests/chat_tests.cpp Source/AiChat.cpp Source/AiSetup.cpp)
target_include_directories(kmf_chat_tests PRIVATE Source)
target_compile_definitions(kmf_chat_tests PRIVATE MIXSIGHT_VERSION="${PROJECT_VERSION}")
target_link_libraries(kmf_chat_tests PRIVATE juce::juce_core juce::juce_events PUBLIC juce::juce_recommended_config_flags)

# Update tests: version compare, release parsing, download + verify + unpack from a local HTTP server.
juce_add_console_app(kmf_update_tests PRODUCT_NAME "kmf_update_tests")
target_sources(kmf_update_tests PRIVATE tests/update_tests.cpp Source/Updater.cpp)
target_include_directories(kmf_update_tests PRIVATE Source)
target_compile_definitions(kmf_update_tests PRIVATE MIXSIGHT_VERSION="0.5.0" JUCE_USE_CURL=0)
target_link_libraries(kmf_update_tests PRIVATE juce::juce_core juce::juce_cryptography PUBLIC juce::juce_recommended_config_flags)
