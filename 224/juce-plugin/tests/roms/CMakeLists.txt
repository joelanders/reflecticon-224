# ROM tests, built on their own (not part of the plugin build):
#   cmake -S tests/roms -B build-c -G Ninja -DJUCE_DIR=/path/to/JUCE
#   cmake --build build-c
cmake_minimum_required(VERSION 3.22)
project(lexicon_rom_tests VERSION 0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_DIR "/path/to/JUCE" CACHE PATH "JUCE 8.0.14")
add_subdirectory(${JUCE_DIR} juce)

# The JUCE-free core, with the engine boot check.
add_executable(test_roms_core test_roms_core.cpp)
target_compile_definitions(test_roms_core PRIVATE ROMS_TEST_BOOT=1)
target_include_directories(test_roms_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../analog)
target_compile_options(test_roms_core PRIVATE -O2 -Wall -Wextra)

# The JUCE layer.
juce_add_console_app(test_rom_library PRODUCT_NAME "test_rom_library")
target_sources(test_rom_library PRIVATE test_rom_library.cpp)
target_compile_definitions(test_rom_library PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(test_rom_library PRIVATE juce::juce_core juce::juce_data_structures juce::juce_events
                      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
