# Ad-hoc code signing for the plugin's bundles (AU, VST3, Standalone).
# No Developer ID, no notarization: the bundles are signed with "-" so that
# Apple silicon loads them and `codesign --verify --deep --strict` passes.
#
# This one file is used two ways.
#
# 1. From CMakeLists.txt (configure time), after juce_add_plugin():
#
#        include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/sign_bundle.cmake)
#        lexplug_sign_bundles(${PLUGIN_TARGET})
#
#    It adds a POST_BUILD step to every format target that exists. Those
#    steps are appended after JUCE's own POST_BUILD steps (moduleinfo.json for
#    VST3, the copy-after-build step when enabled), so the signature is made
#    last and seals everything JUCE put in the bundle. When JUCE's
#    copy-after-build step is on, the installed copy (copied before this step
#    ran, so with a stale signature) is re-signed too.
#
# 2. As a script (build time, or by hand):
#
#        cmake -Dbundle=<dir> [-Dinstalled=<dir>] [-Didentity=-] -P scripts/sign_bundle.cmake
#
#    Signs <dir> (and <installed> if it exists), then verifies each with
#    `codesign --verify --deep --strict`; a failed verification fails the build.

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT bundle)
        message(FATAL_ERROR "sign_bundle.cmake: -Dbundle=<dir> is required")
    endif()
    if(NOT identity)
        set(identity "-")
    endif()
    set(targets "${bundle}")
    if(installed AND EXISTS "${installed}")
        list(APPEND targets "${installed}")
    endif()
    foreach(path IN LISTS targets)
        # No --deep when signing: the bundles hold no nested code, and Apple
        # advises against signing with --deep. --deep is used to verify.
        execute_process(
            COMMAND /usr/bin/codesign --force --sign "${identity}" --timestamp=none "${path}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(
            COMMAND /usr/bin/codesign --verify --deep --strict "${path}"
            COMMAND_ERROR_IS_FATAL ANY)
        message(STATUS "signed (${identity}) and verified: ${path}")
    endforeach()
    return()
endif()

set(_LEXPLUG_SIGN_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

# lexplug_sign_bundles(<juce plugin target>)
function(lexplug_sign_bundles shared_target)
    if(NOT APPLE)
        return()
    endif()
    get_target_property(copy_after_build ${shared_target} JUCE_COPY_PLUGIN_AFTER_BUILD)
    foreach(fmt AU VST3 Standalone)
        set(target ${shared_target}_${fmt})
        if(NOT TARGET ${target})
            continue()
        endif()
        set(installed "")
        if(copy_after_build AND NOT fmt STREQUAL "Standalone")
            get_target_property(product ${shared_target} JUCE_PRODUCT_NAME)
            if(fmt STREQUAL "AU")
                get_target_property(dir ${shared_target} JUCE_AU_COPY_DIR)
                set(installed "${dir}/${product}.component")
            else()
                get_target_property(dir ${shared_target} JUCE_VST3_COPY_DIR)
                set(installed "${dir}/${product}.vst3")
            endif()
        endif()
        if(fmt STREQUAL "Standalone")
            # With the Makefile and Ninja generators the app's
            # RecentFilesMenuTemplate.nib (a MACOSX_PACKAGE_LOCATION source)
            # can land after the POST_BUILD steps, adding an unsealed file.
            # Put the same bytes in place first.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${JUCE_CMAKE_UTILS_DIR}/RecentFilesMenuTemplate.nib
                    $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/RecentFilesMenuTemplate.nib
                COMMENT "stage the Standalone NIB before signing"
                VERBATIM)
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Didentity=-
                -Dbundle=$<TARGET_BUNDLE_DIR:${target}>
                -Dinstalled=${installed}
                -P ${_LEXPLUG_SIGN_SCRIPT}
            COMMENT "ad-hoc codesign: ${target}"
            VERBATIM)
    endforeach()
endfunction()
