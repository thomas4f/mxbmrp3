// ============================================================================
// tests/integration/tests/sessions_test.cpp
// Business-logic integration test across MULTIPLE sessions. Drives a practice
// session, then a race (penalty + lapped rider), then a race2 that reuses a
// departed rider's race number — asserting the plugin computes the right
// positions, gaps (race vs non-race semantics), penalties and states, and that
// per-rider state resets correctly across sessions.
//
// Targets the maintenance invariants CLAUDE.md flags as bug-prone: the reused-
// race-number stale-state trap, and the #240 spurious lead-change on a session
// boundary. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// PiBoSo session enum: 1=Practice, 6=Race1, 7=Race2. state 16 = running.
static constexpr int PRACTICE = 1, RACE1 = 6, RACE2 = 7;

TEST_CASE("sessions: positions, gaps, penalties, and cross-session state reset") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\sessions\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");
    host.addEntry(3,  "Dave");

    // --- Practice (non-race): the gap column is each rider's OWN best lap ------
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    host.classify(PRACTICE, 120000, {
        { .num = 10, .best = 88000, .laps = 3 },   // Alice 1:28.000
        { .num = 22, .best = 89500, .laps = 3 },   // Bob   1:29.500
        { .num = 7,  .best = 90200, .laps = 3 },   // Carol 1:30.200
        { .num = 3,  .best = 91000, .laps = 3 },   // Dave  1:31.000
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(d["session"].value("type", std::string()) == "Practice");
        CHECK(d["session"].value("isRace", true) == false);
        checkStandings(d, {
            { 1, 10, "Alice", "1:28.000" },
            { 2, 22, "Bob",   "1:29.500" },
            { 3,  7, "Carol", "1:30.200" },
            { 4,  3, "Dave",  "1:31.000" },
        });
    }

    // --- Race 1: leader-relative gaps, a penalty (Alice 5s), a lapped rider ----
    host.session(RACE1, /*numLaps=*/10);
    host.classify(RACE1, 300000, {
        { .num = 22, .best = 89500, .laps = 5, .gap = 0 },                    // P1 Leader
        { .num = 10, .best = 88000, .laps = 5, .gap = 2000, .penalty = 5000 },// P2 +2.000, +5s pen
        { .num = 7,  .best = 90200, .laps = 5, .gap = 5000 },                 // P3 +5.000
        { .num = 3,  .best = 90000, .laps = 4, .gap = 0, .gapLaps = 1 },      // P4 +1L (lapped)
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(d["session"].value("type", std::string()) == "Race 1");
        CHECK(d["session"].value("isRace", false) == true);
        checkStandings(d, {
            { 1, 22, "Bob",   "Leader", "", 0,    0    },
            { 2, 10, "Alice", "+2.000", "", 2000, 5000 },
            { 3,  7, "Carol", "+5.000", "", 5000, 0    },
            { 4,  3, "Dave",  "+1L",    "", 0,    0    },
        });
        // #240 guard: a leader change across a session boundary (Alice -> Bob) is
        // NOT an in-session overtake, so no "takes the lead" event must fire.
        CHECK_FALSE(hasEvent(d, "takes the lead"));
    }

    // --- Race 2, reusing #7: Carol leaves, a NEW rider takes race number 7 -----
    // If the plugin failed to clear Carol's per-rider maps, #7 would show
    // "Carol" / her old best. It must be a clean "NewGuy" / 1:35.000.
    host.removeEntry(7);
    host.addEntry(7, "NewGuy");
    host.session(RACE2, /*numLaps=*/10);
    host.classify(RACE2, 60000, {
        { .num = 22, .best = 89500, .laps = 2, .gap = 0 },
        { .num = 7,  .best = 95000, .laps = 2, .gap = 3000 },
        { .num = 10, .best = 88000, .laps = 2, .gap = 6000 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(d["session"].value("type", std::string()) == "Race 2");
        checkStandings(d, {
            { 1, 22, "Bob",    "Leader", "", 0,    0 },
            { 2,  7, "NewGuy", "+3.000", "", 3000, 0 },
            { 3, 10, "Alice",  "+6.000", "", 6000, 0 },
        });
        CHECK_FALSE(hasEvent(d, "takes the lead"));
    }

    host.shutdown();
}
