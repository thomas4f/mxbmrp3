#!/usr/bin/env bash
# ============================================================================
# tests/asan/run.sh
# Build and run the native AddressSanitizer + UBSan memory-safety harness for the
# plugin's portable buffer/index surface. Needs only a native C++17 compiler with
# libasan (default g++/clang on Linux) — no game, no Windows, no Wine. Runs fast,
# so the CI job gates every push.
#
#   ./tests/asan/run.sh
#   CXX=clang++ ./tests/asan/run.sh        # clang's ASan works too
#
# Coverage boundary: this exercises the code that compiles natively (RaceEntryData
# fixed buffers, the leader-timing index clamp, the crash-site container types).
# The full live-callback path under ASan is the MSVC build — see README.md.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
OUT="${HERE}/build"
mkdir -p "${OUT}"

CXX="${CXX:-g++}"

# -include msvc_compat.h : strncpy_s/_TRUNCATE the portable headers need.
# -D'__declspec(x)='     : neutralize the game API header's dllexport annotation.
# -fno-sanitize-recover  : turn any finding into a hard, CI-failing abort.
# No -Wall/-Wextra: this is a sanitizer runtime gate, not a warning gate (the
# cross-build/cppcheck jobs own that), and the production headers carry a few
# pre-existing -Wreorder/-Wclass-memaccess notes that would only add noise here.
#
# -fno-sanitize=float-cast-overflow: the harness DELIBERATELY feeds NaN/Inf/FLT_MAX
# into the production formula `(int)(trackPos * 100.0f)` to prove the CLAMP bounds
# the array write. That float->int cast is itself UB, and clang's -fsanitize=undefined
# bundles float-cast-overflow (gcc's does not), so without this the documented
# CXX=clang++ path would abort on the intentional cast before the clamp is even
# tested — a false alarm on the exact behavior we're verifying is safe. ASan's
# out-of-bounds detection (the real check) is unaffected.
FLAGS=(-std=c++17 -O1 -g
       -fsanitize=address,undefined -fno-sanitize-recover=all
       -fno-sanitize=float-cast-overflow
       -fno-omit-frame-pointer
       -include "${HERE}/msvc_compat.h"
       "-D__declspec(x)="
       "-I${ROOT}/mxbmrp3"
       "-I${ROOT}/tests/integration/harness")

echo "Compiling ASan memory-safety harness (${CXX}) ..."
"${CXX}" "${FLAGS[@]}" "${HERE}/memory_safety_fuzz.cpp" -o "${OUT}/memory_safety_fuzz"

echo "Running under ASan+UBSan ..."
# halt_on_error=1: stop at the first violation. abort_on_error=1: non-zero exit
# so CI fails. detect_leaks stays on (default) — the harness frees what it news.
ASAN_OPTIONS="halt_on_error=1:abort_on_error=1:detect_leaks=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${OUT}/memory_safety_fuzz"
