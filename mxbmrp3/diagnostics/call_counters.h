// ============================================================================
// diagnostics/call_counters.h
// HOW MANY TIMES, not how long -- a per-rebuild call census for the hot layout
// helpers, compiled into TEST BUILDS ONLY.
//
// WHY COUNTS RATHER THAN TIMES. The headless harness runs the plugin under Wine,
// where some libc calls (strcmp above all) cost several times what they do on
// native Windows. A timing comparison made there reported a font-lookup memo as a
// 10x win; on the reporting user's own hardware it was 5%. A CALL COUNT cannot be
// distorted that way -- the number of times a function runs is a property of the
// code, identical on every platform -- so a census taken here transfers, and the
// expensive question ("which helper is called 40 times per rebuild?") is answerable
// from the harness instead of from someone else's game.
//
// TEST BUILDS ONLY, and by compile-time gate rather than a runtime `if`: these sit
// inside activeTheme() and layout(), which are themselves the hot path this is
// hunting, so a branch per call would tax the very thing being measured. The
// shipping DLL has no counter and no branch -- MXB_COUNT_CALL compiles to nothing.
// ============================================================================
#pragma once

#if defined(MXBMRP3_TEST_BUILD)

#include <cstdio>
#include <string>

namespace CallCounters {

// One slot per counted helper. Add to BOTH the enum and kNames -- they are indexed
// in lockstep and a mismatch prints the wrong label rather than failing to build,
// so keep them adjacent.
enum Slot {
    ACTIVE_THEME = 0,
    LAYOUT,
    FONT_INDEX_BY_NAME,
    GET_FONT,
    GET_SCALED_DIMENSIONS,
    PLAN_PANEL,
    ADD_STRING,
    SLOT_COUNT
};

inline const char* const* names() {
    static const char* kNames[SLOT_COUNT] = {
        "activeTheme", "layout", "getFontIndexByName", "getFont",
        "getScaledDimensions", "planPanel", "addString"
    };
    return kNames;
}

// Plain longs, not atomics: every counted helper runs on the game thread (the
// render build), and an atomic increment inside activeTheme() would cost more than
// the call it is measuring. A stray count from another thread would skew a
// diagnostic, not corrupt anything.
inline long long* counts() {
    static long long s_counts[SLOT_COUNT] = {};
    return s_counts;
}

inline void bump(Slot s) { ++counts()[s]; }

inline void reset() {
    long long* c = counts();
    for (int i = 0; i < SLOT_COUNT; ++i) c[i] = 0;
}

// Per-rebuild averages, which is the figure that means something: a helper called
// twice per rebuild is fine and one called forty times is the finding.
inline std::string report(long long rebuilds) {
    std::string out = "=== CALL CENSUS (per rebuild, test build only) ===\n";
    char line[160];
    if (rebuilds <= 0) return out + "  (no rebuilds recorded)\n";
    const long long* c = counts();
    const char* const* n = names();
    for (int i = 0; i < SLOT_COUNT; ++i) {
        snprintf(line, sizeof(line), "  %-22s %10lld total  %8.1f per rebuild\n",
                 n[i], c[i], static_cast<double>(c[i]) / static_cast<double>(rebuilds));
        out += line;
    }
    return out;
}

}  // namespace CallCounters

#define MXB_COUNT_CALL(slot) ::CallCounters::bump(::CallCounters::slot)

#else

#define MXB_COUNT_CALL(slot) ((void)0)

#endif  // MXBMRP3_TEST_BUILD
