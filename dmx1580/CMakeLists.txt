# The DMX 15-80 plugin. Hosts key saved sessions on the manufacturer and plugin codes: never change them
# once released. Rflx is shared by every reflecticon plugin (the 224 first used it). Its codes
# differ from the DMX project's own plugin (Dx15/Dx80), so both can be installed.

# The dump repairs as a C string: ROM 3's SHA-256 before and after, and bit masks (no ROM bytes).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/core/patches.txt DMX_PATCHES_TEXT)
set(DMX_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(WRITE ${DMX_GEN}/dmx1580_patches.h.tmp
     "// generated from core/patches.txt\n#pragma once\nstatic const char kPatches[] = R\"DMXP(${DMX_PATCHES_TEXT})DMXP\";\n")
configure_file(${DMX_GEN}/dmx1580_patches.h.tmp ${DMX_GEN}/dmx1580_patches.h COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/patches.txt)

add_machine_plugin(dmx1580
    PRODUCT_NAME "ReflecticonDMX1580"
    PLUGIN_NAME "Reflecticon DMX 15-80"
    COMPANY_NAME "joelanders"
    BUNDLE_ID "net.joelanders.reflecticon.dmx1580"
    MANUFACTURER_CODE Rflx
    PLUGIN_CODE Dm15
    VST3_CATEGORIES Fx Delay
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/dmx1580_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/dmx1580_machine.cpp)
target_include_directories(dmx1580 PRIVATE ${DMX_GEN})

# check_machine ROM_DIR [RAW_OUT]: the core against the project's command line, typed settings read
# back from the display, the kit's host checks, a live change, a machine-option change. JUCE-free.
add_executable(dmx1580_check_machine tests/check_machine.cpp plugin/dmx1580_machine.cpp)
target_include_directories(dmx1580_check_machine PRIVATE ${DMX_GEN})
target_compile_options(dmx1580_check_machine PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -g -Wall>)
