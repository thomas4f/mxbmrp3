// ============================================================================
// tests/integration/tests/spectate_click_test.cpp
// Click-to-spectate is offered on exactly the riders it can actually reach.
//
// Standings, Map, Event Log and Session Charts all let you click a rider to put the
// camera on them. Only Standings had a rule for WHICH riders qualify; Map handed out a
// click region for every marker it drew, and the two new surfaces had none. A region on
// an unreachable rider is worse than no region: requestSpectateRider() finds no match in
// the game's vehicle list, consumes the request and drops it, so the row highlights, the
// click lands, and nothing happens.
//
// PluginData::isRiderSpectatable() is now the single gate for all four. This pins its
// semantics, and pins the Event Log end-to-end: an event row is clickable only when the
// event names a rider AND that rider is reachable. Session-level rows ("Final lap") name
// nobody; a retirement names someone who has just become unreachable — the same event that
// puts a rider in the log is the one that disqualifies them from being clicked, which is
// exactly the case a per-surface rule would get wrong.
//
// The Event Log assertions go through the real rebuild (visible HUD + spectate draw
// state), so they cover the raceNum plumbed onto EventLogEntry as well as the gate.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <chrono>
#include <thread>

namespace {
constexpr int RACE1 = 6;
}

TEST_CASE("spectate gate: only active on-track riders are clickable") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spectate_click\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Carol");
    host.draw();
    host.spectateVehicles({ { 10, "Alice" } }, /*curSelectionIndex=*/0);

    // state: 0=Racing, 1=DNS, 3=Retired, 4=DSQ. pit: 1 = in the garage.
    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
        { .num = 33, .laps = 1, .gap = 900 },
    });
    CHECK(host.isRiderSpectatable(10));
    CHECK(host.isRiderSpectatable(22));

    // A rider with no standings row at all is not in the classification the spectate list
    // is built from — and must not be clickable just because some HUD drew them.
    CHECK_FALSE(host.isRiderSpectatable(99));
    CHECK_FALSE(host.isRiderSpectatable(-1));

    SUBCASE("retired, DSQ and DNS riders are not clickable") {
        host.classify(RACE1, 2000, {
            { .num = 10, .laps = 2, .gap = 0 },
            { .num = 22, .laps = 1, .gap = 500, .state = 3 },   // retired
            { .num = 33, .laps = 1, .gap = 900, .state = 4 },   // DSQ
        });
        CHECK(host.isRiderSpectatable(10));
        CHECK_FALSE(host.isRiderSpectatable(22));
        CHECK_FALSE(host.isRiderSpectatable(33));
    }

    SUBCASE("a rider in the pits is not clickable") {
        host.classify(RACE1, 2000, {
            { .num = 10, .laps = 2, .gap = 0 },
            { .num = 22, .laps = 1, .gap = 500, .pit = 1 },
            { .num = 33, .laps = 1, .gap = 900 },
        });
        CHECK_FALSE(host.isRiderSpectatable(22));
        CHECK(host.isRiderSpectatable(33));
        // ...and becomes clickable again on leaving the pits (the gate is live state, not
        // a one-way exclusion).
        host.classify(RACE1, 3000, {
            { .num = 10, .laps = 2, .gap = 0 },
            { .num = 22, .laps = 2, .gap = 500 },
            { .num = 33, .laps = 1, .gap = 900 },
        });
        CHECK(host.isRiderSpectatable(22));
    }

    host.shutdown();
}

TEST_CASE("event log: a row is clickable only when it names a reachable rider") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spectate_click\\");

    host.eventLogSetVisible(true);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.draw();
    // Spectate draw state: click-to-spectate is only offered off your own bike.
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*curSelectionIndex=*/0);
    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    });

    // A fastest-lap event names #22, who is out on track → one clickable row.
    host.raceLap(RACE1, /*raceNum=*/22, /*lap=*/1, /*lapTime=*/90000, /*best=*/2);
    host.draw();
    CHECK_MESSAGE(host.eventLogSpectateRegionCount() == 1,
                  "a fastest-lap row naming an on-track rider should be clickable");

    // #22 retires. The retirement event names them, but they are no longer reachable — so
    // NEITHER the new row nor the earlier fastest-lap row (same rider) may stay clickable.
    // This is the case a per-surface rule gets wrong: the rider number is still right.
    host.communication(/*raceNum=*/22, /*state=*/3);
    host.classify(RACE1, 2000, {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500, .state = 3 },
    });
    host.draw();
    CHECK_MESSAGE(host.eventLogSpectateRegionCount() == 0,
                  "rows naming a retired rider must not be clickable");

    host.shutdown();
}

TEST_CASE("event log: auto-hiding drops the click targets with the rows") {
    // The click/hover block in EventLogHud::update() runs BEFORE the auto-hide check, and
    // rebuildRenderData() (the only other place that clears the regions) does not run while
    // hidden. So an auto-hide that cleared only the quads/strings left the spectate regions
    // live: a click on empty screen where a row used to be still switched the camera. The
    // ON-but-hidden path returns above the input block and was never affected, which is why
    // this drives the TIMEOUT path specifically.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spectate_click\\");

    host.eventLogSetVisible(true);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.draw();
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*curSelectionIndex=*/0);
    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    });

    // AUTO_HIDE at the minimum duration, then an event naming an on-track rider.
    host.eventLogSetAutoHide(true, /*durationMs=*/1000);
    host.raceLap(RACE1, /*raceNum=*/22, /*lap=*/1, /*lapTimeMs=*/90000, /*best=*/2);
    host.draw();
    REQUIRE_MESSAGE(host.eventLogSpectateRegionCount() == 1,
                    "the fresh event row should be clickable while shown");

    // Let it hide. The rows are gone from the screen, so their click targets must be too.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    host.draw();
    CHECK_MESSAGE(host.eventLogSpectateRegionCount() == 0,
                  "an auto-hidden event log must not keep hit-testing");

    host.shutdown();
}
