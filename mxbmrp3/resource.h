// ============================================================================
// resource.h
// Version defines for the Windows resource file (.rc) and the runtime string.
//
// MAJOR/MINOR/PATCH are the manual source of truth (bump them for a release). The
// 4th component (BUILD) is stamped AUTOMATICALLY at build time from the git commit
// count - see the "StampVersion" target in mxbmrp3.vcxproj, which writes
// version_build.g.h (git-ignored) before every compile. Nothing else to edit:
// VER_STRING and core/plugin_constants.h's PLUGIN_VERSION derive from these.
// ============================================================================
#pragma once

#define VER_MAJOR 1
#define VER_MINOR 27
#define VER_PATCH 5

// Generated per build (git-ignored); defines VER_BUILD_AUTO = git commit count.
// The StampVersion target writes it before ClCompile/ResourceCompile, so it always
// exists for cl AND rc during a real build. The guard below is ONLY so a FRESH CLONE
// (before the first build, file absent) doesn't hard-error under IntelliSense / a
// static analyzer - a plain #include of a missing file is fatal, so the #ifndef
// fallback alone can't save it. During real builds the file is present, so rc.exe
// still gets the true FILEVERSION (never the 0 fallback).
#if defined(__INTELLISENSE__)
    // VS IntelliSense never has the generated file; use the fallback below.
#elif defined(__has_include)
#   if __has_include("version_build.g.h")
#       include "version_build.g.h"
#   endif
#else
#   include "version_build.g.h"   // rc.exe / older toolchains: present during builds
#endif
#ifndef VER_BUILD_AUTO
#define VER_BUILD_AUTO 0
#endif
#define VER_BUILD VER_BUILD_AUTO

// Compose "MAJOR.MINOR.PATCH.BUILD" from the numeric macros (single source of truth,
// so the string can never drift from the FILEVERSION). Works in both cl and rc.
#define VER_STRINGIZE2(x) #x
#define VER_STRINGIZE(x) VER_STRINGIZE2(x)
#define VER_STRING \
    VER_STRINGIZE(VER_MAJOR) "." VER_STRINGIZE(VER_MINOR) "." \
    VER_STRINGIZE(VER_PATCH) "." VER_STRINGIZE(VER_BUILD)
