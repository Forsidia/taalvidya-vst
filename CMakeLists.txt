cmake_minimum_required(VERSION 3.22)
project(TablaVST VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)

juce_add_plugin(TablaVST
    COMPANY_NAME "TablaVST"
    PLUGIN_MANUFACTURER_CODE Tbvs
    PLUGIN_CODE Tbla
    FORMATS Standalone VST3
    PRODUCT_NAME "Taal Vidya"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB BOL_WAVS CONFIGURE_DEPENDS Samples/*.wav)
juce_add_binary_data(TablaSamples SOURCES ${BOL_WAVS})
set_target_properties(TablaSamples PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_generate_juce_header(TablaVST)

target_sources(TablaVST PRIVATE
    Source/Taal.cpp
    Source/BuiltinTaals.cpp
    Source/BolLibrary.cpp
    Source/PluginProcessor.cpp
    Source/TaalView.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(TablaVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TablaVST PRIVATE
    TablaSamples
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
