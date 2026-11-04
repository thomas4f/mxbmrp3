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
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

#include <windows.h>

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
    CHECK(has(app, "\"feat_twitch\""));                               // 2.27.0: Twitch chat connection in use
    CHECK(has(app, "\"feat_youtube\""));                              // 2.28.0: YouTube chat connection in use
    CHECK(has(app, "\"hud_stream_chat\""));                           // the global chat HUD, added by hand
    CHECK(has(app, "\"feat_companion\""));                            // companion HUD window adoption flag
    CHECK(has(app, "\"feat_thread\""));                               // plugin worker thread adoption flag
    CHECK(has(app, "\"feat_hwaccel\""));                              // GPU-rendering setting for both windows
    CHECK(has(app, "\"ach_pct\""));                                   // 2.20.0: achievement tiers earned, percent
    CHECK(has(app, "\"ach_unlocked\""));                              // 2.20.0: achievements earned at any tier
    // 2.21.0: a row with a tier rides along by id; a locked one is absent.
    CHECK_FALSE(has(app, "\"ach_config_reloads\""));
    host.configReloaded();                                             // Tinkerer Bronze
    const std::string earned = host.analyticsAppStarted();
    CHECK(has(earned, "\"ach_config_reloads\":1"));
    CHECK_FALSE(has(earned, "\"ach_races\""));
    CHECK(has(app, "\"isDebug\":true"));   // capture mode routes to the debug bucket (belt-and-suspenders)

    // --- panel_theme: the label, through the REAL AssetManager lookup. ---
    // The classifier's own rules are pinned in tests/unit/test_analytics_theme.cpp;
    // what only this layer can prove is that the payload asks the live theme
    // registry, in an order where the answer exists. If the property were built
    // before asset discovery every install would report "missing", and a unit test
    // holding the flag itself could never see that.
    CHECK(has(app, "\"panel_theme\":\"none\""));   // nothing selected in this scenario
    if (host.hasThemeGeometry()) {
        // A SHIPPED name reports itself: installing under the shipped theme's name
        // makes the lookup resolve, so the label is the name rather than "custom".
        // carbon-dark, since the theme cull: "debug" is no longer in
        // kShippedThemes (its master stays in assets/, unshipped).
        host.installTheme("carbon-dark", 1.0f, 1.0f, /*titleBand=*/1, /*card=*/1);
        CHECK(has(host.analyticsAppStarted(), "\"panel_theme\":\"carbon-dark\""));

        // A third-party theme is counted but never named — the user's folder name
        // is exactly what must not reach the payload.
        host.installTheme("skinner-pack-2000", 1.0f, 1.0f, /*titleBand=*/1, /*card=*/1);
        const std::string custom = host.analyticsAppStarted();
        CHECK(has(custom, "\"panel_theme\":\"custom\""));
        CHECK(!has(custom, "skinner-pack-2000"));

        // Back to unthemed, so nothing below inherits a theme.
        host.clearTheme();
        CHECK(has(host.analyticsAppStarted(), "\"panel_theme\":\"none\""));
    }

    // --- language: the pack IN EFFECT, through the real I18n state. ---
    // The label's rules are pinned in tests/unit/test_analytics_language.cpp;
    // this proves the payload reads the RESOLVED pack and the stored choice.
    // Untouched, the selection is the System default.
    CHECK(has(app, "\"language_system\":1"));
    CHECK(has(app, "\"language\":\""));
    // English picked explicitly: "en", and no longer the System default.
    host.setLanguage("");
    {
        const std::string en = host.analyticsAppStarted();
        CHECK(has(en, "\"language\":\"en\""));
        CHECK(has(en, "\"language_system\":0"));
    }
    // A stored pack that is not installed shows English, so it reports English,
    // and its folder name never reaches the payload.
    host.setLanguage("no-such-pack-xyz");
    {
        const std::string gone = host.analyticsAppStarted();
        CHECK(has(gone, "\"language\":\"en\""));
        CHECK(!has(gone, "no-such-pack-xyz"));
    }
    host.setLanguage("system");

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
    // ...and the address it tried to touch (2.29.0).
    CHECK(has(crash, "\"av_addr\":\"null+0x28\""));
    // The same crash once more as an error report for the Errors page (2.22.0):
    // one object, grouped by module, the whole walk one frame per line - a frame
    // the event's 180-character stack could not carry is here.
    std::string report;
    CHECK(host.analyticsDrainErrors(report) == 1);
    CHECK(has(report, "\"errorType\":\"mxbikes.exe\""));
    CHECK(has(report, "mxbikes.exe+0x2a42f0 0xC0000005 read game"));
    CHECK(has(report, "\"severity\":\"fatal\""));
    CHECK(has(report, "\"kind\":\"crash\""));
    // The target heads the trace, not the message: the dashboard groups on the
    // message, and a heap address would give every crash its own group.
    CHECK(has(report, "\"stackTrace\":\"accessed null+0x28\\nmxbikes.exe+0x2a42f0"));
    CHECK_FALSE(has(report, "read null+0x28"));
    CHECK(has(report, "mxbmrp3.dlo+0xeaab4\\nmxbmrp3.dlo+0x1234\\nntdll.dll+0x1234\\nkernel32.dll+0x5678"));
    CHECK(has(report, "\"appVersion\":\"9.9.9\""));   // the version that CRASHED, from the marker
    CHECK_FALSE(has(report, "\"eventName\""));         // not an event

    // --- Null-frame attribution contract of the backtrace resolver. ---
    // A call through a null function pointer (Rip==0) must resolve to the
    // canonical "unknown+0x0". Regression: GetModuleHandleExA treats a NULL
    // lpModuleName as "return the host EXE's handle" (even with FROM_ADDRESS on
    // some Windows builds), which used to label null-call frames as
    // <hostexe>+<0 minus the exe base> — the "mxbikes.exe+0xfffffffec0000000"
    // rows on the v1.27.7.44 dashboard, splitting one signature in two and
    // smearing it onto the game module.
    if (!host.resolveFrame(0).empty()) {
        CHECK(host.resolveFrame(0) == "unknown+0x0");
        // Sanity on the resolved path: an address inside a module names it.
        std::string self = host.resolveFrame(
            reinterpret_cast<unsigned long long>(&GetModuleHandleA));
        CHECK(has(self, "+0x"));
        CHECK(!has(self, "unknown"));
    }

    host.shutdown();
}

// A pack the player made, staged where a player installs one (Documents\...\
// mxbmrp3\languages\<name>\) and selected: the ping counts it as "custom" and
// its folder name never leaves. The guard removes the plugin's synced copy, a
// tree every test of the run shares (see i18n_test.cpp's SyncedPack).
TEST_CASE("analytics wiring: a user's own language pack reports as custom, never by name") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\analytics_language\\";
    const char* kFolder = "my-own-lang-pack";
    const std::string root = save + "mxbmrp3";
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\languages").c_str(), nullptr);
    const std::string dir = root + "\\languages\\" + kFolder;
    CreateDirectoryA(dir.c_str(), nullptr);
    {
        const std::string ini = "[pack]\nname = Mine\n";
        HANDLE h = CreateFileA((dir + "\\language.ini").c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(h != INVALID_HANDLE_VALUE);
        DWORD written = 0;
        WriteFile(h, ini.data(), static_cast<DWORD>(ini.size()), &written, nullptr);
        CloseHandle(h);
    }
    struct Synced {
        std::string dir;
        ~Synced() {
            DeleteFileA((dir + "\\language.ini").c_str());
            RemoveDirectoryA(dir.c_str());
        }
    } synced{ std::string("plugins\\mxbmrp3_data\\languages\\") + kFolder };

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasAnalytics());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Display]\nlanguage=my-own-lang-pack\n");
    REQUIRE(host.startup(save.c_str()) >= 0);
    host.analyticsPrime();

    const std::string app = host.analyticsAppStarted();
    CHECK(has(app, "\"language\":\"custom\""));
    CHECK(has(app, "\"language_system\":0"));
    CHECK(!has(app, kFolder));
}
