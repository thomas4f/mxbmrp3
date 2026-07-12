#!/usr/bin/env bash
# ============================================================================
# tests/integration/build.sh
# Thin wrapper over the Makefile: parallel, incremental cross-compile of the
# plugin to a Windows x64 DLL (tests/integration/build/mxbmrp3_test.dlo).
#
# The Makefile is the engine (dependency tracking + parallelism + ccache); this
# just invokes it with a sensible -j and forwards args. See README.md.
#
#   ./build.sh            # incremental build, auto -j
#   ./build.sh clean      # remove build/
#   ./build.sh -B         # force full rebuild
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Require the posix-threads mingw variant (std::thread/std::mutex).
if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
    echo "ERROR: mingw-w64 not found. Install with:" >&2
    echo "  sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64" >&2
    exit 1
fi

exec make -C "$HERE" -j"$(nproc)" "$@"
