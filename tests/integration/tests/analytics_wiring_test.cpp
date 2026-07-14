// ============================================================================
// tests/integration/tests/analytics_wiring_test.cpp
// Analytics event wiring, via the dry-run capture seam. AnalyticsManager's only
// real effect is a network POST the headless harness can't see, so this drives
// the payload build and the remote-sampling gate DIRECTLY through hooks — no
// network (capture mode makes the real senders no-ops, so a test build never
// phones home) and no background threads. It pins the contract the reviewer
// called out:
//   * app_started is the always-sent tier (built regardless of the sample), and
//     carries the anonymous identity + feature flags + isDebug;
//   * a FULL launch enqueues session_end + custom events;
//   * a MINIMAL launch (aptabase_full_sample rolled us out) drops both;
//   * a crash is NEVER gated — it enqueues even on a minimal launch.
// Analytics is compiled into the test DLL but never auto-inits (GAME_HAS_ANALYTICS
// is 0 in the test build), so only these hooks exercise it. See API_COVERAGE.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

TEST_CASE("analytics wiring: app_started always built; sampling gates session_end + custom; crash never gated") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasAnalytics());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\analytics_wiring\\");

    // Fake identity/session/host + capture mode (real senders become no-ops).
    host.analyticsPrime();

    // --- app_started: the always-sent tier, with the anonymous payload. ---
    const std::string app = host.analyticsAppStarted();
    CHECK(has(app, "\"eventName\":\"app_started\""));
    CHECK(has(app, "\"install_id\":\"test-install-000000000000\""));  // the primed anon id
    CHECK(has(app, "\"version_status\":\"new\""));
    CHECK(has(app, "\"feat_director\""));                             // a representative feature flag
    CHECK(has(app, "\"feat_companion\""));                            // companion HUD window adoption flag
    CHECK(has(app, "\"feat_thread\""));                               // plugin worker thread adoption flag
    CHECK(has(app, "\"isDebug\":true"));   // capture mode routes to the debug bucket (belt-and-suspenders)

    // --- FULL launch: session_end + custom both enqueue. ---
    host.analyticsSetFullLaunch(true);
    host.analyticsQueueSessionEnd();
    host.analyticsQueueCustom("link_clicked");
    std::string full;
    int nFull = host.analyticsDrainPending(full);
    CHECK(nFull == 2);
    CHECK(has(full, "\"eventName\":\"session_end\""));
    CHECK(has(full, "\"eventName\":\"link_clicked\""));

    // --- MINIMAL launch: the remote sample rolled us out of the full tier, so both drop. ---
    host.analyticsSetFullLaunch(false);
    host.analyticsQueueSessionEnd();
    host.analyticsQueueCustom("link_clicked");
    std::string minimal;
    int nMinimal = host.analyticsDrainPending(minimal);
    CHECK(nMinimal == 0);                  // nothing enqueued — both gated
    CHECK(minimal.empty());

    // --- Crash is NEVER gated: still minimal, yet the crash path enqueues. ---
    host.analyticsSeedCrash(
        "Z:\\tmp\\mxbmrp3-tests\\analytics_wiring\\mxbmrp3\\pending_crash_test.json",
        "mxbikes.exe+0x2a42f0", "0xC0000005");
    std::string crash;
    int nCrash = host.analyticsDrainPending(crash);
    CHECK(nCrash == 1);
    CHECK(has(crash, "\"eventName\":\"crash\""));
    CHECK(has(crash, "mxbikes.exe+0x2a42f0"));   // the fault (leaf) carried through
    // The faulting-stack backtrace round-trips marker -> event as a space-delimited
    // "module+0xoffset ..." string prop (2.12.0). The leaf appears first; a deeper
    // frame confirms the whole list survived, not just the fault field.
    CHECK(has(crash, "\"stack\":"));
    CHECK(has(crash, "mxbmrp3.dlo+0xeaab4"));
    // Access-violation sub-type also rides along (2.13.0).
    CHECK(has(crash, "\"av_type\":\"read\""));

    host.shutdown();
}
