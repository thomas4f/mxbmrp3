// ============================================================================
// core/analytics_theme.h
// The active panel theme, reduced to ONE analytics label.
//
// WHY A CLASSIFIER RATHER THAN THE NAME. A theme is a folder in
// mxbmrp3_data/themes/ and the setting stores its NAME, so the raw value is a
// string a user typed — anything from "carbon-dark" to a personal folder name. The
// shipped set is ours and is safe to report verbatim; everything else collapses
// to "custom", which still answers the question worth asking (how many players
// run a third-party theme) without an arbitrary string from someone's disk
// leaving the machine. The rest of this payload holds to the same line: the
// only identifier ever sent is a random UUID.
//
// WHY THE `installed` FLAG. The stored name and the theme actually on screen
// are not the same question. An unknown name degrades to a flat panel WITHOUT
// rewriting the setting (see the asset-pack invariant in CLAUDE.md), so a
// user who deleted a theme folder still has its name in their INI while
// looking at unthemed panels. Reporting that as "custom" would both overstate
// third-party adoption and hide the broken reference — hence "missing", which
// is a diagnosis rather than a bucket.
//
// Pure and header-only so the unit suite can reach it: analytics_manager.h is
// Win32/WinHTTP-bound and does not link into tests/unit.
// ============================================================================
#pragma once

#include <string>

namespace AnalyticsTheme {

// The themes that ship in mxbmrp3_data/themes/, reported by name because they
// are ours. Sorted for readability only; membership is what matters.
//
// Adding a theme folder without adding it here would silently file its users
// under "custom" — so tests/unit/test_analytics_theme.cpp walks the shipped
// directory and fails on exactly that omission.
inline constexpr const char* kShippedThemes[] = {
    "carbon-dark", "carbon-light",
};

inline bool isShipped(const std::string& name) {
    for (const char* s : kShippedThemes) {
        if (name == s) return true;
    }
    return false;
}

// What the player is ACTUALLY looking at:
//   ""                     -> "none"    (no theme selected — the flat panel)
//   a shipped theme        -> its name  ("carbon-dark", "carbon-light")
//   another, installed     -> "custom"  (third-party or hand-rolled)
//   another, NOT installed -> "missing"  (stale setting; renders unthemed)
//
// `installed` is the caller's real lookup (AssetManager::getThemeByName), which
// matches exactly — so a case variant of a shipped name is a different folder,
// and reporting it as "custom" is the truth rather than a near-miss.
inline std::string label(const std::string& themeName, bool installed) {
    if (themeName.empty()) return "none";
    if (!installed) return "missing";
    return isShipped(themeName) ? themeName : "custom";
}

}  // namespace AnalyticsTheme
