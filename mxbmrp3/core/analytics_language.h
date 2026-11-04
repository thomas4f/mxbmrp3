// ============================================================================
// core/analytics_language.h
// The language the player READS, reduced to ONE analytics label.
//
// WHAT IS IN EFFECT, NOT WHAT IS STORED. The setting stores a choice ("system",
// a folder, or "" for English), and the default "system" says nothing about the
// language on screen until it is resolved against Windows and the installed
// packs. The label is the resolved pack (I18n::activeFolder()), so a Swedish
// Windows with the Swedish pack reports "sv" and a Polish one, which no pack
// matches, reports "en". Whether the choice was the System default travels
// beside it as language_system, which is what tells "picked English" apart from
// "Windows asked for a language we do not have".
//
// WHY A CLASSIFIER RATHER THAN THE FOLDER. A pack is a folder a user may name
// anything, like a theme. The shipped packs are ours and report themselves;
// any other collapses to "custom", so a folder name off someone's disk never
// leaves the machine (the same line analytics_theme.h holds).
//
// No "missing": a stored folder that is not installed shows English, and
// English is what the player reads, so that is what is reported.
//
// Pure and header-only so the unit suite can reach it: analytics_manager.h is
// Win32/WinHTTP-bound and does not link into tests/unit.
// ============================================================================
#pragma once

#include <string>

namespace AnalyticsLanguage {

// The packs that ship in mxbmrp3_data/languages/, reported by folder name
// because they are ours ("pseudo" is the developer pseudo-locale). Membership is
// what matters; tests/unit/test_analytics_language.cpp walks the shipped
// directory and fails when a pack folder lands without being added here, which
// would otherwise file its readers under "custom" with no way to notice.
inline constexpr const char* kShippedPacks[] = {
    "de", "es", "fr", "it", "nb", "nl", "pseudo", "pt", "sv", "tr",
};

inline bool isShipped(const std::string& folder) {
    for (const char* s : kShippedPacks) {
        if (folder == s) return true;
    }
    return false;
}

// The pack in effect ("" = English) as the label sent:
//   ""              -> "en"
//   a shipped pack  -> its folder ("fr", "sv", ...)
//   any other       -> "custom"
inline std::string label(const std::string& activeFolder) {
    if (activeFolder.empty()) return "en";
    return isShipped(activeFolder) ? activeFolder : "custom";
}

}  // namespace AnalyticsLanguage
