// ============================================================================
// tests/integration/tests/http_robust_test.cpp
// HTTP-server survival under slow/partial/malformed clients — the "HTTP requests
// timing out" concern. The embedded server (cpp-httplib) reads requests on a
// small pool of worker threads; nothing a client controls runs on the game
// thread (buildJsonSnapshot is game-thread work, read back as a cached string
// under a mutex). This asserts that property holds: a client that opens a
// connection and never finishes its request, or sends garbage, must not crash
// the server, wedge it for other clients, or stall the game thread's snapshot.
//
// Kept below the worker-pool count so we're testing "other clients still served",
// not "pool exhausted waiting on the httplib read timeout" (which would depend on
// that timeout's duration). Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("http robustness: slow/partial/malformed clients don't wedge the server") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\http_robust\\");
    REQUIRE(host.startHttp());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
    });

    // Baseline: a normal fetch works.
    REQUIRE(host.state().is_object());

    // --- Two slow-loris clients: connect, send a partial request header with no
    //     terminating blank line, and HOLD the connections open. ---------------
    const char* partial = "GET /api/state HTTP/1.0\r\nHost: x\r\nX-Slow: ";
    uintptr_t s1 = host.rawConnectSend(partial, (int)strlen(partial));
    uintptr_t s2 = host.rawConnectSend(partial, (int)strlen(partial));
    REQUIRE(PluginHost::rawValid(s1));
    REQUIRE(PluginHost::rawValid(s2));

    // The game thread is independent of the server's worker threads — the direct
    // snapshot must stay responsive regardless of what clients are doing.
    CHECK(host.snapshot().is_object());

    // Another, completed request must still be served while the two hang open.
    {
        auto served = host.state();
        REQUIRE(served.is_object());
        checkStandings(served, {
            { 1, 10, "Alice", "Leader" },
            { 2, 22, "Bob",   "+1.500" },
        });
    }

    host.rawClose(s1);
    host.rawClose(s2);

    // --- A malformed / oversized burst: garbage bytes, then close. -----------
    std::string junk(64 * 1024, 'A');                 // not a valid request line
    uintptr_t s3 = host.rawConnectSend(junk.data(), (int)junk.size());
    if (PluginHost::rawValid(s3)) host.rawClose(s3);

    // Server survived the malformed input and still serves valid JSON.
    CHECK(host.state().is_object());
    CHECK(host.snapshot().is_object());

    host.shutdown();
}
