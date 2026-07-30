# ============================================================================
# cmake/stamp_version.cmake — write mxbmrp3/version_build.g.h from the git
# commit count. Run at BUILD time (not configure) by the mxbmrp3_stamp_version
# target, so the 4th version component tracks HEAD rather than whenever CMake
# last ran. This is the vcxproj StampVersion target, moved.
#
#   cmake -DMXB_SRC=<mxbmrp3 dir> -P cmake/stamp_version.cmake
#
# A git failure falls back to 0 so the build never breaks, and the file is only
# rewritten when the value actually changes — resource.h is included by exactly
# one TU plus the .rc, but a needless touch would still rebuild them every time.
#
# MXBMRP3_VER_BUILD pins the value instead of asking git. It exists for PIXEL
# COMPARISONS between two checkouts: the count is derived from HEAD, so a branch
# and its base ALWAYS differ here, and any screenshot showing the version differs
# across them by construction — a floor that reads like a rendering change and
# cannot be reached by fixing the renderer. companion_demo.sh sets it. It is a
# capture-time knob only; nothing in a shipping build sets it, and if it is unset
# behaviour is exactly as before.
# ============================================================================
if(DEFINED ENV{MXBMRP3_VER_BUILD} AND NOT "$ENV{MXBMRP3_VER_BUILD}" STREQUAL "")
    set(_count "$ENV{MXBMRP3_VER_BUILD}")
    set(_rc 0)
else()
    execute_process(
        COMMAND git -C "${MXB_SRC}" rev-list --count HEAD
        OUTPUT_VARIABLE _count
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _rc)
endif()

if(NOT _rc EQUAL 0 OR _count STREQUAL "")
    set(_count 0)
endif()

set(_out "${MXB_SRC}/version_build.g.h")
set(_text "#pragma once\n#define VER_BUILD_AUTO ${_count}\n")

set(_existing "")
if(EXISTS "${_out}")
    file(READ "${_out}" _existing)
endif()
if(NOT _existing STREQUAL _text)
    file(WRITE "${_out}" "${_text}")
    message(STATUS "version_build.g.h: VER_BUILD_AUTO ${_count}")
endif()
