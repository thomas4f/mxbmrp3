// ============================================================================
// tests/integration/tests/deinit_test.cpp
// EventDeinit and RaceDeinit — the two callbacks that tear the plugin's world
// down without the plugin itself shutting down.
//
// Both were carried as ⚪ untested in API_COVERAGE.md, and they are exactly the
// shape that earns a leftover-state bug: they clear, and nothing downstream
// notices if they clear too little. The plugin then keeps serving the previous
// event's riders — a standings board still listing riders who are no longer on
// track, gaps computed against a leader from a session that ended.
//
// This is the mirror of racenum_reuse_test.cpp: that one pins per-rider eviction
// when ONE rider leaves, this one pins the wholesale clear when EVERYONE does.
//
// Plugin-logic test, so it reads the DIRECT snapshot (host.snapshot()) — no HTTP
// server. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

namespace {

// A populated session: three riders, classified, with track positions and a lap
// each — i.e. every per-rider container PluginData keeps is non-empty.
void populate(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");
    host.classify(RACE1, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
        { .num = 7,  .best = 92000, .laps = 3, .gap = 3200 },
    });
    host.raceLap(RACE1, 10, /*lapNum=*/3, /*lapTimeMs=*/90000);
    host.raceTrackPosition({
        { .num = 10, .trackPos = 0.50f },
        { .num = 22, .trackPos = 0.45f },
        { .num = 7,  .trackPos = 0.40f },
    });
}

int riderCount(const nlohmann::json& d) {
    return static_cast<int>(d.value("standings", nlohmann::json::array()).size());
}

}  // namespace

TEST_CASE("RaceDeinit clears the race state") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\deinit-race\\");

    populate(host);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        REQUIRE(riderCount(d) == 3);
    }

    host.raceDeinit();
    host.draw();
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        // The standings board must be empty, not stale. A partial clear leaves
        // the previous session's riders on screen for the whole next session.
        CHECK(riderCount(d) == 0);
        CHECK(riderByNum(d, 10).is_null());
        CHECK(riderByNum(d, 22).is_null());
        CHECK(riderByNum(d, 7).is_null());
    }

    // The clear must not wedge anything: a fresh race repopulates normally.
    populate(host);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(riderCount(d) == 3);
        CHECK(riderByNum(d, 22).value("num", -1) == 22);
    }

    host.shutdown();
}

TEST_CASE("EventDeinit clears the event state") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\deinit-event\\");

    populate(host);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        REQUIRE(riderCount(d) == 3);
    }

    host.eventDeinit();
    host.draw();
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(riderCount(d) == 0);
        CHECK(riderByNum(d, 10).is_null());
    }

    // EventDeinit fires when the player leaves the track entirely, so the next
    // thing the plugin sees may be a DIFFERENT event. Nothing from the old one
    // may survive into it — including a race number that gets reused by someone
    // else, which is the reuse trap racenum_reuse_test.cpp covers per-rider.
    host.eventInit("OtherTrack", "Dave");
    host.raceEvent("OtherTrack");
    host.session(RACE1, /*numLaps=*/5);
    host.addEntry(10, "Dave");           // #10 was Alice in the previous event
    host.classify(RACE1, 60000, {
        { .num = 10, .best = 95000, .laps = 1, .gap = 0 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(riderCount(d) == 1);
        // `name` is the display-truncated form; `fullName` is the real one.
        CHECK(riderByNum(d, 10).value("fullName", "") == "Dave");
        CHECK(riderByNum(d, 10).value("bestLapMs", -1) == 95000);
    }

    host.shutdown();
}

TEST_CASE("deinit callbacks are safe with nothing to clear") {
    // The game can fire either deinit without a matching init (leaving a session
    // that never started, quitting from the menu). Both must be no-ops, not
    // faults — a crash here takes the host game down with it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\deinit-empty\\");

    host.raceDeinit();
    host.eventDeinit();
    host.raceDeinit();
    host.draw();

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    CHECK(riderCount(d) == 0);

    host.shutdown();
}
