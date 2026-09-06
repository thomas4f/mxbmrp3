// ============================================================================
// core/analytics_manager_internal.h
// Shared internal helpers for the AnalyticsManager translation units
// (analytics_manager*.cpp): the two clock readings both the live identity /
// session code and the test seam need. Header-inline so every TU sees one
// definition without ODR conflicts.
// ============================================================================
#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <string>

namespace AnalyticsInternal {

// Seconds since the Unix epoch (UTC).
inline unsigned long long epochSecondsNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;
    return (u.QuadPart - EPOCH_DIFF_100NS) / 10000000ULL;
}

// Per-launch session id, in Aptabase's exact format: epoch SECONDS * 1e8 plus
// an 8-digit random suffix (an ~18-digit number). This is critical — Aptabase
// derives the session's start time as (sessionId / 100000000), so a plain
// epoch-millis id decodes to a 1970 session date and the event never appears in
// the dashboard. Matches what Aptabase's own SDKs emit.
inline std::string makeSessionId() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100ns ticks since 1601; convert to seconds since Unix epoch.
    const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;
    unsigned long long epochSeconds = (u.QuadPart - EPOCH_DIFF_100NS) / 10000000ULL;

    // 8-digit random suffix (0..99,999,999).
    unsigned int rnd = 0;
    unsigned long long suffix;
    if (BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<unsigned char*>(&rnd),
                                       sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        suffix = rnd % 100000000ULL;
    } else {
        suffix = u.QuadPart % 100000000ULL;  // fallback: sub-second clock bits
    }

    unsigned long long sid = epochSeconds * 100000000ULL + suffix;
    char out[32];
    snprintf(out, sizeof(out), "%llu", sid);
    return std::string(out);
}

}  // namespace AnalyticsInternal
