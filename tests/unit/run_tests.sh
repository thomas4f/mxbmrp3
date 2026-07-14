#!/usr/bin/env bash
# ============================================================================
# tests/unit/run_tests.sh
# Build and run the pure-logic unit tests (doctest) on any host with a C++17
# compiler. No game, no Windows, no dependencies to install — doctest is a
# single vendored header. Used locally and by CI.
#
#   ./tests/unit/run_tests.sh            # run all
#   ./tests/unit/run_tests.sh -tc='*hex*'  # doctest filter (forwarded)
#   ASAN=1 ./tests/unit/run_tests.sh     # + AddressSanitizer/UBSan over every TU
#
# ASAN=1 rebuilds the SAME suite under -fsanitize=address,undefined so the whole
# portable-logic surface these tests already exercise (sanitizeUntrusted, fitText,
# formatting, parsing, session-charts math, …) is checked for out-of-bounds /
# use-after-free / UB, not just for correct results. Reuses the SOURCES list below
# so new tests get sanitizer coverage automatically — no second list. The CI
# memory-safety job runs this alongside tests/asan/. See tests/asan/README.md.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
OUT="${HERE}/build"
mkdir -p "${OUT}"

CXX="${CXX:-g++}"
# -I harness: doctest.h · -I mxbmrp3: the real production headers under test.
CXXFLAGS=(-std=c++17 -Wall -Wextra -O1 -g -pthread
          "-I${ROOT}/tests/integration/harness" "-I${ROOT}/mxbmrp3")

# Opt-in sanitizers (ASAN=1). -fno-sanitize-recover turns any finding into a
# hard, CI-failing abort; frame pointers keep the reported stacks readable.
# -fno-sanitize=float-cast-overflow: same rationale as tests/asan/run.sh — this
# runner is a memory-safety gate (OOB / use-after-free), not a policer of the
# intentional-benign float->int cast idiom; clang's -fsanitize=undefined bundles
# that pedantic check (gcc's doesn't), so suppressing it keeps CXX=clang++ robust
# across both ASan runners without weakening OOB detection.
BIN="${OUT}/unit_tests"
if [[ "${ASAN:-0}" == "1" ]]; then
    CXXFLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=all
               -fno-sanitize=float-cast-overflow -fno-omit-frame-pointer)
    BIN="${OUT}/unit_tests_asan"
    echo "ASAN=1 — building unit suite under AddressSanitizer + UBSan"
fi

# Every unit TU. Exactly one (test_plugin_utils.cpp) defines the doctest impl +
# main via DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN; every ADDITIONAL TU just
# `#include "doctest.h"` with NO config macro (defining the impl twice fails to
# link with multiple-definition errors). Add more TUs to this list.
SOURCES=("${HERE}/test_plugin_utils.cpp"
         "${HERE}/test_notice_priority.cpp"
         "${HERE}/test_analytics_remote_config.cpp"
         "${HERE}/test_analytics_endpoint.cpp"
         "${HERE}/test_director_airtime.cpp"
         "${HERE}/test_session_charts_math.cpp"
         "${HERE}/test_tooltip_length.cpp"
         "${HERE}/test_update_asset_select.cpp"
         "${HERE}/test_ui_config.cpp"
         "${HERE}/test_render_frame_buffer.cpp"
         "${HERE}/test_crash_stack_format.cpp"
         "${ROOT}/mxbmrp3/core/ui_config.cpp")

echo "Compiling unit tests (${CXX}, doctest) ..."
"${CXX}" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "${BIN}"

echo
ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
    "${BIN}" "$@"
