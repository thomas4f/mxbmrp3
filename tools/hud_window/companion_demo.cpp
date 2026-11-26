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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
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
    // "rpm N": the gear scene riding at N rpm, with a bike that shifts at 10500
    // and hits the limiter at 12000 -- for eyeballing the RPM widget's strip
    // (turn it on with EXTRA_INI=$'[RpmWidget]\nvisible=1'). At or past 12000 the
    // limiter flash is held in its lit phase ("rpmdark": its dark phase).
    int rpmDemo = -1;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "rpm") rpmDemo = std::atoi(argv[a + 1]);
    // "slide N": the gear scene riding at 15 m/s with the path N degrees clockwise
    // of the nose (rear out to the right for N > 0) -- for eyeballing the Slide
    // widget (turn it on with EXTRA_INI=$'[SlideWidget]\nvisible=1').
    bool slideDemo = false;
    float slideDeg = 0.0f;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "slide") { slideDemo = true; slideDeg = static_cast<float>(std::atof(argv[a + 1])); }
    // "wheelie N": the gear scene up on the back wheel at N degrees, having peaked
    // 15 higher a moment ago so the Wheelie widget's peak marker shows (turn it on
    // with EXTRA_INI=$'[WheelieWidget]\nvisible=1').
    int wheelieDemo = -1;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "wheelie") wheelieDemo = std::atoi(argv[a + 1]);
    // "stance N": the gear scene with the rider standing (1) or seated (0), from
    // a frame carrying the stance as newer MX Bikes builds send it (turn the
    // widget on with EXTRA_INI=$'[StanceWidget]\nvisible=1').
    int stanceDemo = -1;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "stance") stanceDemo = std::atoi(argv[a + 1]);
    // "shift MS": the gear scene shifting 3 -> 4 with Motion on, photographed MS
    // into the digit roll (hud/digit_roll.h): this window cuts the digits at
    // their line (core/render_clip.h), the game fades them instead.
    int shiftDemo = -1;
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "shift") shiftDemo = std::atoi(argv[a + 1]);
    // "nowplaying N": a player playing, with N the controls it takes and their
    // state as MXBMRP3_Test_MediaPublish reads them (1 seek | 2 shuffle | 4 loop,
    // 8 shuffle on, 16 loop over one track, 32 over the list). Turn the media
    // controls and the widget on with
    // EXTRA_INI=$'[General]\nnowPlaying=1\n[NowPlayingWidget]\nvisible=1'.
    for (int a = 1; a + 1 < argc; ++a)
        if (std::string(argv[a]) == "nowplaying")
            host.mediaPublish("Hold On To The Line", "The Riders", 4, 83000, 214000, 60, std::atoi(argv[a + 1]));
    if (rpmDemo >= 0)
        host.eventInit("Southwick", "Thomas", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 0.0f, 10500, 12000);
    else
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
    // "hudscroll <eventlog|laplog|chat|records|friends>": that HUD alone with a
    // long list, the pointer up over it and the list moved back two rows on its
    // scrollbar (hud/hud_scroll.h); a live list then gets one more entry, so the
    // caption reads "1 new". Records rests on the player's own row. With
    // "glide MS" Motion is on and the clock is held MS into a one-row move, so
    // the capture shows the rows crossing the list's edges.
    std::string hudScroll;
    // "toast": the gear scene with an achievement toast up (Tinkerer, from a config
    // reload) -- for eyeballing the card's default place among the widgets. Pair it
    // with EXTRA_INI=$'[Achievements]\ntoastMs=30000' so the card outlives the hold.
    bool toastMode = false;
    // "ghosttoast": the same, earned the real way - 3.5 s of the bike riding on
    // upright with the rider off, through the telemetry handler (Ghost Ride).
    bool ghostToastMode = false;
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
    // "timedrace N": the preview grid in a 15:00 + N laps race, 12:34 left, with the
    // Time widget and the Timing panel's Time/Format rows on -- every place the
    // session clock reads "12:34 + N laps". "dashtime" swaps the Timing panel and
    // notices for the Dash on its Time page (they share the centre stack's box).
    int timedRaceLaps = 0;
    bool dashTimeMode = false;
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
    // "fmx": the FMX HUD mid-trick, so the trick stack, the combo arc and the
    // stats strip all have live content. The strip is the reason this scene
    // exists - four measurements told apart by four glyphs is the one thing a
    // capture can settle and the draw-list numbers cannot.
    bool fmxMode = false;
    bool gapbarMode = false;
    // "flags": a race with a rider in every flag-marker state the Standings, Map and
    // Radar draw -- stationary hazard (crashed), wrong way, blue flag, last lap and
    // finished -- so the markers' sprite + colour pairing (rider_flag_icons.h) can be
    // compared before and after a change. The wrong-way state is confirmed on a
    // steady_clock timer with no seam, so this scene waits it out (~1.6 s).
    bool flagsMode = false;
    // "pitlap": the timing scene plus a practice cut (no time, splits intact) and the
    // lap after it abandoned for the pits mid-lap, S1 crossed -- the Lap Log rows that
    // say INVALID and PIT. Pair it with EXTRA_INI=$'[LapLogHud]\nvisible=1'.
    bool pitlapMode = false;
    // "deltatrace": a reference lap, a second one at a varying pace and a third
    // under way, ridden on a simulated lap clock, with the Delta Trace shown and
    // the Map's lap delta on (Session PB): the new lap drawing over the old.
    bool deltaMode = false;
    // "dash": a race under way for the Dash's pages - two laps ridden and a third
    // in progress on the simulated lap clock, rivals ahead and behind, revs,
    // inputs and fuel fed every step, and drawn ON TRACK (the Fuel rows read "-"
    // in spectate). Pair it with EXTRA_INI=$'[DashHud]\nvisible=1\npage=<key>'.
    bool dashMode = false;
    // "motion N": Motion at Normal, the settings menu opening N ms ago, the clock
    // then held so the capture is that instant of the fade. "motiontab N": the
    // same, for the Appearance tab's body fading in over the General tab's panel.
    // A row of N gives the frames of an animation (see TESTING.md Layer 6).
    int motionMs = -1;
    bool motionTab = false;
    // "motionhud NAME N": the same for one HUD (by harness id) coming in - pair
    // it with a scene that shows it, e.g. "flags motionhud map_hud 120".
    const char* motionHud = nullptr;
    // "motiondeck N": Motion at Normal, the carousel deck stepped (Deck +) once
    // the window is up, the clock then held N ms after the step.
    int deckStepMs = -1;
    bool decksDemo = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "flags") flagsMode = true;
        if (std::string(argv[a]) == "gamepad") gamepadMode = true;
        if (std::string(argv[a]) == "gear") gearMode = true;
        if (std::string(argv[a]) == "rpm") gearMode = true;
        if (std::string(argv[a]) == "slide") gearMode = true;
        if (std::string(argv[a]) == "wheelie") gearMode = true;
        if (std::string(argv[a]) == "stance") gearMode = true;
        if (std::string(argv[a]) == "shift") gearMode = true;
        // "decks" ["air"]: every deck on and filled the way the mocks
        // show them (see the gearMode branch).
        if (std::string(argv[a]) == "decks") { gearMode = true; decksDemo = true; }
        if (std::string(argv[a]) == "toast") { gearMode = true; toastMode = true; }
        if (std::string(argv[a]) == "ghosttoast") { gearMode = true; ghostToastMode = true; }
        if (std::string(argv[a]) == "timing") timingMode = true;
        if (std::string(argv[a]) == "pitlap") { timingMode = true; pitlapMode = true; }
        // "gapbar": the timing scene plus the Gap Bar (off by default) with a live gap
        // planted, the ghost marker (from the PB the timing laps set) and two opponents,
        // so the bar renders with everything it can show. For eyeballing its glyph.
        if (std::string(argv[a]) == "gapbar") { timingMode = true; gapbarMode = true; }
        if (std::string(argv[a]) == "eventlog") eventlogMode = true;
        // "update": publish a fake UPDATE_AVAILABLE, so the settings footer's
        // "vX.Y.Z available!" chip is on screen. Composes with the other modes -- it
        // only sets checker state.
        if (std::string(argv[a]) == "update") host.updateSetAvailable("9.9.9");
        if (std::string(argv[a]) == "records") recordsMode = true;
        if (std::string(argv[a]) == "preview") { previewMode = true; recordsMode = true; }
        if (std::string(argv[a]) == "timedrace" && a + 1 < argc) {
            previewMode = true; recordsMode = true; timedRaceLaps = std::atoi(argv[a + 1]);
        }
        if (std::string(argv[a]) == "dashtime") dashTimeMode = true;
        if (std::string(argv[a]) == "maplost") mapLostMode = true;
        if (std::string(argv[a]) == "update")  updateMode = true;
        if (std::string(argv[a]) == "hudscroll" && a + 1 < argc) {
            hudScroll = argv[a + 1];
            if (hudScroll == "eventlog") eventlogMode = true;
            if (hudScroll == "laplog") timingMode = true;
            if (hudScroll == "records") recordsMode = true;
        }
        if (std::string(argv[a]) == "fmx")     fmxMode = true;
        if (std::string(argv[a]) == "deltatrace") deltaMode = true;
        if (std::string(argv[a]) == "dash") dashMode = true;
        if ((std::string(argv[a]) == "motion" || std::string(argv[a]) == "motiontab") && a + 1 < argc) {
            motionMs = std::atoi(argv[a + 1]);
            motionTab = std::string(argv[a]) == "motiontab";
        }
        if (std::string(argv[a]) == "motiondeck" && a + 1 < argc) deckStepMs = std::atoi(argv[a + 1]);
        if (std::string(argv[a]) == "motionhud" && a + 2 < argc) {
            motionHud = argv[a + 1];
            motionMs = std::atoi(argv[a + 2]);
        }
        // The missing-assets warning on the Dash, raised as if discovery found
        // nothing (test builds never raise it themselves); the staged assets still
        // draw. "brokeninstall real" draws the same rows with the real font instead
        // of the block letters, for a pixel diff of the two.
        if (std::string(argv[a]) == "brokeninstall") {
            gearMode = true;
            const bool real = a + 1 < argc && std::string(argv[a + 1]) == "real";
            host.installWarning(0, 0, true, real);
        }
        // "welcome" / "updated" / "updatemsg": the gear scene with a startup message
        // on the Dash (a fresh install, a new release line, a release available).
        if (std::string(argv[a]) == "welcome") { gearMode = true; host.messagesStartup(true, "1.33"); }
        if (std::string(argv[a]) == "updated") { gearMode = true; host.messagesStartup(false, "1.33", "1.32"); }
        if (std::string(argv[a]) == "updatemsg") { gearMode = true; host.updateSetAvailable("1.34.0"); }
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
        if (pitlapMode) {
            host.raceLap(1, 4, 2, 0, /*best=*/0, /*split0=*/36400, /*split1=*/73300);
            host.raceSplit(1, 4, /*lapNum=*/2, /*split=*/0, 36100);
            host.raceTrackPosition({ { .num = 4, .trackPos = 0.40f } });
            host.draw();      // the lap ticking, as it was when Esc was pressed
            host.runStop();   // Esc
            host.runDeinit(); // ...to the pits
        }
        if (gapbarMode) {
            // Alone in the centre stack: the Timing panel and the all-time-PB notice
            // share its top box and draw over it.
            host.setHudVisible("gap_bar_hud", true);
            host.setHudVisible("timing_hud", false);
            host.noticesSetVisible(false);
            host.addEntry(10, "R10");
            host.addEntry(22, "R22");
            host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 2, .gap = 0 },
                                  { .num = 10, .best = 109000, .laps = 2, .gap = 800 },
                                  { .num = 22, .best = 110000, .laps = 2, .gap = 1900 } });
            host.raceTrackPosition({ { .num = 4, .trackPos = 0.42f }, { .num = 10, .trackPos = 0.61f },
                                     { .num = 22, .trackPos = 0.27f } });
            host.gapBarForceGap(-450, true);   // 0.45s up on PB: green, grows right
            // Start/finish and two splits (meters along the ~1417 m stadium), so the
            // split ticks (on by default) have somewhere to go.
            host.trackCenterline(stadium(), { 10.0f, 480.0f, 950.0f, 0.0f });
        }
    } else if (deltaMode) {
        auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
        auto mapLapDelta = host.sym<void (*)(int)>("MXBMRP3_Test_MapSetLapDelta");
        if (setNowUs && mapLapDelta) {
            host.setHudVisible("delta_trace_hud", true);
            mapLapDelta(1);
            host.trackCenterline(stadium(), { 10.0f, 480.0f, 950.0f, 0.0f });
            long long lapStart = 1000000000LL;
            int lapMs = 0;
            // One sample per 0.5% of the lap; `wave` swings the pace around 60 s.
            auto ride = [&](int to, float wave) {
                double t = 0.0;
                for (int p = 1; p <= to; ++p) {
                    t += 300.0 + wave * std::sin(p * 0.035 + 2.0) + wave * 0.4 * std::sin(p * 0.12);
                    setNowUs(lapStart + static_cast<long long>(t * 1000.0));
                    host.raceTrackPosition({ { 4, p / 200.0f } });
                }
                lapMs = static_cast<int>(t + 300.0);
            };
            setNowUs(lapStart - 20000);
            host.raceTrackPosition({ { 4, 0.99f } });
            setNowUs(lapStart);
            host.raceTrackPosition({ { 4, 0.00f } });
            ride(197, 0.0f);
            lapStart += static_cast<long long>(lapMs) * 1000LL;
            setNowUs(lapStart);
            host.classify(1, 0, { { .num = 4, .best = lapMs, .laps = 1, .gap = 0 } });
            host.raceTrackPosition({ { 4, 0.00f } });
            host.raceLap(1, 4, /*lapNum=*/1, lapMs, /*best=*/1, lapMs / 3, lapMs * 2 / 3);
            ride(197, 14.0f);
            lapStart += static_cast<long long>(lapMs) * 1000LL;
            setNowUs(lapStart);
            host.classify(1, 0, { { .num = 4, .best = lapMs, .laps = 2, .gap = 0 } });
            host.raceTrackPosition({ { 4, 0.00f } });
            host.raceLap(1, 4, /*lapNum=*/2, lapMs, /*best=*/0, lapMs / 3, lapMs * 2 / 3);
            ride(80, -10.0f);   // the clock stays put: the capture is of this moment
        }
    } else if (dashMode) {
        auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
        if (setNowUs) {
            // A 7 l tank and a bike that shifts at 10500 and hits the limiter at
            // 12000, re-initialised so the player is entered first (see the preview
            // scene's note on the first RaceAddEntry).
            host.eventInit("Southwick", "Thomas", 1600.0f, 2, "Test 450", "MX1", "southwick", 0, "",
                           7.0f, 10500, 12000, 13000);
            host.raceEvent("Southwick", /*type=*/2);   // Race
            host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
            struct Rider { int num; const char* name; const char* bike; float ahead; };
            const Rider grid[] = {
                {  4, "Thomas",      "Test 450",              0.000f },
                { 10, "Wilhelmina",  "Kawasaki KX450 2023",   0.060f },
                { 22, "Bartholomew", "Honda CRF450R 2023",    0.018f },
                {  7, "Cassiopeia",  "Yamaha YZ450F 2023",   -0.012f },
                {  3, "Aleksander",  "KTM 450 SX-F 2023",    -0.050f },
                { 18, "Genevieve",   "Husqvarna FC 450 2023", -0.110f },
            };
            for (const Rider& r : grid) host.addEntry(r.num, r.name, r.bike);
            host.runInit(6);
            host.trackCenterline(stadium(), { 10.0f, 480.0f, 950.0f, 0.0f });
            // Back on track before each stint: raceLap() draws in spectate, and the
            // switch either way clears the telemetry and its history.
            host.drawWithState(0);
            long long lapStart = 1000000000LL;
            int lapMs = 0, step = 0;
            float fuel = 6.2f;
            auto positions = [&](float pos) {
                std::vector<TrackRow> rows;
                for (const Rider& r : grid) {
                    const float p = pos + r.ahead;
                    rows.push_back({ .num = r.num, .trackPos = p - std::floor(p) });
                }
                host.raceTrackPosition(rows);
            };
            // One sample per 0.5% of the lap, the pace swinging around 60 s by
            // `wave`; revs, gear and inputs follow a corner every quarter lap.
            auto ride = [&](int to, float wave) {
                double t = 0.0;
                for (int p = 1; p <= to; ++p, ++step) {
                    t += 300.0 + wave * std::sin(p * 0.035 + 2.0) + wave * 0.4 * std::sin(p * 0.12);
                    setNowUs(lapStart + static_cast<long long>(t * 1000.0));
                    positions(p / 200.0f);
                    const float c = std::sin(p * 0.157f);   // +1 straight, -1 corner
                    fuel -= 0.0021f;
                    TelemetryRow r;
                    r.speed = 18.0f + 7.0f * c;
                    r.gear = c > 0.3f ? 4 : c > -0.4f ? 3 : 2;
                    r.rpm = 8200 + static_cast<int>(2600.0f * std::sin(p * 0.47f));
                    r.throttle = std::clamp(0.55f + 0.6f * c, 0.0f, 1.0f);
                    r.frontBrake = std::clamp(-0.9f * c - 0.2f, 0.0f, 1.0f);
                    r.fuel = fuel;
                    r.trackPos = p / 200.0f;
                    r.time = static_cast<float>(step) * 0.3f;
                    host.telemetryFrame(r);
                }
                lapMs = static_cast<int>(t + 300.0);
            };
            auto classify = [&](int laps, int best) {
                host.classify(6, laps * 60000, { { .num = 10, .best = best - 700, .laps = laps, .gap = 0 },
                                      { .num = 22, .best = best - 200, .laps = laps, .gap = 2100 },
                                      { .num = 4,  .best = best,       .laps = laps, .gap = 3050 },
                                      { .num = 7,  .best = best + 400, .laps = laps, .gap = 3900 },
                                      { .num = 3,  .best = best + 900, .laps = laps, .gap = 6200 },
                                      { .num = 18, .best = best + 2300, .laps = laps, .gap = 11800 } });
            };
            setNowUs(lapStart - 20000);
            positions(0.99f);
            setNowUs(lapStart);
            positions(0.0f);
            ride(197, 0.0f);
            int best = lapMs;
            lapStart += static_cast<long long>(lapMs) * 1000LL;
            setNowUs(lapStart);
            classify(1, best);
            positions(0.0f);
            host.raceLap(6, 10, 1, best - 700, /*best=*/1, best / 3, best * 2 / 3);
            host.raceLap(6, 4, 1, lapMs, /*best=*/1, lapMs / 3, lapMs * 2 / 3);
            host.drawWithState(0);
            ride(197, 14.0f);
            best = std::min(best, lapMs);
            lapStart += static_cast<long long>(lapMs) * 1000LL;
            setNowUs(lapStart);
            classify(2, best);
            positions(0.0f);
            host.raceLap(6, 4, 2, lapMs, /*best=*/lapMs <= best ? 2 : 0, lapMs / 3, lapMs * 2 / 3);
            host.raceLap(6, 22, 2, best - 200, /*best=*/2, best / 3, best * 2 / 3);
            host.drawWithState(0);
            ride(120, -10.0f);   // the clock stays put: the capture is of this moment
        }
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
            if (timedRaceLaps > 0) host.session(/*session=*/6, timedRaceLaps, /*lengthMs=*/900000);
            else                   host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
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
            host.classify(6, timedRaceLaps > 0 ? 754000 : 0, rows);   // 12:34 left
            if (timedRaceLaps > 0) {
                host.setHudVisible("records_hud", false);
                host.setHudVisible("time_widget", true);
                host.setHudVisible("session_widget", true);
                if (dashTimeMode) {
                    host.setHudVisible("timing_hud", false);
                    host.noticesSetVisible(false);
                    host.setHudVisible("dash_hud", true);
                    // From the Laps page to the Time page, which is off by default: pair
                    // with EXTRA_INI=$'[DashHud]\nnotice_takeover=0\npage=laps\npage_time=1'.
                    // The first step only clears the first-step hint over the page.
                    for (int i = 0; i < 2; ++i) {
                        host.injectHotkey("dash_next");
                        host.drawWithState(0);
                        host.drawWithState(0);
                    }
                } else {
                    host.setHudVisible("timing_hud", true);
                    host.timingReadouts(1u << 2 | 1u << 3);   // READOUT_TIME | READOUT_SESSION
                }
            }
            // Not under "dashtime": the PB they set holds the Dash on its notice.
            if (!dashTimeMode) {
                host.raceLap(6, 4, 0, 109500, /*best=*/1, /*split0=*/36200, /*split1=*/73000);
                host.raceLap(6, 4, 1, 108231, /*best=*/2, /*split0=*/35900, /*split1=*/72400);
            }
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
        if (hudScroll == "records") {
            // Twenty-four records, nine of them faster than the player's PB.
            static const char* const names[] = { "Wilhelmina", "Bartholomew", "Cassiopeia", "Aleksander",
                "Genevieve", "Maximilian", "Seraphina", "Leopold", "Ottilie", "Bertrand", "Rosalind",
                "Evander", "Clementine", "Thaddeus", "Philippa", "Barnaby", "Henrietta", "Lysander",
                "Marguerite", "Ignatius", "Josephine", "Cornelius", "Winifred", "Augustin" };
            std::string json = R"({"notice":"demo","records":[)";
            for (int i = 0; i < 24; ++i) {
                char row[160];
                snprintf(row, sizeof(row),
                         R"(%s{"player":"%s","bike":"Honda CRF450RWE","laptime":%d,"timestamp":"2024-05-01T12:00:00Z"})",
                         i ? "," : "", names[i], 105000 + i * 350 + (i >= 9 ? 300 : 0));
                json += row;
            }
            json += "]}";
            host.recordsSetFetchStub(0, json);
        } else {
            host.recordsSetFetchStub(0, R"({"notice":"demo","records":[
            {"player":"Wilhelmina","bike":"Kawasaki KX450SR","laptime":109900,"timestamp":"2024-05-01T12:34:56Z"},
            {"player":"Bartholomew","bike":"Honda CRF450RWE","laptime":110500,"timestamp":"2024-05-02T08:00:00Z"},
            {"player":"Cassiopeia","bike":"Yamaha YZ450FX","laptime":111001,"timestamp":"2024-05-03T18:30:00Z"}]})");
        }
        // The fetch gate holds a 5 s cooldown measured from a zero-initialized start
        // timestamp, and under Wine GetTickCount() counts from wineserver start -- so a
        // fetch driven immediately on a fresh prefix is silently refused.
        while (GetTickCount() < 6000) Sleep(100);
        host.recordsStartFetch();
        for (int i = 0; i < 100 && host.recordsFetchState() == 1; ++i) Sleep(50);
        fprintf(stderr, "records fetch state: %d\n", host.recordsFetchState());
    } else if (flagsMode) {
        // A three-lap race: #10 has taken the flag, so everyone else is on the last
        // lap; #3 is a lap down just ahead of the player (blue flag); #22 has crashed
        // just ahead (stationary hazard); #7 is riding backward further on (wrong way).
        host.raceEvent("Southwick", /*type=*/2);   // Race
        host.session(/*session=*/6, /*numLaps=*/3, /*lengthMs=*/0);
        host.addEntry(4, "Thomas");   // the player first: see the preview scene
        host.addEntry(10, "Wilhelmina");
        host.addEntry(22, "Bartholomew");
        host.addEntry(7, "Cassiopeia");
        host.addEntry(3, "Aleksander");
        host.addEntry(18, "Genevieve");
        host.runInit(6);
        host.setHudVisible("standings_hud", true);
        host.setHudVisible("map_hud", true);
        host.setHudVisible("radar_hud", true);
        host.eventLogSetVisible(true);
        host.classify(6, 300000, { { .num = 10, .laps = 3, .gap = 0 },
                                   { .num = 4,  .laps = 2, .gap = 9000 },
                                   { .num = 22, .laps = 2, .gap = 9500 },
                                   { .num = 7,  .laps = 2, .gap = 12000 },
                                   { .num = 18, .laps = 2, .gap = 20000 },
                                   { .num = 3,  .laps = 1, .gap = 0 } });
        host.raceLap(6, 10, 3, 108000, /*best=*/1, 36000, 72000);
        // World positions along the stadium's first straight (from the origin, +Z),
        // so the Map spreads the markers out and the Radar has riders near the player.
        auto positions = [&](float wrongWay, int crashed) {
            host.raceTrackPosition({ { .num = 4,  .trackPos = 0.500f, .posZ = 120.0f },
                                     { .num = 10, .trackPos = 0.050f, .posZ = 10.0f },
                                     { .num = 22, .trackPos = 0.530f, .crashed = crashed, .posZ = 150.0f },
                                     { .num = 7,  .trackPos = wrongWay, .posZ = 250.0f },
                                     { .num = 18, .trackPos = 0.300f, .posZ = 60.0f },
                                     { .num = 3,  .trackPos = 0.520f, .posZ = 135.0f } });
        };
        positions(0.640f, 0);
        positions(0.635f, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        positions(0.630f, 1);
    } else if (updateMode) {

        // The tag reads UpdateChecker directly, so announcing a version is the whole
        // scene -- but the PANEL still has to be opened, exactly as the default
        // branch does it. Omitting that is a blank capture, not a missing tag.
        host.updateSetAvailable("9.9.9");
        host.settleStartupMessages();   // else the menu opens on the Updates tab
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
    } else if (fmxMode) {
        // Fly a backflip and hold it in GRACE, which is where the stats strip is
        // frozen and fully populated: duration, distance across the ground,
        // height above take-off and peak rotation. The clock is driven by hand
        // (fmxSetNowUs) so the airborne debounce and the grace window play out
        // deterministically rather than at whatever rate Wine gets round to.
        host.setHudVisible("fmx_hud", true);
        host.runStart();
        long long t = 1'000'000;
        float x = 0.0f;
        auto tick = [&](TelemetryRow r) {
            t += 10'000;
            x += r.speed * 0.01f;
            r.posX = x;
            r.time = t / 1.0e6f;
            host.fmxSetNowUs(t);
            host.telemetryFrame(r);
        };
        for (int i = 0; i < 20; ++i) { TelemetryRow r; r.speed = 18.0f; r.posY = 100.0f; tick(r); }
        // 1.4s of air: ~420 degrees of pitch, rising 6.5m and coming back down.
        for (int i = 0; i < 140; ++i) {
            TelemetryRow r;
            r.speed = 18.0f; r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = -300.0f;
            const float u = i / 139.0f;
            r.posY = 100.0f + 6.5f * 4.0f * u * (1.0f - u);
            tick(r);
        }
        // Two grounded frames: the trick lands and enters GRACE, where the stats
        // strip holds. Stopping short of landingGracePeriod keeps it there.
        for (int i = 0; i < 2; ++i) { TelemetryRow r; r.speed = 18.0f; r.posY = 100.0f; tick(r); }
        // The clock stays frozen deliberately: restoring it lets grace elapse,
        // the chain bank and then time out, and the panel empties itself
        // somewhere in the hold before the shutter.
    } else if (gearMode) {
        // nothing: the HUD renders its default-visible widgets over the empty scene
        if (toastMode) host.configReloaded();   // Tinkerer - Bronze, taken on the next draw

        if (ghostToastMode) {
            long long t = 1'000'000;
            for (int i = 0; i < 36; ++i) {
                t += 100'000;
                host.statsSetNowUs(t);
                TelemetryRow r;
                r.crashed = 1; r.speed = 6.0f; r.roll = 10.0f; r.trackPos = 0.30f;
                host.telemetryFrame(r);
            }
        }
        if (rpmDemo >= 0) {
            // The limiter flash's lit phase, or its dark one with "rpmdark".
            bool dark = false;
            for (int a = 1; a < argc; ++a) if (std::string(argv[a]) == "rpmdark") dark = true;
            host.setMotionNowUs(dark ? 100000 : 0);
            TelemetryRow r;
            r.speed = 20.0f;
            r.gear = 3;
            r.rpm = rpmDemo;
            r.trackPos = 0.30f;
            host.telemetryFrame(r);
        }
        if (slideDemo) {
            // On track FIRST: the view-state change clears the telemetry.
            host.drawWithState(0);
            TelemetryRow r;
            r.speed = 15.0f;
            r.gear = 3;
            r.trackPos = 0.30f;
            const float rad = slideDeg * 3.14159265f / 180.0f;
            r.velX = 15.0f * std::sin(rad);
            r.velZ = 15.0f * std::cos(rad);
            host.telemetryFrame(r);
        }
        if (decksDemo) {
            // Every deck filled, the way the mocks show them, so a capture compares
            // against the mock deck for deck (the mock-match step: a placeholder
            // hides a difference). "decks" is the ground scene: a 22-rider race,
            // P3 on lap 4 of 10 with three laps ridden and the fourth under way,
            // a wheelie held while a slide runs. "decks air" swaps the riding
            // for a whip landed clean, for the Combo deck. The later decks ride
            // the same scene: a gate start from 7th, the gap to P2 shrinking by
            // 0.3 s a lap, two splits of lap 4 timed after the riding (the
            // sectors' trap speeds). DECKS_ONLY="speed_deck race_deck" in the
            // environment shows only those decks, for a crop per deck;
            // DECKS_TRICKS=N chains N tricks in the air scene (the chip stack);
            // DECKS_NOTICE puts a toast on the Notice deck (below).
            bool air = false;
            for (int a = 1; a < argc; ++a) {
                if (std::string(argv[a]) == "air") air = true;
            }
            host.setMasterEnabled(PluginHost::Master::Decks, true);   // off for a new install
            const char* only = std::getenv("DECKS_ONLY");
            const char* posEnv = std::getenv("DECKS_POS");
            const int myPos = posEnv ? std::clamp(std::atoi(posEnv), 1, 22) : 3;
            for (const char* deck : { "speed_deck", "race_deck", "timing_deck", "wheelie_deck",
                                      "slide_deck", "combo_deck", "gap_deck", "rival_deck",
                                      "spin_deck", "inputs_deck", "standings_deck", "session_deck",
                                      "notice_deck", "fuel_deck", "media_deck", "laplog_deck", "carousel_deck" })
                if (!only || std::strstr(only, deck)) host.setHudVisible(deck, true);
            auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
            host.eventInit("Southwick", "Thomas", 1600.0f, 2, "Test 450", "MX1", "southwick", 0, "",
                           7.0f, 10500, 12000, 13000);
            host.raceEvent("Southwick", /*type=*/2);   // Race
            host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
            // Start/finish and two splits, so the Timing deck's sectors split
            host.trackCenterline(stadium(), { 10.0f, 480.0f, 950.0f, 0.0f });
            constexpr int PLAYER = 4;
            std::vector<int> grid;
            grid.push_back(PLAYER);
            for (int n = 10; grid.size() < 22; ++n) grid.push_back(n);
            for (int n : grid) {
                char nm[16];
                snprintf(nm, sizeof(nm), n == PLAYER ? "Thomas" : n == 11 ? "Vega" : "R%d", n);
                host.addEntry(n, nm);
            }
            host.runInit(6);
            host.drawWithState(0);
            // The standings: P3, behind P2 by 1.4 s, 1.1 s, then 0.8 s at the
            // line (the Rival deck's -0.3/L), and 1.4 s ahead of P4, who
            // carries a 5 s penalty (the Standings deck's chip)
            auto classify = [&](int laps, int playerPos = -1, int state = 16, int toP2 = -1) {
                if (playerPos < 0) playerPos = myPos;
                std::vector<ClassRow> rows;
                int gap = 0;
                std::vector<int> order;
                for (int num : grid) if (num != PLAYER && num != 10 && num != 11) order.push_back(num);
                order.insert(order.begin(), { 10, 11 });
                order.insert(order.begin() + (playerPos - 1), PLAYER);
                for (size_t i = 0; i < order.size(); ++i) {
                    rows.push_back({ .num = order[i], .best = 107000 + static_cast<int>(i) * 150,
                                     .laps = laps, .gap = gap, .penalty = i == 3 ? 5000 : 0 });
                    gap += i == 1 ? (toP2 >= 0 ? toP2 : 1700 - 300 * std::max(laps, 1)) : i == 2 ? 1400 : 600;
                }
                host.classify(6, laps * 108000, rows, state);
            };
            // The start: the grid with the player 7th, the gate held, then
            // dropped at 0.5 s and the bike away 0.18 s later; 3rd by the end
            // of the opening sector (+4)
            host.runStart();
            host.raceSessionState(6, 256);
            classify(0, 7, 256);
            host.raceSessionState(6, 16);
            classify(0, 7, 0x20);
            host.setMotionNowUs(500'000);
            classify(0, 7, 16);
            host.setMotionNowUs(680'000);
            host.drawWithState(0);   // on track first: the switch clears telemetry
            {
                TelemetryRow r;
                r.speed = 4.0f;
                r.velZ = 4.0f;
                r.time = 0.68f;
                r.gear = 1;
                r.rpm = 9000;
                r.frontMaterial = r.rearMaterial = 1;
                r.posY = 100.0f;
                host.telemetryFrame(r);
                host.drawWithState(0);
            }
            // The launch (the Spin gauge): 2 s of 10 ms
            // samples, the rear spinning up to 46% and settling to 10%
            for (int ms = 690; ms <= 2600; ms += 10) {
                const float t = static_cast<float>(ms - 680) / 1000.0f;
                TelemetryRow r;
                r.speed = 4.0f + 6.0f * t;
                r.velZ = r.speed;
                r.time = static_cast<float>(ms) / 1000.0f;
                r.gear = t < 1.0f ? 1 : 2;
                r.rpm = 10500;
                r.posY = 100.0f;
                const float slip = t < 0.3f ? 0.46f * t / 0.3f : std::max(0.10f, 0.46f - 0.36f * (t - 0.3f) / 0.6f);
                r.wheelSpeedRear = r.speed * (1.0f + slip);
                host.setMotionNowUs(static_cast<int64_t>(ms) * 1000);
                host.telemetryFrame(r);
                host.drawWithState(0);
            }
            classify(0);
            // Three laps ridden on the lap clock (best 1:47.906, last 1:48.231),
            // the fourth 72.4 s in: one track sample per 0.5% of a lap, so the
            // live delta has a best lap to compare against.
            long long lapStart = 1'000'000'000LL;
            auto ride = [&](int lapMs, int to, int from = 1) {
                for (int p = from; p <= to; ++p) {
                    if (setNowUs) setNowUs(lapStart + static_cast<long long>(lapMs) * 1000LL * p / 200);
                    host.raceTrackPosition({ { PLAYER, p / 200.0f } });
                }
            };
            if (setNowUs) setNowUs(lapStart - 20000);
            host.raceTrackPosition({ { PLAYER, 0.99f } });
            if (setNowUs) setNowUs(lapStart);
            host.raceTrackPosition({ { PLAYER, 0.0f } });
            const int laps[3] = { 108950, 107906, 108231 };
            // Lap 3 with its splits and a speed through each sector: the bests
            // lap 4's sectors are set against (Timing's speed difference, Gap's
            // sector delta)
            auto lapSpeed = [&](float speed, float t0) {
                for (int i = 0; i < 20; ++i) {
                    TelemetryRow r;
                    r.speed = speed - 0.5f + 0.05f * static_cast<float>(i);
                    r.velZ = r.speed;
                    r.time = t0 + 0.005f * static_cast<float>(i);
                    host.setMotionNowUs(static_cast<int64_t>(r.time * 1.0e6f));
                    r.gear = 4;
                    r.rpm = 11000;
                    r.posY = 100.0f;
                    host.telemetryFrame(r);
                    host.drawWithState(0);
                }
            };
            for (int l = 0; l < 3; ++l) {
                if (l == 2) {
                    ride(laps[l], 66);
                    lapSpeed(24.5f, 2.70f);
                    host.raceSplit(6, PLAYER, l, 0, laps[l] / 3);
                    ride(laps[l], 133, 67);
                    lapSpeed(26.5f, 2.85f);
                    host.raceSplit(6, PLAYER, l, 1, laps[l] * 2 / 3);
                    ride(laps[l], 199, 134);
                } else {
                    ride(laps[l], 199);
                }
                lapStart += static_cast<long long>(laps[l]) * 1000LL;
                if (setNowUs) setNowUs(lapStart);
                classify(l + 1);
                host.raceTrackPosition({ { PLAYER, 0.0f } });
                host.raceLap(6, PLAYER, l, laps[l], /*best=*/l < 2 ? 1 : 0, laps[l] / 3, laps[l] * 2 / 3);
                host.drawWithState(0);
            }
            // 72.4 s into lap 4, 0.21 s up on the best at this point of the
            // lap; at the first split 1.0 s behind P2, at the second 0.8 s
            // (the Rival deck's trend)
            ride(107906 - 1400, 66);
            classify(3, -1, 16, 1000);
            long long now = 3'000'000;   // past the launch's clock
            int n = 0;
            float x = 0.0f;
            float rearSlip = 0.0f;       // the Spin deck's live share
            // One fresh 10 ms sample on the deck and FMX clocks
            auto step = [&](float speed, int gear, float pitch, float slideDeg, bool frontUp, bool rearUp,
                            float posY, float pitchVel, float yawVel, float roll, float susp) {
                TelemetryRow r;
                r.speed = speed + 0.001f * static_cast<float>(n++ % 7);
                r.gear = gear;
                r.rpm = 11200 + (n % 9) * 30;
                r.trackPos = 0.66f;
                r.frontMaterial = frontUp ? 0 : 1;
                r.rearMaterial = rearUp ? 0 : 1;
                if (rearSlip > 0.0f) r.wheelSpeedRear = r.speed * (1.0f + rearSlip);
                r.pitch = pitch;
                r.pitchVel = pitchVel;
                r.yawVel = yawVel;
                r.roll = roll;
                r.suspVelFront = susp;
                // The Inputs deck's two lines: a corner every 1.4 s, braking
                // into it, then the throttle rolled back on to full
                const float ph = std::fmod(static_cast<float>(now) / 1.0e6f, 1.4f) / 1.4f;
                r.frontBrake = ph < 0.25f ? 0.85f * std::sin(3.14159265f * ph / 0.25f) : 0.0f;
                r.throttle = ph < 0.25f ? 0.0f : ph < 0.35f ? 0.25f : std::min(1.0f, 0.3f + (ph - 0.35f) * 2.2f);
                const float rad = slideDeg * 3.14159265f / 180.0f;
                r.velX = r.speed * std::sin(rad);
                r.velZ = r.speed * std::cos(rad);
                x += r.speed * 0.01f;
                r.posZ = x;
                r.posY = posY;
                now += 10'000;
                r.time = static_cast<float>(now) / 1.0e6f;
                host.setMotionNowUs(now);
                host.fmxSetNowUs(now);
                host.telemetryFrame(r);
                host.drawWithState(0);
            };
            auto ground = [&](int frames, float speed, int gear, float slideDeg = 0.0f, float susp = 0.0f) {
                for (int i = 0; i < frames; ++i) step(speed, gear, 0.0f, slideDeg, false, false, 100.0f, 0, 0, 0, susp);
            };
            ground(30, 27.0f, 4);
            // This lap's top speed: 98 km/h
            ground(30, 27.3f, 5);
            ground(10, 22.0f, 4);
            host.raceSplit(6, PLAYER, 3, 0, 35800);
            ride(107906 - 1400, 133, 67);
            classify(3);
            if (!air) {
                // A long wheelie first (the best hold)...
                for (int i = 0; i < 1180; ++i) step(17.5f, 4, -30.0f, 0, true, false, 100.0f, 0, 0, 0, 0);
                ground(30, 17.5f, 4);
                // ...then up at 34 degrees for 4.2 s, banking three slides on the
                // way and a fourth 22 degrees left still running
                for (int i = 0; i < 420; ++i) {
                    const int k = i % 105;
                    const float slide = i >= 330 ? 22.0f : (k < 80 ? 20.0f : 0.0f);
                    step(17.5f, 4, -34.0f, slide, true, false, 100.0f, 0, 0, 0, 0);
                }
            } else {
                // A chain landed clean (the Combo deck's chips, its stats and
                // its stamp): whips and a backflip, one after the other; the
                // last chain in the stack with `decks air` past the first
                const auto whip = [&]() {
                    for (int i = 0; i < 160; ++i) {
                        const float u = static_cast<float>(i) / 159.0f;
                        step(13.0f, 4, 0.0f, 0.0f, true, true, 100.0f + 5.0f * 4.0f * u * (1.0f - u),
                             0.0f, i < 80 ? -110.0f : 110.0f, 0.0f, 0.0f);   // out left, and back
                    }
                    ground(20, 13.0f, 4);
                };
                const auto backflip = [&]() {
                    for (int i = 0; i < 120; ++i) {   // a full turn at 300 deg/s
                        const float u = static_cast<float>(i) / 119.0f;
                        step(13.0f, 4, 0.0f, 0.0f, true, true, 100.0f + 9.0f * 4.0f * u * (1.0f - u),
                             -300.0f, 0.0f, 0.0f, 0.0f);
                    }
                    ground(20, 13.0f, 4);
                };
                const char* tricksEnv = std::getenv("DECKS_TRICKS");
                const int tricks = tricksEnv ? std::clamp(std::atoi(tricksEnv), 1, 8) : 1;
                for (int k = 0; k < tricks; ++k) {
                    if (std::getenv("DECKS_FLIPS") && k % 2 == 1) backflip();
                    else whip();
                }
                // The clock stays put: the decks hold this moment for the capture
            }
            // Lap 4's second split, timed after the riding: sector 2 at 90 km/h
            rearSlip = 0.31f;   // spinning out of the corner
            ground(30, 25.0f, 4);
            host.raceSplit(6, PLAYER, 3, 1, 71559);
            host.drawWithState(0);
            host.telemetryWithStance(1, 25.0f);   // standing on the straight: the Speed deck's stance
            // The split's draw() flips the view state and back, which clears
            // the telemetry, and the stance frame carries no inputs: the
            // Inputs deck's lines and columns need their 2 s again (its own
            // capture: the Speed deck's stance does not outlive these frames)
            if (only && std::strstr(only, "inputs_deck")) ground(200, 25.0f, 4);
            // DECKS_SESSION_STATE=<flags>: the session's state changes last,
            // so the Session deck's stamp is up (512: RACE OVER)
            if (const char* st = std::getenv("DECKS_SESSION_STATE")) {
                host.raceSessionState(6, std::atoi(st));
                host.drawWithState(0);
            }
            // DECKS_FUEL=1|low: two more laps at about 0.58 L each, so the
            // Fuel deck has an average and the laps left (low: under two)
            if (const char* fu = std::getenv("DECKS_FUEL")) {
                const bool lowTank = std::string(fu) == "low";
                const float tank[4] = { 5.2f, 4.62f, 4.05f, lowTank ? 0.9f : 3.9f };
                for (int i = 0; i < 4; ++i) {
                    host.drawWithState(0);   // raceLap draws spectating: back on track first
                    TelemetryRow r;
                    r.speed = 20.0f;
                    r.gear = 3;
                    r.time = 30.0f + static_cast<float>(i);
                    r.fuel = tank[i];
                    host.telemetryFrame(r);
                    host.drawWithState(0);
                    if (i < 3) host.raceLap(6, PLAYER, 3 + i, 108000 + 300 * i);
                }
                host.drawWithState(0);
            }
            // DECKS_NOTICE=achievement|message|both: what the Notice deck
            // shows (both: the message up, the achievement waiting behind it)
            if (const char* nt = std::getenv("DECKS_NOTICE")) {
                const std::string what = nt;
                if (what == "achievement" || what == "both") host.configReloaded();   // Tinkerer
                host.drawWithState(0);
                if (what == "message" || what == "both") host.injectHotkey("standings");
                host.drawWithState(0);
                host.drawWithState(0);
            }
        }
        if (wheelieDemo >= 0) {
            // Up past the peak, then settled back at the angle: the widget smooths,
            // so each posture is held for long enough to arrive.
            for (int peak = 1; peak >= 0; --peak) {
                for (int i = 0; i < 80; ++i) {
                    TelemetryRow r;
                    r.speed = 12.0f;
                    r.gear = 2;
                    r.trackPos = 0.30f;
                    r.frontMaterial = wheelieDemo > 0 ? 0 : 1;
                    r.pitch = -static_cast<float>(wheelieDemo + (peak && wheelieDemo > 0 ? 15 : 0));
                    host.telemetryFrame(r);
                    host.drawWithState(0);   // on track: the telemetry is the player's
                }
            }
        }
        if (stanceDemo >= 0) {
            host.drawWithState(0);   // on track first: the view-state change clears the telemetry
            host.telemetryWithStance(stanceDemo, 20.0f);
        }
    } else {
        // Default (or a specific tab named on the command line, e.g. "Timing").
        const char* tab = "General";
        int notches = 0;     // "scroll N": the tab's list moved N steps down
        int glideMs = -1;    // "glide MS": ...with Motion on, photographed MS into the glide
        for (int a = 1; a < argc; ++a) {
            std::string s(argv[a]);
            if (s == "tab" && a + 1 < argc) tab = argv[a + 1];
            if (s == "scroll" && a + 1 < argc) notches = std::atoi(argv[a + 1]);
            if (s == "glide" && a + 1 < argc) glideMs = std::atoi(argv[a + 1]);
            // "notes": a release out, with long notes, for the Updates tab.
            if (s == "notes") host.updateSetAvailable("9.9.9");
        }
        host.showSettings(true);
        // "tab About": the About page is not in the tab list (see
        // settings_tab_about.cpp); it opens from the footer button, which is
        // what the hook behind clickAbout presses.
        if (std::string(tab) == "About") host.clickAbout();
        else host.setActiveTab(tab);
        // "find <query>": the settings Find page on that query (as if typed + Enter).
        for (int a = 1; a + 1 < argc; ++a) {
            if (std::string(argv[a]) == "find") host.settingsFind(argv[a + 1]);
        }
        // Move the first list on the tab that scrolls, before the
        // surface is pinned below. With "glide", Motion is on and its clock is
        // held MS into the move, so the capture shows the rows crossing the edges.
        if (notches != 0) {
            host.draw();
            for (int id = 0; id < 6; ++id) {   // ScrollView::Id, to COUNT
                const PluginHost::ScrollView v = host.settingsScroll(id);
                if (!v.laidOut || v.maxRest <= 0.0f) continue;
                const long long t0 = 1000000000ll;
                if (glideMs >= 0) { host.setMotion(2); host.setMotionNowUs(t0); host.draw(); }
                host.stepScroll(id, notches);
                if (glideMs >= 0) host.setMotionNowUs(t0 + glideMs * 1000ll);
                break;
            }
        }
        // "hover <tooltip id>": leave the mouse stand-in over that row, so the
        // capture shows its hover band and tooltip.
        const char* hover = nullptr;
        for (int a = 1; a + 1 < argc; ++a) {
            if (std::string(argv[a]) == "hover") hover = argv[a + 1];
        }
        float hx = 0.0f, hy = 0.0f;
        if (hover) host.draw();   // the regions exist once the tab has drawn
        if (hover && host.settingsRegionCenter(hover, &hx, &hy)) {
            host.injectMouse(true, hx, hy, 0);
        } else {
            host.injectMouse(false);
        }
        // "open <tooltip id>": click that row's dropdown box, so the capture
        // shows its list open (the mouse is moved off it afterwards).
        for (int a = 1; a + 1 < argc; ++a) {
            if (std::string(argv[a]) != "open") continue;
            float dx = 0.0f, dy = 0.0f, dw = 0.0f;
            host.draw();
            if (host.settingsDropdownSpan(argv[a + 1], &dx, &dy, &dw)) {
                host.clickAt(dx + dw * 0.5f, dy);
                host.injectMouse(true, 0.01f, 0.99f, 0);
            }
        }
    }
    // Pin the companion as the active surface so surface-scoped chrome (the settings
    // menu, the pointer) renders on the companion window we screenshot — it never
    // takes focus, so it's never the foreground-derived active surface under Wine.
    host.forceActiveSurface(1);
    const int drawState = (dashMode || dashTimeMode || slideDemo || wheelieDemo >= 0 || shiftDemo >= 0 || stanceDemo >= 0 ||
                           decksDemo) ? 0 : 1;   // ON_TRACK: Dash Fuel rows, Slide, Wheelie, Stance, the decks
    host.drawWithState(drawState);

    // "toggles N": the mouse stand-in over the Toggles strip's row N, so the capture
    // shows the strip and that row's hover name ("toggles -1": awake, mouse off it).
    for (int a = 1; a + 1 < argc; ++a) {
        if (std::string(argv[a]) != "toggles") continue;
        host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
        host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
        float tx = 0.5f, ty = 0.5f;
        host.toggleStripRow(std::atoi(argv[a + 1]), &tx, &ty);
        host.injectMouse(true, tx, ty, 0); host.draw(); host.draw();
    }

    if (!hudScroll.empty()) {
        const char* name = hudScroll == "eventlog" ? "event_log_hud" : hudScroll == "laplog" ? "lap_log_hud"
                         : hudScroll == "chat" ? "stream_chat_hud" : hudScroll == "records" ? "records_hud"
                         : "friends_hud";
        int laps = 2, lines = 0;
        const auto arrive = [&] {   // one more entry at a live list's edge
            if (hudScroll == "eventlog") {
                for (int i = 0; i < 2; ++i) { host.directorToggleLock(); host.draw(); }
            } else if (hudScroll == "laplog") {
                host.raceLap(1, 4, laps, 108900 + (laps * 731) % 1900, 0, 36100, 72600);
                ++laps;
            } else if (hudScroll == "chat") {
                static const char* const said[] = { "nice line through the whoops", "that holeshot though",
                    "send it!", "gg", "who is on the 450?", "rhythm section looking clean",
                    "LETS GOOO", "watch the berm on turn 3", "how many laps left?", "clean pass",
                    "o7", "that landing was smooth", "hi from Sweden", "first time watching, love it",
                    "big whip!", "track looks so rutted" };
                static const char* const who[] = { "dirtdiver_88", "holeshot_hannah", "rutmaster", "berm_buster" };
                char raw[320];
                snprintf(raw, sizeof(raw), "@badges=;color=;display-name=%s;id=m%d;tmi-sent-ts=1700000000000 "
                         ":%s!%s@%s.tmi.twitch.tv PRIVMSG #chan :%s", who[lines % 4], lines, who[lines % 4],
                         who[lines % 4], who[lines % 4], said[lines % 16]);
                host.twitchInject(raw);
                ++lines;
            }
            host.draw();
        };
        host.setEveryHudVisible(false);
        host.setHudVisible(name, true);
        host.setHudOffset(name, 0.0f, 0.0f);
        // Driven on the game surface, where the injected pointer is in HUD space;
        // the capture is pinned back to the companion afterwards.
        host.forceActiveSurface(0);
        if (hudScroll == "eventlog") for (int i = 0; i < 12; ++i) { host.directorToggleLock(); host.draw(); }
        if (hudScroll == "laplog") for (int i = 0; i < 16; ++i) arrive();
        if (hudScroll == "chat") { host.twitchSimulateConnected(); for (int i = 0; i < 24; ++i) arrive(); }
        if (hudScroll == "friends") {
            if (auto inject = host.sym<void (*)(const char*, const char*, const char*, const char*, const char*,
                                                const char*, int)>("MXBMRP3_Test_FriendsInject")) {
                static const char* const pals[] = { "Wilhelmina", "dirtdiver_88", "holeshot_hannah", "rutmaster",
                    "berm_buster", "Genevieve", "Leopold", "whoops_king", "Seraphina", "scrub_lord",
                    "Barnaby", "Clementine", "roost_rider", "Ottilie" };
                for (int i = 0; i < 14; ++i) {
                    const bool racing = i % 3 != 2;
                    inject(pals[i], racing ? "MXB Sunday Club" : "", racing ? "Southwick" : "",
                           racing ? "Race 1" : "", racing ? "In Progress" : "", racing ? "6 TO GO" : "", 0);
                }
            }
        }
        for (int i = 0; i < 4; ++i) host.draw();
        PluginHost::QuadRect card, panel;
        if (host.hudCardRect(name, &card, &panel)) {
            const float cx = static_cast<float>(card.l + card.r) * 0.5f;
            const float cy = static_cast<float>(card.t + card.b) * 0.5f;
            host.injectMouse(true, cx, cy, 0);
            host.draw(); host.draw();
            // The bar's track: the tallest narrow quad in the panel's right
            // padding that stays inside the card's height (a theme's frame
            // slice there runs the panel's whole height).
            PluginHost::QuadRect track{};
            for (const PluginHost::QuadRect& q : host.hudQuadRects(name)) {
                if (q.l > card.r - 0.003 && q.r <= panel.r + 0.001 && q.r - q.l < 0.02 &&
                    q.t > card.t + 0.002 && q.b < card.b - 0.002 && q.b - q.t > track.b - track.t) track = q;
            }
            if (track.b > track.t) {
                const float bx = static_cast<float>(track.l + track.r) * 0.5f;
                // A static list moves down from home; a live one away from its newest end.
                const bool down = hudScroll == "records" || hudScroll == "friends";
                const float by = static_cast<float>(down ? track.b + 0.006 : track.t - 0.006);
                const auto press = [&](float y) {
                    host.injectMouse(true, bx, y, 0); host.draw();
                    host.injectMouse(true, bx, y, 1); host.draw();
                    host.injectMouse(true, bx, y, 0); host.draw();
                };
                long long glideMs = -1;
                for (int a = 1; a + 1 < argc; ++a)
                    if (std::string(argv[a]) == "glide") glideMs = std::atoll(argv[a + 1]);
                const long long t0 = 1000000000ll;
                if (glideMs >= 0) {
                    host.setMotion(2);
                    host.setMotionNowUs(t0);
                    host.draw();
                    press(by);
                    host.setMotionNowUs(t0 + glideMs * 1000ll);
                } else {
                    const auto before = host.hudStringRows(name);
                    press(by);   // one row: the Event Log's texts alternate, so two would look still
                    // Newest first: the top is the live edge, so move down instead.
                    const auto after = host.hudStringRows(name);
                    bool same = before.size() == after.size();
                    for (size_t i = 0; same && i < before.size(); ++i) same = before[i].text == after[i].text;
                    if (same && !down) press(static_cast<float>(track.b + 0.006));
                    press(same && !down ? static_cast<float>(track.b + 0.006) : by);
                    if (!down) arrive();
                }
            } else {
                fprintf(stderr, "hudscroll: no scrollbar on %s (list fits?)\n", name);
            }
            host.injectMouse(true, cx, cy, 0);
            host.draw(); host.draw();
        }
        host.forceActiveSurface(1);
        host.draw(); host.draw();
    }

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
    // Motion: settle with the menu closed (or on General), then open it (or switch
    // tab) once the window is up and step the clock to motionMs, at most a frame's
    // worth per draw, and hold it there.
    long long motionNow = 1000000000ll, motionEnd = 0;
    int motionHeld = 0;
    if (motionMs >= 0) {
        host.setMotion(2);
        host.setMotionNowUs(motionNow);
        if (motionHud) host.setHudVisibleBoth(motionHud, false);
        else if (motionTab) host.setActiveTab("General");
        else host.showSettings(false);
    }
    // "shift MS": shift once the window is up, then hold the clock MS in.
    if (shiftDemo >= 0 || deckStepMs >= 0) {
        host.setMotion(2);
        host.setMotionNowUs(motionNow);
    }
    for (int i = 0; i < seconds * 50; ++i) {
        if (shiftDemo >= 0 && i == 20) host.telemetry(10.0f, 3);   // moving: the gear shows (standing: "-")
        if (shiftDemo >= 0 && i == 25) host.telemetry(10.0f, 4);
        if (shiftDemo >= 0 && i == 27) host.setMotionNowUs(motionNow + shiftDemo * 1000ll);
        if (deckStepMs >= 0 && i == 25) host.injectHotkey("deck_next");
        if (deckStepMs >= 0 && i == 27) host.setMotionNowUs(motionNow + deckStepMs * 1000ll);
        if (motionMs >= 0 && i == 25) {
            if (motionHud) host.setHudVisibleBoth(motionHud, true);
            else if (motionTab) host.setActiveTab("Appearance");
            else { host.showSettings(true); host.setActiveTab("Appearance"); }
            motionEnd = motionNow + motionMs * 1000ll;
        }
        if (motionEnd > motionNow) {
            motionNow = std::min(motionEnd, motionNow + 16000ll);
            host.setMotionNowUs(motionNow);
        }
        host.drawWithState(drawState);
        // The frame to photograph is on screen once the clock has reached its
        // target and the companion has drawn it: tell companion_demo.sh, whose
        // poll would otherwise take the first frame with any content - from before
        // the change it is meant to show.
        if ((shiftDemo >= 0 || deckStepMs >= 0) && i == 37) {
            if (FILE* f = fopen("motion.ready", "w")) fclose(f);
        }
        if (motionEnd > 0 && motionNow == motionEnd && ++motionHeld == 10) {
            if (FILE* f = fopen("motion.ready", "w")) fclose(f);
        }
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
