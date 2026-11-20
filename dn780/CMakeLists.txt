# The DN780 plugin. Hosts key saved sessions on the manufacturer and plugin codes: never change them
# once released. Rflx is shared by every reflecticon plugin (the 224 first used it).
add_machine_plugin(dn780
    PRODUCT_NAME "ReflecticonDN780"
    PLUGIN_NAME "Reflecticon DN780"
    COMPANY_NAME "joelanders"
    BUNDLE_ID "net.joelanders.reflecticon.dn780"
    MANUFACTURER_CODE Rflx
    PLUGIN_CODE Dn78
    VST3_CATEGORIES Fx Reverb
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/dn780_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/dn780_machine.cpp)

# check_machine ROM_DIR: the machine through the kit's generic host checks, the readouts against
# the video settings, the factory presets. JUCE-free.
add_executable(dn780_check_machine tests/check_machine.cpp plugin/dn780_machine.cpp)
target_compile_options(dn780_check_machine PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -g -Wall>)
