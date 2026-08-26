// ============================================================================
// tools/hud_window/companion_demo.cpp
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
    // "records": the Records HUD with server records loaded AND the player's own PB
    // row highlighted -- the row whose last column runs closest to the panel edge, and
    // the only way to look at that edge without the live provider. Implies the timing
    // scenario, since the player row only appears once there IS a personal best.
    bool recordsMode = false;
    // "preview": a FIXED REFERENCE scene. Records (as above) plus a six-rider RACE
    // grid, so all five reference panels -- Position, Lap, Timing, Standings, Records --
    // carry content. Built for a theme-preview page that has since been deleted; kept
    // because a scene that does not move is useful to compare anything against. It exists so the
    // preview can be lined up against the real renderer at a fixed scene rather than
    // against whatever the last capture happened to hold; see that tool's README.
    bool previewMode = false;
    // "maplost": the MAP in zoom mode with the player far off the track, which is the
    // one state the off-view pointer exists for and the one a normal capture can never
    // reach -- zoom follows the player, so on any ordinary scene the track is always
    // in view and the arrow never draws. Renders the map alone (no settings menu) so
    // the panel is what the shot is of.
    bool mapLostMode = false;
    // "update": announces an available release so the Updates row wears its tag.
    // Screenshottable only this way -- the tag is driven by UpdateChecker's live
    // status, which a demo with no network never reaches.
    bool updateMode = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "gamepad") gamepadMode = true;
        if (std::string(argv[a]) == "gear") gearMode = true;
        if (std::string(argv[a]) == "timing") timingMode = true;
        if (std::string(argv[a]) == "eventlog") eventlogMode = true;
        // "update": publish a fake UPDATE_AVAILABLE, so the settings footer's
        // "vX.Y.Z available!" chip and the Version widget's notification panel are on
        // screen. Composes with the other modes -- it only sets checker state.
        if (std::string(argv[a]) == "update") host.updateSetAvailable("9.9.9");
        if (std::string(argv[a]) == "records") recordsMode = true;
        if (std::string(argv[a]) == "preview") { previewMode = true; recordsMode = true; }
        if (std::string(argv[a]) == "maplost") mapLostMode = true;
        if (std::string(argv[a]) == "update")  updateMode = true;
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
    } else if (recordsMode) {
        // A TRACK ID, which the default scene has no reason to set: the player PB row
        // is looked up by StatsManager::getPersonalBest(trackId, bikeName), so with the
        // id empty there is no PB, no highlighted row, and the one row this scene
        // exists to look at never renders.
        host.eventInit("Southwick", "Thomas", 1600.0f, 2, "Test 450", "MX1",
                       /*trackId=*/"southwick");
        if (previewMode) {
            // A RACE, not the Testing session the plain records scene runs: it is what
            // gives the Standings table positions to colour (the podium ramp), gaps to
            // right-align and a player row to highlight mid-field, and what makes the
            // Lap widget read "3/10" instead of a bare lap number. Six riders against a
            // ten-row table, so the reserved-but-empty rows are on screen too -- the
            // state a theme has to look right in and the one a full grid hides.
            //
            // The bike names are the game's own strings, because that is what
            // getBikeBrandColor keys on: invent one and every brand mark on the plates
            // comes out the neutral grey, which is the single most colourful thing in
            // the row and the one a theme is judged against.
            host.raceEvent("Southwick", /*type=*/2);   // Race
            host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
            struct Rider { int num; const char* name; const char* bike; int best, gap; };
            const Rider grid[] = {
                { 10, "Wilhelmina",  "Kawasaki KX450 2023",   108900,     0 },
                { 22, "Bartholomew", "Honda CRF450R 2023",    109400,  1450 },
                {  7, "Cassiopeia",  "Yamaha YZ450F 2023",    109900,  3100 },
                {  4, "Thomas",      "Test 450",              108231,  4720 },
                {  3, "Aleksander",  "KTM 450 SX-F 2023",     110400,  8300 },
                { 18, "Genevieve",   "Husqvarna FC 450 2023", 111200, 14050 },
            };
            // THE PLAYER IS ENTERED FIRST, and that is load-bearing rather than
            // tidy: the eventInit just above re-initialises the event, and the first
            // RaceAddEntry after a session reset is taken as the local player
            // ("Local player identified: raceNum=..." in the log). Add the field in
            // finishing order and #10 becomes the player -- Position reads 1/6, the
            // gaps come out leader-relative and the row highlight lands on the wrong
            // rider, all of it plausible enough to screenshot without noticing.
            host.addEntry(4, "Thomas", "Test 450");
            for (const Rider& r : grid) if (r.num != 4) host.addEntry(r.num, r.name, r.bike);
            host.runInit(6);   // the session the label reads, not the shared setup's Practice
            std::vector<ClassRow> rows;
            for (const Rider& r : grid)
                rows.push_back({ .num = r.num, .best = r.best, .laps = 2, .gap = r.gap });
            host.classify(6, 0, rows);
            host.raceLap(6, 4, 0, 109500, /*best=*/1, /*split0=*/36200, /*split1=*/73000);
            host.raceLap(6, 4, 1, 108231, /*best=*/2, /*split0=*/35900, /*split1=*/72400);
        } else {
            host.session(1, 0, 0);
            host.runInit(1);
            // Two laps to establish the player PB (same setup timingMode uses), then a
            // records response fetched in through the stub -- no network, no provider.
            host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 2, .gap = 0 } });
            host.raceLap(1, 4, 0, 109500, /*best=*/1, /*split0=*/36200, /*split1=*/73000);
            host.raceLap(1, 4, 1, 108231, /*best=*/2, /*split0=*/35900, /*split1=*/72400);
        }
        // THROUGH THE FETCH, not recordsParse(): parsing alone leaves the HUD in its
        // IDLE state, which renders "Click Compare to load records." and no rows at all.
        // The stub short-circuits the network inside the real fetch path, so the HUD
        // ends in SUCCESS with these records exactly as a live provider would leave it.
        // Slower than the player's 1:48.231 so the player row sorts to the top and is
        // on screen; long rider and bike names, because the columns are sized for them.
        host.recordsSetFetchStub(0, R"({"notice":"demo","records":[
            {"player":"Wilhelmina","bike":"Kawasaki KX450SR","laptime":109900,"timestamp":"2024-05-01T12:34:56Z"},
            {"player":"Bartholomew","bike":"Honda CRF450RWE","laptime":110500,"timestamp":"2024-05-02T08:00:00Z"},
            {"player":"Cassiopeia","bike":"Yamaha YZ450FX","laptime":111001,"timestamp":"2024-05-03T18:30:00Z"}]})");
        // The fetch gate holds a 5 s cooldown measured from a zero-initialized start
        // timestamp, and under Wine GetTickCount() counts from wineserver start -- so a
        // fetch driven immediately on a fresh prefix is silently refused.
        while (GetTickCount() < 6000) Sleep(100);
        host.recordsStartFetch();
        for (int i = 0; i < 100 && host.recordsFetchState() == 1; ++i) Sleep(50);
        fprintf(stderr, "records fetch state: %d\n", host.recordsFetchState());
    } else if (updateMode) {
        // The tag reads UpdateChecker directly, so announcing a version is the whole
        // scene -- but the PANEL still has to be opened, exactly as the default
        // branch does it. Omitting that is a blank capture, not a missing tag.
        host.updateSetAvailable("9.9.9");
        host.showSettings(true);
        host.setActiveTab("General");
    } else if (mapLostMode) {
        auto MapVisible = host.sym<void(*)(int)>("MXBMRP3_Test_MapSetVisible");
        auto MapZoom    = host.sym<void(*)(int)>("MXBMRP3_Test_MapSetZoom");
        if (MapVisible) MapVisible(1);
        if (MapZoom)    MapZoom(1);
        // 900m off the stadium's origin -- well past the default 100m zoom range, so
        // the ribbon culls away entirely and the pointer is the only thing left to say
        // where the track went. Yaw 30 so the player's own icon is visibly rotated
        // rather than sitting at a lucky right angle.
        //
        // +Z rather than -Z so the arrow pins to the BOTTOM of the map panel: the
        // panel's top-right corner is under the demo's own camera/menu buttons, and
        // the first capture put the arrow behind them.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.30f,
                                   .posX = 0.0f, .posZ = 900.0f, .yaw = 30.0f } });
        // The panel rect and the pointer's rect on stderr, because a CAPTURE CANNOT
        // SETTLE THIS. The companion surface renders at 0.967 of the game's with a
        // centred viewport (see this tool's README), so "is the arrow on the edge?"
        // read off a screenshot is a guess -- and the first look at this scene was a
        // wrong one: the arrow was at the panel's top-right corner, correctly, and
        // simply outside the region I had cropped. These two lines answer it.
        host.draw();
        const auto mp = host.hudScreenEdges("map_hud");
        const auto mq = host.hudQuadRects("map_hud");
        fprintf(stderr, "maplost: panel l=%.4f t=%.4f r=%.4f b=%.4f\n",
                mp.l/1e6, mp.t/1e6, mp.r/1e6, mp.b/1e6);
        if (!mq.empty()) {
            const auto& a = mq.back();   // the pointer: drawn last
            fprintf(stderr, "maplost: pointer l=%.4f t=%.4f r=%.4f b=%.4f\n",
                    a.l, a.t, a.r, a.b);
        }
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

    // "dump": print the plugin's own draw list for the five reference panels and
    // exit, without opening a window. It fed the deleted theme-preview page's
    // golden tables and has no consumer in the tree today; it survives as the one
    // way to read the renderer's actual numbers from the command line — the README
    // is explicit that a capture cannot settle geometry (the companion surface
    // renders at 0.967 of the game's) and that the plugin's numbers are the
    // truth. Regenerating those tables by hand from a screenshot is how they
    // went stale before; this makes it a command.
    //
    // Panel-relative pixels at 1920x1080, which is the tables' unit. The string
    // y is the glyph's em TOP, as the hook reports it; the page draws with
    // textBaseline 'middle', so the table adds half the tier's em and this
    // prints the raw y for that arithmetic to be applied per row.
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) != "dump") continue;
        // Warm up in WALL TIME, not in frames. A HUD rebuilds on its own update
        // pass, so a fetch that landed after the last draw is state the draw list
        // has not caught up with (Records renders its IDLE text and the table is
        // empty) -- but standings ROWS also SLIDE, and that animation is driven by
        // the clock rather than by the frame count. Five back-to-back draws left it
        // mid-flight: the player's row printed at emTop 141.6 against row 3's 142.3,
        // one row drawn on top of another, and its highlight followed it there. A
        // dump is a golden's source, so it has to be the settled state and the same
        // one every run.
        for (int i = 0; i < 100; ++i) {
            host.draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        struct P { const char* id; int which; };
        const P panels[] = { { "position", 10 }, { "lap", 9 }, { "timing", 2 },
                             { "standings", 0 }, { "records", 8 },
                             // MAP is last and slightly apart: it is here
                             // because it is the legacy caption chain's
                             // representative, and comparing its title against a
                             // plan panel's is the only way to see the two chains
                             // disagree in numbers rather than in a screenshot.
                             { "map", 11 } };
        for (const P& pan : panels) {
            const auto quads = host.hudQuadRects(PluginHost::HudId(pan.which));
            if (quads.empty()) continue;
            // The PANEL's own edges, not quads[0]: on a themed panel the first
            // quad is the frame's top-left SLICE, so reading the origin off it
            // is right by luck and the size wrong by a slice.
            const auto edge = host.hudScreenEdges(PluginHost::HudId(pan.which));
            const auto rect = host.hudPanelRect(PluginHost::HudId(pan.which));
            const double ox = edge.l / 1e6, oy = edge.t / 1e6;
            printf("PANEL %s x=%.1f y=%.1f w=%.1f h=%.1f\n", pan.id,
                   ox * 1920.0, oy * 1080.0,
                   rect.w / 1e6 * 1920.0, rect.h / 1e6 * 1080.0);
            for (size_t i = 1; i < quads.size(); ++i) {
                printf("BOX   %s %zu x=%.1f y=%.1f w=%.1f h=%.1f\n", pan.id, i,
                       (quads[i].l - ox) * 1920.0, (quads[i].t - oy) * 1080.0,
                       (quads[i].r - quads[i].l) * 1920.0,
                       (quads[i].b - quads[i].t) * 1080.0);
            }
            for (const auto& st : host.hudStringRows(PluginHost::HudId(pan.which))) {
                if (st.text.empty()) continue;
                printf("STR   %s \"%s\" x=%.1f emTop=%.1f\n", pan.id, st.text.c_str(),
                       (st.x - ox) * 1920.0, (st.y - oy) * 1080.0);
            }
        }
        host.shutdown();
        return 0;
    }

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
