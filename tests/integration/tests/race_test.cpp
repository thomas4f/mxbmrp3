// ============================================================================
// tests/integration/tests/race_test.cpp
// Behavioral integration test: drive a known synthetic race through the real
// PiBoSo callbacks and assert the standings the plugin computes (via its own
// /api/state JSON snapshot). Exercises the whole pipeline — api-export layer ->
// adapters -> PluginData change detection -> HttpServer::buildJsonSnapshot — on
// Linux, under Wine, with no game and no Windows.
//
// Self-contained: doctest provides main() and the reporting; PluginHost loads
// the DLL and drives it; nlohmann::json asserts the snapshot. Built and run by
// tests/integration/run_tests.sh. The DLL path comes from argv (see the runner).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// One plugin lifecycle, driven through successive classification updates — the
// plugin is stateful (it re-derives standings on each update), so the phases run
// sequentially against a single running instance rather than as independent
// SUBCASEs. Each phase snapshots /api/state and asserts what changed.
TEST_CASE("race: standings order, gaps, overtake and DSQ") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\race\\");

    // Event: a race on TestTrack; local player is Alice (#10).
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);

    // Add entries in REVERSE finishing order — a correct result proves position
    // comes from the classification array order, not insertion order.
    host.addEntry(7,  "Carol");
    host.addEntry(22, "Bob");
    host.addEntry(10, "Alice");

    // --- Phase 1: initial grid — Alice P1, Bob +1.5, Carol +3.2 --------------
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 5, .gap = 3200 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        // Session fields (constant across phases).
        CHECK(d["session"].value("type", std::string()) == "Race 1");
        CHECK(d["session"].value("isRace", false) == true);
        CHECK(d["session"].value("time", std::string()) == "05:00");     // 300000ms
        CHECK(d["session"].value("trackName", std::string()) == "TestTrack");
        checkStandings(d, {
            { 1, 10, "Alice", "Leader", "1:30.000" },
            { 2, 22, "Bob",   "+1.500", "1:31.000" },
            { 3,  7, "Carol", "+3.200", "1:32.500" },
        });
    }

    // --- Phase 2: overtake — Bob takes the lead; Alice drops to P2 (+0.8) -----
    host.classify(6, 300000, {
        { .num = 22, .best = 91000, .laps = 6, .gap = 0 },
        { .num = 10, .best = 90000, .laps = 6, .gap = 800 },
        { .num = 7,  .best = 92500, .laps = 6, .gap = 3600 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        checkStandings(d, {
            { 1, 22, "Bob",   "Leader", "1:31.000" },
            { 2, 10, "Alice", "+0.800", "1:30.000" },
            { 3,  7, "Carol", "+3.600", "1:32.500" },
        });
        CHECK(hasEvent(d, "#22 takes the lead"));
    }

    // --- Phase 3: DSQ — Carol disqualified via RaceCommunication -------------
    // Gap becomes the DSQ label, order unchanged, best lap retained, per-rider
    // state flips to 4 (DSQ).
    host.communication(/*raceNum=*/7, /*state=*/4);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        checkStandings(d, {
            { 1, 22, "Bob",   "Leader", "1:31.000" },
            { 2, 10, "Alice", "+0.800", "1:30.000" },
            { 3,  7, "Carol", "DSQ",    "1:32.500" },
        });
        CHECK(riderByNum(d, 7).value("state", -1) == 4);
        CHECK(hasEvent(d, "#7 disqualified"));
    }

    host.shutdown();
}
