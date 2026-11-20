# The kit's CMake side: one call per machine plugin.
#
#   add_machine_plugin(<target>
#       PRODUCT_NAME "..."  [PLUGIN_NAME "..."]  COMPANY_NAME "..."  BUNDLE_ID "..."
#                                                        # PRODUCT_NAME names the files (no spaces);
#                                                        # PLUGIN_NAME is what hosts show
#       MANUFACTURER_CODE Abcd  PLUGIN_CODE Abcd          # hosts key saved sessions on these
#       VST3_CATEGORIES Fx Reverb
#       SOURCES machine.cpp ...                           # must include KIT_DEFINE_PLUGIN(...)
#       [INCLUDE_DIRS ...]  [LINK ...])
#
# Gives the plugin (AU, VST3, Standalone on macOS; VST3, Standalone elsewhere), ad-hoc signed on
# macOS, and <target>_editor_shot (not shipped): the editor offscreen plus a state round trip.
# The including project sets C++20, the architectures and fetches JUCE (top-level CMakeLists.txt).

include(${CMAKE_CURRENT_LIST_DIR}/sign_bundle.cmake)
set(KIT_JUCE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(add_machine_plugin target)
    cmake_parse_arguments(A "" "PRODUCT_NAME;PLUGIN_NAME;COMPANY_NAME;BUNDLE_ID;MANUFACTURER_CODE;PLUGIN_CODE"
                          "VST3_CATEGORIES;SOURCES;INCLUDE_DIRS;LINK" ${ARGN})
    if(NOT A_PLUGIN_NAME)
        set(A_PLUGIN_NAME "${A_PRODUCT_NAME}")
    endif()
    if(APPLE)
        set(formats AU VST3 Standalone)
    else()
        set(formats VST3 Standalone)
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "${A_PRODUCT_NAME}"
        PLUGIN_NAME "${A_PLUGIN_NAME}"
        COMPANY_NAME "${A_COMPANY_NAME}"
        BUNDLE_ID "${A_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE ${A_MANUFACTURER_CODE}
        PLUGIN_CODE ${A_PLUGIN_CODE}
        FORMATS ${formats}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES ${A_VST3_CATEGORIES}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "The effect processes the audio input."
        COPY_PLUGIN_AFTER_BUILD FALSE)
    kit_sign_bundles(${target})
    target_sources(${target} PRIVATE
        ${KIT_JUCE_DIR}/kit_processor.cpp
        ${KIT_JUCE_DIR}/kit_editor.cpp
        ${KIT_JUCE_DIR}/kit_panel.cpp
        ${A_SOURCES})
    target_include_directories(${target} PRIVATE ${KIT_JUCE_DIR} ${A_INCLUDE_DIRS})
    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_STANDALONE_PLUGIN_AUTO_OPEN_AUDIO_INPUT=1)
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils ${A_LINK}
        PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(${target}_editor_shot PRODUCT_NAME "${target}_editor_shot")
    target_sources(${target}_editor_shot PRIVATE ${KIT_JUCE_DIR}/kit_editor_shot.cpp)
    target_link_libraries(${target}_editor_shot PRIVATE ${target} juce::juce_audio_utils)
endfunction()
