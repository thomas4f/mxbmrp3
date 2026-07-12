// ============================================================================
// tests/integration/tests/ideal_lap_test.cpp
// Ideal lap = the sum of a rider's BEST sector times across all their laps (the
// theoretical best if they strung their best sectors together). Emitted per rider
// as `idealLapMs` in /api/state (used by the battle/focus cards), computed by
// PluginData::updateIdealLap from each RaceLap's sector splits — so drive two laps
// whose best sectors come from DIFFERENT laps and assert the ideal is their sum,
// not either actual lap time. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("ideal lap: sum of a rider's best sectors across laps") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ideal_lap\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");

    // A classification puts #10 in the standings array (that's what the snapshot
    // iterates); the per-rider idealLapMs is attached from the ideal-lap data.
    host.classify(RACE1, 200000, { { .num = 10, .best = 89000, .laps = 2 } });

    // raceLap splits are ACCUMULATED (S1, then S1+S2); sector3 = lapTime - split1.
    // Lap 1 sectors: 30.0 / 32.0 / 28.0  → splits 30.0, 62.0 ; lap 90.0
    host.raceLap(RACE1, /*raceNum=*/10, /*lap=*/1, /*lapTimeMs=*/90000, /*best=*/0, 30000, 62000);
    // Lap 2 sectors: 29.0 / 33.0 / 27.0  → splits 29.0, 62.0 ; lap 89.0
    host.raceLap(RACE1, 10, 2, 89000, 0, 29000, 62000);

    // Best S1 = 29.0 (lap 2), best S2 = 32.0 (lap 1), best S3 = 27.0 (lap 2)
    // → ideal 88.0s, which is faster than either real lap (90.0 / 89.0).
    auto d = host.snapshot();
    REQUIRE(d.is_object());
    auto r = riderByNum(d, 10);
    REQUIRE(r.is_object());
    CHECK(r.value("idealLapMs", -1) == 88000);
}
