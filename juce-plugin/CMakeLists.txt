# The plugin around the row machine and its 8080 host (macOS: AU, VST3,
# Standalone; Windows and Linux: VST3, Standalone). See README.md.
#
#   cmake -S . -B build-a -G Ninja \
#         -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   cmake --build build-a
#
# Without FETCHCONTENT_SOURCE_DIR_JUCE, JUCE 8.0.14 is fetched from GitHub.
cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# The plugin's identity, all in this one block ("Lexicon" is a trademark and
# is not used). Hosts identify a plugin by its codes and bundle ID: changing
# them later orphans saved sessions.
# Four-character codes: the manufacturer code needs at least one capital;
# ---------------------------------------------------------------------------
set(PLUGIN_PRODUCT_NAME "Reflexion224")
set(PLUGIN_COMPANY_NAME "Reflexion224")
set(PLUGIN_MANUFACTURER_CODE Rflx)
set(PLUGIN_CODE Rf24)
set(PLUGIN_BUNDLE_ID "net.joelanders.reflexion224")
set(PLUGIN_TARGET Reflexion224)

project(${PLUGIN_TARGET} VERSION 0.1.2 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Byte-exactness with the web build: no fused multiply-adds anywhere (the
# analog boards' filters are double-precision recurrences), and never
# -ffast-math. Applies to every target below, JUCE's included. MSVC's
# /fp:precise does not contract unless /fp:contract is also given (VS 2022);
# its <complex> and libm are not libc++'s, so Windows output can still differ
# in the last bits from the macOS and web builds.
if(MSVC)
    add_compile_options(/fp:precise /bigobj /utf-8)
    add_compile_definitions(_USE_MATH_DEFINES)   # M_PI
    # The static CRT: the plugin and the app need no VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-ffp-contract=off)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The machine: header-only (source/engine.hpp includes the row machine, the
# 8080 host and the analog boards from their own directories).
add_library(lexplug_engine INTERFACE)
target_include_directories(lexplug_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/../analog)
# Debug info in every build type, for the RT-allocation test's call sites.
target_compile_options(lexplug_engine INTERFACE $<IF:$<CXX_COMPILER_ID:MSVC>,/Z7,-g>)

# Profile-guided optimization (clang and clang-cl; scripts/pgo_train.sh writes the
# profile from the speed gate on your own ROMs). The machine is branchy
# interpreter code, and a profile is worth 8-17% of the block time. The
# profile names functions, never ROM bytes.
#   LEXPLUG_PGO_GENERATE=ON   instrument the machine's translation units
#   LEXPLUG_PGO_PROFILE=FILE  compile them with this merged .profdata
set(LEXPLUG_PGO_PROFILE "" CACHE FILEPATH "A merged clang .profdata for the machine (see scripts/pgo_train.sh)")
option(LEXPLUG_PGO_GENERATE "Instrument the machine for a PGO training run" OFF)
if(LEXPLUG_PGO_GENERATE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lexplug_engine INTERFACE -fprofile-instr-generate)
    target_link_options(lexplug_engine INTERFACE -fprofile-instr-generate)
elseif(LEXPLUG_PGO_PROFILE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(NOT EXISTS "${LEXPLUG_PGO_PROFILE}")
        message(FATAL_ERROR "LEXPLUG_PGO_PROFILE: no such file ${LEXPLUG_PGO_PROFILE}")
    endif()
    # The plugin's other code has no counts in the profile, and a profile from
    # a slightly older source still applies to the functions it knows.
    target_compile_options(lexplug_engine INTERFACE
        "-fprofile-instr-use=${LEXPLUG_PGO_PROFILE}"
        -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
    message(STATUS "PGO: the machine is compiled with ${LEXPLUG_PGO_PROFILE}")
elseif(LEXPLUG_PGO_PROFILE OR LEXPLUG_PGO_GENERATE)
    message(WARNING "PGO options are clang-only and ignored with ${CMAKE_CXX_COMPILER_ID} (MSVC: /GL /LTCG:PGI then /LTCG:PGO by hand)")
endif()

juce_add_plugin(${PLUGIN_TARGET}
    PRODUCT_NAME "${PLUGIN_PRODUCT_NAME}"
    COMPANY_NAME "${PLUGIN_COMPANY_NAME}"
    BUNDLE_ID "${PLUGIN_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    # The standalone is an effect: it needs the input. Without a usage
    # description macOS hands the app silence instead of asking.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The reverb processes the audio input."
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Ad-hoc code signing for every format, after JUCE's own post-build steps
# (and re-signing the installed copies if COPY_PLUGIN_AFTER_BUILD is turned on).
include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/sign_bundle.cmake)
lexplug_sign_bundles(${PLUGIN_TARGET})

# The data the plugin carries: the web demo's catalogs (plus the extra ones
# for 224 v3.2 and 224 TEST), the measured stored-byte tables (tools/
# measure_stored) and the slider tooltips. Hashes, names, display tables and
# measured behaviour only; never ROM bytes.
set(LEXPLUG_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../web-demo/page)
file(GLOB LEXPLUG_CATALOGS CONFIGURE_DEPENDS
    ${LEXPLUG_WEB_DIR}/catalogs/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/catalogs-extra/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/catalogs-extra/stored/*.stored.json)
juce_add_binary_data(lexplug_data
    HEADER_NAME LexplugData.h
    NAMESPACE LexplugData
    SOURCES ${LEXPLUG_CATALOGS} ${LEXPLUG_WEB_DIR}/param_help.json)

# The catalog model, share links, tooltips and saved state (JUCE-free).
add_library(lexplug_model STATIC
    source/catalog/json.cpp
    source/catalog/catalog.cpp
    source/catalog/help.cpp
    source/catalog/share.cpp
    source/state/plugin_state.cpp)
target_include_directories(lexplug_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source/catalog)
# It is linked into the VST3 shared module: Linux's linker refuses a
# non-PIC static library there.
set_target_properties(lexplug_model PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(${PLUGIN_TARGET} PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/embedded.cpp
    plugin/operator_port.cpp
    editor/editor_view.cpp)

target_compile_definitions(${PLUGIN_TARGET}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # The standalone opens the default input at start: it is an effect.
        JUCE_STANDALONE_PLUGIN_AUTO_OPEN_AUDIO_INPUT=1)

target_link_libraries(${PLUGIN_TARGET}
    PRIVATE
        juce::juce_audio_utils
        lexplug_data
    PUBLIC
        lexplug_engine
        lexplug_model
        juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Tests (console programs; not shipped). Each takes a ROM directory argument.
# ---------------------------------------------------------------------------
# The byte-exact gate: the plugin's processor against web-demo/wasm/web.cpp's lex_render loop.
juce_add_console_app(byte_exact PRODUCT_NAME "byte_exact")
target_sources(byte_exact PRIVATE tests/byte_exact/byte_exact.cpp)
target_link_libraries(byte_exact PRIVATE ${PLUGIN_TARGET} juce::juce_audio_utils)

# The RT-allocation gate: operator new/delete counted inside processBlock.
juce_add_console_app(rt_alloc PRODUCT_NAME "rt_alloc")
target_sources(rt_alloc PRIVATE tests/rt_alloc/rt_alloc.cpp)
target_link_libraries(rt_alloc PRIVATE ${PLUGIN_TARGET} juce::juce_audio_utils)

# The rate bridge alone (no machine, no JUCE): latency and continuity at every rate.
add_executable(bridge_check tests/byte_exact/bridge_check.cpp)
target_link_libraries(bridge_check PRIVATE lexplug_engine)
target_compile_options(bridge_check PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)

# Offline == realtime: one host automation timeline through the processor in
# several block schemes, realtime and non-realtime; the outputs must agree bit
# for bit.
juce_add_console_app(offline_realtime PRODUCT_NAME "offline_realtime")
target_sources(offline_realtime PRIVATE tests/offline_realtime/offline_realtime.cpp)
target_link_libraries(offline_realtime PRIVATE ${PLUGIN_TARGET} juce::juce_audio_utils)

# A picture of the editor on a booted machine (offscreen; for reports).
juce_add_console_app(editor_shot PRODUCT_NAME "editor_shot")
target_sources(editor_shot PRIVATE tests/offline_realtime/editor_shot.cpp)
target_link_libraries(editor_shot PRIVATE ${PLUGIN_TARGET} juce::juce_audio_utils)

# The firmware dropdown's request tracking, with no ROMs, audio or window: a
# pick is sent once, survives a tick that still sees the old set running, and is
# not undone (editor_view.cpp's applyRomSets).
juce_add_console_app(editor_firmware_select PRODUCT_NAME "editor_firmware_select")
target_sources(editor_firmware_select PRIVATE tests/editor/firmware_select.cpp editor/editor_view.cpp)
# The test pumps the message loop (MessageManager::runDispatchLoopUntil) so the
# asynchronous notification an onChange handler would read the box in also runs.
target_compile_definitions(editor_firmware_select PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(editor_firmware_select PRIVATE lexplug_model juce::juce_gui_basics
                                                  juce::juce_recommended_config_flags)
