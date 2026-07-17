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

#include <filesystem>
#include <fstream>

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

