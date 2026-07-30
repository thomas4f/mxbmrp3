# ============================================================================
# CMakeLists.txt — test registration ONLY. This builds nothing.
#
# WHAT THIS IS FOR. Two jobs, added in that order:
#   1. Register the gate scripts with CTest, so the project has a STANDARD test
#      runner instead of a hand-written one (the post-mortem below).
#   2. Pull in mxbmrp3/CMakeLists.txt — THE definition of the plugin, shared by
#      the MSVC shipping build and the mingw cross-build. It replaced
#      mxbmrp3.vcxproj's explicit list and tests/integration/Makefile's glob,
#      which described the same sources and could not be compared.
# A native Linux configure only does (1): the plugin is Windows-only and gated
# behind if(WIN32), so this file still builds nothing here.
#
# WHY IT REPLACED A BESPOKE RUNNER. tests/run_all.sh grew its own stage table,
# tool-presence checks, skip semantics, summary and exit-code policy — and then
# needed its own lint to verify that the skip semantics worked, which in turn
# needed a self-test. Five commits of machinery to reimplement, imperfectly,
# what CTest already does:
#
#   hand-rolled                       CTest equivalent
#   ----------------------------------------------------------------------
#   SKIP_RC=3 + a `SKIP:` line        SKIP_RETURN_CODE (same convention)
#   `tools` column + command -v       SKIP_RETURN_CODE + mxb_gate's TOOLS arg
#   `--fast` flag                     ctest -L fast   (labels)
#   `--keep-going`                    ctest's default; --stop-on-failure opts out
#   per-stage PASS/FAIL/SKIP lines    ctest's own progress + summary
#   inline log tail on failure        ctest --output-on-failure
#   (nothing — it ran serially)       ctest -j N
#
# The exit-3 convention SURVIVED the migration because it was never the bespoke
# part: SKIP_RETURN_CODE is exactly this idea, standardised. Only the runner
# around it was homemade.
#
# Two more hand-written pieces went the same way afterwards, and for the same
# reason — a maintained implementation already existed:
#
#   tests/unit/run_tests.sh       -> tests/unit/CMakeLists.txt (three targets,
#                                    one source list, and INCREMENTAL)
#   tools/coverage_report.py      -> gcovr, via tests/unit/coverage.sh
#                                    (--filter/--exclude/--fail-under-line)
#
# USAGE
#   cmake -S . -B build/tests            # configure once (fast, builds nothing)
#   ctest --test-dir build/tests --output-on-failure
#   ctest --test-dir build/tests -L fast          # no mingw/wine needed
#   ctest --test-dir build/tests -j 4             # parallel
#   ctest --test-dir build/tests -R settings      # by name
#
# VERIFYING THE SKIP PROPERTY (do this after touching mxb_gate or a TOOLS list):
# run ctest with the toolchains hidden from PATH — build a directory of symlinks
# to the real binaries minus mingw/wine/clang/cppcheck/makensis/node/gcovr, point
# PATH at it, and run the suite. Every gate must report Passed or Skipped; a
# Failed there is this bug again.
#
# ADDING A GATE: one mxb_gate() call. Labels are `fast` (runs anywhere with a
# C++17 compiler) or `slow` (needs mingw/wine/nsis).
# ============================================================================
# 3.21 for CMakePresets.json support (see that file); nothing else here needs it.
cmake_minimum_required(VERSION 3.21)
# C and CXX are enabled because tests/unit builds the doctest suite here (it used
# to be a hand-rolled compile in a shell script), and mxbmrp3/ builds the plugin
# when configured for Windows (MSVC or the mingw toolchain).
# Names the generated Visual Studio solution (build/msvc/mxbmrp3.sln).
project(mxbmrp3 C CXX)

enable_testing()

# ccache when present, matching what tests/integration/Makefile did: it caches
# objects across even a clean rebuild, which is what keeps the 117-TU cross-build
# cheap in CI (the workflow caches ~/.ccache between runs).
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# The doctest unit suite compiles the production headers against a Win32 SHIM
# with GCC flags; it is a Linux-side target and was never meant for MSVC, where
# `-O1` becomes `/O1` and collides with the Debug config's `/RTC1`. Configure it
# only where it can actually build.
if(NOT MSVC)
    add_subdirectory(tests/unit)
endif()

# The plugin itself (mxbmrp3/CMakeLists.txt) — the single definition shared by
# the MSVC shipping build and the mingw cross-build. It is Windows-only, so a
# native Linux configure (this file's usual job: registering gates, building
# nothing) skips it entirely and behaves exactly as before.
if(WIN32)
    add_subdirectory(mxbmrp3)
    add_subdirectory(tools)   # the two dev tools that shared the old solution
endif()

# Register one gate script as a CTest test.
#   NAME    — ctest -R matches on this
#   LABEL   — fast | slow
#   TIMEOUT — seconds; the integration suite is the long pole
#   TOOLS   — space-separated executables the gate needs, or "-" for none
# Every gate runs from the repo root, and exit 3 means "prerequisite missing on
# this machine" — CTest reports that as SKIPPED, not failed.
#
# TOOLS IS MANDATORY AND POSITIONAL, on purpose. The gate scripts do NOT exit 3
# themselves — they print a clear "ERROR: wine not found" and exit 1, which is
# right when a human runs one directly and asks for it by name. Detecting the
# prerequisite was run_all.sh's `tools` column, and the first cut of this file
# migrated SKIP_RETURN_CODE without it: on a box with no mingw/wine/clang/
# cppcheck/nsis/node, 11 of the then-24 gates reported FAILED not SKIPPED. CI
# never saw it because each CI job installs its own toolchain.
#
# Making it a required positional argument is the fix that outlasts the bug: a
# gate cannot be registered without answering "what does this need?", because
# CMake errors on the missing argument. (A wrong or short list is still
# possible — that one shows up the first time someone runs on a bare box.)
# Re-verify the whole property by running ctest with a PATH that has the
# toolchains removed; nothing may report FAILED.
function(mxb_gate NAME LABEL TIMEOUT TOOLS)
    set(guard "")
    if(NOT TOOLS STREQUAL "-")
        string(REPLACE " " ";" _tools "${TOOLS}")
        foreach(t IN LISTS _tools)
            string(APPEND guard
                   "command -v ${t} >/dev/null 2>&1 || "
                   "{ echo 'SKIP: ${t} not installed'; exit 3; }; ")
        endforeach()
    endif()
    add_test(NAME ${NAME}
             COMMAND ${CMAKE_COMMAND} -E env bash -c "${guard}${ARGN}"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(${NAME} PROPERTIES
        LABELS "${LABEL}"
        TIMEOUT ${TIMEOUT}
        SKIP_RETURN_CODE 3)
endfunction()

# --- fast: no mingw, no wine. Ordered cheapest first for readability; CTest
#     runs them in registration order serially, or in any order under -j.
mxb_gate(style fast 120 "-" "./tests/integration/check_style.sh")
mxb_gate(api-guards fast 120 "-" "./tests/integration/check_api_guards.sh")
mxb_gate(visibility-gates fast 120 "-" "./tests/integration/check_visibility_gates.sh")
mxb_gate(mt-flags fast 120 "-" "./tests/integration/check_mt_flags.sh")
mxb_gate(hud-raw-cache fast 120 "-" "./tests/integration/check_hud_raw_cache.sh")
# The lint's own must-catch/must-exempt cases. Separate gate so a matcher
# regression names the broken case instead of just going quiet on the tree.
mxb_gate(hud-raw-cache-selftest fast 120 "-" "./tests/integration/check_hud_raw_cache.sh --self-test")
# The provisioning hook, which is what makes an unprovisioned box distinguishable
# from a healthy one — so it is the last file that should be uncovered. TOOLS is
# "-" deliberately and the fixture stubs the whole toolchain to keep it that way:
# a gate that can SKIP cannot be the thing that protects the skip detector.
mxb_gate(session-hook fast 120 "-" "./tests/integration/check_session_hook.sh")
mxb_gate(docs fast 120 "python3" "python3 tools/check_docs.py")
mxb_gate(python-lint fast 120 "ruff" "ruff check .")
mxb_gate(vendored-manifest fast 120 "python3" "python3 tools/check_vendored_manifest.py")
# Guarded with the subdirectory that defines them: $<TARGET_FILE:unit_tests> is
# a generate-time error when the target does not exist, so an MSVC configure
# would fail outright rather than just skipping these.
if(TARGET unit_tests)
    mxb_gate(unit fast 600 "g++" "cmake --build '${CMAKE_BINARY_DIR}' --target unit_tests -j && '$<TARGET_FILE:unit_tests>'")
    mxb_gate(unit-coverage fast 900 "g++ python3 gcovr" "./tests/unit/coverage.sh 95 '${CMAKE_BINARY_DIR}'")
    mxb_gate(unit-asan fast 900 "g++" "cmake --build '${CMAKE_BINARY_DIR}' --target unit_tests_asan -j && '$<TARGET_FILE:unit_tests_asan>'")
endif()
mxb_gate(asan-harness fast 600 "g++" "./tests/asan/run.sh")
# ci-covers-differently: the CI job runs `npm ci` + `npx playwright test` with
# setup-node's npm cache and a keyed Playwright browser cache; run.sh exists to
# find-or-fetch Chromium on a dev box.
#
# NOT the same browser, though — and that has bitten once. run.sh uses whatever
# Chromium is preinstalled (rev 1194 in the CI sandbox) because it cannot
# download; the CI job installs the revision @playwright/test actually pins
# (1228). A mobile-layout assertion passed locally and failed in CI on the same
# commit. So: local green is NOT proof of CI green for this gate, and a web test
# must assert a PROPERTY (does not overflow, fills the width) rather than an
# exact pixel, because pixels are a property of the browser build.
mxb_gate(web-overlay fast 900 "node" "./tests/web/run.sh")

# --- slow: needs mingw-w64, wine, clang, cppcheck or nsis. The TOOLS column is
#     what turns "not installed" into SKIPPED here; the scripts themselves exit 1
#     with a readable error when run directly.
mxb_gate(cross-build slow 1800 "x86_64-w64-mingw32-g++" "./tests/integration/build.sh")
mxb_gate(integration slow 2400 "x86_64-w64-mingw32-g++ wine" "./tests/integration/run_tests.sh")
mxb_gate(persistence slow 900 "x86_64-w64-mingw32-g++ wine" "./tests/integration/run_persist_test.sh")
mxb_gate(fuzz-config slow 900 "x86_64-w64-mingw32-g++ wine" "./tests/integration/run_fuzz.sh")
mxb_gate(fuzz-callbacks slow 900 "x86_64-w64-mingw32-g++ wine" "./tests/integration/run_fuzz_callbacks.sh")
mxb_gate(perf slow 900 "x86_64-w64-mingw32-g++ wine" "./tests/integration/run_perf.sh")
mxb_gate(shim-constants slow 300 "x86_64-w64-mingw32-g++" "./tests/unit/shim/regen_constants.sh --check")
mxb_gate(thread-safety slow 900 "clang++ x86_64-w64-mingw32-g++" "./tests/integration/check_thread_safety.sh")
mxb_gate(game-configs slow 1800 "x86_64-w64-mingw32-g++" "./tests/integration/check_game_configs.sh")
mxb_gate(cppcheck slow 900 "cppcheck" "./tests/run_cppcheck.sh")
mxb_gate(installer slow 900 "makensis wine python3" "./tests/integration/run_installer_test.sh")

# CTest runs these in order within a label; the integration suite dominates, so
# `-j` is worth using locally. CI runs them per-job instead (see tests.yml).

# Every gate that runs the cross-built plugin shares ONE artifact:
# tests/integration/build/mxbmrp3_test.dlo. They each call build.sh (which
# relinks it in place) and then load it under Wine from that same directory —
# so under `ctest -j` one gate can be relinking the DLL while another is loading
# it. That is not a plugin bug and it does not look like one either: the runner
# reports `exit=1 dumps=0 reached_init=0 faults=0`, i.e. it never reached init
# and recorded no fault, which reads exactly like a fuzz finding. Observed as
# fuzz-config + fuzz-callbacks failing under -j 4 and both passing serially.
#
# RESOURCE_LOCK is CTest's own answer (tests naming the same lock never run
# concurrently), so this stays a property rather than another hand-rolled
# mechanism. The suite's wall clock is dominated by `integration` either way,
# and the gates that DON'T touch the artifact — game-configs (-fsyntax-only),
# thread-safety, installer, web-overlay, the unit trio and every fast lint —
# are deliberately left out of the lock and still run in parallel.
#
# A new gate that runs the cross-built DLL belongs in this list.
set_tests_properties(cross-build integration persistence fuzz-config
                     fuzz-callbacks perf
                     PROPERTIES RESOURCE_LOCK mxbmrp3_test_dll)

# `perf` additionally runs ALONE. The lock above shares an artifact; perf shares
# something the lock cannot express — the CPU itself. It asserts the average and
# both p99s against the 2.08ms (480fps) frame budget, so any concurrent gate is
# not noise around the measurement, it IS the measurement: a `game-configs`
# compile or the Playwright browser next to it inflates the very numbers the gate
# exists to police. Observed failing under `ctest -j 3` on a 4-core box and
# passing serially at 39s, which is a false alarm about the frame budget, not a
# regression — and a perf gate that cries wolf gets ignored, which is worse than
# not having one.
#
# RUN_SERIAL is CTest's own answer (the test runs with no other test
# concurrent), so this stays a property rather than a rule about how to invoke
# ctest. Cost is bounded: perf is ~40s of a suite whose wall clock is dominated
# by `integration`.
#
# SCOPE: local `ctest -j` only. CI does NOT run perf through CTest —
# .github/workflows/tests.yml invokes ./tests/integration/run_perf.sh directly as
# a job step, where this property has no effect. That is fine today (steps within
# a job are sequential and other jobs are on their own runners, so CI's perf run
# is already alone), but it is not protected BY this line: adding a parallel step
# to that job would starve perf again with nothing here to stop it. Routing CI
# through CTest would NOT close that gap — RUN_SERIAL only excludes other TESTS,
# so it is a no-op when perf is the only one selected. The rule is instead stated
# at the step itself in tests.yml, where someone editing that job will read it.
set_tests_properties(perf PROPERTIES RUN_SERIAL TRUE)
