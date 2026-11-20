# The StarGate 323 plugin. Hosts key saved sessions on the manufacturer and plugin codes: never change them
# once released. Rflx is shared by every reflecticon plugin (the 224 first used it).
add_machine_plugin(sg323
    PRODUCT_NAME "ReflecticonSG323"
    PLUGIN_NAME "Reflecticon StarGate 323"
    COMPANY_NAME "joelanders"
    BUNDLE_ID "net.joelanders.reflecticon.sg323"
    MANUFACTURER_CODE Rflx
    PLUGIN_CODE Sg32
    VST3_CATEGORIES Fx Reverb
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/sg323_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin/sg323_machine.cpp)

# check_machine ROM_DIR [RAW_IN RAW_OUT]: the core on a raw input (compare with the StarGate project's
# cpp/build/run), readouts, an impulse through the host, the kit's host checks, a live change, cost.
add_executable(sg323_check_machine tests/check_machine.cpp plugin/sg323_machine.cpp)
target_compile_options(sg323_check_machine PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -g -Wall>)
