# ============================================================================
# cmake/copy_to_plugin_path.cmake — the vcxproj's PostBuildEvent:
#   if defined MXB_PLUGIN_PATH copy "$(TargetPath)" "%MXB_PLUGIN_PATH%\..."
#
#   cmake -DDLL=<path to the built .dlo> -DVAR=MXB_PLUGIN_PATH \
#         -P cmake/copy_to_plugin_path.cmake
#
# VAR names the environment variable to read — MXB_PLUGIN_PATH, GPB_PLUGIN_PATH
# or KRP_PLUGIN_PATH. Per-game because a single build now produces all three
# plugins: with one shared variable, building everything dropped the GP Bikes and
# KRP plugins into the MX Bikes plugins folder, where PiBoSo loads every .dlo it
# finds and would try to run two plugins built for other games.
#
# WHY A SCRIPT AND NOT THE ONE-LINER. Expressing that batch line directly in
# add_custom_command does not survive the trip: VERBATIM escapes `%` for MSBuild,
# so %MXB_PLUGIN_PATH% reached cmd.exe as %%MXB_PLUGIN_PATH%% ("The syntax of the
# command is incorrect"), and `if` is shell syntax rather than a program for
# CMake to run. Going through cmake -P sidesteps cmd.exe quoting entirely.
#
# It also restores a behaviour the direct port had lost: this reads the
# environment at BUILD time, exactly as MSBuild did, so setting MXB_PLUGIN_PATH
# does not require re-configuring the tree.
# ============================================================================
if(NOT DEFINED DLL OR NOT DEFINED VAR)
    message(FATAL_ERROR "copy_to_plugin_path: -DDLL=<file> -DVAR=<env var> required")
endif()

if("$ENV{${VAR}}" STREQUAL "")
    return()   # not set: nothing to do, same as the batch `if defined` guard
endif()

file(TO_CMAKE_PATH "$ENV{${VAR}}" DEST)
get_filename_component(NAME "${DLL}" NAME)

if(NOT IS_DIRECTORY "${DEST}")
    message(FATAL_ERROR
            "${VAR} is set to '${DEST}', which is not a directory. "
            "Fix or unset it — failing here rather than letting you test a stale "
            "plugin, which is what the deploy step exists to prevent.")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${DEST}/${NAME}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Failed to copy ${NAME} to ${DEST} (is the game running?)")
endif()
message(STATUS "Deployed ${NAME} -> ${DEST}")
