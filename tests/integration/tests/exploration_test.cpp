// ============================================================================
// tests/integration/tests/exploration_test.cpp
// The exploration and hidden achievements (core/exploration_stats.h): the
// signals a startup, a race, a lap, a crash and the once-a-second tick feed,
// driven through the real callbacks and read back through the achievement
// rows. Pins:
//
//   1. STARTUP: a user pack and a styled custom.css are noticed (Dress-up,
//      Pad Painter, Stylist), a fresh install gets ONE summary toast for them,
//      a new day counts (Regular), the small hours are Night Owl and the first
//      run's anniversary is Anniversary -- on the injected clock, never the
//      runner's. A test build is not Homebrew.
//   2. HAND EDITS: a settings file or a stats file the plugin wrote loads clean;
//      the same file changed outside the game earns Under the Hood / Backup
//      Plan, by the file's own fingerprint.
//   3. RACES: Photo Finish, Wire to Wire, Lapped the Field and Charger from the
//      classification the finish is recorded with; Rage Quit from leaving a
//      race unfinished; Sandbagger from a PB on the last lap.
//   4. LAPS: Palindrome, Deja Vu and Metronome from lap times alone.
//   5. CRASHES: Baker's Dozen at thirteen in a session and the crash-tally nod
//      at ninety-nine, on the crash edge the tally already counts.
//   6. THE TICK: spectate and rumble time, Frame Perfect, On Air and Steady
//      Hands from the per-second tick, driven directly.
//   7. TOASTS OFF: Ungrateful is recorded even though it cannot be shown.
//   8. FMX: a chain of five backflips taken down by a crash is Case of the
//      Mondays, and the airtime of the landed ones is Air Miles.
//   9. SETTINGS: Tyre Kicker counts the HUDs ever seen on at a save, kept
//      across a restart, not the ones on together.
//  10. THE LOAD ORDER: a companion window the display target opens during
//      the settings load (before the stats load) counts once and toasts
//      nothing of its own; an old crash dump is Phoenix's baseline, not a
//      comeback; the overlay's per-process total accumulates across runs;
//      a pit stop is not a Rage Quit (the race's end without a finish is);
//      a hand edit is caught by RELOAD_CONFIG, not only the next start.
//  11. Test Pilot from an experimental setting seen on at a save; Profile
//      Hopper from every real switch, whichever path made it.
//
// Back Marker (lapped three times) is not driven here: a lapped rider's finish
// hangs on the leader's, which the harness does not model.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"
#include "nlohmann/json.hpp"

#include <direct.h>
#include <cstdio>
#include <string>
#include <vector>

static constexpr int RACE1 = 6;
static constexpr int RACE2 = 7;
static constexpr int PRACTICE = 1;

static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\exploration\\";
static const std::string kUserDir = "Z:\\tmp\\mxbmrp3-tests\\exploration\\mxbmrp3";
static const std::string kStatsPath = kUserDir + "\\mxbmrp3_stats.json";
static const std::string kIniPath = kUserDir + "\\mxbmrp3_settings.ini";

namespace {

void cleanSaveDir() {
    std::remove(kStatsPath.c_str());
    std::remove(kIniPath.c_str());
    std::remove((kUserDir + "\\web\\custom.css").c_str());
    // The served copy the asset sync makes of it (CWD-relative), which a later
    // case would otherwise inherit as a styled overlay.
    std::remove("plugins\\mxbmrp3_data\\web\\custom.css");
    _rmdir((kUserDir + "\\gamepads\\mypad").c_str());
    std::remove((kUserDir + "\\mxbmrp3_tracked_riders.json").c_str());   // Stalker starts empty
    std::remove((kUserDir + "\\crashes\\a.dmp").c_str());
    std::remove((kUserDir + "\\crashes\\b.dmp").c_str());
}

// A startup that writes both files and leaves, so the next one loads them.
void seed() {
    PluginHost seed(dllPath());
    REQUIRE(seed.loaded());
    seed.startup(kSaveWin);
    seed.save();
    seed.shutdown();
}

nlohmann::json readStats() {
    const std::string txt = ini::readFile(kStatsPath);
    if (txt.empty()) return nlohmann::json();
    return nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
}

void enterPractice(PluginHost& host) {
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();
}

}  // namespace

TEST_CASE("exploration: a startup notices the packs, the stylesheet, the day and the hour") {
    cleanSaveDir();
    seed();
    // A user gamepad pack (an empty folder is a pack for this purpose) and a
    // custom.css with an actual rule in it.
    _mkdir((kUserDir + "\\gamepads").c_str());
    _mkdir((kUserDir + "\\gamepads\\mypad").c_str());
    _mkdir((kUserDir + "\\web").c_str());
    ini::writeFile(kUserDir + "\\web\\custom.css", "/* mine */\n.rider { color: red; }\n");

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2026, 6, 15, 12);
        host.startup(kSaveWin);
        CHECK(host.achievementTier("dress_up") == 1);
        CHECK(host.achievementValue("dress_up") == doctest::Approx(1.0));
        CHECK(host.achievementTier("stylist") == 1);
        CHECK(host.achievementValue("regular") == doctest::Approx(1.0));
        CHECK(host.achievementTier("homebrew") == 0);        // a test build is not a home build
        // Granted silently on load, one card for the two.
        CHECK(host.achievementToastsQueued() == 1u);
        CHECK(host.achievementLastToast() == "2 achievements unlocked|See Settings > Achievements");
        host.shutdown();
    }
    // The next day, at three in the morning: another day counted, and a
    // session in the small hours.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2026, 6, 16, 3);
        host.startup(kSaveWin);
        CHECK(host.achievementValue("regular") == doctest::Approx(2.0));
        CHECK(host.achievementTier("night_owl") == 0);
        enterPractice(host);
        CHECK(host.achievementTier("night_owl") == 1);
        CHECK(host.achievementLastToast() == "Night Owl|Ride between two and five in the morning");
        host.runDeinit();
        host.shutdown();
    }
    // A year to the day after the first run.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2027, 6, 15, 12);
        host.startup(kSaveWin);
        enterPractice(host);
        CHECK(host.achievementTier("anniversary") == 1);
        host.runDeinit();
        auto j = readStats();
        REQUIRE(j.is_object());
        CHECK(j["exploration"].value("firstRunDate", "") == "2026-06-15");
        CHECK(j["exploration"].value("daysUsed", 0.0) == doctest::Approx(3.0));
        host.shutdown();
    }
    // A one-shot, so a LATER anniversary is the same award and not a second
    // one. Worth an assertion because the signal is marked repeatedly - the
    // once-a-minute tick marks it on every tick of the day, every year - and
    // only mark()'s idempotence keeps that from meaning anything.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2028, 6, 15, 12);
        host.startup(kSaveWin);
        enterPractice(host);
        CHECK(host.achievementValue("anniversary") == doctest::Approx(1.0));
        CHECK(host.achievementTier("anniversary") == 1);
        host.runDeinit();
        host.shutdown();
    }
}

TEST_CASE("exploration: a file the plugin wrote loads clean; one edited by hand is noticed") {
    cleanSaveDir();
    seed();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        CHECK(host.achievementTier("under_hood") == 0);
        CHECK(host.achievementTier("backup_plan") == 0);
        host.save();
        host.statsSave();
        host.shutdown();
    }
    // A key changed above the settings file's trailer; a number changed in the
    // stats file.
    {
        std::string ini = ini::readFile(kIniPath);
        REQUIRE(ini.find("[Fingerprint]") != std::string::npos);
        const size_t at = ini.find("[General]");
        REQUIRE(at != std::string::npos);
        ini.insert(at + std::string("[General]\n").size(), "handEdited=1\n");
        ini::writeFile(kIniPath, ini);
    }
    {
        auto j = readStats();
        REQUIRE(j.is_object());
        REQUIRE(j.contains("fingerprint"));
        j["global"]["raceCount"] = 500;
        ini::writeFile(kStatsPath, j.dump(2) + "\n");
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("under_hood") == 1);
    CHECK(host.achievementTier("backup_plan") == 1);
    CHECK(host.achievementValue("races") == doctest::Approx(500.0));   // the edit itself still counts
    host.shutdown();
}

TEST_CASE("exploration: RELOAD_CONFIG notices a hand edit without waiting for the next start") {
    // The documented workflow: auto-save off, edit the INI, reload. The next
    // save re-fingerprints the file, so a check only at startup missed it.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.save();
    CHECK(host.achievementTier("under_hood") == 0);
    host.loadSettings(kSaveWin);                          // reloaded as written: not an edit
    CHECK(host.achievementTier("under_hood") == 0);
    std::string ini = ini::readFile(kIniPath);
    const size_t at = ini.find("[General]");
    REQUIRE(at != std::string::npos);
    ini.insert(at + std::string("[General]\n").size(), "handEdited=1\n");
    ini::writeFile(kIniPath, ini);
    host.loadSettings(kSaveWin);
    CHECK(host.achievementTier("under_hood") == 1);
    host.shutdown();
}

TEST_CASE("exploration: a companion window opened by the display target counts once, silently") {
    // The settings load opens it BEFORE the stats file loads, so its own feed
    // is wiped with the load; the startup pull counts it, and no per-row toast
    // outlives the load (the summary card is the one upgrade toast).
    cleanSaveDir();
    seed();
    {
        std::string ini = ini::readFile(kIniPath);
        const size_t at = ini.find("displayTarget=IN_GAME");
        REQUIRE(at != std::string::npos);
        ini.replace(at, std::string("displayTarget=IN_GAME").size(), "displayTarget=COMPANION");
        ini::writeFile(kIniPath, ini);
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementValue("second_screen") == doctest::Approx(1.0));
    CHECK(host.achievementTier("second_screen") == 1);
    CHECK(host.achievementToastsQueued() == 1u);          // the summary, for this and the hand edit
    CHECK(host.achievementLastToast() == "2 achievements unlocked|See Settings > Achievements");
    host.shutdown();
}

TEST_CASE("exploration: Phoenix takes an old dump as the baseline, not as a comeback") {
    cleanSaveDir();
    _mkdir((kUserDir + "\\crashes").c_str());
    ini::writeFile(kUserDir + "\\crashes\\a.dmp", "x");
    seed();
    {
        // A stats file from before the exploration block: no baseline in it.
        auto j = readStats();
        REQUIRE(j.is_object());
        j.erase("exploration");
        ini::writeFile(kStatsPath, j.dump(2) + "\n");
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        CHECK(host.achievementTier("phoenix") == 0);      // the upgrade start: baseline taken
        host.statsSave();
        host.shutdown();
    }
    CHECK(readStats()["exploration"].value("crashDumpsSeen", 0) == 1);
    ini::writeFile(kUserDir + "\\crashes\\b.dmp", "x");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("phoenix") == 1);          // a dump since the last start
    host.shutdown();
}

TEST_CASE("exploration: the race-shaped signals come from the classification a finish is recorded with") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Six riders. The player starts last, leads every lap from lap one, laps
    // the whole field and wins by five hundredths.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 25; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();
    std::vector<ClassRow> grid;
    for (int n = 21; n <= 25; ++n) grid.push_back({ .num = n, .laps = 0 });
    grid.push_back({ .num = 10, .laps = 0 });
    host.classify(RACE1, 1000, grid);           // the player sits sixth before lap one
    host.raceLap(RACE1, 10, 1, 90000);          // lap one: still sixth on the last order
    std::vector<ClassRow> lead;
    lead.push_back({ .num = 10, .best = 90000, .laps = 1, .gap = 0 });
    for (int n = 21; n <= 25; ++n) lead.push_back({ .num = n, .best = 95000, .laps = 1, .gap = 5000 });
    host.classify(RACE1, 91000, lead);
    host.raceLap(RACE1, 10, 2, 90000);
    host.raceLap(RACE1, 10, 3, 90000);
    std::vector<ClassRow> finish;
    finish.push_back({ .num = 10, .best = 90000, .laps = 3, .gap = 0 });
    finish.push_back({ .num = 21, .best = 95000, .laps = 2, .gap = 50, .gapLaps = 1 });
    for (int n = 22; n <= 25; ++n) finish.push_back({ .num = n, .best = 95000, .laps = 2, .gap = 20000, .gapLaps = 1 });
    host.classify(RACE1, 300000, finish);
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("charger") == doctest::Approx(5.0));   // sixth after lap one, first at the flag
    CHECK(host.achievementTier("charger") == 1);
    CHECK(host.achievementValue("lapped_field") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(0.0));   // sixth on lap one
    // NOT a photo finish. #21's row says gap 50 - but gapLaps 1, so that is a
    // lap and five hundredths, and this very race is the Lapped the Field one
    // above. The old code read `gap` alone and scored both.
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(0.0));

    // Led from the first lap: Wire to Wire.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } });
    host.raceLap(RACE1, 10, 1, 90000);
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 200000, {
        { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
        { .num = 21, .best = 91000, .laps = 2, .gap = 3000 },
    });
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(0.0));   // three seconds is not a tenth

    // Leaving a race after a lap, before the flag: Rage Quit.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.raceLap(RACE1, 10, 1, 90000);
    host.runDeinit();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(0.0));   // off the track is not out of the race
    host.eventDeinit();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(1.0));
    CHECK(host.achievementTier("rage_quit") == 1);

    // A pit stop after lap one, then the finish: no quit. RunDeinit is "bike
    // leaves the track", which a pit visit is too.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.raceLap(RACE1, 10, 1, 90000);
    host.runDeinit();                                     // pits
    host.runInit(RACE1);                                  // rejoins the same session
    host.raceLap(RACE1, 10, 2, 90000);
    host.raceLap(RACE1, 10, 3, 90000);
    host.classify(RACE1, 300000, { { .num = 10, .best = 90000, .laps = 3, .gap = 0 } });
    host.runDeinit();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(1.0));
    // Leaving race two unfinished counts, once, when the next session starts.
    host.session(RACE2, 3, 0);
    host.runInit(RACE2);
    host.runStart();
    host.raceLap(RACE2, 10, 1, 90000);
    host.runDeinit();
    host.session(PRACTICE, 0, 480000);
    host.runInit(PRACTICE);
    host.runStart();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(2.0));
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(2.0));  // not again at the event's end

    // A PB on the last lap of a three-lap race: Sandbagger.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.raceLap(RACE1, 10, 1, 95000, /*best=*/1);
    host.raceLap(RACE1, 10, 2, 94000, /*best=*/1);
    CHECK(host.achievementTier("sandbagger") == 0);
    host.raceLap(RACE1, 10, 3, 93000, /*best=*/1);
    CHECK(host.achievementTier("sandbagger") == 1);
    host.classify(RACE1, 300000, { { .num = 10, .best = 93000, .laps = 3, .gap = 0 } });
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: a race nobody else started is Just Me Then") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // One rider on the grid: the server emptied out, or nobody else turned up.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 1, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 1000, { { .num = 10, .laps = 0 } });
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("solo_race") == doctest::Approx(1.0));
    CHECK(host.achievementTier("solo_race") == 1);
    // It is not a win worth anything else: with nobody to beat there is no
    // field to lap and no runner-up to pip.
    CHECK(host.achievementValue("lapped_field") == doctest::Approx(0.0));
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(0.0));
    host.runDeinit();

    // A one-shot: a second solo race is the same award, not a second one. The
    // flag is idempotent, so riding alone all season leaves it at 1.
    host.session(RACE2, 1, 0);
    host.runInit(RACE2);
    host.runStart();
    host.classify(RACE2, 1000, { { .num = 10, .laps = 0 } });
    host.raceLap(RACE2, 10, 1, 90000);
    host.classify(RACE2, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("solo_race") == doctest::Approx(1.0));
    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Charger counts from the opening split, not from the end of lap one") {
    // The row used to start counting at the END of lap one, so the run to the
    // first corner - the part of a race a charge actually happens in - scored
    // nothing. It now starts at the player's own first crossing of the opening
    // split, armed by the gate dropping.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 25; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    // Sixth on the gate.
    std::vector<ClassRow> grid = order({ 21, 22, 23, 24, 25, 10 }, 0);
    host.classify(RACE1, -5000, grid, /*sessionState=*/32);
    host.classify(RACE1, 1000, grid, /*sessionState=*/16);

    // A blinding start: fourth by the opening split. THIS is where Charger
    // starts counting, so those two places are not credited.
    host.classify(RACE1, 20000, order({ 21, 22, 23, 10, 24, 25 }, 0));
    host.raceSplit(RACE1, 10, 1, 0, 25000);

    // Then work forward to the win: fourth to first is three.
    host.classify(RACE1, 90000, order({ 21, 10, 22, 23, 24, 25 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 180000, order({ 10, 21, 22, 23, 24, 25 }, 2));
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 190000, order({ 10, 21, 22, 23, 24, 25 }, 2));

    // Three, not the five a grid-to-flag count would give: the gate-to-turn-one
    // places belong to Holeshot, and counting them here would pay twice.
    CHECK(host.achievementValue("charger") == doctest::Approx(3.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Photo Finish is the lap-time margin, and it lands at the flag") {
    // Regression test for a shipped bug. The margin used to be read off
    // StandingsData::gap, which PluginData deliberately caches across the flag
    // (the API zeroes gaps as the leader crosses, and the standings would
    // flicker without the substitution) - so it reported the gap from BEFORE
    // the runner-up closed it. Photo Finish unlocked once in 1728 installs.
    // finish_margin.h differences the two riders' lap logs instead.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } });
    // 183500 against 183543: forty-three thousandths over two laps.
    host.raceLap(RACE1, 10, 1, 92000);
    host.raceLap(RACE1, 21, 1, 92100);
    host.raceLap(RACE1, 10, 2, 91500);
    host.raceLap(RACE1, 21, 2, 91443);
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(0.0));   // still racing

    // The gap column here is the stale live one, two and a half seconds from
    // the last split. The old code would have read exactly this and refused.
    host.classify(RACE1, 200000, {
        { .num = 10, .best = 91500, .laps = 2, .gap = 0 },
        { .num = 21, .best = 91443, .laps = 2, .gap = 2500 },
    });
    // At the flag, NOT at RunDeinit: the plugin gets no callbacks once the
    // player is back in the menus, so a toast queued there has no Draw to
    // render into and first surfaces at the next track load.
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(1.0));
    CHECK(host.achievementTier("photo_finish") == 1);

    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(1.0));   // a flag, not a count

    // A second, wider win does not raise it either way.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE2, 1, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");
    host.runInit(RACE2);
    host.runStart();
    host.classify(RACE2, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } });
    host.raceLap(RACE2, 10, 1, 90000);
    host.raceLap(RACE2, 21, 1, 94000);
    host.classify(RACE2, 100000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 21, .best = 94000, .laps = 1, .gap = 4000 },
    });
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(1.0));
    host.shutdown();
}

TEST_CASE("exploration: Lapped the Field ignores riders who never raced to the flag") {
    // Regression test for a shipped bug: the scan required gapLaps >= 1 from
    // EVERY row in the classification, whatever its state. One rider who
    // retired on lap one - or a DNS that never left the grid - sits there with
    // gapLaps 0 and vetoed the row no matter how far ahead the player finished,
    // which in a public lobby is most races.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 23; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 },
        { .num = 22, .laps = 0 }, { .num = 23, .laps = 0 },
    });
    host.raceLap(RACE1, 10, 1, 90000);
    host.raceLap(RACE1, 10, 2, 90000);
    // The leader takes the flag; #21 and #22 are a lap down, #23 retired on lap
    // one and is still listed, a lap up on nobody.
    host.classify(RACE1, 180000, {
        { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
        { .num = 21, .best = 99000, .laps = 1, .gap = 8000, .gapLaps = 1 },
        { .num = 22, .best = 99000, .laps = 1, .gap = 9000, .gapLaps = 1 },
        { .num = 23, .best = 99000, .laps = 0, .gap = 0, .gapLaps = 0, .state = 3 },
    });
    // The lapped pair cross once more, which is where they are classified.
    host.classify(RACE1, 190000, {
        { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
        { .num = 21, .best = 99000, .laps = 2, .gap = 8000, .gapLaps = 1 },
        { .num = 22, .best = 99000, .laps = 2, .gap = 9000, .gapLaps = 1 },
        { .num = 23, .best = 99000, .laps = 0, .gap = 0, .gapLaps = 0, .state = 3 },
    });
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("lapped_field") == doctest::Approx(1.0));
    host.shutdown();
}

TEST_CASE("exploration: the holeshot, a last-lap pass, a race of attrition and a clean run") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Eight riders over three laps. The player is sixth off the gate but first
    // through the opening split, works forward, and takes the lead on the last
    // lap; two of the eight retire on the way.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 27; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();

    // The gate. PRE_START -> IN_PROGRESS arms the watcher; the classification's
    // own hold (Complete) -> racing flip is the gate physically dropping.
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    std::vector<ClassRow> grid;
    for (int n = 21; n <= 27; ++n) grid.push_back({ .num = n, .laps = 0 });
    grid.push_back({ .num = 10, .laps = 0 });
    host.classify(RACE1, -5000, grid, /*sessionState=*/32);
    host.classify(RACE1, 1000, grid, /*sessionState=*/16);

    // First to the opening split takes the holeshot; everyone behind is one
    // bool test and nothing else.
    host.raceSplit(RACE1, 10, 1, 0, 25000);
    host.raceSplit(RACE1, 21, 1, 0, 25200);
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));

    // Third after lap one, second after lap two, first at the flag: the place
    // that matters is the one taken between the last two.
    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    host.classify(RACE1, 90000, order({ 21, 22, 10, 23, 24, 25, 26, 27 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 180000, order({ 21, 10, 22, 23, 24, 25, 26, 27 }, 2));
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 270000, order({ 10, 21, 22, 23, 24, 25, 26, 27 }, 2));
    host.raceLap(RACE1, 10, 3, 90000);

    // The flag, with #26 and #27 retired. Everyone still racing is classified,
    // so the finish is recorded here.
    std::vector<ClassRow> finish = order({ 10, 21, 22, 23, 24, 25 }, 3);
    finish.push_back({ .num = 26, .best = 90000, .laps = 1, .state = 3 });
    finish.push_back({ .num = 27, .best = 90000, .laps = 2, .state = 3 });
    host.classify(RACE1, 280000, finish);

    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(1.0));
    // Eight starters, two of them gone: a quarter exactly.
    CHECK(host.achievementValue("survivor") == doctest::Approx(1.0));
    // Three laps, nothing on any of their sheets.
    CHECK(host.achievementValue("clean_laps") == doctest::Approx(3.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("exploration: what does NOT earn the holeshot, the pass, the attrition or the streak") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Six riders, three laps. A rival takes the holeshot, the player leads
    // wire to wire (so gains nothing on the last lap), one rider retires, and a
    // penalty on lap two breaks the clean run.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 25; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    std::vector<ClassRow> grid;
    grid.push_back({ .num = 10, .laps = 0 });
    for (int n = 21; n <= 25; ++n) grid.push_back({ .num = n, .laps = 0 });
    host.classify(RACE1, -5000, grid, /*sessionState=*/32);
    host.classify(RACE1, 1000, grid, /*sessionState=*/16);

    // A rival is through first. The player's own split, later, changes nothing:
    // the holeshot was settled by the first one.
    host.raceSplit(RACE1, 21, 1, 0, 25000);
    host.raceSplit(RACE1, 10, 1, 0, 25400);
    CHECK(host.achievementValue("holeshots") == doctest::Approx(0.0));

    auto order = [&](int laps) {
        std::vector<ClassRow> rows;
        rows.push_back({ .num = 10, .best = 90000, .laps = laps });
        for (int n = 21; n <= 25; ++n) rows.push_back({ .num = n, .best = 91000, .laps = laps });
        return rows;
    };
    host.classify(RACE1, 90000, order(1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 180000, order(2));
    // A penalty lands on lap two, so lap two is not clean and the run restarts.
    host.communication(10, 0, /*communication=*/2, 5);
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 270000, order(2));
    host.raceLap(RACE1, 10, 3, 90000);

    std::vector<ClassRow> finish = order(3);
    finish[5].laps = 1;
    finish[5].state = 3;          // one retirement out of six is not a quarter
    host.classify(RACE1, 280000, finish);

    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));   // led throughout
    CHECK(host.achievementValue("survivor") == doctest::Approx(0.0));
    // Lap one was clean, lap two carried the penalty, lap three was clean
    // again: the best run is one, never three.
    CHECK(host.achievementValue("clean_laps") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    host.runDeinit();
    host.eventDeinit();

    // A third race that would score both rows if either test were the loose
    // one. FOUR starters with one retirement is a quarter exactly, so only the
    // minimum-starters floor keeps Survivor off it. And the player climbs from
    // fourth to second by lap two, then holds station: places were gained, just
    // not on the last lap, so comparing against the FIRST lap would fire.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE2, 3, 0);
    host.addEntry(10, "Alice");
    for (int n = 31; n <= 33; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE2);
    host.runStart();
    auto four = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    host.classify(RACE2, 1000, four({ 31, 32, 33, 10 }, 0));
    host.classify(RACE2, 90000, four({ 31, 32, 33, 10 }, 1));
    host.raceLap(RACE2, 10, 1, 90000);                       // fourth
    host.classify(RACE2, 180000, four({ 31, 10, 32, 33 }, 2));
    host.raceLap(RACE2, 10, 2, 90000);                       // second
    host.classify(RACE2, 270000, four({ 31, 10, 32, 33 }, 2));
    host.raceLap(RACE2, 10, 3, 90000);                       // still second
    std::vector<ClassRow> small = four({ 31, 10, 32 }, 3);
    small.push_back({ .num = 33, .best = 90000, .laps = 1, .state = 3 });
    host.classify(RACE2, 280000, small);

    CHECK(host.achievementValue("survivor") == doctest::Approx(0.0));        // four starters is too few
    CHECK(host.achievementValue("solo_race") == doctest::Approx(0.0));       // four of them is not alone
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));   // the places came earlier
    CHECK(host.achievementValue("charger") == doctest::Approx(2.0));          // fourth to second, just not late

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Palindrome, Deja Vu and Metronome from lap times alone") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);
    host.raceLap(PRACTICE, 10, 1, 83321);        // 1:23.321
    CHECK(host.achievementTier("palindrome") == 1);
    CHECK(host.achievementTier("deja_vu") == 0);
    host.raceLap(PRACTICE, 10, 2, 90000);
    host.raceLap(PRACTICE, 10, 3, 90000);        // the same time twice
    CHECK(host.achievementTier("deja_vu") == 1);
    // Five in a row within a tenth (the two 90000s count toward the five).
    CHECK(host.achievementValue("metronome") == doctest::Approx(0.0));
    host.raceLap(PRACTICE, 10, 4, 90050);
    host.raceLap(PRACTICE, 10, 5, 90080);
    host.raceLap(PRACTICE, 10, 6, 90010);
    CHECK(host.achievementValue("metronome") == doctest::Approx(1.0));
    // A hit closes its window: four more consistent laps are not four more
    // hits, the fifth is the second run.
    host.raceLap(PRACTICE, 10, 7, 90020);
    host.raceLap(PRACTICE, 10, 8, 90030);
    host.raceLap(PRACTICE, 10, 9, 90040);
    host.raceLap(PRACTICE, 10, 10, 90060);
    CHECK(host.achievementValue("metronome") == doctest::Approx(1.0));
    host.raceLap(PRACTICE, 10, 11, 90070);
    CHECK(host.achievementValue("metronome") == doctest::Approx(2.0));
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: developer mode switched on by hand lands at the config reload") {
    cleanSaveDir();
    seed();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("developer") == 0);
    host.save();
    {
        std::string ini = ini::readFile(kIniPath);
        const size_t at = ini.find("developerMode=0");
        REQUIRE(at != std::string::npos);
        ini.replace(at, std::string("developerMode=0").size(), "developerMode=1");
        ini::writeFile(kIniPath, ini);
    }
    host.loadSettings(kSaveWin);                          // RELOAD_CONFIG
    CHECK(host.achievementTier("developer") == 1);
    CHECK(host.achievementTier("under_hood") == 1);       // and the hand edit itself
    host.shutdown();
}

TEST_CASE("settings: [Advanced] crashOnReload is inert without developer mode") {
    // The dev knob that faults on RELOAD_CONFIG (to exercise the crash report
    // path against the live services) needs developerMode=1 as well, so a key
    // pasted from a bug thread cannot take a player's game down. With it on,
    // the reload really does fault, which no in-process test can survive - so
    // what is pinned here is the gate, not the fault.
    cleanSaveDir();
    seed();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.save();
    {
        std::string ini = ini::readFile(kIniPath);
        const size_t at = ini.find("developerMode=0");
        REQUIRE(at != std::string::npos);
        ini.insert(at, "crashOnReload=1\n");
        ini::writeFile(kIniPath, ini);
    }
    host.loadSettings(kSaveWin);                          // RELOAD_CONFIG, and still here
    CHECK(host.achievementTier("under_hood") == 1);       // the reload ran to its end
    host.shutdown();
}

// THE LOAD DOES NOT WRITE TO THE COUNTERS. It used to raise the metric behind
// any earned one-shot sitting under its threshold, so a row would not read
// "Earned" beside "1 / 13". That fired on every row whose threshold was ever
// raised as well: a real tally of 37 director cuts would have been rewritten to
// 500 and kept, and the usage survey reads those tallies. The tier is what the
// file says, the number is what the player did, and they are allowed to disagree.
TEST_CASE("exploration: an earned one-shot keeps its own number, whatever the threshold") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.statsSave();
        seed.shutdown();
    }
    // A file from before Baker's Dozen was a number: the flag, and the row earned.
    {
        auto j = readStats();
        REQUIRE(j.is_object());
        j["exploration"]["bakersDozen"] = 1.0;
        j["achievements"]["unlocked"]["bakers_dozen"] = { { "tier", 1 }, { "halfway", 0 } };
        ini::writeFile(kStatsPath, j.dump(2) + "\n");
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("bakers_dozen") == 1);            // earned stays earned
    CHECK(host.achievementValue("bakers_dozen") == doctest::Approx(1.0));   // and unrewritten
    host.statsSave();
    {
        const auto after = readStats();
        REQUIRE(after.is_object());
        CHECK(after["exploration"].value("bakersDozen", 0.0) == doctest::Approx(1.0));
    }
    host.shutdown();
}

TEST_CASE("exploration: the clock is read once a minute, so a night ride and a new day count mid-session") {
    cleanSaveDir();
    seed();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.setLocalTime(2026, 6, 15, 12);
    host.startup(kSaveWin);
    enterPractice(host);                               // a session started at noon
    CHECK(host.achievementTier("night_owl") == 0);
    CHECK(host.achievementValue("regular") == doctest::Approx(1.0));
    // Fifty-nine ticks say nothing; the sixtieth reads the clock.
    host.setLocalTime(2026, 6, 16, 3);                 // past midnight, into the small hours
    for (int s = 0; s < 59; ++s) host.explorationTick(false, false, /*onTrack=*/true, 100, 0);
    CHECK(host.achievementTier("night_owl") == 0);
    host.explorationTick(false, false, /*onTrack=*/true, 100, 0);
    CHECK(host.achievementTier("night_owl") == 1);
    CHECK(host.achievementValue("regular") == doctest::Approx(2.0));   // the new day, without a restart
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: On a Roll is the best run of consecutive days, across a month end and a restart") {
    cleanSaveDir();
    seed();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2026, 6, 29, 12);
        host.startup(kSaveWin);
        CHECK(host.achievementValue("day_streak") == doctest::Approx(1.0));
        enterPractice(host);
        // The next day, read off the once-a-minute clock: two in a row.
        host.setLocalTime(2026, 6, 30, 1);
        for (int s = 0; s < 60; ++s) host.explorationTick(false, false, /*onTrack=*/true, 100, 0);
        CHECK(host.achievementValue("day_streak") == doctest::Approx(2.0));
        host.runStop();
        host.runDeinit();
        host.statsSave();
        host.shutdown();
    }
    {
        // July the first continues June the thirtieth's run: three, and Bronze.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2026, 7, 1, 12);
        host.startup(kSaveWin);
        CHECK(host.achievementValue("day_streak") == doctest::Approx(3.0));
        CHECK(host.achievementTier("day_streak") == 1);
        host.statsSave();
        host.shutdown();
    }
    {
        // A skipped day starts a new run of one; the row keeps the three.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.setLocalTime(2026, 7, 3, 12);
        host.startup(kSaveWin);
        CHECK(host.achievementValue("day_streak") == doctest::Approx(3.0));
        CHECK(host.achievementValue("regular") == doctest::Approx(5.0));   // the seed's day, then these four
        host.statsSave();
        auto j = readStats();
        REQUIRE(j.is_object());
        CHECK(j["exploration"].value("dayStreak", 0) == 1);
        host.shutdown();
    }
}

TEST_CASE("exploration: Well Travelled counts server names, never the same one twice, never offline") {
    cleanSaveDir();
    seed();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", /*serverType=*/0, "");
    CHECK(host.achievementValue("servers") == doctest::Approx(0.0));
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", 1, "Alpha Racing");
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", 2, "Beta Practice");
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", 1, "Alpha Racing");
    CHECK(host.achievementValue("servers") == doctest::Approx(2.0));
    host.statsSave();
    auto j = readStats();
    REQUIRE(j.is_object());
    CHECK(j["exploration"]["servers"].size() == 2u);
    host.shutdown();
}

TEST_CASE("exploration: a custom.css in the plugin's own web folder counts for Stylist too") {
    // The docs say to copy the bundled sample to custom.css beside it, in the
    // folder the overlay serves from; that file is the styled overlay as much as
    // one in Documents is.
    cleanSaveDir();
    seed();
    _mkdir("plugins"); _mkdir("plugins\\mxbmrp3_data"); _mkdir("plugins\\mxbmrp3_data\\web");
    ini::writeFile("plugins\\mxbmrp3_data\\web\\custom.css", "/* mine */\n.rider { color: red; }\n");
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        CHECK(host.achievementTier("stylist") == 1);
        host.shutdown();
    }
    std::remove("plugins\\mxbmrp3_data\\web\\custom.css");
}

TEST_CASE("exploration: a config reload rescans the user's packs and stylesheet") {
    cleanSaveDir();
    seed();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("dress_up") == 0);
    CHECK(host.achievementTier("stylist") == 0);
    // Dropped in after the start: a pack folder and a stylesheet with a rule.
    _mkdir((kUserDir + "\\gamepads").c_str());
    _mkdir((kUserDir + "\\gamepads\\mypad").c_str());
    _mkdir((kUserDir + "\\web").c_str());
    ini::writeFile(kUserDir + "\\web\\custom.css", ".rider { color: red; }\n");
    host.loadSettings(kSaveWin);                       // RELOAD_CONFIG
    CHECK(host.achievementTier("dress_up") == 1);
    CHECK(host.achievementValue("dress_up") == doctest::Approx(1.0));
    CHECK(host.achievementTier("stylist") == 1);
    host.shutdown();
}

TEST_CASE("exploration: Baker's Dozen and the crash-tally nod ride the crash edge") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);
    for (int i = 1; i <= 99; ++i) {
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 1 } });
        host.telemetry(10.0f, 3);
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 0 } });
        host.telemetry(10.0f, 3);
        if (i == 12) {
            CHECK(host.achievementTier("bakers_dozen") == 0);
            CHECK(host.achievementValue("bakers_dozen") == doctest::Approx(12.0));   // the row counts up
        }
        if (i == 13) CHECK(host.achievementTier("bakers_dozen") == 1);
        if (i == 98) {
            CHECK(host.achievementTier("ninety_nine") == 0);
            CHECK(host.achievementValue("ninety_nine") == doctest::Approx(98.0));
        }
    }
    CHECK(host.achievementTier("ninety_nine") == 1);
    CHECK(host.achievementValue("crashes") == doctest::Approx(99.0));
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: the once-a-second tick feeds the time sums, Frame Perfect, On Air and Steady Hands") {
    cleanSaveDir();
    {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);
    // An hour of spectating with rumble on, at 480 frames, with three overlay
    // connections seen, on track and crash-free.
    // The hour is split by a spell off track: a pit visit pauses Steady Hands'
    // clock, it does not restart it -- the half-hour row is already earned by
    // then, so the run's VALUE either side of the break is what shows it.
    for (int s = 0; s < 1800; ++s) {
        host.explorationTick(/*spectating=*/true, /*rumbleLive=*/true, /*onTrack=*/true, 480, 3);
    }
    for (int s = 0; s < 60; ++s) {
        host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/false, 480, 3);
    }
    CHECK(host.achievementTier("steady_hands") == 1);                          // half an hour, unbroken
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(1800.0));   // the run so far, in seconds
    for (int s = 0; s < 1800; ++s) {
        host.explorationTick(/*spectating=*/true, /*rumbleLive=*/true, /*onTrack=*/true, 480, 3);
    }
    // The hour is the sum; the row asks for six, so it is not earned yet. The
    // VALUE is what the tick feeds, and that is what this case is about.
    CHECK(host.achievementValue("armchair") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(host.achievementTier("armchair") == 0);
    // A flag now: a live pad means rumble is switched on, and one tick says so.
    CHECK(host.achievementTier("good_vibrations") == 1);
    // A one-shot at the plugin's own 480fps target. The signal is still the
    // best second ever seen, which is what earns it.
    CHECK(host.achievementTier("frame_perfect") == 1);
    CHECK(host.achievementValue("frame_perfect") == doctest::Approx(480.0));
    CHECK(host.achievementValue("on_air") == doctest::Approx(3.0));
    CHECK(host.achievementTier("steady_hands") == 1);
    // Iron Butt counts the DAY, not the session, so the minute spent off track
    // between the two half-hours is the only thing missing from it: an hour of
    // riding split by a break still reads as an hour. The old per-session
    // figure would have shown the second half only.
    CHECK(host.achievementValue("session_time") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(host.achievementTier("session_time") == 1);
    host.runStop();
    host.runDeinit();
    auto j = readStats();
    REQUIRE(j.is_object());
    CHECK(j["exploration"].value("spectateHours", 0.0) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(j["exploration"].value("framePerfect", 0.0) == doctest::Approx(480.0));
    // The running day total is PERSISTED, not just the max: that is what makes
    // a crash cost the current stint rather than the whole day.
    CHECK(j["exploration"].value("todayRideSec", 0.0) == doctest::Approx(3600.0));
    CHECK(j["exploration"].value("rideDay", 0) > 0);
    host.shutdown();
    }   // the first host is destroyed (DLL unloaded) before the next one loads it
    // The overlay's connection total restarts with the process: the next
    // run's two connections make five, not a maximum of three.
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    again.startup(kSaveWin);
    again.explorationTick(false, false, false, 60, 2);
    CHECK(again.achievementValue("on_air") == doctest::Approx(5.0));
    again.shutdown();
}

TEST_CASE("exploration: parked on track is not riding - the RIDE rows need the bike moving") {
    // Every row that says RIDE needs the wheels turning. Before this the tick
    // only asked isPlayerRunning(), so leaving the bike parked on track
    // overnight earned Iron Butt Platinum AND Steady Hands - the exact
    // "scumbag it" route the catalogue tries not to leave open, and on Steady
    // Hands a particularly silly one, since a parked bike cannot crash.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);

    // An hour parked on track: engine on, session running, nothing moving.
    for (int s = 0; s < 3600; ++s) {
        host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/true, 480, 0,
                             /*moving=*/false);
    }
    CHECK(host.achievementValue("session_time") == doctest::Approx(0.0));
    CHECK(host.achievementTier("session_time") == 0);
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(0.0));
    CHECK(host.achievementTier("steady_hands") == 0);
    // Good Vibrations asks only that rumble is ON, so a parked bike with a live
    // pad earns it: it is a fact about the settings, not about the rider. The
    // settings read at startup is its real feed; this tick is the backstop for
    // a player who switches rumble on mid-session.
    host.explorationTick(/*spectating=*/false, /*rumbleLive=*/true, /*onTrack=*/true, 480, 0,
                         /*moving=*/false);
    CHECK(host.achievementTier("good_vibrations") == 1);

    // Half an hour actually riding earns Steady Hands; an hour earns Iron Butt.
    for (int s = 0; s < 3600; ++s) {
        host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/true, 480, 0,
                             /*moving=*/true);
    }
    CHECK(host.achievementValue("session_time") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(host.achievementTier("session_time") == 1);
    CHECK(host.achievementTier("steady_hands") == 1);
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(3600.0));

    host.runStop();
    host.runDeinit();
    host.shutdown();
}


TEST_CASE("exploration: a pit visit keeps Steady Hands' clock and the session scratch") {
    // RunDeinit -> RunInit in the SAME session is a pit stop, and the review
    // found it wiping the per-session scratch: Steady Hands restarted at zero,
    // Charger and Wire to Wire forgot lap one. A new session type still resets.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);
    for (int s = 0; s < 100; ++s) host.explorationTick(false, false, /*onTrack=*/true, 100, 0);
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(100.0));
    host.runStop();
    host.runDeinit();                   // into the pits...
    host.runInit(PRACTICE);
    host.runStart();                    // ...and back out, the same practice session
    for (int s = 0; s < 100; ++s) host.explorationTick(false, false, /*onTrack=*/true, 100, 0);
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(200.0));
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Ungrateful is recorded with the toasts off") {
    cleanSaveDir();
    seed();
    std::string ini = ini::readFile(kIniPath);
    const size_t section = ini.find("[Achievements]");
    REQUIRE(section != std::string::npos);
    const size_t key = ini.find("visible=1", section);
    REQUIRE(key != std::string::npos);
    ini.replace(key, 9, "visible=0");
    ini::writeFile(kIniPath, ini);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("ungrateful") == 1);
    CHECK(host.achievementToastsQueued() == 0u);
    // The hand edit above is also a hand edit.
    CHECK(host.achievementTier("under_hood") == 1);
    host.shutdown();
}

TEST_CASE("exploration: a five-backflip chain taken down by a crash is Case of the Mondays") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasFmx());
    enterPractice(host);

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
    auto groundTicks = [&](int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); };
    auto airTicks = [&](int n, float pitchVel) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = pitchVel;
            tick(r);
        }
    };
    groundTicks(20);
    for (int i = 0; i < 5; ++i) {
        airTicks(150, -300.0f);   // a backflip, 1.5 s in the air
        groundTicks(80);          // land, through the grace: banked into the chain
    }
    // The chain is five deep and open. A crash now takes it.
    TelemetryRow crashed;
    crashed.crashed = 1;
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 1 } });
    tick(crashed);
    tick(crashed);
    CHECK(host.achievementTier("mondays") == 1);
    // Nothing banked: the chain never completed.
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(0.0));
    // The AIRTIME is kept even so. It is measured off the flight now, not off a
    // landed trick, so the seconds spent upside down before it all went wrong
    // still count - which is the whole point of "airtime is airtime". The
    // flights before the crash landed cleanly; only the last one aborts.
    CHECK(host.achievementValue("air_miles") > 0.0);
    // Weakest Link reads what the chain was worth when it broke.
    CHECK(host.achievementValue("chain_lost") > 0.0);
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Weakest Link keeps the richest chain lost, not a sum, and needs no five") {
    // Two backflips crashed out of is a chain short of Mondays but worth
    // points; a later one-flip loss is worth less and leaves the value alone.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasFmx());
    enterPractice(host);

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
    auto groundTicks = [&](int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); };
    auto airTicks = [&](int n, float pitchVel) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = pitchVel;
            tick(r);
        }
    };
    auto crashOut = [&]() {
        TelemetryRow crashed;
        crashed.crashed = 1;
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 1 } });
        tick(crashed);
        tick(crashed);
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 0 } });
        groundTicks(400);         // back on the ground, the chain cooldown over
    };
    groundTicks(20);
    for (int i = 0; i < 2; ++i) {
        airTicks(150, -300.0f);
        groundTicks(80);
    }
    crashOut();
    const double twoFlips = host.achievementValue("chain_lost");
    CHECK(twoFlips > 0.0);
    CHECK(host.achievementTier("mondays") == 0);
    // A poorer chain lost later does not add to it or replace it.
    airTicks(150, -300.0f);
    groundTicks(80);
    crashOut();
    CHECK(host.achievementValue("chain_lost") == doctest::Approx(twoFlips));
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Tyre Kicker counts HUDs ever switched on, not on at once") {
    // The row asks that every HUD be TRIED, not that all of them be on
    // together: a HUD seen on at a settings save stays counted after it is
    // switched off again, across a restart.
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.save();
        // THE PRESTIGE BADGE IS NOT ONE OF THE HUDS THIS COUNTS. It is earned,
        // not switched on, so it belongs in neither half of the fraction - and
        // it defaults to visible (a locked one simply draws nothing), so
        // counting it handed every fresh install a free row toward this.
        // Asserted on the persisted TRIED SET, which is where it showed.
        {
            host.statsSave();   // the tried set lives in the STATS file
            const auto tried = readStats()["exploration"]["huds"];
            REQUIRE(tried.is_array());
            for (const auto& n : tried) CHECK(n.get<std::string>() != "prestige_widget");
        }
        const double defaults = host.achievementValue("tyre_kicker");
        CHECK(defaults > 0.0);                            // the default setup has HUDs on
        CHECK(defaults < 100.0);                          // but not all of them
        host.setEveryHudVisible(true);
        // The two HUDs that show THEMSELVES are not in the count: the Direct GL
        // confirmation (armed by that setting's prompt) and the version widget
        // (an update notice, the donation nudge). Off, every switch still reads
        // as 100%; counted, Platinum would need both events.
        REQUIRE(host.setHudVisible("gl_confirm", false));
        REQUIRE(host.setHudVisible("version_widget", false));
        // Counted at the switch, not at the deferred save.
        CHECK(host.achievementValue("tyre_kicker") == doctest::Approx(100.0));
        host.save();
        CHECK(host.achievementValue("tyre_kicker") == doctest::Approx(100.0));
        // Earned, not Platinum: the row is a one-shot at 100% now, so switching
        // the last HUD on wins it outright and there is no ladder left to
        // climb. The VALUE is what this case is about - that every switch on
        // reads 100, and that the two self-showing HUDs are out of the count -
        // and that is asserted either side of this line.
        CHECK(host.achievementTier("tyre_kicker") == 1);
        host.setEveryHudVisible(false);
        host.save();
        CHECK(host.achievementValue("tyre_kicker") == doctest::Approx(100.0));   // tried, not on
        host.statsSave();
        host.shutdown();
    }
    {
        // The tried set is the persisted fact, not the percent alone: a
        // restart with everything off still reads every HUD as tried.
        auto j = readStats();
        REQUIRE(j.is_object());
        CHECK(j["exploration"]["huds"].is_array());
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.setEveryHudVisible(false);
        host.save();
        CHECK(host.achievementValue("tyre_kicker") == doctest::Approx(100.0));
        host.shutdown();
    }
}

TEST_CASE("exploration: Test Pilot is an experimental setting on at a save") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.save();
    CHECK(host.achievementTier("test_pilot") == 0);      // both ship off
    host.setPluginThreadFlag(true);
    host.save();
    CHECK(host.achievementTier("test_pilot") == 1);
    CHECK(host.achievementLastToast() == "Test Pilot|Turn on an experimental setting");
    host.shutdown();
}

// THE TWO HALVES ARRIVE FROM DIFFERENT CALLBACKS. "Settled" asks that every
// racing rider has a finish time, which comes from the classification; the margin
// is differenced from the LAP LOGS, which RaceLap fills. Nothing guarantees the
// order, so the classification can settle first -- and the race is recorded then,
// with the margin unreadable. It used to be recorded anyway and the flag latched,
// so Photo Finish was simply lost; RunDeinit retries the margin now, and only the
// margin, so nothing else the finish moved can count twice.
TEST_CASE("exploration: a photo finish is still caught when the laps land after the flag") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } });
    host.raceLap(RACE1, 10, 1, 92000);
    host.raceLap(RACE1, 21, 1, 92100);

    // The field settles on the SECOND lap with only the first in the logs.
    host.classify(RACE1, 200000, {
        { .num = 10, .best = 91500, .laps = 2, .gap = 0 },
        { .num = 21, .best = 91443, .laps = 2, .gap = 2500 },
    });
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(0.0));   // nothing to measure
    CHECK(host.achievementValue("races") == doctest::Approx(1.0));          // the race still counted

    // ...and the laps arrive after it.
    host.raceLap(RACE1, 10, 2, 91500);
    host.raceLap(RACE1, 21, 2, 91443);
    host.runDeinit();
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(1.0));
    CHECK(host.achievementValue("races") == doctest::Approx(1.0));          // and only once
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Grand Tour is the PERCENT of this build's settings tabs, not a count") {
    // Three tabs are game-gated (Records, Friends, FMX), so "open them all" as a
    // fixed NUMBER would be short of everything here and unreachable on karts.
    // The row is a percent of what this build actually has, and only SettingsHud
    // knows that, so it passes the count in.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasWhatsNew());   // openSettingsTab is one of that hook's exports
    for (const char* tab : { "General", "Appearance", "Hotkeys", "Riders", "Rumble", "Helmet" }) {
        REQUIRE(host.openSettingsTab(tab));
    }
    // SIX tabs opened. A count would read six (seven at the very most, if the
    // restored tab were a seventh), and a percent of the ~30 this build has
    // reads twenty-odd - so one number tells the two apart without this case
    // having to know how many tabs there are.
    const double six = host.achievementValue("grand_tour");
    CHECK(six > 7.0);
    REQUIRE(host.openSettingsTab("General"));   // already opened: the set does not grow
    CHECK(host.achievementValue("grand_tour") == doctest::Approx(six));
    CHECK(host.achievementTier("grand_tour") == 0);   // the row asks for all of them

    // AND ALL OF THEM IS REACHABLE. The denominator counted every tab
    // isTabAvailable accepts, which includes About -- hidden from the sidebar and
    // opened by a footer button that sets the tab directly, so nothing ever
    // recorded it. The row topped out one tab short of 100 forever. Walking the
    // tab LIST is the check: open every name the sidebar offers and the fraction
    // has to close.
    const std::vector<std::string> tabs = host.settingsTabNames();
    REQUIRE(tabs.size() > 6);
    for (const std::string& tab : tabs) REQUIRE(host.openSettingsTab(tab.c_str()));
    CHECK(host.achievementValue("grand_tour") == doctest::Approx(100.0));
    CHECK(host.achievementTier("grand_tour") == 1);
    host.shutdown();
}

// A NAME THIS BUILD DOES NOT LIST IS NOT PROGRESS. The tab set is persisted and
// keeps every name ever opened -- About, recorded until it stopped being; a
// game-gated tab from another game. Sizing the SET against the listed count let
// those stand in for tabs the player can actually reach, and the row closed a tab
// early: all but one listed, plus About, read as all of them.
TEST_CASE("exploration: Grand Tour ignores a recorded tab this build does not list") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.statsSave();
        seed.shutdown();
    }
    // A file that already holds two such names: About, which the footer button
    // used to record, and a tab from a build that has one this does not.
    {
        auto j = readStats();
        REQUIRE(j.is_object());
        j["exploration"]["tabs"] = { "About", "FMX" };
        ini::writeFile(kStatsPath, j.dump(2) + "\n");
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasWhatsNew());
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();
    const std::vector<std::string> tabs = host.settingsTabNames();
    REQUIRE(tabs.size() > 3);

    // Every listed tab but the last. With the two stale names counted, the set
    // is already the size of the list and the row reads 100 with a tab to go.
    for (size_t i = 0; i + 1 < tabs.size(); ++i) REQUIRE(host.openSettingsTab(tabs[i].c_str()));
    host.draw();
    CHECK(host.achievementValue("grand_tour") < 100.0);
    CHECK(host.achievementTier("grand_tour") == 0);

    // The one that IS listed closes it.
    REQUIRE(host.openSettingsTab(tabs.back().c_str()));
    host.draw();
    CHECK(host.achievementValue("grand_tour") == doctest::Approx(100.0));
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("exploration: Profile Hopper counts a switch by hand and by auto-switch alike") {
    // One choke point (SettingsManager::switchProfile) so the sidebar, the
    // hotkey and auto-switch all count, and a switch to the active profile
    // (which it rejects) does not.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    // ...and the sidebar arrow does NOT auto-repeat while held. Every other
    // _UP/_DOWN pair walks a value, where holding is the point; holding this one
    // cycled the four profiles several times a second, which made the row
    // something you earn by leaning on an arrow rather than by using profiles.
    REQUIRE(host.hasProfileArrowRepeats());
    CHECK_FALSE(host.profileArrowRepeats());
    host.switchProfile(1);                                // Practice -> Qualify, by hand
    CHECK(host.achievementValue("profile_hopper") == doctest::Approx(1.0));
    host.switchProfile(1);                                // already there: not a switch
    CHECK(host.achievementValue("profile_hopper") == doctest::Approx(1.0));
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack", /*type=*/2);
    host.setAutoSwitch(true);
    host.session(RACE1, 10);                              // -> Race, by auto-switch
    CHECK(host.activeProfile() == 2);
    CHECK(host.achievementValue("profile_hopper") == doctest::Approx(2.0));
    host.shutdown();
}

TEST_CASE("exploration: Stalker counts riders put on the tracked list, not re-adds") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("stalker") == 0);
    CHECK(host.trackRider("Bob"));
    CHECK_FALSE(host.trackRider("Bob"));                  // already tracked: no count
    CHECK(host.achievementValue("stalker") == doctest::Approx(1.0));
    CHECK(host.trackRider("Carol"));
    CHECK(host.achievementValue("stalker") == doctest::Approx(2.0));
    CHECK(host.achievementTier("stalker") == 0);          // the row asks for ten
    for (int i = 2; i < 10; ++i) {
        CHECK(host.trackRider(("Rider" + std::to_string(i)).c_str()));
    }
    CHECK(host.achievementValue("stalker") == doctest::Approx(10.0));
    CHECK(host.achievementTier("stalker") == 1);
    CHECK(host.achievementLastToast() == "Stalker|Track 10 riders (@turkishmonk)");
    host.shutdown();
}

TEST_CASE("exploration: Big Hit is the peak magnitude of the acceleration vector, and it only rises") {
    // The game already averages acceleration over 10ms, so a single-sample
    // spike cannot set this - what reaches the plugin is a real impact. It is a
    // high-water mark: a gentle lap after a big one must not walk it back.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);

    auto frame = [&](float ax, float ay, float az) {
        TelemetryRow r;
        r.accelX = ax; r.accelY = ay; r.accelZ = az;
        host.telemetryFrame(r);
    };

    // Sitting on the wheels: about 1g, and no achievement anywhere near it.
    frame(0.0f, 1.0f, 0.0f);
    CHECK(host.achievementValue("peak_g") == doctest::Approx(1.0).epsilon(0.01));
    CHECK(host.achievementTier("peak_g") == 0);

    // A 3-4-5 landing: the magnitude, not any one axis, is what counts. Well
    // short of the row's one tier, so it records the number and awards nothing.
    frame(3.0f, 4.0f, 0.0f);
    CHECK(host.achievementValue("peak_g") == doctest::Approx(5.0).epsilon(0.01));
    CHECK(host.achievementTier("peak_g") == 0);

    // Rolling gently again leaves the record alone.
    frame(0.0f, 1.0f, 0.0f);
    CHECK(host.achievementValue("peak_g") == doctest::Approx(5.0).epsilon(0.01));

    // A physics glitch is not a record: past the plausibility ceiling it is
    // dropped whole rather than clamped, so it cannot mint an unbeatable value.
    // 500g would otherwise clear the 35g row on a bug.
    frame(0.0f, 500.0f, 0.0f);
    CHECK(host.achievementValue("peak_g") == doctest::Approx(5.0).epsilon(0.01));
    CHECK(host.achievementTier("peak_g") == 0);

    frame(0.0f, 6.0f, 8.0f);   // 10g: bigger, still not 35
    CHECK(host.achievementValue("peak_g") == doctest::Approx(10.0).epsilon(0.01));
    CHECK(host.achievementTier("peak_g") == 0);

    // 36g, and inside the ceiling: the one tier there is.
    frame(0.0f, 36.0f, 0.0f);
    CHECK(host.achievementValue("peak_g") == doctest::Approx(36.0).epsilon(0.01));
    CHECK(host.achievementTier("peak_g") == 1);

    host.runStop();
    host.runDeinit();
    host.shutdown();
}

// LAST GASP READS A PAIR OF POSITIONS, and the two callbacks that feed it can
// arrive either way round: the pair comes from RaceLap and the finish from the
// classification, and when the field settles first the pair is still one lap
// behind. Read then, a place taken on the PENULTIMATE lap is credited as a
// last-lap pass. The same race the margin rows carry, and the same answer:
// defer to RunDeinit, by which point the lap has landed.
TEST_CASE("exploration: Last Gasp waits for the final lap when the flag beats it") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    auto startRace = [&]() {
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, 3, 0);
        host.addEntry(10, "Alice");
        for (int n = 21; n <= 24; ++n) host.addEntry(n, "Rider");
        host.runInit(RACE1);
        host.runStart();
        host.classify(RACE1, 1000, order({ 10, 21, 22, 23, 24 }, 0));
    };

    // THE PASS WAS ON LAP TWO of three: fifth after lap one, second after lap
    // two, second at the flag. Nothing was taken on the last lap.
    startRace();
    host.classify(RACE1, 90000, order({ 21, 22, 23, 24, 10 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 180000, order({ 21, 10, 22, 23, 24 }, 2));
    host.raceLap(RACE1, 10, 2, 90000);
    // The field settles on lap three with only two laps in: the finish counts,
    // and the row it cannot answer yet waits rather than guessing.
    host.classify(RACE1, 270000, order({ 21, 10, 22, 23, 24 }, 3));
    REQUIRE(host.achievementValue("races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));
    // ...and the lap arrives after it. Second after lap two, second at the
    // flag: no pass, and the answer does not change at the retry either.
    host.raceLap(RACE1, 10, 3, 90000);
    host.runDeinit();
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));
    host.eventDeinit();

    // THE SAME ORDERING, and this time the pass IS on the last lap: second
    // after both laps, first at the flag. Deferring must not lose it.
    startRace();
    host.classify(RACE1, 90000, order({ 21, 10, 22, 23, 24 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 180000, order({ 21, 10, 22, 23, 24 }, 2));
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 270000, order({ 10, 21, 22, 23, 24 }, 3));
    REQUIRE(host.achievementValue("races") == doctest::Approx(2.0));
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));   // still owed
    host.raceLap(RACE1, 10, 3, 90000);
    host.runDeinit();
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(1.0));
    // Once. RunDeinit consumes the deferral, so a second pass over it - a pit
    // stop, an event teardown - cannot count the same race again.
    host.eventDeinit();
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(1.0));

    host.shutdown();
}

// CHARGER STARTS AT THE FIRST CORNER, and the arm that captures it used to
// survive a split it could not read. A player still outside the classification
// order reports -1 at their lap-one split; the arm stayed up, LAP TWO's split
// claimed it instead, and the row's start moved forward to a place the rider
// had already worked for - billing them for the places gained in between.
TEST_CASE("exploration: Charger counts from the first corner, not the second lap's") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 27; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    // The gate drops on a classification the player is not in yet - a late join,
    // a slow first report - so the split that follows has no position to read.
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    std::vector<ClassRow> grid = order({ 21, 22, 23, 24, 25, 26, 27 }, 0);
    host.classify(RACE1, -5000, grid, /*sessionState=*/32);
    host.classify(RACE1, 1000, grid, /*sessionState=*/16);
    host.raceSplit(RACE1, 10, 1, 0, 25000);

    // Eighth and last after lap one. Nothing claimed the start, so this is the
    // fallback, and it is what Charger has to count from.
    host.classify(RACE1, 90000, order({ 21, 22, 23, 24, 25, 26, 27, 10 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);

    // Third by the time the SECOND lap's opening split goes by. A split 0 on any
    // lap but the first is not a start, and must not be taken for one.
    host.classify(RACE1, 115000, order({ 21, 22, 10, 23, 24, 25, 26, 27 }, 1));
    host.raceSplit(RACE1, 10, 2, 0, 115000);

    // ...and the win. Eight to one is seven places, which is the race the rider
    // actually rode; reading the start off lap two would call it two.
    host.raceLap(RACE1, 10, 2, 90000);
    host.classify(RACE1, 190000, order({ 10, 21, 22, 23, 24, 25, 26, 27 }, 2));
    CHECK(host.achievementValue("charger") == doctest::Approx(7.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// A RESTART IS A NEW RACE, and nothing in the run lifecycle says so: restarting
// re-enters the same session type, exactly as a pit stop does, and
// recordSessionStart() keeps the per-race state across that on purpose. So the
// abandoned attempt's finish latch was still set when the restarted race
// finished, and the whole record went in the bin - no race count, no win, no
// podium - while its start position and lead carried into the new one. The gate
// dropping is the one signal that separates the two.
TEST_CASE("exploration: a restarted race is a race, and it starts from scratch") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 27; ++n) host.addEntry(n, "Rider");

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    auto gate = [&]() {
        host.raceSessionState(RACE1, 256);
        host.raceSessionState(RACE1, 16);
        std::vector<ClassRow> grid = order({ 21, 22, 23, 24, 25, 26, 27, 10 }, 0);
        host.classify(RACE1, -5000, grid, /*sessionState=*/32);
        host.classify(RACE1, 1000, grid, /*sessionState=*/16);
    };

    // RACE ONE, ridden to the flag at the back of an eight-rider grid.
    host.session(RACE1, 3, 0);
    host.runInit(RACE1);
    host.runStart();
    gate();
    for (int lap = 1; lap <= 3; ++lap) {
        host.classify(RACE1, 90000 * lap, order({ 21, 22, 23, 24, 25, 26, 27, 10 }, lap));
        host.raceLap(RACE1, 10, lap, 90000);
    }
    host.classify(RACE1, 280000, order({ 21, 22, 23, 24, 25, 26, 27, 10 }, 3));
    REQUIRE(host.achievementValue("races") == doctest::Approx(1.0));
    host.runDeinit();

    // RESTART: same session type, so nothing in the lifecycle has changed, and a
    // single lap this time. Second on it and second at the flag.
    host.session(RACE1, 1, 0);
    host.runInit(RACE1);
    host.runStart();
    gate();
    host.classify(RACE1, 90000, order({ 21, 10, 22, 23, 24, 25, 26, 27 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, order({ 21, 10, 22, 23, 24, 25, 26, 27 }, 1));

    // The race counted at all, which it did not before.
    CHECK(host.achievementValue("races") == doctest::Approx(2.0));
    // ...and it was judged on its own running. Second to second is no places
    // gained; reading last place off the abandoned attempt would call it six.
    CHECK(host.achievementValue("charger") == doctest::Approx(0.0));
    // One lap has no last lap to pass anybody on. The old race's eighth place
    // sitting in the pair would have made second at the flag a last-gasp win.
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// EARLY ACCESS IS A LIVE SETTING, and the Updates tab changes it with a click
// that reloads nothing - so a row marked only in the startup/reload read stayed
// locked until the next launch, for a player looking straight at the switch they
// just flipped. The 1Hz tick is the backstop, the same one Good Vibrations has.
TEST_CASE("exploration: Early Access lands on the switch, not on the next launch") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasUpdateChannel());

    // Stable out of the box, and no amount of riding earns it.
    host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/true, 60);
    REQUIRE(host.achievementValue("prerelease") == doctest::Approx(0.0));

    // The click. Nothing else happens - no reload, no restart.
    host.setUpdateChannel(true);
    host.explorationTick(false, false, true, 60);
    CHECK(host.achievementValue("prerelease") == doctest::Approx(1.0));
    CHECK(host.achievementTier("prerelease") == 1);

    host.shutdown();
}

// A RESTART IS NOT A QUIT. Every rider is pulled off the track by one, so the
// RunDeinit that follows arms Rage Quit for a race nobody chose to leave - and
// for a moment the gate drop of the restarted race consumed that arm on the
// spot, crediting the row to a rider who then went on to finish. The arm needs
// no help: finishing clears it, and a real session change consumes it.
TEST_CASE("exploration: a race restarted under you is not a race you quit") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    auto gate = [&]() {
        host.raceSessionState(RACE1, 256);
        host.raceSessionState(RACE1, 16);
        std::vector<ClassRow> grid = order({ 21, 10 }, 0);
        host.classify(RACE1, -5000, grid, /*sessionState=*/32);
        host.classify(RACE1, 1000, grid, /*sessionState=*/16);
    };

    // Two laps in when the race is restarted under them: laps on the board, no
    // finish, so RunDeinit arms the quit exactly as walking away would.
    host.session(RACE1, 3, 0);
    host.runInit(RACE1);
    host.runStart();
    gate();
    for (int lap = 1; lap <= 2; ++lap) {
        host.classify(RACE1, 90000 * lap, order({ 21, 10 }, lap));
        host.raceLap(RACE1, 10, lap, 90000);
    }
    host.runDeinit();
    REQUIRE(host.achievementValue("rage_quit") == doctest::Approx(0.0));   // not yet: still on the fence

    // Back out for the restart, and this time ridden to the flag.
    host.session(RACE1, 1, 0);
    host.runInit(RACE1);
    host.runStart();
    gate();
    host.classify(RACE1, 90000, order({ 10, 21 }, 1));
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, order({ 10, 21 }, 1));
    REQUIRE(host.achievementValue("races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(0.0));

    // ...and still nothing once the event is torn down: the finish cleared it.
    host.runDeinit();
    host.eventDeinit();
    CHECK(host.achievementValue("rage_quit") == doctest::Approx(0.0));

    host.shutdown();
}

// A REPLAY IS NOT A RACE YOU RODE. The finish is recorded off the all-rider
// classification - which a replay delivers in full, the player's own entry
// among it - so without a gate, watching the replay of a race credits it: the
// race, the win, the podium, the clean run. It does not even need a second
// viewing. On a fresh launch nothing has been recorded yet, so watching one
// credits it the first time. RunStart is what says the player is the one riding.
TEST_CASE("exploration: a race watched back is not a race finished") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };

    // Everything a replay of the player's own winning race delivers: the gate
    // dropping, the laps, the splits, and a settled classification with the
    // player first. What it does NOT deliver is the run lifecycle - watching one
    // from the menus never puts the player on track, so there is no RunInit and
    // no RunStart, and isPlayerRunning() stays false throughout.
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    host.classify(RACE1, -5000, order({ 10, 21 }, 0), /*sessionState=*/32);
    host.classify(RACE1, 1000, order({ 10, 21 }, 0), /*sessionState=*/16);
    host.raceSplit(RACE1, 10, 1, 0, 25000);
    for (int lap = 1; lap <= 2; ++lap) {
        host.classify(RACE1, 90000 * lap, order({ 10, 21 }, lap));
        host.raceLap(RACE1, 10, lap, 90000);
    }
    host.classify(RACE1, 190000, order({ 10, 21 }, 2));

    CHECK(host.achievementValue("races") == doctest::Approx(0.0));
    CHECK(host.achievementValue("wins") == doctest::Approx(0.0));
    CHECK(host.achievementValue("podiums") == doctest::Approx(0.0));
    CHECK(host.achievementValue("holeshots") == doctest::Approx(0.0));

    // ...and the same race RIDDEN counts in full, so the zeroes above are the
    // gate rather than a broken setup.
    host.session(RACE1, 2, 0);
    host.runInit(RACE1);
    host.runStart();
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    host.classify(RACE1, -5000, order({ 10, 21 }, 0), /*sessionState=*/32);
    host.classify(RACE1, 1000, order({ 10, 21 }, 0), /*sessionState=*/16);
    host.raceSplit(RACE1, 10, 1, 0, 25000);
    for (int lap = 1; lap <= 2; ++lap) {
        host.classify(RACE1, 90000 * lap, order({ 10, 21 }, lap));
        host.raceLap(RACE1, 10, lap, 90000);
    }
    host.classify(RACE1, 190000, order({ 10, 21 }, 2));
    CHECK(host.achievementValue("races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wins") == doctest::Approx(1.0));
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// ============================================================================
// THE FOUR RACE ROWS THAT READ A FINISH SIDEWAYS. Perfect Race wants four
// things at once; Choke and Consolation Prize want a win that was not one.
// Each is a mark(), so every case below asserts the NEAR MISS first and the
// hit second - a row that fires on the near miss would otherwise be invisible,
// because by the time the real case runs it is already earned.
// ============================================================================
namespace {

// A three-lap race off a gate drop, the player as #10 among four rivals.
struct RaceRun {
    PluginHost& host;
    static std::vector<ClassRow> order(const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    }
    void start() {
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, 3, 0);
        host.addEntry(10, "Alice");
        for (int n = 21; n <= 24; ++n) host.addEntry(n, "Rider");
        host.runInit(RACE1);
        host.runStart();
        // PRE_START then IN_PROGRESS arms the gate-drop watch; the gate HOLD
        // (a classification reporting Complete) and then a racing one is the
        // drop itself - the edge that arms the holeshot and starts the
        // per-race scratch over.
        host.raceSessionState(RACE1, 256);
        host.raceSessionState(RACE1, 16);
        host.classify(RACE1, -5000, order({ 10, 21, 22, 23, 24 }, 0), /*sessionState=*/32);
        host.classify(RACE1, 1000, order({ 10, 21, 22, 23, 24 }, 0), /*sessionState=*/16);
    }
    void holeshot() { host.raceSplit(RACE1, 10, 1, 0, 25000); }
    void rivalHoleshot() { host.raceSplit(RACE1, 21, 1, 0, 24000); }
    // One lap: the field's order after it, then the player's lap. best=2 is the
    // overall fastest lap so far, which is what the fastest-lap flag reads.
    void lap(const std::vector<int>& nums, int lapNum, bool fastest = false) {
        host.classify(RACE1, 90000 * lapNum, order(nums, lapNum));
        host.raceLap(RACE1, 10, lapNum, 90000, fastest ? 2 : 0);
    }
    void finish(const std::vector<int>& nums) {
        host.classify(RACE1, 270000, order(nums, 3));
        host.runDeinit();
        host.eventDeinit();
    }
};

}  // namespace

TEST_CASE("exploration: Perfect Race needs the holeshot, the lead, the lap and the win") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    RaceRun race{ host };

    // THE NEAR MISS: led from the gate to the flag and won it, but the fastest
    // lap of the race was someone else's. Three of the four.
    race.start();
    race.holeshot();
    race.lap({ 10, 21, 22, 23, 24 }, 1);
    race.lap({ 10, 21, 22, 23, 24 }, 2);
    race.lap({ 10, 21, 22, 23, 24 }, 3);
    race.finish({ 10, 21, 22, 23, 24 });
    REQUIRE(host.achievementValue("races") == doctest::Approx(1.0));
    REQUIRE(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    // The holeshot really landed, so the zero below is the missing lap and not
    // a gate drop the harness never delivered.
    REQUIRE(host.achievementValue("holeshots") == doctest::Approx(1.0));
    CHECK(host.achievementTier("perfect_race") == 0);

    // ...AND WITH THE LAP AS WELL. Same race, all four.
    race.start();
    race.holeshot();
    race.lap({ 10, 21, 22, 23, 24 }, 1, /*fastest=*/true);
    race.lap({ 10, 21, 22, 23, 24 }, 2);
    race.lap({ 10, 21, 22, 23, 24 }, 3);
    race.finish({ 10, 21, 22, 23, 24 });
    CHECK(host.achievementTier("perfect_race") == 1);
    CHECK(host.achievementLastToast() ==
          "Perfect Race|Holeshot, every lap led, fastest lap, win");

    host.shutdown();
}

TEST_CASE("exploration: Perfect Race is not earned from the second row of the grid") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    RaceRun race{ host };

    // Every lap led and the fastest lap of the race, but the holeshot was
    // taken by someone else - the lead came later. Wire to Wire counts it
    // (first at the end of lap one is its question); this row does not.
    race.start();
    race.rivalHoleshot();
    race.lap({ 10, 21, 22, 23, 24 }, 1, /*fastest=*/true);
    race.lap({ 10, 21, 22, 23, 24 }, 2);
    race.lap({ 10, 21, 22, 23, 24 }, 3);
    race.finish({ 10, 21, 22, 23, 24 });
    REQUIRE(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    CHECK(host.achievementTier("perfect_race") == 0);

    host.shutdown();
}

TEST_CASE("exploration: Choke is the lead into the final lap, and not at the flag") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    RaceRun race{ host };

    // THE LEAD KEPT. Led into the last lap and won it, so there is nothing
    // here for either of the last-lap rows.
    race.start();
    race.lap({ 10, 21, 22, 23, 24 }, 1);
    race.lap({ 10, 21, 22, 23, 24 }, 2);
    race.lap({ 10, 21, 22, 23, 24 }, 3);
    race.finish({ 10, 21, 22, 23, 24 });
    CHECK(host.achievementTier("choke") == 0);
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));

    // THE LEAD LOST on the last lap. The same pair Last Gasp reads, from the
    // rider it was taken from - so the pass lands on nobody here and the choke
    // lands on us.
    race.start();
    race.lap({ 10, 21, 22, 23, 24 }, 1);
    race.lap({ 10, 21, 22, 23, 24 }, 2);
    race.lap({ 21, 10, 22, 23, 24 }, 3);
    race.finish({ 21, 10, 22, 23, 24 });
    CHECK(host.achievementTier("choke") == 1);
    CHECK(host.achievementValue("last_lap_pass") == doctest::Approx(0.0));

    host.shutdown();
}

TEST_CASE("exploration: Consolation Prize is the fastest lap with nothing to show for it") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    RaceRun race{ host };

    // THE FASTEST LAP, AND A PODIUM. Third is a result, so the row stays put.
    race.start();
    race.lap({ 21, 22, 10, 23, 24 }, 1, /*fastest=*/true);
    race.lap({ 21, 22, 10, 23, 24 }, 2);
    race.lap({ 21, 22, 10, 23, 24 }, 3);
    race.finish({ 21, 22, 10, 23, 24 });
    REQUIRE(host.achievementValue("fastest_laps") == doctest::Approx(1.0));
    CHECK(host.achievementTier("consolation") == 0);

    // THE FASTEST LAP, AND FOURTH. Off the podium with the quickest lap of the
    // race in your pocket, which is the whole row.
    race.start();
    race.lap({ 21, 22, 23, 10, 24 }, 1, /*fastest=*/true);
    race.lap({ 21, 22, 23, 10, 24 }, 2);
    race.lap({ 21, 22, 23, 10, 24 }, 3);
    race.finish({ 21, 22, 23, 10, 24 });
    CHECK(host.achievementTier("consolation") == 1);

    host.shutdown();
}

TEST_CASE("exploration: Favorite Spot is three crashes in a ROW at one place, not three anywhere") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);

    // One crash, then one somewhere else, then one back at the first place.
    // Three crashes at two corners: the run never gets past one.
    auto crashAt = [&](float pos) {
        host.raceTrackPosition({ { .num = 10, .trackPos = pos, .crashed = 1 } });
        host.telemetry(10.0f, 3, 0.0f, pos);
        host.raceTrackPosition({ { .num = 10, .trackPos = pos, .crashed = 0 } });
        host.telemetry(10.0f, 3, 0.0f, pos);
    };
    crashAt(0.30f);
    crashAt(0.60f);
    crashAt(0.30f);
    CHECK(host.achievementValue("favorite_spot") == doctest::Approx(1.0));
    CHECK(host.achievementTier("favorite_spot") == 0);

    // Now three running, and the second is a few metres up the road from the
    // first - a hundredth of a lap is the same corner, not the same metre.
    crashAt(0.305f);
    CHECK(host.achievementValue("favorite_spot") == doctest::Approx(2.0));
    crashAt(0.30f);
    CHECK(host.achievementTier("favorite_spot") == 1);

    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("exploration: Digging a Hole is ten UNBROKEN seconds, and traction ends the run") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    enterPractice(host);

    long long t = 1'000'000;
    // A tenth of a second of it: standing still (0.5 m/s) with the rear wheel
    // doing 20. Anything else for a frame is the run over.
    auto frame = [&](float speed, float rearWheel) {
        t += 100'000;
        host.statsSetNowUs(t);
        TelemetryRow r;
        r.speed = speed;
        r.wheelSpeedRear = rearWheel;
        host.telemetryFrame(r);
    };
    // The first frame of a run only takes the reference, so a run of N frames
    // measures N-1 tenths; the counts below carry the spare tenth rather than
    // sitting on a whole second, which floating-point tenths cannot be trusted
    // to land exactly on.
    auto dig = [&](int tenths) { for (int i = 0; i < tenths; ++i) frame(0.5f, 20.0f); };
    // The same wheelspin with the rear wheel OFF THE GROUND.
    auto spinInAir = [&](int tenths) {
        for (int i = 0; i < tenths; ++i) {
            t += 100'000;
            host.statsSetNowUs(t);
            TelemetryRow r;
            r.speed = 0.5f;
            r.wheelSpeedRear = 20.0f;
            r.rearMaterial = 0;          // no contact
            host.telemetryFrame(r);
        }
    };

    // A WHEEL SPINNING IN THE AIR IS NOT DIGGING. Hung up on an obstacle, nosed
    // over, or on a stand: the slip reads exactly the same as a buried rear
    // tyre, and only the contact flag tells them apart. Fifteen seconds of it,
    // which is half again what the row asks for.
    spinInAir(150);
    CHECK(host.achievementValue("digging") == doctest::Approx(0.0));
    CHECK(host.achievementTier("digging") == 0);

    // Nine seconds, a moment of grip, and nine more. Eighteen seconds of it in
    // total and not ten in a row, so the row does not move - and the VALUE
    // shows the best run rather than the sum.
    dig(95);
    frame(0.5f, 0.5f);        // the tyre bites: same place, no slip
    dig(95);
    CHECK(host.achievementValue("digging") == doctest::Approx(9.0));
    CHECK(host.achievementTier("digging") == 0);

    // Riding away is the same break, and so is a wheel spinning while the bike
    // is actually going somewhere - a wheelspinning start is not a hole.
    frame(20.0f, 40.0f);
    dig(55);
    CHECK(host.achievementValue("digging") == doctest::Approx(9.0));

    // And ten seconds without interruption.
    frame(20.0f, 20.0f);
    dig(105);
    CHECK(host.achievementValue("digging") == doctest::Approx(10.0));
    CHECK(host.achievementTier("digging") == 1);

    host.runStop();
    host.runDeinit();
    host.shutdown();
}

// WIRE TO WIRE IS ABOUT LAPS, NOT THE FIRST CORNER. Its test used to pair
// m_ledEveryLap with m_firstLapPosition == 1, which was harmless while that
// field WAS the lap-one position: both said the same thing. Charger moved it to
// the opening SPLIT, and the pair then quietly demanded the holeshot as well -
// in every gate-drop race, because the player always crosses that split. A rider
// second into turn one, ahead by the end of the lap and never headed after, was
// refused a row that reads "Lead every lap of a race".
TEST_CASE("exploration: Wire to Wire counts a lead taken in the first lap, not the first corner") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    for (int n = 21; n <= 24; ++n) host.addEntry(n, "Rider");
    host.runInit(RACE1);
    host.runStart();
    host.raceSessionState(RACE1, 256);
    host.raceSessionState(RACE1, 16);
    // The gate drops with the player SECOND, and second is where they come out
    // of turn one: the opening split reads P2.
    host.classify(RACE1, -5000, order({ 21, 10, 22, 23, 24 }, 0), /*sessionState=*/32);
    host.classify(RACE1, 1000, order({ 21, 10, 22, 23, 24 }, 0), /*sessionState=*/16);
    host.raceSplit(RACE1, 10, 1, 0, 25000);

    // ...and the lead is taken before the end of lap one, then never given up.
    for (int lap = 1; lap <= 3; ++lap) {
        host.classify(RACE1, 90000 * lap, order({ 10, 21, 22, 23, 24 }, lap));
        host.raceLap(RACE1, 10, lap, 90000);
    }
    host.classify(RACE1, 270000, order({ 10, 21, 22, 23, 24 }, 3));
    REQUIRE(host.achievementValue("races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    // Charger still reads the corner it was moved to read: second to first is
    // one place gained, which is the whole point of separating the two.
    CHECK(host.achievementValue("charger") == doctest::Approx(1.0));
    // And the holeshot was somebody else's, so Perfect Race is not owed here.
    CHECK(host.achievementTier("perfect_race") == 0);

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// THE FIRST TIMING LINE IS NOT ALWAYS SPLIT ONE. Splits are numbered from the
// start/finish line, and a grid does not have to sit behind it: on MXB Test
// Track the S/F line is mid-lap, so the first line a rider meets off the gate
// reports splitIndex 1. Holeshot asked for index 0, so on that track nobody
// could earn it - measured in a real session, alongside farm14 (index 0 first)
// and MXB Club (index 0 first, but only after an S/F crossing the API does not
// report at all, which is a separate problem and not this one).
TEST_CASE("exploration: the holeshot is the first line crossed, whatever its index") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    auto order = [&](const std::vector<int>& nums, int laps) {
        std::vector<ClassRow> rows;
        for (int n : nums) rows.push_back({ .num = n, .best = 90000, .laps = laps });
        return rows;
    };
    auto gridStart = [&]() {
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, 3, 0);
        host.addEntry(10, "Alice");
        for (int n = 21; n <= 24; ++n) host.addEntry(n, "Rider");
        host.runInit(RACE1);
        host.runStart();
        host.raceSessionState(RACE1, 256);
        host.raceSessionState(RACE1, 16);
        host.classify(RACE1, -5000, order({ 10, 21, 22, 23, 24 }, 0), /*sessionState=*/32);
        host.classify(RACE1, 1000, order({ 10, 21, 22, 23, 24 }, 0), /*sessionState=*/16);
    };

    // A track whose opening line is split TWO. The player is through it first.
    gridStart();
    host.raceSplit(RACE1, 10, 1, /*splitIndex=*/1, 12177);
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));
    host.runDeinit();
    host.eventDeinit();

    // ...and it is still the FIRST one that counts, not any one: a rival takes
    // the opening line on the next race, so the player's own crossing a moment
    // later is not a holeshot however early its index reads.
    gridStart();
    host.raceSplit(RACE1, 21, 1, /*splitIndex=*/1, 12000);
    host.raceSplit(RACE1, 10, 1, /*splitIndex=*/0, 12100);
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));   // unchanged
    host.runDeinit();
    host.eventDeinit();

    host.shutdown();
}

// A GRID OF ONE. Alone in the lobby every line is yours first and every lap is
// led, so the three rows that read the field are off the table until one
// other rider is entered - which is enough to stop the offline farm without
// asking for a full grid.
TEST_CASE("exploration: Holeshot, Wire to Wire and Perfect Race need another rider on the grid") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    auto perfectRace = [&](const std::vector<int>& grid, int dns = -1) {
        auto rows = [&](int laps) {
            std::vector<ClassRow> r;
            for (int n : grid) r.push_back({ .num = n, .best = 90000, .laps = laps, .state = n == dns ? 1 : 0 });
            return r;
        };
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, 3, 0);
        for (int n : grid) host.addEntry(n, n == 10 ? "Alice" : "Rider");
        host.runInit(RACE1);
        host.runStart();
        host.raceSessionState(RACE1, 256);
        host.raceSessionState(RACE1, 16);
        host.classify(RACE1, -5000, rows(0), /*sessionState=*/32);
        host.classify(RACE1, 1000, rows(0), /*sessionState=*/16);
        host.raceSplit(RACE1, 10, 1, 0, 25000);                 // first over the first line
        for (int lap = 1; lap <= 3; ++lap) {
            host.classify(RACE1, 90000 * lap, rows(lap));
            host.raceLap(RACE1, 10, lap, 90000, lap == 1 ? 2 : 0);   // lap one the race's fastest
        }
        host.classify(RACE1, 270000, rows(3));
        host.runDeinit();
        host.eventDeinit();
    };

    // ALONE: the whole Perfect Race, with nobody to take it from.
    perfectRace({ 10 });
    CHECK(host.achievementValue("races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("solo_race") == doctest::Approx(1.0));
    CHECK(host.achievementValue("holeshots") == doctest::Approx(0.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(0.0));
    CHECK(host.achievementTier("perfect_race") == 0);

    // ONE OTHER RIDER is a grid: the same race, and all three land.
    perfectRace({ 10, 21 });
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));
    CHECK(host.achievementTier("perfect_race") == 1);

    // ONE OTHER RIDER WHO SAT IN THE PITS is not: the classification lists two
    // entries with one marked DNS, and the gate-drop count reads starters, the
    // same count the finish rows read - so the arm and the rows agree that
    // this was riding round alone.
    perfectRace({ 10, 21 }, /*dns=*/21);
    CHECK(host.achievementValue("holeshots") == doctest::Approx(1.0));      // unchanged
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(1.0));   // unchanged

    host.shutdown();
}
