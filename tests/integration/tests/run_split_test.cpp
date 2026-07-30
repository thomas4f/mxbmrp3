// ============================================================================
// tests/integration/tests/run_split_test.cpp
// RunSplit — the player's own split crossing.
//
// The plugin deliberately does NOTHING with it. RunSplit is player-only, which
// makes it useless while spectating, so all split handling — current-lap splits,
// posDeltaSplit, the lap log — moved to RaceSplit (all riders) and this handler
// stayed behind as a documented stub.
//
// That is a real contract, not an absence of one, and it is the kind that rots
// quietly: the obvious "fix" for a split bug is to start handling the callback
// named RunSplit, which double-counts every player split against the RaceSplit
// that already handled it.
//
// The observable has to be chosen carefully. Current-lap splits are an IN-GAME
// display and never reach /api/state, so a snapshot-only assertion cannot see
// them — verified by mutation: teaching RunSplit to call updateCurrentLapSplit()
// left every snapshot byte-identical and an earlier version of this test passed.
// So the real assertion reads the accumulators directly through
// MXBMRP3_Test_CurrentLapSplits; the snapshot comparison stays as the broad net
// for anything else the handler might start touching.
//
// API_COVERAGE.md carried this as a fuzz-only "gap"; a stub still needs the
// assertion, it just asserts nothing happened.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("RunSplit is a no-op: the snapshot is unchanged across it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\runsplit\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE1, 120000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1200 },
    });
    host.runInit(RACE1);

    const std::string before = host.rawSnapshot();
    REQUIRE_FALSE(before.empty());

    // Alice (#10) is the player; nothing has recorded a split for her yet.
    REQUIRE_FALSE(host.currentLapSplits(10).present);

    // Every split of a lap, including a repeat and an out-of-range index.
    host.runSplit(/*splitIndex=*/0, /*splitTimeMs=*/30000, /*bestDiffMs=*/-250);
    host.runSplit(1, 61000, 120);
    host.runSplit(1, 61000, 120);
    host.runSplit(99, 90000, 0);
    host.draw();

    // The assertion that actually bites: no current-lap split state was created.
    // A handler that started recording would materialize it here while leaving
    // the snapshot untouched.
    const auto splits = host.currentLapSplits(10);
    CHECK_FALSE(splits.present);
    CHECK(splits.s1 == -1);
    CHECK(splits.s2 == -1);
    CHECK(splits.s3 == -1);

    CHECK(host.rawSnapshot() == before);

    host.shutdown();
}

TEST_CASE("RunSplit does not disturb the splits RaceSplit computed") {
    // The trap this guards: RunSplit arrives for the player interleaved with the
    // RaceSplit that actually did the work. If RunSplit ever starts writing, the
    // player's split lands twice and posDeltaSplit is computed off a doubled
    // sample — visible only for the player, and only in a real session.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\runsplit-race\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE1, 0, {
        { .num = 10, .best = 0, .laps = 0, .gap = 0 },
        { .num = 22, .best = 0, .laps = 0, .gap = 500 },
    });
    host.runInit(RACE1);

    // Bob leads at the split, so Alice is -1 there.
    host.raceSplit(RACE1, /*raceNum=*/22, /*lapNum=*/0, /*splitIndex=*/0, /*splitTimeMs=*/30000);
    host.raceSplit(RACE1, /*raceNum=*/10, /*lapNum=*/0, /*splitIndex=*/0, /*splitTimeMs=*/30500);
    host.classify(RACE1, 31000, {
        { .num = 22, .best = 0, .laps = 0, .gap = 0 },
        { .num = 10, .best = 0, .laps = 0, .gap = 500 },
    });

    const auto beforeAlice = riderByNum(host.snapshot(), 10);
    const auto beforeBob   = riderByNum(host.snapshot(), 22);
    REQUIRE_FALSE(beforeAlice.is_null());

    // RaceSplit did the work: Alice's split 1 accumulator holds her crossing.
    const auto beforeSplits = host.currentLapSplits(10);
    REQUIRE(beforeSplits.present);
    REQUIRE(beforeSplits.s1 == 30500);

    // The player's RunSplit for the SAME crossing must not touch it. If it did,
    // the player's split would land twice — a doubled sample that only shows up
    // for the local rider, and only in a real session.
    host.runSplit(/*splitIndex=*/0, /*splitTimeMs=*/30500, /*bestDiffMs=*/0);
    // ... and a LATER split index must not be invented from the player-only feed.
    host.runSplit(/*splitIndex=*/1, /*splitTimeMs=*/61000, /*bestDiffMs=*/0);
    host.draw();

    const auto afterSplits = host.currentLapSplits(10);
    CHECK(afterSplits.s1 == beforeSplits.s1);
    CHECK(afterSplits.s2 == -1);        // RaceSplit never reported split 2
    CHECK(afterSplits.s3 == -1);
    CHECK(afterSplits.lapNum == beforeSplits.lapNum);

    CHECK(riderByNum(host.snapshot(), 10) == beforeAlice);
    CHECK(riderByNum(host.snapshot(), 22) == beforeBob);

    host.shutdown();
}
