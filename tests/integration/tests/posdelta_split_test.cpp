// ============================================================================
// tests/integration/tests/posdelta_split_test.cpp
// RaceSplit → posDeltaSplit: the standings "+/- since the last split" column
// (positions a rider has gained/lost since they last crossed a split line).
// RaceSplit snapshots each rider's current position as a rolling reference
// (PluginData::recordSplitReference, races only); the snapshot then emits
// posDeltaSplit = referencePosition - currentPosition per rider. Drive a split
// for each rider, then reorder the field and assert the deltas. Self-contained
// doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("posDeltaSplit: positions gained/lost since the last split") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\posdelta_split\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Carol");

    // Grid at the split: #10 P1, #22 P2, #33 P3.
    host.classify(RACE1, 100000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1000 },
        { .num = 33, .best = 92000, .laps = 1, .gap = 2000 },
    });

    // Each rider crosses a split — snapshots their CURRENT position as the
    // reference (P1 / P2 / P3 respectively).
    host.raceSplit(RACE1, /*raceNum=*/10, /*lap=*/1, /*splitIdx=*/0, /*ms=*/30000);
    host.raceSplit(RACE1, 22, 1, 0, 31000);
    host.raceSplit(RACE1, 33, 1, 0, 32000);

    // #33 charges to the front; #10 and #22 each drop one: #33 P1, #10 P2, #22 P3.
    host.classify(RACE1, 120000, {
        { .num = 33, .best = 92000, .laps = 1, .gap = 0 },
        { .num = 10, .best = 90000, .laps = 1, .gap = 500 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1500 },
    });

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    // reference - current: #33 3→1 = +2 gained; #10 1→2 = -1; #22 2→3 = -1.
    CHECK(riderByNum(d, 33).value("posDeltaSplit", -99) == 2);
    CHECK(riderByNum(d, 10).value("posDeltaSplit", -99) == -1);
    CHECK(riderByNum(d, 22).value("posDeltaSplit", -99) == -1);

    host.shutdown();
}
