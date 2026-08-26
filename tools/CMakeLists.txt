# ============================================================================
# tools/CMakeLists.txt — the two Windows dev tools that used to live in
# mxbmrp3.sln as hand-maintained vcxproj files. NOT every tool under tools/:
# mxbmrp3_hud_window builds through its own companion_demo.sh (mingw + Wine).
# Only these two were vcxproj projects.
#
# They came along in the CMake migration for one reason: deleting mxbmrp3.sln
# would otherwise have orphaned them, leaving two build definitions in a repo
# that had just got down to one. Neither ships — they are console utilities a
# developer runs by hand — so they get plain defaults and none of the plugin's
# release flags.
#
#   mxbmrp3_replay  — drives a recorded callback tape through a built .dlo
#   mxbmrp3_fontgen — regenerates the shipped .fnt bitmap fonts from a .ttf
#                     (also builds cross-platform via its own test.sh, which is
#                     what CI runs; this target is just the Windows convenience)
#
# ============================================================================

# Both are Windows-only in this form: mxbmrp3_replay LoadLibrary()s a .dlo, and the
# both were MSVC-only projects. The Linux side builds fontgen through test.sh.
if(NOT WIN32)
    return()
endif()

function(mxb_add_tool NAME)
    add_executable(${NAME} ${ARGN})
    set_target_properties(${NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # _CRT_SECURE_NO_WARNINGS: both use fopen/sscanf deliberately. UNICODE pair
    # matches the vcxproj's <CharacterSet>Unicode</CharacterSet>, the same
    # setting whose omission broke the plugin's Debug build during the migration.
    target_compile_definitions(${NAME} PRIVATE
        UNICODE _UNICODE _CONSOLE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        # Static CRT, as before, so a tool binary runs without the redist.
        set_property(TARGET ${NAME} PROPERTY
                     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(${NAME} PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
endfunction()

mxb_add_tool(mxbmrp3_replay ${CMAKE_CURRENT_SOURCE_DIR}/replay/main.cpp)
target_include_directories(mxbmrp3_replay PRIVATE ${CMAKE_SOURCE_DIR}/mxbmrp3)

mxb_add_tool(mxbmrp3_fontgen
    ${CMAKE_CURRENT_SOURCE_DIR}/fontgen/mxbmrp3_fontgen.cpp
    ${CMAKE_SOURCE_DIR}/mxbmrp3/vendor/miniz/miniz.c
    ${CMAKE_SOURCE_DIR}/mxbmrp3/vendor/miniz/miniz_tdef.c
    ${CMAKE_SOURCE_DIR}/mxbmrp3/vendor/miniz/miniz_tinfl.c)

