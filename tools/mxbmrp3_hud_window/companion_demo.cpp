// ============================================================================
// tools/mxbmrp3_hud_window/companion_demo.cpp
// Opens the plugin's REAL in-process companion window and holds it up so it can
// be screenshotted. Loads the cross-compiled DLL, drives a "Testing session +
// settings menu open" scenario through the actual callbacks, enables the
// companion window (MXBMRP3_Test_CompanionWindow), then keeps pumping Draw so the
// window thread has live primitives to render. Run under Wine (+Xvfb for capture):
//
//   x86_64-w64-mingw32-g++ -std=c++17 -I ../../tests/integration/harness \
//       companion_demo.cpp -o companion_demo.exe -lws2_32
//   wine companion_demo.exe mxbmrp3_test.dlo
// ============================================================================
#include "plugin_host.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

// Closed "stadium" centerline (two straights + two semicircles) so the Map draws.
static std::vector<TrackSegmentRow> stadium() {
    const double PI = 3.14159265358979323846, R = 130.0, L = 300.0;
    const int straightN = 8, curveN = 24;
    std::vector<TrackSegmentRow> segs;
    auto addStraight = [&](double total) {
        for (int i = 0; i < straightN; ++i) { TrackSegmentRow s; s.type = 0; s.length = float(total / straightN); segs.push_back(s); }
    };
    auto addSemicircle = [&]() {
        for (int i = 0; i < curveN; ++i) { TrackSegmentRow s; s.type = 1; s.radius = float(R); s.length = float(PI * R / curveN); segs.push_back(s); }
    };
    addStraight(L); addSemicircle(); addStraight(L); addSemicircle();
    segs[0].startX = 0; segs[0].startY = 0; segs[0].angle = 0;
    return segs;
}

int main(int argc, char** argv) {
    const char* dll = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }

    host.startup("Z:\\tmp\\mxbmrp3-tests\\companion\\");
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick", /*type=*/1);  // Testing
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.classify(1, 0, { { .num = 4, .best = 0, .laps = 0, .gap = 0 } });
    host.trackCenterline(stadium());
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.30f } });
    host.telemetry(0.0f, 3, 0.0f, 0.30f);   // gear 3 (6-speed set in eventInit) -> shows the digit "3"

    // "gamepad" mode: preview the gamepad widget with a faked connected controller.
    // "gear" mode: just the default-visible widgets (incl. the gear widget) with no
    // settings menu covering them — for eyeballing widget rendering. Otherwise the
    // default settings-menu scene.
    bool gamepadMode = false, gearMode = false, timingMode = false, eventlogMode = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "gamepad") gamepadMode = true;
        if (std::string(argv[a]) == "gear") gearMode = true;
        if (std::string(argv[a]) == "timing") timingMode = true;
        if (std::string(argv[a]) == "eventlog") eventlogMode = true;
    }
    if (gamepadMode) {
        host.fakeGamepad(true);
    } else if (eventlogMode) {
        // Event Log showcase with the DIRECTOR transparency feed: run a spectated race with
        // the auto-director enabled and the "Director" event type on, so each shot decision
        // (and a lock) lands in the log with the camera icon. Uses the real director path.
        host.eventLogSetVisible(true);
        host.eventLogEnableDirector(true);
        host.raceEvent("Southwick", /*type=*/2);   // Race
        host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
        const int nums[4] = { 10, 22, 7, 3 };
        for (int n : nums) { char nm[16]; snprintf(nm, sizeof(nm), "R%d", n); host.addEntry(n, nm); }
        host.directorSetEnabled(true);              // logs "Auto-director enabled"
        host.directorSetStories(1 /*battles*/ | 4 /*fastest*/ | 8 /*pace*/);
        auto classify = [&]{ host.classify(6, 200000, {
            { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
            { .num = 22, .best = 90500, .laps = 3, .gap = 1200 },   // <=2500 -> battle
            { .num = 7,  .best = 91000, .laps = 3, .gap = 2600 },
            { .num = 3,  .best = 91500, .laps = 3, .gap = 5000 }, }); };
        auto positions = [&]{ host.raceTrackPosition({
            { .num = 10, .trackPos = 0.50f }, { .num = 22, .trackPos = 0.49f },
            { .num = 7,  .trackPos = 0.40f }, { .num = 3,  .trackPos = 0.30f } }); };
        long long t = 1000;
        host.directorSetNowMs(t); classify(); positions();
        for (int i = 0; i < 6; ++i) { t += 600; host.directorSetNowMs(t); positions(); classify(); }
        host.directorToggleLock();                  // logs "Locked on #N"
        host.directorSetNowMs(-1);
    } else if (timingMode) {
        // Timing HUD showcase: enable the primary gap + several secondary chips, then
        // complete a couple of player laps so the references (PB / Overall / Last Lap /
        // All-Time) populate. With no live lap running the chips show their full-lap
        // reference TIME in the shared value slot (the single-value-chip behavior), not
        // a "-" placeholder. Player is #4 (see setup above).
        // GapTypeFlags (from timing_hud.h, kept as literals here to avoid pulling plugin
        // headers into the demo): PB=1, IDEAL=2, OVERALL=4, ALLTIME=8, RECORD=16, LASTLAP=32.
        // Default comparison set (Session PB + Alltime PB = 2 rows) so the stack matches the
        // shipped default the Notices divider is tuned for. (Enabling more rows makes the panel
        // taller; the notice/gapbar defaults assume the default height.)
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/1 | 8);
        // (The all-time-PB notice now flashes ONE SNAP BELOW the Timing HUD — no overlap — so
        // it no longer needs suppressing; capture early to see the stack, late for just Timing.)
        // Two completed laps: sets the best-lap entry, lap log and all-time PB. Splits at
        // ~1/3 and 2/3 so the sector references exist too.
        host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 2, .gap = 0 } });
        host.raceLap(1, 4, 0, 109500, /*best=*/1, /*split0=*/36200, /*split1=*/73000);
        host.raceLap(1, 4, 1, 108231, /*best=*/2, /*split0=*/35900, /*split1=*/72400);
    } else if (gearMode) {
        // nothing: the HUD renders its default-visible widgets over the empty scene
    } else {
        // Default (or a specific tab named on the command line, e.g. "Timing").
        const char* tab = "General";
        for (int a = 1; a < argc; ++a) {
            std::string s(argv[a]);
            if (s == "tab" && a + 1 < argc) tab = argv[a + 1];
        }
        host.showSettings(true);
        host.setActiveTab(tab);
    }
    // Pin the companion as the active surface so surface-scoped chrome (the settings
    // menu, the pointer) renders on the companion window we screenshot — it never
    // takes focus, so it's never the foreground-derived active surface under Wine.
    host.forceActiveSurface(1);
    host.draw();

    host.companionWindow(true);   // open the standalone window
    fprintf(stderr, "companion window opened; holding for capture...\n");

    // "close" mode: reproduce the user clicking the window's X button — post WM_CLOSE
    // to the companion window from OUTSIDE its thread, so its own WndProc handles it
    // on the window thread (the path that used to self-join and crash). Then keep
    // running and shut down; a clean exit(0) means the teardown is safe.
    bool closeTest = (argc > 3 && std::string(argv[3]) == "close");

    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    for (int i = 0; i < seconds * 50; ++i) {
        host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (closeTest && i == seconds * 25) {  // halfway: simulate the X button
            HWND hwnd = FindWindowW(L"MXBMRP3CompanionWindow", nullptr);
            fprintf(stderr, "posting WM_CLOSE to companion window (hwnd=%p)\n", (void*)hwnd);
            if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    host.companionWindow(false);
    host.shutdown();
    fprintf(stderr, "clean shutdown\n");
    return 0;
}
