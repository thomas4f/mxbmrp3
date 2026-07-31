# ============================================================================
# tests/unit/CMakeLists.txt
# The pure-logic unit suite (doctest), in three flavours: plain, ASan/UBSan and
# gcov coverage.
#
# WHY CMAKE AND NOT THE SHELL SCRIPT THIS REPLACED. run_tests.sh hand-rolled the
# compile and link: a SOURCES array, a manual C compile of miniz, per-flavour
# flag juggling and output paths, and a full rebuild of every TU on every run.
# CMake was already in the tree for CTest, so all of that is now declarative —
# and incremental, which the script never was.
#
# The three flavours are separate TARGETS rather than separate build trees
# because they differ only in compile flags; one configure builds whichever you
# ask for:
#
#   cmake --build build/tests --target unit_tests        # ~1s to run
#   cmake --build build/tests --target unit_tests_asan   # ASan + UBSan
#   cmake --build build/tests --target unit_tests_cov    # gcov instrumented
#
# The root CMakeLists.txt registers all three as gates (`unit`, `unit-asan`,
# `unit-coverage`), so `ctest -L fast` runs them without anyone naming a target.
# ============================================================================

set(MXB_ROOT ${CMAKE_SOURCE_DIR})

option(MXB_UNIT_WERROR "Treat warnings as errors in the unit-test targets" ON)

# Every unit TU, plus the production sources linked into them. Exactly one TU
# (test_plugin_utils.cpp) defines the doctest impl + main; the rest just
# `#include "doctest.h"`. Adding a test = one line here.
set(MXB_UNIT_SOURCES
    test_plugin_utils.cpp
    test_notice_priority.cpp
    test_analytics_remote_config.cpp
    test_analytics_endpoint.cpp
    test_director_airtime.cpp
    test_session_charts_math.cpp
    test_segment_cumulative.cpp
    test_fmx_scoring.cpp
    test_tooltip_length.cpp
    test_update_asset_select.cpp
    test_ui_config.cpp
    test_render_frame_buffer.cpp
    test_crash_stack_format.cpp
    test_blue_flag_detect.cpp
    test_settings_serde.cpp
    test_director_scoring.cpp
    test_director_detect.cpp
    test_cpp_js_parity.cpp
    test_camera_resolve.cpp
    test_standings_context_window.cpp
    test_lap_log_plan.cpp
    test_plate_geometry.cpp
    test_records_window.cpp
    test_radar_fade.cpp
    test_peak_marker.cpp
    test_standings_gap_plan.cpp
    test_text_wrap.cpp
    test_hud_sw_renderer.cpp
    test_hud_sw_assets.cpp
    test_thread_detach_grace.cpp
    shim/logger_stub.cpp
    ${MXB_ROOT}/mxbmrp3/core/hud_sw_renderer.cpp
    ${MXB_ROOT}/mxbmrp3/core/ui_config.cpp)

# hud_sw_renderer.cpp's .fnt atlas decode needs miniz's inflate. The coverage
# flavour needs all four TUs, not just tinfl: at -O0 (which coverage forces, to
# keep the line mapping honest) the compiler stops discarding miniz.h's static
# zlib-compat shims, and those reference symbols living in the other three.
set(MXB_MINIZ_MIN ${MXB_ROOT}/mxbmrp3/vendor/miniz/miniz_tinfl.c)
set(MXB_MINIZ_ALL
    ${MXB_ROOT}/mxbmrp3/vendor/miniz/miniz_tinfl.c
    ${MXB_ROOT}/mxbmrp3/vendor/miniz/miniz.c
    ${MXB_ROOT}/mxbmrp3/vendor/miniz/miniz_tdef.c
    ${MXB_ROOT}/mxbmrp3/vendor/miniz/miniz_zip.c)

# Shared configuration. Each flag is load-bearing; see the comments.
function(mxb_configure_unit_target TGT)
    target_include_directories(${TGT} PRIVATE
        ${MXB_ROOT}/tests/integration/harness   # doctest.h
        ${MXB_ROOT}/mxbmrp3                     # the real production headers
        ${CMAKE_CURRENT_SOURCE_DIR}/shim)       # Win32 typedef stand-ins; LAST so
                                                # a real header always wins
    target_compile_definitions(${TGT} PRIVATE
        GAME_MXBIKES                            # pick a game explicitly, as the cross-build does
        # plugin_constants.h #errors on an NDEBUG build with no analytics keys, to
        # stop a keyless DLL reaching users. A unit binary is never shipped, and
        # -DCMAKE_BUILD_TYPE=Release adds NDEBUG, so without this the suite fails
        # to compile with a message about shipping secrets. The deleted
        # run_tests.sh set its own flags and could never hit this.
        MXBMRP3_ALLOW_NO_ANALYTICS
        # Shared C++/JS golden vectors (test_cpp_js_parity.cpp reads this;
        # tests/web/tests/parity.spec.js asserts the JS side of the same file).
        PARITY_FIXTURE="${MXB_ROOT}/tests/fixtures/cpp_js_parity.json")
    target_compile_options(${TGT} PRIVATE
        # -Werror on the TEST targets only — nothing shipped is built here, so the
        # cost of a future compiler adding a warning is a red unit build, not a
        # blocked release. It is the enforcement for the initializer-list ordering
        # in base_hud.h / input_manager.h / plugin_data.h, which had drifted into
        # 18 -Wreorder warnings that -Wall printed and nobody acted on. Turn off
        # with -DMXB_UNIT_WERROR=OFF if a bleeding-edge compiler blocks you.
        $<$<BOOL:${MXB_UNIT_WERROR}>:-Werror>
        # Stubs the Windows-only attribute so vendor/piboso/mxb_api.h parses under
        # native g++ (its structs are plain C — only the unexported function decls
        # carry __declspec). A FUNCTION-LIKE macro, so it cannot go in
        # target_compile_definitions: CMake silently drops those.
        -D__declspec\(x\)=
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra
            # Annex-K strncpy_s/_TRUNCATE, which glibc lacks and the plugin's POD
            # structs use. REUSED from the ASan harness rather than duplicated.
            -include${MXB_ROOT}/tests/asan/msvc_compat.h
            # miniz.h defines dozens of static shims nothing here calls; the
            # MSVC/mingw builds compile it with -w, so don't drown the run.
            -Wno-unused-function>)
    set_target_properties(${TGT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${TGT} PRIVATE Threads::Threads)
endfunction()

find_package(Threads REQUIRED)

# --- plain -----------------------------------------------------------------
add_executable(unit_tests ${MXB_UNIT_SOURCES} ${MXB_MINIZ_MIN})
mxb_configure_unit_target(unit_tests)
target_compile_options(unit_tests PRIVATE -O1 -g)

# --- ASan + UBSan ----------------------------------------------------------
# -fno-sanitize-recover makes any finding a hard, CI-failing abort.
# -fno-sanitize=float-cast-overflow: same rationale as tests/asan/run.sh — this
# is a memory-safety gate (OOB / use-after-free), not a policer of the
# intentional float->int cast idiom that clang's -fsanitize=undefined bundles.
add_executable(unit_tests_asan EXCLUDE_FROM_ALL ${MXB_UNIT_SOURCES} ${MXB_MINIZ_MIN})
mxb_configure_unit_target(unit_tests_asan)
target_compile_options(unit_tests_asan PRIVATE
    -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
    -fno-sanitize=float-cast-overflow -fno-omit-frame-pointer)
target_link_options(unit_tests_asan PRIVATE -fsanitize=address,undefined)

# --- gcov coverage ---------------------------------------------------------
# -O0 keeps the line mapping honest; at -O1 gcc folds lines together and the
# report reads as noise.
add_executable(unit_tests_cov EXCLUDE_FROM_ALL ${MXB_UNIT_SOURCES} ${MXB_MINIZ_ALL})
mxb_configure_unit_target(unit_tests_cov)
target_compile_options(unit_tests_cov PRIVATE -O0 -g --coverage)
target_link_options(unit_tests_cov PRIVATE --coverage)
