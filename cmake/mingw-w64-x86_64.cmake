# ============================================================================
# cmake/mingw-w64-x86_64.cmake — cross-compile to a Windows x64 DLL on Linux.
#
#   cmake -S . -B build/cross --toolchain cmake/mingw-w64-x86_64.cmake \
#         -DMXBMRP3_TEST_BUILD=ON
#
# This is the toolchain the headless test suite uses (tests/integration/build.sh
# wraps it). It is NOT a shippable configuration — see mxbmrp3/CMakeLists.txt.
# ============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mxb_prefix x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${_mxb_prefix}-gcc)
set(CMAKE_CXX_COMPILER ${_mxb_prefix}-g++)
set(CMAKE_RC_COMPILER  ${_mxb_prefix}-windres)

# Look for headers/libs in the mingw sysroot, but keep finding PROGRAMS on the
# host (cmake/ccache/etc. are Linux binaries).
set(CMAKE_FIND_ROOT_PATH /usr/${_mxb_prefix})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
