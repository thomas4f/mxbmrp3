// ============================================================================
// tests/unit/test_lap_timer.cpp
// The display rider's live lap-timer state machine (core/lap_timer.h),
// previously split between plugin_data_types.h (state) and PluginData methods
// (transitions), reachable only through the DLL under Wine. Pins:
//   - S/F detection via wraparound: only a backward jump past WRAP_THRESHOLD
//     counts, and only when unanchored or the lap number changed — driving
//     forward normally, or jitter near the line, must not re-anchor
//   - THE GRID-START GRACE: after onRaceStart() the opening lap's S/F crossing
//     must NOT re-anchor to 0 (it would drop the grid->S/F time and make the
//     display jump when the first official split resyncs). The grace ends via
//     onLapComplete (lap 1 done) or invalidateAnchor (lap died in the pits) —
//     after either, the next crossing anchors normally
//   - official splits: S1 -> sector 1 + split1 stamped, S2 -> sector 2 +
//     split2 stamped, both resync the anchor to accumulated time
//   - invalidateAnchor: elapsed reads the -1 placeholder, track monitoring
//     stays alive so the next S/F crossing recovers
//   - sector elapsed math: S2/S3 read relative to the stamped splits, -1
//     before their split exists
// Transition STATE is asserted, not wall-clock durations — the timer anchors
// on the real steady_clock, so elapsed values are only checked for
// anchor-accumulated offsets and placeholders.
// The end-to-end wiring (display-rider gating, spectate switches) stays with
// PluginData and is pinned by the integration lap/timing tests.
// ============================================================================
#include "doctest.h"

#include "core/lap_timer.h"

namespace {

// Drive one normal lap of forward progress up to just before the line.
void driveTo(LapTimer& t, float from, float to, int lapNum, float step = 0.1f) {
    for (float p = from; p <= to; p += step) {
        t.onTrackPosition(p, lapNum);
    }
}

}  // namespace

TEST_CASE("S/F crossing detection") {
    LapTimer t;

    SUBCASE("first sample only initializes the monitor") {
        CHECK_FALSE(t.onTrackPosition(0.95f, 0));
        CHECK(t.trackMonitorInitialized);
        CHECK_FALSE(t.anchorValid);
    }
    SUBCASE("wraparound with a lap change anchors from 0") {
        t.onTrackPosition(0.95f, 0);
        CHECK(t.onTrackPosition(0.05f, 1));
        CHECK(t.anchorValid);
        CHECK(t.anchorAccumulatedTime == 0);
        CHECK(t.currentLapNum == 1);
        CHECK(t.currentSector == 0);
        CHECK(t.lastSplit1Time == -1);
        CHECK(t.lastSplit2Time == -1);
    }
    SUBCASE("wraparound without a lap change re-anchors only when unanchored") {
        t.onTrackPosition(0.95f, 3);
        CHECK(t.onTrackPosition(0.05f, 3));  // unanchored -> anchors
        // Anchored and same lap: a second backward jump is jitter, not a lap.
        t.onTrackPosition(0.95f, 3);
        CHECK_FALSE(t.onTrackPosition(0.05f, 3));
    }
    SUBCASE("forward progress never triggers") {
        t.onTrackPosition(0.10f, 0);
        driveTo(t, 0.2f, 0.9f, 0);
        CHECK_FALSE(t.anchorValid);
    }
    SUBCASE("small backward jitter never triggers") {
        t.onTrackPosition(0.50f, 0);
        CHECK_FALSE(t.onTrackPosition(0.45f, 0));
        CHECK_FALSE(t.anchorValid);
    }
}

TEST_CASE("grid-start grace") {
    LapTimer t;
    t.onTrackPosition(0.90f, 0);  // on the grid, before the gate
    t.onRaceStart();
    CHECK(t.anchorValid);
    CHECK(t.anchoredFromRaceStart);
    CHECK(t.currentLapNum == 0);

    SUBCASE("the opening lap's S/F crossing keeps the green-flag anchor") {
        // Simulate the official split resync from the grid->S/F run first, so
        // the anchor carries real accumulated time the crossing must not drop.
        t.onOfficialSplit(41000, 0, 1);
        CHECK_FALSE(t.onTrackPosition(0.05f, 1));  // crossing suppressed
        CHECK(t.anchorValid);
        CHECK(t.anchorAccumulatedTime == 41000);   // NOT reset to 0
        CHECK(t.anchoredFromRaceStart);            // grace persists until lap completes
    }
    SUBCASE("lap 1 completing ends the grace; lap 2 anchors normally") {
        t.onTrackPosition(0.05f, 1);   // suppressed opening crossing
        t.onLapComplete(1);
        CHECK_FALSE(t.anchoredFromRaceStart);
        CHECK(t.anchorAccumulatedTime == 0);
        driveTo(t, 0.1f, 0.9f, 1);
        CHECK(t.onTrackPosition(0.05f, 2));  // normal S/F anchor again
    }
    SUBCASE("pitting on lap 1 abandons the grace with the anchor") {
        t.invalidateAnchor();
        CHECK_FALSE(t.anchoredFromRaceStart);
        CHECK(t.getElapsedLapTime() == -1);
        // The next S/F crossing must re-anchor rather than be suppressed —
        // otherwise the timer would stay stuck on the placeholder all lap.
        t.onTrackPosition(0.95f, 0);
        CHECK(t.onTrackPosition(0.05f, 1));
        CHECK(t.anchorValid);
    }
}

TEST_CASE("official splits resync the anchor and advance the sector") {
    LapTimer t;
    t.onTrackPosition(0.95f, 0);
    t.onTrackPosition(0.05f, 1);  // anchored at 0

    t.onOfficialSplit(30500, 1, 0);  // S1
    CHECK(t.currentSector == 1);
    CHECK(t.lastSplit1Time == 30500);
    CHECK(t.anchorAccumulatedTime == 30500);

    t.onOfficialSplit(62000, 1, 1);  // S2
    CHECK(t.currentSector == 2);
    CHECK(t.lastSplit2Time == 62000);
    CHECK(t.anchorAccumulatedTime == 62000);

    // Elapsed reads: lap time is at least the accumulated anchor; sector times
    // are relative to the stamped splits.
    CHECK(t.getElapsedLapTime() >= 62000);
    CHECK(t.getElapsedSectorTime(0) >= 62000);          // S1 view = full lap time
    CHECK(t.getElapsedSectorTime(1) >= 62000 - 30500);  // from S1
    CHECK(t.getElapsedSectorTime(2) >= 0);              // from S2
}

TEST_CASE("sector elapsed reads -1 before their split exists") {
    LapTimer t;
    t.onTrackPosition(0.95f, 0);
    t.onTrackPosition(0.05f, 1);
    CHECK(t.getElapsedSectorTime(1) == -1);  // S1 not crossed yet
    CHECK(t.getElapsedSectorTime(2) == -1);  // S2 not crossed yet
    CHECK(t.getElapsedSectorTime(3) == -1);  // out of range
}

TEST_CASE("invalidateAnchor keeps track monitoring alive") {
    LapTimer t;
    t.onTrackPosition(0.95f, 0);
    t.onTrackPosition(0.05f, 1);
    t.onOfficialSplit(30500, 1, 0);

    t.invalidateAnchor();
    CHECK(t.getElapsedLapTime() == -1);
    CHECK(t.getElapsedSectorTime(0) == -1);
    CHECK(t.trackMonitorInitialized);

    // Next crossing recovers (unanchored path, same lap number is fine).
    t.onTrackPosition(0.95f, 1);
    CHECK(t.onTrackPosition(0.05f, 2));
    CHECK(t.anchorValid);
}

TEST_CASE("pause freezes elapsed; new anchors clear pause") {
    LapTimer t;
    t.onTrackPosition(0.95f, 0);
    t.onTrackPosition(0.05f, 1);

    t.pause();
    CHECK(t.isPaused);
    int frozen = t.getElapsedLapTime();
    CHECK(t.getElapsedLapTime() == frozen);  // paused: same read twice

    t.resume();
    CHECK_FALSE(t.isPaused);

    t.pause();
    t.onOfficialSplit(30500, 1, 0);  // setAnchor clears pause state
    CHECK_FALSE(t.isPaused);
}

TEST_CASE("reset returns every field to construction state") {
    LapTimer t;
    t.onTrackPosition(0.95f, 0);
    t.onTrackPosition(0.05f, 1);
    t.onOfficialSplit(30500, 1, 0);
    t.onRaceStart();
    t.reset();
    CHECK_FALSE(t.anchorValid);
    CHECK_FALSE(t.trackMonitorInitialized);
    CHECK_FALSE(t.anchoredFromRaceStart);
    CHECK(t.currentLapNum == 0);
    CHECK(t.currentSector == 0);
    CHECK(t.lastSplit1Time == -1);
    CHECK(t.lastSplit2Time == -1);
    CHECK(t.getElapsedLapTime() == -1);
}
