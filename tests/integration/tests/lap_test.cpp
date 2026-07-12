// ============================================================================
// tests/integration/tests/lap_test.cpp
// RaceLap: lap completion → per-rider last lap, overall-fastest-lap detection
// (the `fastest` chip) and the fastest-lap event — and that the fastest lap
// correctly MOVES to whoever beats it. Previously only fuzz-covered (survival,
// not correctness); this asserts the observable effects in /api/state.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("race lap: last lap, fastest-lap chip + event, and it moves when beaten") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\lap\\");

    // Local player Alice (#10) — she's the spectated/camera rider, so her per-
    // rider last lap surfaces in the snapshot.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // --- Phase 1: Alice sets the overall fastest lap (1:31.000) --------------
    host.classify(RACE1, 300000, {
        { .num = 10, .best = 91000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 92000, .laps = 3, .gap = 1500 },
    });
    host.raceLap(RACE1, /*raceNum=*/10, /*lapNum=*/3, /*lapTimeMs=*/91000, /*best=*/2);
    host.raceLap(RACE1, /*raceNum=*/22, /*lapNum=*/3, /*lapTimeMs=*/93000, /*best=*/0);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        const auto alice = riderByNum(d, 10);
        CHECK(alice.value("lastLapMs", -1) == 91000);          // RaceLap → last lap
        CHECK(alice.value("lastLap", std::string()) == "1:31.000");
        CHECK(hasChip(alice, "fastest"));                       // owns the fastest lap
        CHECK_FALSE(hasChip(riderByNum(d, 22), "fastest"));
        CHECK(hasEvent(d, "#10 fastest lap"));
    }

    // --- Phase 2: Bob goes faster (1:30.500) — the fastest lap moves to him ---
    // The chip is "this rider's classification best == the overall best lap", so
    // update Bob's classification best alongside his RaceLap.
    host.classify(RACE1, 320000, {
        { .num = 10, .best = 91000, .laps = 4, .gap = 0 },
        { .num = 22, .best = 90500, .laps = 4, .gap = 1500 },
    });
    host.raceLap(RACE1, /*raceNum=*/22, /*lapNum=*/4, /*lapTimeMs=*/90500, /*best=*/2);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(hasChip(riderByNum(d, 22), "fastest"));           // moved to Bob
        CHECK_FALSE(hasChip(riderByNum(d, 10), "fastest"));     // Alice lost it
        CHECK(hasEvent(d, "#22 fastest lap"));
    }

    host.shutdown();
}
