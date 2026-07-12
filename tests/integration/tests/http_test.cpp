// ============================================================================
// tests/integration/tests/http_test.cpp
// The one test that exercises the real HTTP serving path end to end: start the
// embedded server, drive a small race, and fetch /api/state over an actual
// socket. Asserts the server is reachable, returns valid JSON, and serves the
// SAME content the plugin builds directly (state() == snapshot()).
//
// The plugin-LOGIC tests deliberately bypass this and read snapshot() directly
// (no server, no gating) — see TESTING.md. This test owns the server/socket
// coverage so that split doesn't leave the serving path untested.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("http: the server serves /api/state and it matches the direct snapshot") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\http\\");
    REQUIRE(host.startHttp());        // starts the server + waits for it to answer

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
    });

    // Fetched over a real socket: non-empty and valid JSON with the expected shape.
    const std::string raw = host.rawState();
    CHECK_FALSE(raw.empty());
    const auto served = host.state();
    REQUIRE(served.is_object());
    CHECK(served["session"].value("type", std::string()) == "Race 1");
    checkStandings(served, {
        { 1, 10, "Alice", "Leader" },
        { 2, 22, "Bob",   "+1.500" },
    });

    // The server serves exactly what the plugin builds directly.
    const auto direct = host.snapshot();
    REQUIRE(direct.is_object());
    CHECK(served["standings"] == direct["standings"]);
    CHECK(served["session"]["type"] == direct["session"]["type"]);

    host.shutdown();
}
