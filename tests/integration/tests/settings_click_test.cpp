// ============================================================================
// tests/integration/tests/settings_click_test.cpp
// The settings-menu CLICK path, headless. Until now the converted stepped
// controls (SteppedControl descriptors, commits 9c9929c/d276b3e + the closing
// pass) were verified in-game only — the harness can't synthesize OS mouse
// input. The MXBMRP3_Test_SettingsClickStepped seam routes a click through the
// REAL path (handleClick: hit-test over m_clickRegions -> dispatchRegion ->
// applySteppedControl) at a built region's center, with the hold-repeat
// counter forced so the acceleration tiers are drivable.
//
// Invariants pinned (each is a distinct descriptor behavior):
//  1. clampInt + acceleration: GapBar Width steps 1% per click, x10 when held
//     past the second accel tier, and CLAMPS at the 400% max (no wrap).
//  2. The modernized GapBar Range (user-approved UX change): 250ms steps over
//     the unchanged 1000-5000ms bounds, accelerated, clamped.
//  3. FIXED_INT: the Records count steps exactly +/-1 per click EVEN WHEN the
//     hold counter says x10 — the non-accelerated kind must ignore the tier.
//  4. Values changed by clicks persist through the real save path (the INI is
//     the black-box output, same idiom as the persist/defer tests).
//
// GapBar stepped layout order (settings_tab_gap_bar.cpp registration order):
//   0 = Width, 1 = Range, 2 = Freeze, 3 = Marker scale.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\settings_click\\";
static const char* INI  =
    "Z:\\tmp\\mxbmrp3-tests\\settings_click\\mxbmrp3\\mxbmrp3_settings.ini";

// Read "key=value" for a HUD section, honoring the sparse-save layout: a value
// the user changed lives in the ACTIVE profile's diff section ("[Sec:Practice]"
// here — tests start on the Practice profile), while an untouched value exists
// only in the base "[Sec]" section. Profile wins when present. -1 when absent.
static int iniIntOne(const char* path, const std::string& header, const std::string& key) {
    std::ifstream f(path);
    std::string line;
    bool in = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') {
            in = (line == header);
            continue;
        }
        if (in && line.rfind(key + "=", 0) == 0) {
            return std::stoi(line.substr(key.size() + 1));
        }
    }
    return -1;
}
static int iniInt(const char* path, const std::string& section, const std::string& key) {
    int v = iniIntOne(path, "[" + section + ":Practice]", key);
    return (v >= 0) ? v : iniIntOne(path, "[" + section + "]", key);
}

// The same lookup for a key whose value is a NAME rather than a number. Empty
// when absent. (iniIntOne std::stoi's the value, so it cannot read these -- it
// throws.)
static std::string iniStrOne(const char* path, const std::string& header, const std::string& key) {
    std::ifstream f(path);
    std::string line;
    bool in = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') {
            in = (line == header);
            continue;
        }
        if (in && line.rfind(key + "=", 0) == 0) {
            return line.substr(key.size() + 1);
        }
    }
    return std::string();
}
static std::string iniStr(const char* path, const std::string& section, const std::string& key) {
    std::string v = iniStrOne(path, "[" + section + ":Practice]", key);
    return !v.empty() ? v : iniStrOne(path, "[" + section + "]", key);
}

// labelMode's ordinal, read back from its NAME. The gap bar used to write this key
// as a bare int and now writes the name Map and Radar always wrote (one enum, one
// spelling -- see stringToLabelMode); the cycle assertions below are about stepping
// and wrapping, so they keep their modular arithmetic and this does the translation.
// -1 for an unknown or absent value, which fails the REQUIRE below rather than
// quietly reading as NONE.
// Every HUD tab opens with the standard Opacity and Scale sliders, which are
// stepped controls too, so a tab's own steppers start at this index.
static const int kStd = 2;

// The Gap Bar's Reference cycle position from the saved key: Default (written
// as HudDefaults::REFERENCE_FOLLOW, 3) is state 0, then Session PB, All-time
// and Last lap.
static int iniRefState(const char* path, const std::string& section) {
    const int v = iniInt(path, section, "reference");
    return v == 3 ? 0 : v + 1;
}

static int iniLabelMode(const char* path, const std::string& section) {
    const std::string v = iniStr(path, section, "labelMode");
    if (v == "NONE") return 0;
    if (v == "POSITION") return 1;
    if (v == "RACE_NUM") return 2;
    if (v == "BOTH") return 3;
    return -1;
}

TEST_CASE("settings clicks: stepped controls step, accelerate, clamp, and persist") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);

    // Open the menu on the Gap Bar tab; a draw builds the click regions.
    host.showSettings(true);
    host.setActiveTab("Gap Bar");
    host.draw();

    // Six stepped controls on this tab, up and down arrows for each: the
    // standard Opacity and Scale lead (kStd), then Width, Range, Freeze and
    // Marker scale.
    REQUIRE(host.steppedCount(true) == kStd + 4);
    REQUIRE(host.steppedCount(false) == kStd + 4);

    // Baseline from the real save path.
    host.save();
    const int width0 = iniInt(INI, "GapBarHud", "barWidth");
    const int range0 = iniInt(INI, "GapBarHud", "gapRange");
    REQUIRE(width0 >= 50);
    REQUIRE(range0 == 750);   // GapBarHud::RANGE_AUTO, the default

    // --- 1. Width (index kStd, clampInt, 1% step, accelerated) -----------------
    REQUIRE(host.clickStepped(kStd, /*up=*/true));                    // +1
    REQUIRE(host.clickStepped(kStd, /*up=*/true, /*holdRepeats=*/16)); // +10 (x10 tier)
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "barWidth") == width0 + 11);

    // Clamp at the 400% max: hammer accelerated ups, then verify the ceiling
    // holds and a single down steps back off it.
    for (int i = 0; i < 60; ++i) host.clickStepped(kStd, true, 16);
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "barWidth") == 400);
    REQUIRE(host.clickStepped(kStd, /*up=*/false));
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "barWidth") == 399);

    // --- 2. Range (index kStd + 1): the modernized 250ms accelerated stepper -------
    // Auto is the step below 1s: down from it clamps, up from it is 1s.
    host.clickStepped(kStd + 1, /*up=*/false);
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "gapRange") == range0);         // clamped at Auto
    REQUIRE(host.clickStepped(kStd + 1, /*up=*/true));                    // +250
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "gapRange") == 1000);
    for (int i = 0; i < 20; ++i) host.clickStepped(kStd + 1, true, 16);   // accelerate to the cap
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "gapRange") == 5000);           // clamped, not wrapped
    REQUIRE(host.clickStepped(kStd + 1, /*up=*/false));
    host.save();
    CHECK(iniInt(INI, "GapBarHud", "gapRange") == 4750);

    // --- 3. Records count (FIXED_INT): +/-1 per click, NEVER accelerates ----
    host.setActiveTab("Records");
    host.draw();
    REQUIRE(host.steppedCount(true) == kStd + 1);
    host.save();
    const int recs0 = iniInt(INI, "RecordsHud", "recordsToShow");
    REQUIRE(recs0 >= 3);
    REQUIRE(host.clickStepped(kStd, /*up=*/true, /*holdRepeats=*/16)); // held: still +1
    REQUIRE(host.clickStepped(kStd, /*up=*/true));                     // +1
    host.save();
    CHECK(iniInt(INI, "RecordsHud", "recordsToShow") == recs0 + 2);

    // Out-of-range index is a clean miss, not a crash.
    CHECK_FALSE(host.clickStepped(99, true));

    host.shutdown();
}

// ============================================================================
// The shared CYCLE controls (CycleControl descriptors): plain mod-N enum/mode
// cycles converted from dedicated enum pairs. Pinned through the real click
// path on the Gap Bar tab, which carries six of them in layout order:
//   cycle 0 = Reference (N=4, Default first), 1 = Splits (N=2),
//   2 = Mode (marker mode, N=4), 3 = Marker colors (N=3), 4 = Marker icon,
//   5 = Marker labels (N=4). Wrap is asserted in BOTH directions,
// and persistence goes through the real save path like the stepped test above.
// ============================================================================
// (Reuses the runner-created save dir - the runner pre-creates only
// SAVE_ROOT/<test-name>, and the assertions below are relative to the freshly
// read baseline, so earlier cases' leftovers don't matter.)
static const char* SAVE_CYCLE = SAVE;
static const char* INI_CYCLE  = INI;

TEST_CASE("settings clicks: cycle controls wrap both directions and persist") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_CYCLE);

    // Select the tab BEFORE opening the menu: show() rebuilds the click
    // regions for the active tab, while a bare setActiveTab after show() does
    // not - the INI restored by startup() (written by the previous case) would
    // otherwise leave the previous session's tab regions in place.
    host.setActiveTab("Gap Bar");
    host.showSettings(true);
    host.draw();

    // Six cycle controls on this tab, arrows both sides: Reference, Splits,
    // Mode, Marker colors, Marker icon, Marker labels.
    REQUIRE(host.cycleCount(true) == 6);
    REQUIRE(host.cycleCount(false) == 6);

    host.save();
    const int ref0    = iniRefState(INI_CYCLE, "GapBarHud");
    const int marker0 = iniInt(INI_CYCLE, "GapBarHud", "markerMode");
    const int label0  = iniLabelMode(INI_CYCLE, "GapBarHud");
    REQUIRE(ref0 >= 0);
    REQUIRE(marker0 >= 0);
    REQUIRE(label0 >= 0);

    // --- Marker labels (index 5, N=4): full forward wrap ---------------------
    REQUIRE(host.clickCycle(5, /*up=*/true));
    host.save();
    CHECK(iniLabelMode(INI_CYCLE, "GapBarHud") == (label0 + 1) % 4);
    for (int i = 0; i < 3; ++i) REQUIRE(host.clickCycle(5, true));
    host.save();
    CHECK(iniLabelMode(INI_CYCLE, "GapBarHud") == label0);   // wrapped home

    // Backward from the base value wraps through the top end.
    REQUIRE(host.clickCycle(5, /*up=*/false));
    host.save();
    CHECK(iniLabelMode(INI_CYCLE, "GapBarHud") == (label0 + 3) % 4);
    REQUIRE(host.clickCycle(5, /*up=*/true));                       // back home
    host.save();
    CHECK(iniLabelMode(INI_CYCLE, "GapBarHud") == label0);

    // --- Mode (index 2, N=4): one step each way is symmetric -----------------
    REQUIRE(host.clickCycle(2, true));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "markerMode") == (marker0 + 1) % 4);
    REQUIRE(host.clickCycle(2, false));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "markerMode") == marker0);

    // --- Splits (index 1, N=2): On <-> Off, either direction ----------------
    const int splits0 = iniInt(INI_CYCLE, "GapBarHud", "showSplits");
    REQUIRE(splits0 >= 0);
    REQUIRE(host.clickCycle(1, true));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "showSplits") == 1 - splits0);
    REQUIRE(host.clickCycle(1, false));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "showSplits") == splits0);

    // --- Reference (index 0, N=4): Default / Session PB / All-time / Last lap
    REQUIRE(host.clickCycle(0, true));
    host.save();
    CHECK(iniRefState(INI_CYCLE, "GapBarHud") == (ref0 + 1) % 4);
    for (int i = 0; i < 3; ++i) REQUIRE(host.clickCycle(0, true));
    host.save();
    CHECK(iniRefState(INI_CYCLE, "GapBarHud") == ref0);   // wrapped home
    REQUIRE(host.clickCycle(0, false));
    host.save();
    CHECK(iniRefState(INI_CYCLE, "GapBarHud") == (ref0 + 3) % 4);
    REQUIRE(host.clickCycle(0, true));
    host.save();
    CHECK(iniRefState(INI_CYCLE, "GapBarHud") == ref0);

    // Out-of-range index is a clean miss, not a crash.
    CHECK_FALSE(host.clickCycle(99, true));

    host.shutdown();
}

// ============================================================================
// Rumble stepper profile-binding guard (SteppedControl::valid): the Rumble
// tab's stepped descriptors bind raw pointers into the ACTIVE rumble config at
// layout time. In per-bike mode a bike swap while the menu is open makes that
// binding stale - a click through the old layout used to land on the PREVIOUS
// bike's profile. The guard swallows such a click (neither profile changes)
// and dirties the layout so the next frame rebuilds against the new profile.
// ============================================================================
static const char* SAVE_RUMBLE = SAVE;   // shared runner-created save dir

TEST_CASE("rumble steppers: bike swap under an open menu can't edit the stale profile") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_RUMBLE);

    host.rumbleSetPerBike(true);
    host.eventInit("Guard Track", "Guard Rider", 1600.0f, 2, "Bike A");

    // Tab before show(), so the menu opens with the Rumble regions built
    // (see the cycle case above).
    host.setActiveTab("Rumble");
    host.showSettings(true);
    host.draw();                                  // layout binds bike A's profile
    REQUIRE(host.steppedCount(true) >= kStd + 4); // 4 steppers per effect row

    // Stepper kStd = Bumps Light (percentFloat, default Off = 0.00), after the
    // standard Opacity and Scale.
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.0f));
    REQUIRE(host.clickStepped(kStd, /*up=*/true));   // +1% edits bike A's profile
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.01f));

    // Swap to bike B WITHOUT redrawing: the menu still shows bike A's layout.
    host.eventInit("Guard Track", "Guard Rider", 1600.0f, 2, "Bike B");
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.0f));  // B at default

    // A click on the stale layout must be swallowed: bike B stays untouched...
    REQUIRE(host.clickStepped(kStd, /*up=*/true));
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.0f));
    // ...and bike A keeps exactly its one deliberate edit (no stray +1%).
    host.eventInit("Guard Track", "Guard Rider", 1600.0f, 2, "Bike A");
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.01f));

    // After a redraw the layout rebinds to the active profile and edits it.
    host.eventInit("Guard Track", "Guard Rider", 1600.0f, 2, "Bike B");
    host.draw();                                  // swallowed click dirtied the layout
    REQUIRE(host.clickStepped(kStd, /*up=*/true));
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.01f)); // edits B now

    // Bike A's tune survives the whole exchange.
    host.eventInit("Guard Track", "Guard Rider", 1600.0f, 2, "Bike A");
    CHECK(host.rumbleActiveBumpsLight() == doctest::Approx(0.01f));

    host.shutdown();
}

// ============================================================================
// Gap Bar marker mode OFF draws no markers. The bar is a flat map of the lap, so
// the rider's OWN marker slides along it; a player asked for a way to stop
// "something sliding across the top of the screen", and Off is that: the marker
// row gone, the bar and the gap text kept. Asserted on the rendered quads: with a
// track position the self marker is one icon quad in Ghost mode, and Off must
// draw exactly that one fewer. Reached the way the player reaches it, by cycling
// the Mode control, and the value 3 round-trips through the file.
// ============================================================================
TEST_CASE("settings clicks: Gap Bar marker mode Off removes the marker row from the render") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_CYCLE);
    REQUIRE(host.hasQuadRects());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.30f } });      // the self marker has somewhere to be
    REQUIRE(host.setHudVisible("gap_bar_hud", true));

    host.setActiveTab("Gap Bar");
    host.showSettings(true);
    host.draw();
    // Start from Ghost (0): no reference lap yet, so the only marker is the rider's own.
    host.save();
    int mode = iniInt(INI_CYCLE, "GapBarHud", "markerMode");
    REQUIRE(mode >= 0);
    while (mode != 0) { REQUIRE(host.clickCycle(2, true)); host.save(); mode = iniInt(INI_CYCLE, "GapBarHud", "markerMode"); }
    host.draw();
    const size_t ghostQuads = host.hudQuadRects("gap_bar_hud").size();
    REQUIRE(ghostQuads > 0);

    // Ghost -> Opponents -> Both -> Off (Mode is cycle 2; Reference and Splits sit before it).
    for (int i = 0; i < 3; ++i) REQUIRE(host.clickCycle(2, true));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "markerMode") == 3);
    host.draw();
    const size_t offQuads = host.hudQuadRects("gap_bar_hud").size();
    CHECK_MESSAGE(offQuads + 1 == ghostQuads,
                  "Off should draw exactly the self marker fewer: " << ghostQuads << " quads in Ghost, " << offQuads << " in Off");

    // One more step wraps back to Ghost, and the marker returns.
    REQUIRE(host.clickCycle(2, true));
    host.save();
    CHECK(iniInt(INI_CYCLE, "GapBarHud", "markerMode") == 0);
    host.draw();
    CHECK(host.hudQuadRects("gap_bar_hud").size() == ghostQuads);
}

// ============================================================================
// Every "hold the time after a split or the line" setting shares one default
// and range (hud/freeze_duration.h): Timing's Freeze, the Gap Bar's Freeze and
// the Lap Log's Gap freeze, 5 s. They had drifted to 5 s / 3 s / 3 s. Those
// three now follow the General tab's Freeze default out of the box (written as
// FreezeDuration::FOLLOW_DEFAULT, -1000), which holds that 5 s. The
// Pitboard's At Splits Freeze (a fixed 10 s until it became a setting) keeps
// 10 s and has no Off (a zero hold would hide the board for good): it wraps
// from 10 s to 1 s.
// ============================================================================
TEST_CASE("freeze settings share one default, and the Pitboard's wraps 1-10 s and persists") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);

    host.save();
    CHECK(iniInt(INI, "General",     "defaultFreeze")   == 5000);
    CHECK(iniInt(INI, "TimingHud",   "displayDuration") == -1000);
    CHECK(iniInt(INI, "GapBarHud",   "freezeDuration")  == -1000);
    CHECK(iniInt(INI, "LapLogHud",   "freezeDuration")  == -1000);
    CHECK(iniInt(INI, "PitboardHud", "freezeDuration")  == 10000);

    // Pitboard tab: Freeze is its one stepped control after the standard
    // Opacity and Scale (default mode At Splits, so it is live).
    host.showSettings(true);
    host.setActiveTab("Pitboard");
    host.draw();
    REQUIRE(host.steppedCount(true) == kStd + 1);

    // Past 10 s it wraps to 1 s, never to 0, and back down to 10 s.
    REQUIRE(host.clickStepped(kStd, /*up=*/true));
    host.save();
    CHECK(iniInt(INI, "PitboardHud", "freezeDuration") == 1000);
    REQUIRE(host.clickStepped(kStd, /*up=*/true));
    host.save();
    CHECK(iniInt(INI, "PitboardHud", "freezeDuration") == 2000);
    for (int i = 0; i < 2; ++i) REQUIRE(host.clickStepped(kStd, /*up=*/false));
    host.save();
    CHECK(iniInt(INI, "PitboardHud", "freezeDuration") == 10000);

    host.shutdown();
}

// ============================================================================
// Rumble's split caret (Bumps, Lockup) sits in its row's gutter, and hovering it
// must still read as hovering the row: the row's description shows. Its click
// region used to be pushed BEFORE the row's tooltip region, and hover takes the
// first match, so the caret swallowed the hover and the description reset to the
// tab's. A click on it must still split the row (clicks skip tooltip regions).
// ============================================================================
TEST_CASE("rumble split caret: hovering it shows the row's description, clicking it splits") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_RUMBLE);
    REQUIRE(host.hasInjectedMouse());
    REQUIRE(host.hasStringRows());

    host.setActiveTab("Rumble");
    host.showSettings(true);
    host.draw();

    auto strings = [&]() {
        std::vector<std::string> out;
        for (const auto& r : host.hudStringRows("settings_hud")) out.push_back(r.text);
        return out;
    };
    auto shows = [&](const char* needle) {
        for (const auto& t : strings()) if (t.find(needle) != std::string::npos) return true;
        return false;
    };

    // The gutter starts at the table's left edge, where the "Effects" heading is
    // drawn; the row's centre line comes from its own tooltip region.
    double gutterX = -1.0;
    for (const auto& r : host.hudStringRows("settings_hud")) {
        if (r.text == "Effects") { gutterX = r.x; break; }
    }
    REQUIRE(gutterX >= 0.0);
    float rowCx = 0.0f, rowCy = 0.0f;
    REQUIRE(host.settingsRegionCenter("rumble.bumps", &rowCx, &rowCy));
    const float caretX = static_cast<float>(gutterX) + 0.004f;

    REQUIRE_FALSE(shows("suspension travel"));
    host.injectMouse(true, caretX, rowCy, 0);
    host.draw();
    host.draw();
    CHECK(shows("suspension travel"));

    REQUIRE_FALSE(shows("- Front"));
    host.clickAt(caretX, rowCy);
    host.draw();
    CHECK(shows("- Front"));

    host.injectMouse(false);
    host.shutdown();
}
