// ============================================================================
// tests/integration/tests/racenum_reuse_test.cpp
// Race-number reuse must inherit NOTHING. Gives rider #22 rich per-rider state
// on every registered surface (standings gap, lap history + best, live-gap
// "active" bit, real-time gap, split/S-F position references), removes him via
// the real RaceRemoveEntry path, re-adds a NEW rider with the same number, and
// asserts every surface reads fresh.
//
// Pins the PerRider<> registry in plugin_data.h: per-rider containers register
// their removeRaceEntry()/clear() eviction at declaration, so a newly added
// map can't silently miss the eviction list (the recurring bug class the old
// hand-maintained erase list produced — see CLAUDE.md Maintenance Invariants).
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

// The laps[] series ({num, t:[...]}) for a race number, or null if absent.
static nlohmann::json lapsByNum(const nlohmann::json& d, int num) {
    for (const auto& l : d.value("laps", nlohmann::json::array()))
        if (l.value("num", -1) == num) return l;
    return nlohmann::json();
}

TEST_CASE("raceNum reuse: a rejoiner inherits no per-rider state") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\racenum_reuse\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // --- Build up rich state for Bob (#22) on every per-rider surface --------
    // Two timestamped classify+batch cycles so the leader stamps timing points
    // and Bob's real-time gap computes (same warm-up as livegaps_test).
    const std::vector<ClassRow> rows = {
        { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
        { .num = 22, .best = 92000, .laps = 2, .gap = 3000 },
    };
    host.classify(RACE1, 100000, rows);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.10f } });  // leader stamps 0.20 @ 100000
    host.classify(RACE1, 102000, rows);
    host.raceTrackPosition({ { 10, 0.40f }, { 22, 0.20f } });  // Bob reaches 0.20 → gap 2000
    host.raceLap(RACE1, /*raceNum=*/10, /*lapNum=*/1, /*lapTimeMs=*/90000, /*best=*/2);
    host.raceLap(RACE1, /*raceNum=*/22, /*lapNum=*/1, /*lapTimeMs=*/92000, /*best=*/2);
    host.raceSplit(RACE1, /*raceNum=*/22, /*lap=*/2, /*splitIdx=*/0, /*ms=*/30000);

    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        // Sanity: the state we're about to assert is evicted really exists.
        REQUIRE(lapsByNum(d, 22).value("t", nlohmann::json::array()).size() == 1);
        auto bob = riderByNum(d, 22);
        REQUIRE(bob.value("gapMs", -1) == 3000);
        REQUIRE(bob.contains("posDeltaSplit"));            // split reference recorded
        REQUIRE(host.hasActiveTrackPos(22) == 1);          // in the last batch
        REQUIRE(host.realTimeGap(22) > 0);                 // live gap computed
    }

    // --- Bob leaves; Carl joins reusing #22, with NO new callbacks in between
    // (the player could be sitting in menus — nothing arrives to refresh state
    // there, so anything not evicted on removal would linger indefinitely).
    host.removeEntry(22);
    host.addEntry(22, "Carl");

    CHECK(host.hasActiveTrackPos(22) == 0);   // stale "active" bit evicted
    CHECK(host.realTimeGap(22) == -1);        // standings entry evicted (no gap to read)
    {
        auto d = host.snapshot();
        CHECK(lapsByNum(d, 22).is_null());    // Bob's lap series evicted
    }

    // --- Carl's first classification: only what the new callbacks say --------
    host.classify(RACE1, 120000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 2, .gap = 15000 },
    });
    {
        auto d = host.snapshot();
        auto carl = riderByNum(d, 22);
        CHECK(carl.value("fullName", std::string()) == "Carl");
        CHECK(carl.value("gapMs", -1) == 15000);           // from the new classify only
        // Bob's position references died with him: no start/S-F/split deltas
        // until Carl earns his own (posDeltaStart falls back to the S/F ref,
        // so all three must be absent).
        CHECK(!carl.contains("posDeltaStart"));
        CHECK(!carl.contains("posDeltaSf"));
        CHECK(!carl.contains("posDeltaSplit"));
        CHECK(carl.value("liveGapValid", true) == false);  // no batch seen yet
        CHECK(lapsByNum(d, 22).is_null());                 // no lap history yet
    }

    host.shutdown();
}
