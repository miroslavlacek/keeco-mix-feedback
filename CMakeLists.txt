cmake_minimum_required(VERSION 3.22)
project(KeecoMixFeedback VERSION 0.4.0 LANGUAGES C CXX)

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

juce_add_plugin(KeecoMixFeedback
    COMPANY_NAME "Keeco"
    PLUGIN_MANUFACTURER_CODE Keco
    PLUGIN_CODE Kmfb
    PRODUCT_NAME "Keeco Mix Feedback"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Analyzer Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KeecoMixFeedback PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AiChat.cpp)

target_include_directories(KeecoMixFeedback PRIVATE Source)

target_compile_definitions(KeecoMixFeedback PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(KeecoMixFeedback
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# MCP bridge for Antigravity / Claude desktop (stdio server that talks to the plugin on 127.0.0.1).
juce_add_console_app(KeecoMcpBridge PRODUCT_NAME "KeecoMcpBridge")
target_sources(KeecoMcpBridge PRIVATE bridge/BridgeMain.cpp)
target_compile_definitions(KeecoMcpBridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(KeecoMcpBridge PRIVATE juce::juce_core juce::juce_events
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
    target_sources(kmf_plugin_harness PRIVATE tests/plugin_harness.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AiChat.cpp)
    target_include_directories(kmf_plugin_harness PRIVATE Source)
    target_compile_definitions(kmf_plugin_harness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Keeco Mix Feedback" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(kmf_plugin_harness PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()

# AI chat tests: stream-json parsing + process runner against a fake agy.
juce_add_console_app(kmf_fake_agy PRODUCT_NAME "kmf_fake_agy")
target_sources(kmf_fake_agy PRIVATE tests/fake_agy.cpp)
target_link_libraries(kmf_fake_agy PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(kmf_chat_tests PRODUCT_NAME "kmf_chat_tests")
target_sources(kmf_chat_tests PRIVATE tests/chat_tests.cpp Source/AiChat.cpp)
target_include_directories(kmf_chat_tests PRIVATE Source)
target_link_libraries(kmf_chat_tests PRIVATE juce::juce_core juce::juce_events PUBLIC juce::juce_recommended_config_flags)
