#!/usr/bin/env bash
# ============================================================================
# tests/integration/build.sh
# Incremental, parallel cross-compile of the plugin to a Windows x64 DLL
# (tests/integration/build/mxbmrp3_test.dlo), where every harness .exe expects
# to find it.
#
# Thin wrapper over CMake, which replaced the Makefile that used to live here.
# The Makefile was a second, independent description of the same source tree —
# find-globbed, MX Bikes only — sitting alongside the vcxproj's explicit list,
# and nothing compared the two. Both are gone: mxbmrp3/CMakeLists.txt is the
# single definition for every toolchain; see its header.
#
#   ./build.sh            # incremental build, auto -j
#   ./build.sh clean      # remove the build tree and the DLL
#   ./build.sh -B         # force a full rebuild
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD_DIR="${ROOT}/build/cross"

# Require the posix-threads mingw variant (std::thread/std::mutex). Exit 1 with a
# readable message, as before: mxb_gate's TOOLS column is what turns a missing
# toolchain into a CTest SKIP, so this path is only reached by a human asking for
# the build by name.
if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
    echo "ERROR: mingw-w64 not found. Install with:" >&2
    echo "  ./tools/install_deps.sh mingw" >&2
    exit 1
fi

EXTRA=()
case "${1:-}" in
clean)
    rm -rf "${BUILD_DIR}" "${HERE}/build/mxbmrp3_test.dlo"
    echo "  CLEAN removed build/cross and the test DLL"
    exit 0
    ;;
-B) EXTRA=(--clean-first) ;;
esac

# Configure only a fresh tree; an existing one re-generates itself from the build
# rule whenever a CMakeLists.txt changes.
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    # --preset, so the toolchain file and MXBMRP3_TEST_BUILD live in exactly one
    # place (CMakePresets.json) rather than here as well.
    ( cd "${ROOT}" && cmake --preset cross >/dev/null )
fi

cmake --build "${BUILD_DIR}" --target mxbmrp3_test -j"$(nproc)" "${EXTRA[@]}"

# The Makefile printed this on every link and it is genuinely useful — a sudden
# drop in exported symbols means a TU quietly stopped being compiled.
DLL="${HERE}/build/mxbmrp3_test.dlo"
if [ -f "${DLL}" ]; then
    echo "  DONE  $(x86_64-w64-mingw32-nm -g --defined-only "${DLL}" 2>/dev/null \
                    | grep -cE ' T ') exported symbols"
fi
