# ============================================================================
# mxbmrp3/CMakeLists.txt — THE definition of the plugin, for every toolchain.
#
# This replaced two independent descriptions of the same source tree:
# mxbmrp3.vcxproj (explicit file list, six hand-maintained config blocks) and
# tests/integration/Makefile (find-globbed, MX Bikes only). Nothing compared
# them: tools/check_vcxproj.py guarded the FILE LIST, not the flags — and two of
# the six config blocks had silently lost <Optimization>, so the GP Bikes and
# KRP release builds shipped unoptimized. That is the cost this file removes.
#
#   MSVC (the shipping .dlo, Windows only):
#     cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
#     cmake --build build/msvc --config Release
#
#   mingw cross-build (the headless test DLL; tests/integration/build.sh wraps it):
#     cmake -S . -B build/cross --toolchain cmake/mingw-w64-x86_64.cmake \
#           -DMXBMRP3_TEST_BUILD=ON
#
# WHY GLOBBING IS CORRECT NOW, HAVING BEEN WRONG BEFORE. A glob was dangerous
# only because a SECOND list existed to disagree with it: a .cpp on disk but
# missing from the vcxproj built clean on Linux and failed at MSVC link time, at
# release. With one definition there is nothing to drift from — whatever is on
# disk is what every toolchain builds. CONFIGURE_DEPENDS re-globs on build, so a
# new file does not need a manual re-configure.
# ============================================================================

option(MXBMRP3_TEST_BUILD
       "Build the mingw/Wine test configuration instead of the shipping DLLs" OFF)

# AddressSanitizer build of the MSVC plugin DLLs, for the memory-safety CI job
# (.github/workflows/tests.yml) and tests/asan/run_asan_msvc.ps1. This must be a
# CONFIGURE-time option, not msbuild /p: switches: the flags it has to undo
# (/RTC1, the static CRT) are baked into the generated vcxproj as literal
# ItemDefinitionGroup metadata, which /p: global properties cannot override.
# (The old hand-written vcxproj took /p:EnableASAN etc. plus a
# Directory.Build.targets CRT override; both stopped reaching the build when the
# projects moved to build/msvc — MSBuild's upward Directory.Build.targets search
# never finds mxbmrp3/, and the MXB-Debug config name they keyed on is gone.)
option(MXBMRP3_ASAN
       "Build the MSVC plugin DLLs with AddressSanitizer (/fsanitize=address)" OFF)
if(MSVC AND MXBMRP3_ASAN)
    # CMake's default Debug flags include /RTC1, which is incompatible with
    # /fsanitize=address (cl hard-errors with D8016). Strip it for both
    # languages — the plugin targets compile C++ plus the vendored miniz C.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

set(MXB_SRC ${CMAKE_CURRENT_SOURCE_DIR})

# --- sources ----------------------------------------------------------------
# Everything under the four product directories, plus miniz. The per-game DLL
# export TU (vendor/piboso/*_api.cpp) is chosen per target below — that was the
# vcxproj's only per-configuration file exclusion.
file(GLOB_RECURSE MXB_SOURCES CONFIGURE_DEPENDS
     ${MXB_SRC}/core/*.cpp
     ${MXB_SRC}/handlers/*.cpp
     ${MXB_SRC}/hud/*.cpp
     ${MXB_SRC}/diagnostics/*.cpp)
file(GLOB MXB_MINIZ CONFIGURE_DEPENDS ${MXB_SRC}/vendor/miniz/*.c)

# core/test_hooks.cpp exposes MXBMRP3_Test_* exports for the integration
# harness. It was kept out of the vcxproj by hand so it could not reach a
# shipping DLL; a glob would have quietly pulled it back in, so the exclusion is
# now a rule rather than an omission.
if(NOT MXBMRP3_TEST_BUILD)
    list(REMOVE_ITEM MXB_SOURCES ${MXB_SRC}/core/test_hooks.cpp)
else()
    # GAME_HAS_DISCORD is 0 under MXBMRP3_TEST_BUILD, so every call compiles out
    # and the TU would only drag the SDK in.
    list(REMOVE_ITEM MXB_SOURCES ${MXB_SRC}/core/discord_manager.cpp)
endif()

# --- version stamp ----------------------------------------------------------
# resource.h's 4th component comes from the git commit count, written to the
# git-ignored version_build.g.h. Re-run on every build (not just configure) so
# the number tracks HEAD; a git failure falls back to 0 so the build never
# breaks. This is the vcxproj's StampVersion target.
add_custom_target(mxbmrp3_stamp_version ALL
    COMMAND ${CMAKE_COMMAND}
            -DMXB_SRC=${MXB_SRC}
            -P ${CMAKE_SOURCE_DIR}/cmake/stamp_version.cmake
    COMMENT "Stamping version_build.g.h from the git commit count"
    VERBATIM)

# --- one target per game ----------------------------------------------------
# NAME       — target and output basename (mxbmrp3.dlo, mxbmrp3_gpb.dlo, ...)
# GAME_DEF   — the compile-time game selector from game/game_config.h
# API_TU     — that game's DLL-export TU
function(mxb_add_plugin NAME GAME_DEF API_TU)
    add_library(${NAME} SHARED ${MXB_SOURCES} ${MXB_MINIZ}
                ${MXB_SRC}/vendor/piboso/${API_TU})
    add_dependencies(${NAME} mxbmrp3_stamp_version)

    set_target_properties(${NAME} PROPERTIES
        OUTPUT_NAME ${NAME}
        PREFIX ""          # no "lib" prefix from the mingw side
        SUFFIX ".dlo"      # PiBoSo loads .dlo, not .dll
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)

    target_include_directories(${NAME} PRIVATE ${MXB_SRC})
    target_compile_definitions(${NAME} PRIVATE ${GAME_DEF} NOMINMAX)

    if(MSVC)
        # /MT (static CRT) is LOAD-BEARING, not a default: it is what makes
        # "no Visual C++ Redistributable required" true (see CHANGELOG v1.15).
        # CMake's own default is /MD — leaving this unset produces a DLL that
        # loads on a dev box and fails for every user without the redist, and no
        # test in this repo can see it.
        #
        # Under MXBMRP3_ASAN the CRT flips to DYNAMIC (/MDd Debug, /MD Release):
        # static ASan is per-module and fails to resolve across the boundary
        # fuzzer's LoadLibrary of the instrumented DLL on recent MSVC toolsets
        # (error 127, ERROR_PROC_NOT_FOUND) — both modules must share the single
        # clang_rt.asan_dynamic-x86_64.dll. See tests/asan/README.md's
        # ASan-runtime note. ASan builds are never shipped, so the redist
        # constraint above doesn't apply to them.
        if(MXBMRP3_ASAN)
            set_property(TARGET ${NAME} PROPERTY
                         MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            target_compile_options(${NAME} PRIVATE /fsanitize=address)
            # ASan requires non-incremental linking (the Debug default is
            # incremental); CMake maps this to LinkIncremental=false.
            target_link_options(${NAME} PRIVATE /INCREMENTAL:NO)
        else()
            set_property(TARGET ${NAME} PROPERTY
                         MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        # UNICODE/_UNICODE is the vcxproj's <CharacterSet>Unicode</CharacterSet>,
        # which MSBuild expands to these two defines and CMake does not. Without
        # them the TCHAR macros resolve to their ANSI variants: logger.cpp passes
        # a std::wstring to SetConsoleTitle, which then binds to SetConsoleTitleA
        # and fails to compile. It only bites in a DEBUG build (the console block
        # is inside #ifdef _DEBUG), which is why the mingw cross-build — always
        # NDEBUG — never saw it.
        target_compile_definitions(${NAME} PRIVATE UNICODE _UNICODE _CONSOLE
            $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
        # /W4 /WX, raised from /W3 with no /WX. The mingw cross-build was already
        # held at -Wall -Wextra -Werror, but that is not the compiler that
        # produces what users run, and MSVC's diagnostics are not a subset of
        # GCC's: raising this found 33 warnings in 3 codes that no other gate in
        # the project could see. Most were hygiene (C4456 shadowed locals), but
        # C4127 caught a divide-by-zero guard comparing two compile-time
        # constants — a branch that could never fire, now a static_assert that
        # can. Both compilers now start and stay at zero.
        target_compile_options(${NAME} PRIVATE
            /W4 /WX /sdl /permissive- /MP
            $<$<NOT:$<CONFIG:Debug>>:/O2 /Ot /Oi /Gy /GL>)

        # The same two exemptions the mingw branch takes, for the same reasons,
        # so the two compilers hold the same bar rather than each having its own
        # idea of what is exempt. miniz is vendored third-party C; the PiBoSo
        # export TU's signatures are the game's, and C4100 is MSVC's spelling of
        # -Wunused-parameter.
        #
        # The /w below costs one cl warning D9025 ("overriding '/W4' with '/w'")
        # per compiler PROCESS — with /MP, parent plus one child per file, times
        # three game targets: ~15 lines on a clean build. It is cosmetic and
        # cannot fail anything (/WX governs C#### compiler warnings; D#### are
        # command-line diagnostics, and neither CI job passes /warnaserror).
        #
        # Scoping the bar with $<COMPILE_LANGUAGE:CXX> does NOT fix it, so don't
        # re-try that: CMake's Visual Studio generator recognises /W4 and hoists
        # it into the <WarningLevel>Level4</WarningLevel> MSBuild property, which
        # is per-CONFIGURATION, not per-language — verified in the generated
        # vcxproj. No generator expression can spare the C files. The only real
        # fix is giving miniz its own target, so it never inherits the flag;
        # deliberately not done, since that puts a second target's runtime
        # library (/MT is load-bearing above) and /GL between us and the shipping
        # DLL to remove cosmetic noise.
        set_source_files_properties(${MXB_MINIZ}
            PROPERTIES COMPILE_OPTIONS "/w")
        set_source_files_properties(${MXB_SRC}/vendor/piboso/${API_TU}
            PROPERTIES COMPILE_OPTIONS "/wd4100")
        target_link_options(${NAME} PRIVATE
            /SUBSYSTEM:CONSOLE           # matches the vcxproj; inert for a DLL
            /DEBUG                       # .pdb for both configs, as before
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF /LTCG>
            # A linker .map lets a crash-dashboard "mxbmrp3.dlo+0xNNNN" offset be
            # resolved without the .pdb. Output-only; archived by the release
            # workflow and make_release.bat next to the .pdb.
            $<$<NOT:$<CONFIG:Debug>>:/MAP>)
        # winmm: SpotterManager's PlaySound. ole32 (SAPI's CoCreateInstance) is
        # already in CMake's default MSVC standard libraries; the mingw branch
        # below links both explicitly.
        target_link_libraries(${NAME} PRIVATE xinput winmm)

        # Analytics secrets, injected from the environment exactly as the vcxproj
        # did: each define is added ONLY when its variable is set, so a plain
        # developer build omits them and the code falls back to its placeholder
        # (analytics disabled). A Release build with neither set hard-#errors in
        # plugin_constants.h by design — that is what stops an official .dlo
        # shipping with analytics silently off.
        #
        # The value is a BARE TOKEN, not a string: plugin_constants.h stringifies
        # it with MXBMRP3_STRINGIFY. Keys are alphanumeric/hyphen so they pass
        # through unquoted, same as /D did.
        #
        # TIMING DIFFERENCE FROM MSBUILD, and it matters for release.yml: MSBuild
        # read these per BUILD, CMake reads them at CONFIGURE. The env must be set
        # for the `cmake -S .. -B ..` step, not just `cmake --build`; a tree
        # configured without them keeps building keyless until it is re-configured.
        foreach(_secret APTABASE_KEY GOATCOUNTER_CODE GOATCOUNTER_TOKEN)
            if(NOT "$ENV{${_secret}}" STREQUAL "")
                target_compile_definitions(${NAME} PRIVATE
                                           MXBMRP3_${_secret}=$ENV{${_secret}})
            endif()
        endforeach()

        # Keep the artifact where make_release.bat and release.yml already look:
        # build\<GAME>-<Config>\<name>.dlo. Those two consumers hardcode these
        # paths, so preserving them is deliberate.
        string(TOUPPER ${GAME_DEF} _g)
        string(REPLACE "GAME_MXBIKES" "MXB" _g ${_g})
        string(REPLACE "GAME_GPBIKES" "GPB" _g ${_g})
        string(REPLACE "GAME_KRP" "KRP" _g ${_g})
        foreach(cfg Debug Release)
            string(TOUPPER ${cfg} _CFG)
            set_target_properties(${NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${_CFG} ${CMAKE_SOURCE_DIR}/build/${_g}-${cfg})
        endforeach()

        # The vcxproj's PostBuildEvent: drop the freshly built plugin straight
        # into the game if MXB_PLUGIN_PATH is set. This is the inner loop —
        # build, alt-tab, test — so it matters more than it looks. Routed through
        # a cmake -P script rather than the batch one-liner; see its header for
        # why the direct translation cannot work.
        # Per game: <MXB|GPB|KRP>_PLUGIN_PATH. One shared variable meant a full
        # build dropped all three plugins into whichever game's folder it named.
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DDLL=$<TARGET_FILE:${NAME}>
                    -DVAR=${_g}_PLUGIN_PATH
                    -P ${CMAKE_SOURCE_DIR}/cmake/copy_to_plugin_path.cmake
            VERBATIM)
    else()
        # mingw cross-build. -w matches the Makefile it replaced: this
        # configuration exists to produce a testable DLL, and the warning bar is
        # held by the -Werror unit targets and the cppcheck/clang gates instead.
        #
        # -O1, NOT the shipping build's optimization. The MSVC targets above are
        # /O2 /Ot /Oi /Gy /GL + /LTCG, so every number run_perf.sh reports comes
        # from a SLOWER binary than the one users get. Measured on this tree
        # (medians of 3 interleaved reps, same host): -O2 is 13.6% faster on
        # Draw average and -O2 -flto 15.9%, with the standings rebuild the
        # biggest single winner at ~25%; -flto also shrinks the DLL 8.7% where
        # -O2 alone shrinks it 1.8%.
        #
        # Kept at -O1 deliberately. The gap is immaterial where it lands (Draw
        # average is 8.9% of the 2083us budget at -O1 and 7.5% at -O2 -flto —
        # 12x headroom either way), -O2 cost ~156s and LTO ~211s per clean
        # cross-build against a job that is already the largest single consumer
        # of CI minutes, and a perf gate measured on the pessimistic build is
        # the right way round: passing here implies passing on what ships.
        # -Wall -Wextra -Werror. This job is the ONLY one that compiles the whole
        # tree, and it used to pass -w — so ~86k lines had no warning bar at all
        # (the -Werror unit targets cover ~1k lines, and cppcheck is not a
        # compiler). Turning them on found 41 first-party warnings; all are fixed,
        # so the bar starts and stays at zero. -Werror rather than report-only on
        # purpose: this project already learned that lesson from cppcheck, which
        # sat non-blocking while banking two real error-severity findings nobody
        # read (commit d297cc01).
        #
        # -Wno-unknown-pragmas: the tree carries MSVC #pragma warning(...)
        # directives for the shipping compiler, which GCC cannot know.
        #
        # -Wshadow=local mirrors MSVC's C4456, which is an ERROR under the
        # shipping build's /WX and is in neither -Wall nor -Wextra — so a local
        # shadowing a local passed every gate here and failed on the user's
        # machine (a `const double b` beside panel_box.h's button box, reported
        # as two bare C2220 lines with no warning text). =local rather than
        # plain -Wshadow deliberately: GCC's full form also flags globals, which
        # would fire on enum members named SHORT/LONG/FIXED that collide with
        # windows.h typedefs and that MSVC is perfectly happy with. The tree is
        # clean at this bar, so it starts and stays at zero like the rest.
        target_compile_options(${NAME} PRIVATE
            -m64 -O1 -Wall -Wextra -Werror -Wshadow=local -Wno-unknown-pragmas)
        target_link_options(${NAME} PRIVATE
            -m64 -static -static-libgcc -static-libstdc++)
        target_link_libraries(${NAME} PRIVATE
            ws2_32 winhttp bcrypt dbghelp xinput winmm ole32 shlwapi
            user32 gdi32 dwmapi)

        # Two exemptions from the warning bar above, both narrow and both stated
        # rather than blanket-suppressed:
        #  - miniz is vendored third-party C. Its warnings are upstream's to fix,
        #    and patching them locally is what vendored.json exists to prevent.
        #  - The PiBoSo export TU's signatures are dictated by the game's API, so
        #    unused parameters are inherent. The NAMES document that contract
        #    (which callback field is which), so they are worth more kept than
        #    commented out to silence a warning we cannot act on.
        set_source_files_properties(${MXB_MINIZ}
            PROPERTIES COMPILE_OPTIONS "-w")
        set_source_files_properties(${MXB_SRC}/vendor/piboso/${API_TU}
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
    endif()
endfunction()

if(MXBMRP3_TEST_BUILD)
    # One MX Bikes target, built as the harness expects to find it. Analytics and
    # Discord are compiled out and SEH is MSVC-only, so this is NOT shippable —
    # every divergence is gated on MXBMRP3_TEST_BUILD / _MSC_VER.
    mxb_add_plugin(mxbmrp3_test GAME_MXBIKES mxb_api.cpp)
    target_compile_definitions(mxbmrp3_test PRIVATE
        NDEBUG MXBMRP3_TEST_BUILD MXBMRP3_ALLOW_NO_ANALYTICS)
    target_include_directories(mxbmrp3_test PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/integration/shim)
    # The harness .exes are built into tests/integration/build/ and load the DLL
    # by bare name from their own directory, so it has to land beside them. Every
    # runner script (run_tests / run_fuzz / run_perf / ...) assumes this path.
    set_target_properties(mxbmrp3_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/integration/build)
else()
    mxb_add_plugin(mxbmrp3     GAME_MXBIKES mxb_api.cpp)
    mxb_add_plugin(mxbmrp3_gpb GAME_GPBIKES gpb_api.cpp)
    mxb_add_plugin(mxbmrp3_krp GAME_KRP     krp_api.cpp)

    # The .rc carries the DLL's FILEVERSION/product strings. MSVC only: the
    # cross-build has no use for version resources and windres would need its
    # own handling.
    if(MSVC)
        foreach(t mxbmrp3 mxbmrp3_gpb mxbmrp3_krp)
            target_sources(${t} PRIVATE ${MXB_SRC}/mxbmrp3.rc)
        endforeach()
    endif()

    # Replaces the build_all Utility project: build every game in one action,
    # which is what Ctrl+Shift+B does in the generated solution anyway.
    add_custom_target(build_all DEPENDS mxbmrp3 mxbmrp3_gpb mxbmrp3_krp)
endif()
