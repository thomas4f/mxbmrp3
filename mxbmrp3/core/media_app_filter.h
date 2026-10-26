// ============================================================================
// core/media_app_filter.h
// Now playing's player filter ([General] nowPlayingApps, INI-only): which
// media players MediaManager follows, matched against a session's app id as
// Windows reports it ("Spotify.exe", "SpotifyAB.SpotifyMusic_...!Spotify",
// "308046B0AF4A39CB" for Firefox, ...). MediaManager logs Windows' current
// player's id whenever it changes, so the log is where a player finds what to type.
//
// Comma-separated names, each matched case-insensitively anywhere in the id;
// empty = every player (Windows' current one). Pure, so it is unit-tested
// (tests/unit/test_media_app_filter.cpp).
// ============================================================================
#pragma once

#include <cctype>
#include <cstddef>

namespace MediaAppFilter {

// The longest filter kept ([General] nowPlayingApps).
constexpr size_t MAX_CHARS = 255;

// True when `appId` matches one of `filter`'s comma-separated names (spaces
// around a name ignored), or when the filter names none.
inline bool matches(const char* appId, const char* filter) {
    if (!filter) return true;
    bool anyName = false;
    const char* p = filter;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        const char* start = p;
        while (*p && *p != ',') ++p;
        const char* end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
        const size_t len = static_cast<size_t>(end - start);
        if (len == 0) continue;
        anyName = true;
        for (const char* a = appId ? appId : ""; *a; ++a) {
            size_t i = 0;
            while (i < len && a[i] &&
                   std::tolower(static_cast<unsigned char>(a[i])) ==
                   std::tolower(static_cast<unsigned char>(start[i]))) {
                ++i;
            }
            if (i == len) return true;
        }
    }
    return !anyName;
}

}  // namespace MediaAppFilter
