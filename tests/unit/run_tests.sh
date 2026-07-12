#!/usr/bin/env bash
# ============================================================================
# tests/unit/run_tests.sh
# Build and run the pure-logic unit tests (doctest) on any host with a C++17
# compiler. No game, no Windows, no dependencies to install — doctest is a
# single vendored header. Used locally and by CI.
#
#   ./tests/unit/run_tests.sh            # run all
#   ./tests/unit/run_tests.sh -tc='*hex*'  # doctest filter (forwarded)
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
OUT="${HERE}/build"
mkdir -p "${OUT}"

CXX="${CXX:-g++}"
# -I harness: doctest.h · -I mxbmrp3: the real production headers under test.
CXXFLAGS=(-std=c++17 -Wall -Wextra -O1 -g
          "-I${ROOT}/tests/integration/harness" "-I${ROOT}/mxbmrp3")

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
         "${HERE}/test_ui_config.cpp"
         "${ROOT}/mxbmrp3/core/ui_config.cpp")

echo "Compiling unit tests (${CXX}, doctest) ..."
"${CXX}" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "${OUT}/unit_tests"

echo
"${OUT}/unit_tests" "$@"
