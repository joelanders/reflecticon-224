# JUCE plugins for rmx16/, dmx1580/, dn780/ and sg323/, built on the code they share
# (shared/). See shared/README.md. The 224 plugin builds on its own from 224/juce-plugin/.
#
#   cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   cmake --build build
#
# Without FETCHCONTENT_SOURCE_DIR_JUCE, JUCE 8.0.14 is fetched from GitHub.
# Bring your own ROMs: nothing here or in the binaries holds ROM bytes.
cmake_minimum_required(VERSION 3.22)
project(Reflecticon VERSION 0.1.0 LANGUAGES C CXX)
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
# Emulators should give the same numbers in every build (plugin, command line, web): no fused
# multiply-adds, never -ffast-math.
if(MSVC)
    add_compile_options(/fp:precise /bigobj /utf-8)
    add_compile_definitions(_USE_MATH_DEFINES)
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

include(shared/juce/kit.cmake)

add_subdirectory(dn780)
add_subdirectory(rmx16)
add_subdirectory(dmx1580)
add_subdirectory(sg323)
