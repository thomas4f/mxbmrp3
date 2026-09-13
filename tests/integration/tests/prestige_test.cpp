// ============================================================================
// tests/integration/tests/prestige_test.cpp
// THE TRADE, end to end: the button's gate, what it takes, what it keeps, and
// what it hands back.
//
// Prestige is the one act in the plugin that destroys progress on purpose, so
// every clause of the promise it makes is pinned here rather than trusted:
//
//   1. IT IS REFUSED until the Platinum Sweep is earned. The button is only
//      drawn then, but the act re-checks -- a UI condition is not a rule.
//   2. DEVELOPER MODE is the second key, and it opens the widget as well as
//      the act, so a build can be laid out and its art checked without a
//      hundred Platinums behind it.
//   3. IT TAKES the achievement tiers and every lifetime counter they read.
//   4. IT KEEPS the personal bests -- which is the whole difference between
//      this and "reset my stats", and the one clause a stray floor in the
//      load path had silently undone (see the pbCount note in
//      stats_manager_persistence.cpp): PBs kept, pbCount 0, and Personal Best
//      must NOT come back on the next load.
//   5. THE LEVEL SURVIVES everything, including the load that follows.
//   6. THE BADGE is invisible until there is a level, and drawable after --
//      the widget draws nothing at all while locked, which is also what keeps
//      it off the Widgets tab.
//
// Plus the other half of this change: a per-track / per-bike maximum row names
// the track or bike CARRYING it, by display name where one has been learned.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile
#include "nlohmann/json.hpp"

#include <cstdio>
#include <string>
#include <vector>

static constexpr int RACE1 = 6;

static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\prestige\\";
static const std::string kStatsPath =
    "Z:\\tmp\\mxbmrp3-tests\\prestige\\mxbmrp3\\mxbmrp3_stats.json";
static const std::string kIniPath =
    "Z:\\tmp\\mxbmrp3-tests\\prestige\\mxbmrp3\\mxbmrp3_settings.ini";

static void cleanSaveDir() {
    std::remove(kStatsPath.c_str());
    std::remove(kIniPath.c_str());
}

namespace {

nlohmann::json readStats() {
    const std::string txt = ini::readFile(kStatsPath);
    if (txt.empty()) return nlohmann::json();
    return nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
}

// A finished race at one track on one bike, with `laps` valid laps. trackId and
// trackName are separate on purpose: the records are keyed by ID and the name is
// what a row shows, and a test that passed the same string for both could not
// tell which one it was reading back.
struct Ride {
    const char* trackId = "sw";
    const char* trackName = "Southwick";
    const char* bike = "Test 450";
    int laps = 3;
    int lapMs = 90000;
    int odoTicks = 20;   // 100 ms apart at 30 m/s -> ~3 m each; 0 = a parked bike
};

// The odometer integrates over the WALL-CLOCK gap between telemetry ticks, so
// it has to be stepped through the injectable clock the same way odometer_test
// does -- a run that never moves leaves every bike on zero, and the "one bike"
// row would have nobody to name.
long long g_clockUs = 0;

void ride(PluginHost& host, const Ride& r) {
    host.eventInit(r.trackName, "Alice", 1600.0f, 2, r.bike, "MX1", /*trackId=*/r.trackId);
    host.raceEvent(r.trackName);
    host.session(RACE1, r.laps, /*lengthMs=*/0, /*state=*/16, /*conditions=*/0);
    host.addEntry(10, "Alice", r.bike);
    host.runInit(RACE1, 0);
    for (int i = 0; i < r.odoTicks; ++i) {
        g_clockUs += 100000;   // 100 ms
        host.statsSetNowUs(g_clockUs);
        host.telemetry(30.0f);
    }
    for (int lap = 1; lap <= r.laps; ++lap) {
        // Each lap a little quicker, and flagged as a best -- which is what
        // makes the lap reach the personal-best store (see pb_scope_test).
        host.raceLap(RACE1, 10, lap, r.lapMs - lap * 100, /*best=*/1);
    }
    std::vector<ClassRow> rows;
    rows.push_back({ .num = 10, .best = r.lapMs - r.laps * 100, .laps = r.laps, .gap = 0 });
    host.classify(RACE1, 300000, rows);
    host.runDeinit();
    host.eventDeinit();
}

}  // namespace

TEST_CASE("prestige: the trade is refused until the ladder is finished") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasAchievements());

    ride(host, Ride{});
    // Something was earned, so this is a real ladder being refused rather than
    // an empty one.
    CHECK(host.achievementTier("laps") >= 0);
    CHECK(host.prestige() == 0);

    CHECK_FALSE(host.takePrestige());
    CHECK(host.prestige() == 0);
    // And nothing moved: a refusal is not a partial wipe.
    CHECK(host.achievementValue("laps") == doctest::Approx(3.0));
}

TEST_CASE("prestige: developer mode is the second key, and it takes the ladder but not the lap book") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasStatsOdometer());   // for statsSave()

    ride(host, Ride{});
    host.statsSave();   // flush, so the pre-trade file is on disk
    {
        const nlohmann::json before = readStats();
        REQUIRE(before.contains("trackBike"));
        // A personal best was actually stored, or clause 4 below tests nothing.
        CHECK(before["global"].value("pbCount", 0) >= 1);
    }

    host.setDeveloperMode(true);
    REQUIRE(host.takePrestige());
    CHECK(host.prestige() == 1);

    // TAKEN: the tiers and the counters underneath them.
    CHECK(host.achievementTier("laps") == 0);
    CHECK(host.achievementValue("laps") == doctest::Approx(0.0));
    CHECK(host.achievementValue("distance") == doctest::Approx(0.0));
    CHECK(host.achievementValue("personal_bests") == doctest::Approx(0.0));

    // KEPT: the lap records, and the track name learned along the way.
    const nlohmann::json after = readStats();
    REQUIRE(after.contains("trackBike"));
    bool anyPb = false;
    for (auto& [key, tb] : after["trackBike"].items()) {
        (void)key;
        if (tb.contains("personalBest")) anyPb = true;
    }
    CHECK(anyPb);
    CHECK(after.value("prestige", 0) == 1);
    REQUIRE(after.contains("trackNames"));
    CHECK(after["trackNames"].value("sw", std::string()) == "Southwick");
    // The counter is honestly zero beside a shelf of stored bests.
    CHECK(after["global"].value("pbCount", 0) == 0);
}

TEST_CASE("prestige: the level and the empty counter survive the next load") {
    // Continues from the file the case above wrote: a prestiged file is exactly
    // the shape the pbCount floor used to mis-heal, and the load is where it
    // did it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());

    CHECK(host.prestige() == 1);
    // NOT handed back by the stored personal bests.
    CHECK(host.achievementValue("personal_bests") == doctest::Approx(0.0));
    CHECK(host.achievementTier("personal_bests") == 0);
    CHECK(host.achievementTier("laps") == 0);
}

TEST_CASE("prestige: the badge draws nothing until there is a level") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasThemeGeometry());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Alice");
    host.draw();

    const PluginHost::PanelRect locked = host.hudPanelRect(PluginHost::HUD_PRESTIGE);
    CHECK(locked.quads == 0);

    host.setPrestige(1);
    host.draw();
    const PluginHost::PanelRect worn = host.hudPanelRect(PluginHost::HUD_PRESTIGE);
    CHECK(worn.quads > 0);
    CHECK(worn.w > 0);
    CHECK(worn.h > 0);
}

// THE NAME AND THE NUMBER MOVE TOGETHER. The telemetry hot path keeps the
// maximum odometer up to date between laps, and used to raise the distance
// without moving the name with it -- so the row showed one bike's name against
// another's total until the next lap rebuilt both from scratch.
TEST_CASE("prestige: Loyal names the bike that has just taken the lead, mid-lap") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasStatsOdometer());

    ride(host, Ride{ .bike = "Slow 250", .odoTicks = 20 });
    CHECK(host.achievementLeader("bike_km") == "Slow 250");

    // A second bike ridden PAST the first, with no lap completed: the only thing
    // that has moved is the telemetry cache, which is the half that was wrong.
    host.eventInit("Southwick", "Alice", 1600.0f, 2, "Fast 450", "MX1", /*trackId=*/"sw");
    host.raceEvent("Southwick");
    host.session(RACE1, 5, 0, 16, 0);
    host.addEntry(10, "Alice", "Fast 450");
    host.runInit(RACE1, 0);
    for (int i = 0; i < 60; ++i) {
        g_clockUs += 100000;
        host.statsSetNowUs(g_clockUs);
        host.telemetry(30.0f);
    }
    CHECK(host.achievementLeader("bike_km") == "Fast 450");
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("prestige: a per-track maximum row names the track carrying it") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());

    // Nothing ridden: nothing to name, and no other row ever carries one.
    CHECK(host.achievementLeader("track_laps") == "");
    CHECK(host.achievementLeader("laps") == "");

    ride(host, Ride{ .trackId = "sw", .trackName = "Southwick", .bike = "Test 450", .laps = 3 });
    CHECK(host.achievementLeader("track_laps") == "Southwick");
    // The bike row names a bike by the game's own name for it -- there is no id
    // to fall back from, so nothing has to be learned for this one.
    CHECK(host.achievementLeader("bike_km") == "Test 450");

    // A second track with MORE laps takes the row over -- the name follows the
    // maximum rather than the last place ridden.
    ride(host, Ride{ .trackId = "wk", .trackName = "Washougal", .bike = "Test 450", .laps = 6 });
    CHECK(host.achievementLeader("track_laps") == "Washougal");

    // A track whose name was never learned reads by its id rather than blank:
    // records written before names were kept still have to say something.
    ride(host, Ride{ .trackId = "unnamed", .trackName = "", .bike = "Test 450", .laps = 9 });
    CHECK(host.achievementLeader("track_laps") == "unnamed");
}

TEST_CASE("prestige: a kept personal best is still readable through the CLASS scope") {
    // THE CLAUSE THE OTHER CASES MISS. They assert the bests are on DISK, which
    // a wipe that stranded them would still satisfy: the default
    // PBScope::CATEGORY does not read a PB by its own key, it reads the class
    // best across every bike in the class, and that walk needs the bike->class
    // map. Wiping that map left every kept PB reachable only as its own bike's
    // time, so the first lap of a class fired the green ALL-TIME PB notice
    // against the wrong reference - the exact fault pb_scope_test.cpp pins,
    // reintroduced by the trade rather than by the lookup.
    //
    // Driven through the NOTICE because that is what a player sees, and it is
    // the one observable that differs between the two references.
    //
    // LAST IN THE FILE deliberately: the two cases above share one stats file
    // on purpose (one writes it, the next asserts what survives the load), and
    // this one rides laps of its own, so anywhere but the end breaks that chain.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());

    // Two bikes in one class, the first a good deal faster. Both PBs are kept
    // by the trade; only the class best is the right reference afterwards.
    ride(host, Ride{ .trackId = "sw", .trackName = "Southwick", .bike = "Fast 450",
                     .laps = 1, .lapMs = 90000 });
    ride(host, Ride{ .trackId = "sw", .trackName = "Southwick", .bike = "Slow 250",
                     .laps = 1, .lapMs = 99000 });

    host.setDeveloperMode(true);
    REQUIRE(host.takePrestige());

    // A lap on the slow bike that beats ITS OWN stored time but is nowhere near
    // the class best. Under the bug this notified; it must not.
    host.eventInit("Southwick", "Alice", 1600.0f, 2, "Slow 250", "MX1", /*trackId=*/"sw");
    host.raceEvent("Southwick");
    host.session(RACE1, 5, 0, 16, 0);
    host.addEntry(10, "Alice", "Slow 250");
    host.runInit(RACE1, 0);
    host.raceLap(RACE1, 10, 1, 95000, /*best=*/1);
    CHECK_FALSE(host.takeNewAllTimePB());
    host.runDeinit();
    host.eventDeinit();
}

// The BUTTON, rather than the trade behind it: where it is drawn, and what it
// does to the row it sits on. Both were wrong in a way only the panel shows --
// it rode the Progress card, which is the tab's header above every page, and it
// carried a TOOLTIP_ROW region for its warning, which paints the full-width
// hover band that no other button on the panel has.
TEST_CASE("prestige: the button is on the Completion page only, and takes no row band") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasWhatsNew());          // openSettingsTab is one of that hook's exports
    host.setDeveloperMode(true);          // the second key: the button is available
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();

    float px = 0.0f, py = 0.0f, nx = 0.0f, ny = 0.0f;
    int pagesWithButton = 0;
    std::string group, buttonGroup;
    // Walk every page. The pager clamps at the last one, so the page standing
    // still is the end of the list.
    for (int guard = 0; guard < 32; ++guard) {
        host.draw();
        const int page = host.achievementsPage(&group);
        if (host.settingsRegionCenter("achievements.prestige", &px, &py)) {
            ++pagesWithButton;
            buttonGroup = group;
        }
        if (!host.settingsRegionCenter("pager.next", &nx, &ny)) break;
        host.clickAt(nx, ny);
        if (host.achievementsPage() == page) break;
    }
    CHECK(pagesWithButton == 1);
    CHECK(buttonGroup == "Completion");

    // BACK to the page the button is on. The walk above ends on the LAST page,
    // which is no longer the Completion one: the pages that do not count towards
    // progress follow it now, and two of them (mxbmrp3, Tinkering) are listed
    // from the start. Paging back until the button reappears asks where it is
    // rather than where the page order happens to put it.
    for (int guard = 0; guard < 32; ++guard) {
        if (host.settingsRegionCenter("achievements.prestige", &px, &py)) break;
        float bx = 0.0f, by = 0.0f;
        REQUIRE(host.settingsRegionCenter("pager.prev", &bx, &by));
        host.clickAt(bx, by);
        host.draw();
    }

    // And on that page: hovering a NORMAL settings row lights the row band,
    // hovering the BUTTON does not - its warning rides on the button's own
    // region, so the pointer resolves to the button rather than to a row-wide
    // tooltip strip laid over it. The band is the only quad that differs between
    // the two hovers (the tooltip text swaps strings, not quads), so the frame's
    // quad count is what tells them apart.
    REQUIRE(host.settingsRegionCenter("achievements.prestige", &px, &py));
    float rx = 0.0f, ry = 0.0f;
    REQUIRE(host.settingsRegionCenter("achievements.toasts", &rx, &ry));   // the Visible row
    host.injectMouse(true, rx, ry, 0);
    host.draw();
    const int rowHover = host.lastGameQuads();
    host.injectMouse(true, px, py, 0);
    host.draw();
    const int buttonHover = host.lastGameQuads();
    INFO("row hover " << rowHover << " quads, button hover " << buttonHover);
    CHECK(buttonHover < rowHover);

    host.injectMouse(false);
    host.shutdown();
}

// The panel does not change height when you page onto the button. Every page of
// the list is padded out to one body height, so the tab -- which is the tallest
// in the panel, and so sets the panel's height on every other tab -- is the same
// size on all thirteen. It was counting ROWS to pad, which assumed rows were the
// only thing a page held.
TEST_CASE("prestige: paging onto the button does not resize the settings panel") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasWhatsNew());
    host.setDeveloperMode(true);          // the button is available, so it is drawn
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();

    const int firstHeight = host.hudPanelRect("settings_hud").h;
    REQUIRE(firstHeight > 0);
    float nx = 0.0f, ny = 0.0f, px = 0.0f, py = 0.0f;
    bool sawButton = false;
    std::string group;
    for (int guard = 0; guard < 32; ++guard) {
        host.draw();
        const int page = host.achievementsPage(&group);
        INFO("page " << page << " (" << group << ")");
        CHECK(host.hudPanelRect("settings_hud").h == firstHeight);
        if (host.settingsRegionCenter("achievements.prestige", &px, &py)) sawButton = true;
        if (!host.settingsRegionCenter("pager.next", &nx, &ny)) break;
        host.clickAt(nx, ny);
        if (host.achievementsPage() == page) break;
    }
    CHECK(sawButton);                     // the page that could have resized it was visited
    host.injectMouse(false);
    host.shutdown();
}

// A MEASUREMENT IN FLIGHT IS PRE-PRESTIGE RIDING. Litres burnt and roost
// seconds reach the lifetime sums in batches - the litres on the odometer's
// ~100m mark, the seconds on a ~1s one - so at the instant the trade is taken
// there is always a little of both measured and not yet banked. Left in place
// they landed on the new ladder a moment later, which is the one thing the
// trade promises will not happen.
TEST_CASE("prestige: a measurement in flight does not land on the new ladder") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasPrestige());
    REQUIRE(host.hasStatsOdometer());
    host.setDeveloperMode(true);

    host.eventInit("Southwick", "Alice", 1600.0f, 2, "Test 450", "MX1", /*trackId=*/"sw",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("Southwick");
    host.session(RACE1, 3, /*lengthMs=*/0, /*state=*/16, /*conditions=*/0);
    host.addEntry(10, "Alice", "Test 450");
    host.addEntry(21, "Rider");
    host.runInit(RACE1, 0);
    host.runStart();
    host.classify(RACE1, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } },
                  /*sessionState=*/16);

    // The rival 8 m up the road: squarely in the roost band, and the player
    // burning a tenth of a litre per tick. Eight ticks is 24 m of the 100 m the
    // litres wait on and 0.7s of the ~1s the seconds wait on, so at the trade
    // both measurements are real and neither has been banked.
    long long t = 1'000'000;
    auto tick = [&](float fuelL) {
        t += 100'000;
        host.statsSetNowUs(t);
        TelemetryRow row;
        row.speed = 30.0f;
        row.fuel = fuelL;
        host.telemetryFrame(row);
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.5f, .posX = 0.0f, .posZ = 0.0f },
                                 { .num = 21, .trackPos = 0.505f, .posX = 0.0f, .posZ = 8.0f } });
    };
    for (int i = 0; i < 8; ++i) tick(8.0f - 0.1f * i);
    REQUIRE(host.achievementValue("fuel_burnt") == doctest::Approx(0.0));
    REQUIRE(host.achievementValue("roost") == doctest::Approx(0.0));

    REQUIRE(host.takePrestige());
    CHECK(host.prestige() == 1);

    // Ride on past both flush marks with nothing left to measure: a steady tank
    // and the rival half a lap away. Anything that shows up now was measured
    // before the trade.
    for (int i = 0; i < 40; ++i) {
        t += 100'000;
        host.statsSetNowUs(t);
        TelemetryRow row;
        row.speed = 30.0f;
        row.fuel = 7.3f;
        host.telemetryFrame(row);
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.5f, .posX = 0.0f, .posZ = 0.0f },
                                 { .num = 21, .trackPos = 0.8f, .posX = 0.0f, .posZ = 480.0f } });
    }
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(0.0));
    CHECK(host.achievementValue("roost") == doctest::Approx(0.0));

    host.runStop();
    host.runDeinit();
    host.eventDeinit();
}
