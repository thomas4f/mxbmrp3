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
        CHECK(host.achievementTier("pad_painter") == 1);
        CHECK(host.achievementTier("dress_up") == 1);
        CHECK(host.achievementValue("dress_up") == doctest::Approx(1.0));
        CHECK(host.achievementTier("stylist") == 1);
        CHECK(host.achievementTier("voice_actor") == 0);
        CHECK(host.achievementValue("regular") == doctest::Approx(1.0));
        CHECK(host.achievementValue("version_hopper") == doctest::Approx(1.0));
        CHECK(host.achievementTier("homebrew") == 0);        // a test build is not a home build
        CHECK(host.achievementTier("signed_copy") == 0);
        // Granted silently on load, one card for the three.
        CHECK(host.achievementToastsQueued() == 1u);
        CHECK(host.achievementLastToast() == "3 achievements unlocked|See Settings > Achievements");
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
    CHECK(host.achievementTier("photo_finish") == 1);
    CHECK(host.achievementValue("lapped_field") == doctest::Approx(1.0));
    CHECK(host.achievementValue("wire_to_wire") == doctest::Approx(0.0));   // sixth on lap one

    // Led from the first lap: Wire to Wire.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0);
    host.addEntry(10, "Alice");
    host.addEntry(21, "Rider");
    host.runInit(RACE1);
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
    CHECK(host.achievementValue("photo_finish") == doctest::Approx(1.0));   // a flag, not a count

    // Leaving a race after a lap, before the flag: Rage Quit.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 3, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
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
    host.raceLap(RACE2, 10, 1, 90000);
    host.runDeinit();
    host.session(PRACTICE, 0, 480000);
    host.runInit(PRACTICE);
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
    host.raceLap(RACE1, 10, 1, 95000, /*best=*/1);
    host.raceLap(RACE1, 10, 2, 94000, /*best=*/1);
    CHECK(host.achievementTier("sandbagger") == 0);
    host.raceLap(RACE1, 10, 3, 93000, /*best=*/1);
    CHECK(host.achievementTier("sandbagger") == 1);
    host.classify(RACE1, 300000, { { .num = 10, .best = 93000, .laps = 3, .gap = 0 } });
    host.runDeinit();
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

TEST_CASE("exploration: an earned one-shot's stored flag of 1.0 reads as its threshold") {
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
    CHECK(host.achievementTier("bakers_dozen") == 1);
    CHECK(host.achievementValue("bakers_dozen") == doctest::Approx(13.0));   // "13 / 13", not "1 / 13"
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
    CHECK(host.achievementTier("pad_painter") == 0);
    CHECK(host.achievementTier("stylist") == 0);
    // Dropped in after the start: a pack folder and a stylesheet with a rule.
    _mkdir((kUserDir + "\\gamepads").c_str());
    _mkdir((kUserDir + "\\gamepads\\mypad").c_str());
    _mkdir((kUserDir + "\\web").c_str());
    ini::writeFile(kUserDir + "\\web\\custom.css", ".rider { color: red; }\n");
    host.loadSettings(kSaveWin);                       // RELOAD_CONFIG
    CHECK(host.achievementTier("pad_painter") == 1);
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
    // clock, it does not restart it.
    for (int s = 0; s < 1800; ++s) {
        host.explorationTick(/*spectating=*/true, /*rumbleLive=*/true, /*onTrack=*/true, 480, 3);
    }
    for (int s = 0; s < 60; ++s) {
        host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/false, 480, 3);
    }
    CHECK(host.achievementTier("steady_hands") == 0);
    CHECK(host.achievementValue("steady_hands") == doctest::Approx(1800.0));   // the run so far, in seconds
    for (int s = 0; s < 1800; ++s) {
        host.explorationTick(/*spectating=*/true, /*rumbleLive=*/true, /*onTrack=*/true, 480, 3);
    }
    CHECK(host.achievementValue("armchair") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(host.achievementTier("armchair") == 1);
    CHECK(host.achievementValue("good_vibrations") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(host.achievementTier("frame_perfect") == 1);
    CHECK(host.achievementValue("frame_perfect") == doctest::Approx(480.0));
    CHECK(host.achievementValue("on_air") == doctest::Approx(3.0));
    CHECK(host.achievementTier("steady_hands") == 1);
    host.runStop();
    host.runDeinit();
    auto j = readStats();
    REQUIRE(j.is_object());
    CHECK(j["exploration"].value("spectateHours", 0.0) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(j["exploration"].value("framePerfect", 0.0) == doctest::Approx(480.0));
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
    CHECK(host.achievementValue("air_miles") == doctest::Approx(0.0));
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
        CHECK(host.achievementTier("tyre_kicker") == 4);
        CHECK(host.achievementLastToast() == "Tyre Kicker - Platinum|Try 100% of the HUDs (@BrinkleyPT)");
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

TEST_CASE("exploration: Profile Hopper counts a switch by hand and by auto-switch alike") {
    // One choke point (SettingsManager::switchProfile) so the sidebar, the
    // hotkey and auto-switch all count, and a switch to the active profile
    // (which it rejects) does not.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
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
    CHECK(host.achievementTier("stalker") == 1);
    CHECK(host.achievementLastToast() == "Stalker - Bronze|Track a rider (@turkishmonk)");
    CHECK_FALSE(host.trackRider("Bob"));                  // already tracked: no count
    CHECK(host.achievementValue("stalker") == doctest::Approx(1.0));
    CHECK(host.trackRider("Carol"));
    CHECK(host.achievementValue("stalker") == doctest::Approx(2.0));
    host.shutdown();
}
