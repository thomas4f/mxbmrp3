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
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

TEST_CASE("http: the server serves /api/state and it matches the direct snapshot") {
    // Stage web files BEFORE the server starts, so set_mount_point() succeeds and
    // the static mount is genuinely in play for the /sw.js + /custom.css checks
    // below. The server's web root is plugins\mxbmrp3_data\web relative to CWD.
    namespace fs = std::filesystem;
    const fs::path webRoot = fs::path("plugins") / "mxbmrp3_data" / "web";
    fs::create_directories(webRoot);
    {
        std::ofstream f(webRoot / "sw.js", std::ios::binary);
        f << "var CACHE_NAME = \"mxbmrp3-overlay-__PLUGIN_VERSION__\";\n";
    }
    {
        std::ofstream f(webRoot / "custom.css", std::ios::binary);
        f << ":root { --test-marker: 1; }\n";
    }

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

    // Regression: /sw.js and /custom.css need CUSTOM serving (version
    // substitution, Cache-Control: no-cache), but both files also exist under
    // the static mount — and httplib serves mounted files BEFORE dispatching
    // Get() handlers, so a plain Get() registration was dead code and the raw
    // on-disk sw.js (placeholder cache name, no no-cache header) shipped to
    // every browser. The fix intercepts the two paths in the pre-routing
    // handler; this pins that the custom path wins WITH the mount active.
    const std::string swFull = host.rawGetFull("/sw.js");
    CHECK(swFull.find("mxbmrp3-overlay-") != std::string::npos);
    CHECK(swFull.find("__PLUGIN_VERSION__") == std::string::npos);   // substituted
    CHECK(swFull.find("Cache-Control: no-cache") != std::string::npos);

    // custom.css: served with the no-cache header (the mount would add
    // ETag/Last-Modified but no Cache-Control — the stale-edit bug).
    const std::string cssFull = host.rawGetFull("/custom.css");
    CHECK(cssFull.find("--test-marker") != std::string::npos);
    CHECK(cssFull.find("Cache-Control: no-cache") != std::string::npos);

    host.shutdown();
}


// THE TOWER THAT SHRANK MID-UPDATE. On a broadcaster's stream the overlay's
// standings tower dropped, every now and then and for a push or two, from the
// full field to its first N rows and grew back: 14 rows, then 10, then 14. The
// surviving rows were always a prefix, which is the signature of a snapshot
// built against a classification order that was still being filled.
// batchUpdateStandings() cleared m_classificationOrder and pushed the riders
// back one at a time; a rider whose pit flag changed logged an event from
// inside that loop, and addEventLogEntry notifies the HTTP server
// synchronously, which builds the snapshot right there when its window is
// open. The order then held only the riders before that one. The field was
// spawning onto the track, so pit exits were arriving every few seconds.
//
// Deterministic because of the build window: the pit-flip classification's
// mid-loop build lands when the window has elapsed, and the Standings
// notification at the END of the same loop is then inside the window, so it
// only marks the cache stale. What /api/state serves right after IS the
// mid-loop snapshot. Before the fix this fetched 10 rows.
TEST_CASE("http: a pit event mid-classification does not serve a truncated tower") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\http\\");
    REQUIRE(host.startHttp());

    host.eventInit("TestTrack", "Rider1");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    std::vector<ClassRow> rows;
    for (int n = 1; n <= 14; ++n) {
        char name[16];
        snprintf(name, sizeof(name), "Rider%d", n);
        host.addEntry(n, name);
        rows.push_back({ .num = n, .best = 90000 + n * 100, .laps = 3, .gap = (n - 1) * 1000 });
    }
    host.classify(6, 300000, rows);
    REQUIRE(host.state()["standings"].size() == 14);

    // Let the build window elapse, so the next event-log notification builds
    // on the spot rather than deferring past the end of the loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    rows[10].pit = 1;                              // the 11th rider enters the pits
    host.classify(6, 300000, rows);
    const auto served = host.state();
    REQUIRE(served.is_object());
    CHECK_MESSAGE(served["standings"].size() == 14,
                  "the snapshot built by the pit event's notification saw a "
                  "half-filled classification order: the overlay tower shrank to "
                  << served["standings"].size() << " rows for this push");
}
