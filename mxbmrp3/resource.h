// ============================================================================
// resource.h
// Version defines for the Windows resource file (.rc) and the runtime string.
//
// MAJOR/MINOR/PATCH are the manual source of truth (bump them for a release). The
// 4th component (BUILD) is stamped AUTOMATICALLY at build time from the git commit
// count - see cmake/stamp_version.cmake, run by the mxbmrp3_stamp_version target,
// which writes version_build.g.h (git-ignored) before every compile. Nothing else to edit:
// VER_STRING and core/plugin_constants.h's PLUGIN_VERSION derive from these.
// ============================================================================
#pragma once

#define VER_MAJOR 1
#define VER_MINOR 35
#define VER_PATCH 1

// Generated per build (git-ignored); defines VER_BUILD_AUTO = git commit count.
// The stamp target writes it before compiling and before rc, so it always
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

// Where the source of THIS build lives, for anyone who opens the binary instead.
// The vX.Y.Z tag is what release.yml cuts in the public repo at the commit it
// builds, so it names the exact source of every shipped .dlo. A dev build between
// releases still carries the PREVIOUS release's numbers (the bump comes with the
// release), so its link shows that release's source, not its own. No commit hash: the
// mirror squashes history, so a private-checkout hash names nothing public.
// Plain literals because rc.exe reads this file too, which is also why the repo
// URL is spelled out rather than built from plugin_constants.h's
// GITHUB_REPO_OWNER/NAME (constexpr, C++ only); plugin_version.cpp
// static_asserts that the two agree.
// Read by mxbmrp3.rc (Comments) and core/plugin_version.cpp (SOURCE_NOTICE).
#define VER_TAG \
    "v" VER_STRINGIZE(VER_MAJOR) "." VER_STRINGIZE(VER_MINOR) "." VER_STRINGIZE(VER_PATCH)
#define VER_SOURCE_URL "https://github.com/thomas4f/mxbmrp3/tree/" VER_TAG
// The project's rules for anyone changing or forking it (name, identity on disk,
// limits), on the same tag. The section asks AI assistants to be pointed at it.
#define VER_RULES_URL \
    "https://github.com/thomas4f/mxbmrp3/blob/" VER_TAG "/CONTRIBUTING.md#building-on-mxbmrp3"
#define VER_COMMENTS   "Open source (MIT). Source: " VER_SOURCE_URL
