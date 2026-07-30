// ============================================================================
// tests/integration/tests/blueflag_test.cpp
// Pins the blue-flag / lapping detection semantics (PluginData::rebuildBlueFlagCaches)
// so the O(n^2) loop can be refactored WITHOUT changing behavior. Drives a real
// race with a spread lap field: a leader a lap+ up closing on one backmarker
// (within the awareness distance) but not another, and asserts exactly which
// riders are blue-flagged / lapping, plus the same-lap early-out and the pit
// exclusion. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

static constexpr int RACE1 = 6;  // PiBoSo session 6 = Race1; state 16 = running

// awarenessThreshold = 100m / trackLength(1600m) = 0.0625 of a lap by default.
TEST_CASE("blue flag: lapping detection, proximity, same-lap early-out, pit exclusion") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\blueflag\\");

    host.eventInit("TestTrack", "Leader");   // default trackLength 1600
    host.raceEvent("TestTrack");
    host.addEntry(1, "Leader");
    host.addEntry(2, "Backmarker-near");
    host.addEntry(3, "Backmarker-far");
    host.addEntry(4, "Second");
    host.session(RACE1, /*numLaps=*/10);

    SUBCASE("spread field: only the approached backmarker is blue-flagged") {
        // Laps: R1 & R4 on lap 3 (leaders); R2 & R3 a lap down (backmarkers).
        host.classify(RACE1, 120000, {
            { .num = 1, .laps = 3 },
            { .num = 4, .laps = 3 },
            { .num = 2, .laps = 1 },
            { .num = 3, .laps = 1 },
        });
        // R1 (lapper) at 0.50 is 0.03 behind R2 (0.53) => within 0.0625 => blue.
        // R3 (0.20) is far from R1 => not blue. R4 shares the lead lap.
        host.raceTrackPosition({
            { .num = 1, .trackPos = 0.50f },
            { .num = 2, .trackPos = 0.53f },
            { .num = 3, .trackPos = 0.20f },
            { .num = 4, .trackPos = 0.10f },
        });

        CHECK(host.isRiderBlueFlagged(2) == true);    // approached backmarker
        CHECK(host.isRiderBlueFlagged(3) == false);   // backmarker, but lapper not near
        CHECK(host.isRiderBlueFlagged(1) == false);   // leader can't be blue-flagged
        CHECK(host.isRiderBlueFlagged(4) == false);   // on the lead lap

        CHECK(host.isRiderLapping(1) == true);        // R1 is the approaching lapper
        CHECK(host.riderLappingTarget(1) == 2);       // ...of R2
        CHECK(host.isRiderLapping(2) == false);       // a backmarker is not lapping
        CHECK(host.isRiderLapping(3) == false);
    }

    SUBCASE("same-lap field: no blue flags (early-out path)") {
        host.classify(RACE1, 120000, {
            { .num = 1, .laps = 3 }, { .num = 2, .laps = 3 },
            { .num = 3, .laps = 3 }, { .num = 4, .laps = 3 },
        });
        host.raceTrackPosition({
            { .num = 1, .trackPos = 0.50f }, { .num = 2, .trackPos = 0.53f },
            { .num = 3, .trackPos = 0.20f }, { .num = 4, .trackPos = 0.10f },
        });
        CHECK(host.isRiderBlueFlagged(2) == false);
        CHECK(host.isRiderLapping(1) == false);
    }

    SUBCASE("pit exclusion: an approached backmarker in the pits is not blue-flagged") {
        host.classify(RACE1, 120000, {
            { .num = 1, .laps = 3 },
            { .num = 4, .laps = 3 },
            { .num = 2, .laps = 1, .pit = 1 },   // same near-lapper geometry, but in the pits
            { .num = 3, .laps = 1 },
        });
        host.raceTrackPosition({
            { .num = 1, .trackPos = 0.50f },
            { .num = 2, .trackPos = 0.53f },
            { .num = 3, .trackPos = 0.20f },
            { .num = 4, .trackPos = 0.10f },
        });
        CHECK(host.isRiderBlueFlagged(2) == false);   // excluded from detection
        CHECK(host.isRiderLapping(1) == false);       // no eligible backmarker to lap
    }

    // Runs after EVERY subcase (doctest re-enters the body per leaf), so each of
    // the three DLL loads is torn down through the orchestrated Shutdown export.
    // This case is where the cost of NOT doing so was measured: back when
    // ~PluginHost was a bare FreeLibrary, the three subcases each unmapped the
    // DLL with the plugin's background threads still live — the
    // unload-without-Shutdown() path CLAUDE.md flags as having cost two shipped
    // crashes — and it crashed 8/10 runs under CPU saturation, 0/10 with this
    // call. The destructor now shuts down as well, so this is idempotent.
    host.shutdown();
}
