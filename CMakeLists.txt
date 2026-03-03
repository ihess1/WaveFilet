cmake_minimum_required(VERSION 3.22)
project(WaveFilet VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is included as a git submodule at deps/JUCE.
# Run: git submodule update --init --recursive
add_subdirectory(deps/JUCE)

juce_add_plugin(WaveFilet
    PLUGIN_MANUFACTURER_CODE Wflt
    PLUGIN_CODE Wflt
    COMPANY_NAME "WaveFilet"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "WaveFilet")

target_sources(WaveFilet
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/WaveformComponent.cpp)

target_compile_definitions(WaveFilet
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(WaveFilet
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
