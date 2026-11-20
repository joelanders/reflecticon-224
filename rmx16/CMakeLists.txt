# The RMX16 plugin. Hosts key saved sessions on the manufacturer and plugin codes: never change them
# once released. Rflx is shared by every reflecticon plugin (the 224 first used it).
add_library(rmx16_core STATIC core/rmx16_core.c)
set_target_properties(rmx16_core PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_compile_options(rmx16_core PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/O2,-O2>)

add_machine_plugin(rmx16
    PRODUCT_NAME "ReflecticonRMX16"
    PLUGIN_NAME "Reflecticon RMX16"
    COMPANY_NAME "joelanders"
    BUNDLE_ID "net.joelanders.reflecticon.rmx16"
    MANUFACTURER_CODE Rflx
    PLUGIN_CODE Rx16
    VST3_CATEGORIES Fx Reverb
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/rmx16_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/rmx16_machine.cpp
    LINK rmx16_core)

# check_machine ROM_DIR [RAW_OUT]: the core, typed settings on all 17 programs, the kit's host
# checks, a live change. JUCE-free.
add_executable(rmx16_check_machine tests/check_machine.cpp plugin/rmx16_machine.cpp)
target_link_libraries(rmx16_check_machine PRIVATE rmx16_core)
target_compile_options(rmx16_check_machine PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -g -Wall>)
