// ============================================================================
// core/plugin_names.h
// The two folder names the plugin owns on a player's disk, spelled once.
//
//   MXB_USER_DIR_NAME  Documents\PiBoSo\<Game>\mxbmrp3\  settings, stats, log,
//                      crashes, tapes, benchmarks, user packs
//   MXB_DATA_DIR_NAME  <Game>\plugins\mxbmrp3_data\      shipped fonts, icons,
//                      packs, web overlay
//
// WHY MACROS, NOT constexpr. Most sites build a path by literal concatenation --
// "%s\\" MXB_USER_DIR_NAME "\\crashes" in the crash filter's snprintf, or
// "plugins\\" MXB_DATA_DIR_NAME "\\web" -- which keeps them compile-time
// literals: no allocation, and safe inside the SEH filter. A constexpr pointer
// cannot be concatenated, so it would force a runtime join at every such site.
//
// WHY THE VALUES NEVER CHANGE. These are folders players already have. A new
// value means every player's settings, stats and PBs "vanish" (the old folder
// is still there, unread) and the updater stops recognising its own data folder.
// The point of one definition is that no site can drift from the others, not
// that the name is meant to move.
//
// Dependency-free on purpose: the logger, the crash handler and header-only
// helpers the unit suite compiles directly all include it.
//
// ENFORCED by tests/integration/check_name_literals.sh: a string literal using
// either name as a path segment anywhere else under mxbmrp3/ fails CI.
// ============================================================================
#pragma once

#define MXB_USER_DIR_NAME "mxbmrp3"
#define MXB_DATA_DIR_NAME "mxbmrp3_data"
