cmake_minimum_required(VERSION 3.25)
project(Transform VERSION 1.43.16 LANGUAGES C CXX)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../_Tools/JUCE/_9.0.4")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE 9.0.4 not found at: ${JUCE_DIR}")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(Transform
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "sl23"
    BUNDLE_ID "com.sl23.transform"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE sl23
    PLUGIN_CODE trf1
    FORMATS VST3
    VST3_CATEGORIES Fx
    PRODUCT_NAME "Transform")

# The logo is required; the background is optional and embedded at build time.
set(TRANSFORM_RESOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/../Resources/logo.svg")
set(TRANSFORM_BACKGROUND "${CMAKE_CURRENT_LIST_DIR}/../Resources/transform_background.jpg")
if(EXISTS "${TRANSFORM_BACKGROUND}")
    list(APPEND TRANSFORM_RESOURCE_FILES "${TRANSFORM_BACKGROUND}")
    target_compile_definitions(Transform PRIVATE TRANSFORM_HAS_BACKGROUND=1)
else()
    target_compile_definitions(Transform PRIVATE TRANSFORM_HAS_BACKGROUND=0)
endif()
juce_add_binary_data(TransformResources SOURCES ${TRANSFORM_RESOURCE_FILES})

juce_generate_juce_header(Transform)
target_sources(Transform PRIVATE
    PluginProcessor.cpp PluginProcessor.h TransformModules.h TransformTune.h TransformPitch.h TransformPitchRouting.h TransformTimePitch.h TransformHarmony.h TransformFreq.h TransformFreeze.h TransformFreezeCreative.h TransformMorph.h TransformVocoder.h TransformDeEss.h TransformChannel.h
    PluginEditor.cpp PluginEditor.h MidiLearnIcon.h
    TransformSequencer.h)
target_compile_features(Transform PRIVATE cxx_std_17)
target_compile_definitions(Transform PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Transform PRIVATE juce::juce_audio_utils TransformResources
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
           juce::juce_recommended_lto_flags)
