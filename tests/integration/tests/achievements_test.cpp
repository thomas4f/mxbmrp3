// ============================================================================
// tests/integration/tests/achievements_test.cpp
// Achievements end to end: the real callbacks feed StatsManager, StatsManager
// feeds AchievementManager, and the result lands in mxbmrp3_stats.json beside
// the numbers it was computed from. Pins the four rules the feature is built on:
//
//   1. A race finish awards its tiers at the record point (not on save), one
//      toast per tier, and the tiers persist through the leave-track flush and
//      come back on the next startup WITHOUT re-toasting.
//   2. UPGRADE: a stats file from before achievements existed (no block) has
//      everything its numbers already satisfy granted silently on load, with
//      ONE summary toast rather than one per tier.
//   3. The toast master gates DISPLAY only: with [Achievements] visible=0 a tier is still
//      earned and stored, and nothing is queued.
//   4. The RELOAD_CONFIG feed (Tinkerer) works from a fresh install, and the
//      widget actually puts the toast on screen on the next draw.
//   5. FMX: a landed backflip whose chain completes reaches the lifetime totals
//      (trick, backflip, airtime, kind) and the "fmx" block of the stats file;
//      the flip toast fires at the chain's completion, not at the landing. A
//      wheelie after it feeds the balance rows (longest hold, back-wheel
//      distance), the best chain, and the second trick kind.
//   6. The race-side extras a finish reads: the weather (Mudder), the grid size
//      (Crowd Surfer), the longest single session (Marathon, a max not a last),
//      and the distinct tracks and bikes (Globetrotter, Collector).
//   7. The lifetime totals behind the rows stay exact whether they were bumped in
//      the clean cache (a gear shift, a crash, the odometer) or recomputed after
//      a lap dirtied it.
//   8. A toast is not consumed while the hide-all-HUDs hotkey keeps the widget
//      off screen: it waits, and shows when the HUDs come back.
//   9. The Stats HUD persists in ONE global [StatsHud] section through the same
//      base-key serializer as every HUD (companion decoupling included), and a
//      file from when it was per-profile loads its base section unchanged.
//  10. Clean Sheet reads the BEST penalty-free run: a penalty ends the run, the
//      best stays; Leap Forward counts a PB beaten by a full second, not any PB;
//      Iron Butt reads the longest session from the file (the session clock is
//      the real one, so its record point is pinned only through persistence).
//  11. The toast's default place: right edge on Speed's, a cell above the Speed
//      and Gear row, never over them -- and a longer toast grows LEFT from that
//      edge (the card is right-anchored, like the centre stack is centre-
//      anchored), so a fixed-left default could not have hugged the corner.
//
// Player = first active RaceAddEntry after EventInit. Self-contained doctest;
// see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile / writeFile
#include "nlohmann/json.hpp"

#include <cstdio>   // std::remove, snprintf
#include <cstdlib>  // std::abs
#include <string>
#include <vector>

static constexpr int RACE1 = 6;

// ONE save directory for every case: run_tests.sh pre-creates
// Z:\tmp\mxbmrp3-tests\<test>\ and nothing creates a sibling, so each case
// starts by deleting the two files the previous one left.
static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\achievements\\";
static const std::string kStatsPath =
    "Z:\\tmp\\mxbmrp3-tests\\achievements\\mxbmrp3\\mxbmrp3_stats.json";
static const std::string kIniPath =
    "Z:\\tmp\\mxbmrp3-tests\\achievements\\mxbmrp3\\mxbmrp3_settings.ini";

static void cleanSaveDir() {
    std::remove(kStatsPath.c_str());
    std::remove(kIniPath.c_str());
    // A styled overlay left by another test's sync would earn Stylist at startup.
    std::remove("plugins\\mxbmrp3_data\\web\\custom.css");
}

namespace {

nlohmann::json readStats(const std::string& path) {
    const std::string txt = ini::readFile(path);
    if (txt.empty()) return nlohmann::json();
    return nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
}

// Drive a race the player (#10) wins from the front, then leave the track (the
// record + flush point). No `best` flag on the laps, so Hot Lapper stays out of
// the picture and the toast count is exact. The track name doubles as the track
// ID: StatsManager keys its records "trackId|bikeName", and the harness's
// default ID is empty, which would fold every track into one.
struct RaceSpec {
    const char* track = "TestTrack";
    const char* bike = "Test 450";
    int riders = 2;          // the player plus riders-1 others
    int laps = 3;
    int conditions = 0;      // 2 = rainy
    int penaltySeconds = 0;  // > 0: the player is handed a penalty before the finish
};

void winRace(PluginHost& host, const RaceSpec& spec) {
    host.eventInit(spec.track, "Alice", 1600.0f, 2, spec.bike, "MX1", /*trackId=*/spec.track);
    host.raceEvent(spec.track);
    host.session(RACE1, spec.laps, /*lengthMs=*/0, /*state=*/16, spec.conditions);
    host.addEntry(10, "Alice", spec.bike);
    std::vector<ClassRow> rows;
    rows.push_back({ .num = 10, .best = 90000, .laps = spec.laps, .gap = 0 });
    for (int i = 1; i < spec.riders; ++i) {
        const int num = 20 + i;
        char name[16];
        snprintf(name, sizeof(name), "Rider %d", num);
        host.addEntry(num, name, spec.bike);
        rows.push_back({ .num = num, .best = 91000, .laps = spec.laps, .gap = 1500 * i });
    }
    host.runInit(RACE1, spec.conditions);
    for (int lap = 1; lap <= spec.laps; ++lap) {
        host.raceLap(RACE1, 10, lap, 90000 + lap * 1000);
    }
    // A penalty is a live race event (cutting, a jump start), so it lands while
    // the race is RUNNING, before the flag. It needs a standing to attach to,
    // so a mid-race classification comes first - one nobody has finished on,
    // else the race is recorded there and the penalty belongs to no race. That
    // is the real ordering: the finish is recorded the moment the field settles.
    if (spec.penaltySeconds > 0) {
        std::vector<ClassRow> midRace = rows;
        for (ClassRow& row : midRace) row.laps = 0;
        host.classify(RACE1, 1000, midRace);
        host.communication(10, 0, /*communication=*/2, spec.penaltySeconds);
    }
    // A classification with the player over the lap count: PluginData marks the
    // rider finished and, with the whole field in, the race is recorded here.
    host.classify(RACE1, 300000, rows);
    host.runDeinit();
    // Leave the track, as the game does between events: without it the next
    // race of the same session type is a continuation of this one (its laps
    // add to this session, and its finish is the one already recorded).
    host.eventDeinit();
}

void winARace(PluginHost& host) { winRace(host, RaceSpec{}); }

}  // namespace

TEST_CASE("achievements: a race win awards tiers, toasts each, persists, and does not re-toast") {
    cleanSaveDir();
    const char* saveWin = kSaveWin;
    const std::string& statsPath = kStatsPath;

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(saveWin);
        REQUIRE_MESSAGE(host.hasAchievements(), "MXBMRP3_Test_Achievement* not exported (test build?)");

        // Fresh file: nothing earned, nothing granted on load, nothing queued.
        CHECK(host.achievementTier("races") == 0);
        CHECK(host.achievementTier("wins") == 0);
        CHECK(host.achievementTier("nope") == -1);
        CHECK(host.achievementToastsQueued() == 0u);

        winARace(host);

        // Bronze in Racer, Winner, Podium Regular and (no crash all session)
        // Rubber Side Down: four tiers, four toasts, in catalogue order.
        CHECK(host.achievementTier("races") == 1);
        CHECK(host.achievementTier("wins") == 1);
        CHECK(host.achievementTier("podiums") == 1);
        CHECK(host.achievementTier("clean_races") == 1);
        CHECK(host.achievementTier("fastest_laps") == 0);
        CHECK(host.achievementValue("laps") == doctest::Approx(3.0));
        CHECK(host.achievementToastsQueued() == 4u);
        CHECK(host.achievementLastToast() == "Rubber Side Down - Bronze|Finish a race without crashing");

        // On disk, keyed by id, beside the numbers it came from.
        auto j = readStats(statsPath);
        REQUIRE(j.is_object());
        REQUIRE(j.contains("achievements"));
        CHECK(j["achievements"]["unlocked"]["races"].value("tier", 0) == 1);
        CHECK(j["achievements"]["unlocked"]["wins"].value("tier", 0) == 1);
        CHECK_FALSE(j["achievements"]["unlocked"]["races"].contains("unlockedAt"));   // no date is collected
        CHECK_FALSE(j["achievements"]["unlocked"].contains("laps"));   // tier 0 rows are not written
        CHECK(j["global"].value("cleanRaceCount", 0) == 1);
        host.shutdown();
    }

    // Next startup: the tiers are back, and nothing was granted on load (the file
    // already carried them), so no summary toast.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(saveWin);
        CHECK(host.achievementTier("races") == 1);
        CHECK(host.achievementTier("clean_races") == 1);
        CHECK(host.achievementToastsQueued() == 0u);
        host.shutdown();
    }
}

TEST_CASE("achievements: a pre-achievements stats file is granted silently with one summary toast") {
    cleanSaveDir();
    const char* saveWin = kSaveWin;
    const std::string& statsPath = kStatsPath;

    // A veteran's file as an older build wrote it: numbers, no achievements
    // block, and a personal best from before pbCount existed (so no counter for
    // it). 10 races take Racer to Silver -- two tiers, ONE achievement -- 3 wins
    // are Winner and Podium Regular, and the stored PB is a floor for the count:
    // four achievements, five tiers. The card counts achievements.
    {
        PluginHost seed(dllPath());   // creates <save>\mxbmrp3\ on a clean tree
        REQUIRE(seed.loaded());
        seed.startup(saveWin);
        seed.shutdown();
    }
    ini::writeFile(statsPath,
        "{ \"version\": 1, \"global\": { \"raceCount\": 10, \"firstPositions\": 3 },\n"
        "  \"trackBike\": { \"TestTrack|Test 450\": { \"personalBest\": { \"lapTime\": 90000 } } } }\n");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    CHECK(host.achievementTier("races") == 2);
    CHECK(host.achievementTier("wins") == 1);
    CHECK(host.achievementTier("podiums") == 1);
    CHECK(host.achievementTier("personal_bests") == 1);
    CHECK(host.achievementValue("personal_bests") == doctest::Approx(1.0));
    CHECK(host.achievementToastsQueued() == 1u);
    CHECK(host.achievementLastToast() == "4 achievements unlocked|See Settings > Achievements");

    // The grant is persisted on the next save like any other tier.
    host.statsSave();
    auto j = readStats(statsPath);
    REQUIRE(j.is_object());
    CHECK(j["achievements"]["unlocked"]["races"].value("tier", 0) == 2);
    CHECK(j["global"].value("pbCount", 0) == 1);   // the backfilled floor is written back
    host.shutdown();
}

TEST_CASE("achievements: Tinkerer fires on a config reload and the widget shows it") {
    cleanSaveDir();
    const char* saveWin = kSaveWin;
    const std::string& statsPath = kStatsPath;

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    CHECK(host.achievementTier("config_reloads") == 0);
    CHECK_FALSE(host.achievementToastShowing());

    host.configReloaded();
    CHECK(host.achievementTier("config_reloads") == 1);
    CHECK(host.achievementValue("config_reloads") == doctest::Approx(1.0));
    CHECK(host.achievementToastsQueued() == 1u);
    CHECK(host.achievementLastToast() == "Tinkerer|Reload the config");   // a one-shot toasts with no metal

    // The widget takes the toast on the next draw and keeps it up (5 s default).
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
    host.runInit(1);
    host.draw();
    CHECK(host.achievementToastShowing());

    // A second reload is a count, not a tier: no new toast.
    host.configReloaded();
    CHECK(host.achievementTier("config_reloads") == 1);
    CHECK(host.achievementToastsQueued() == 1u);

    // The counter survives a save/load: two reloads on disk.
    host.runDeinit();
    auto j = readStats(statsPath);
    REQUIRE(j.is_object());
    CHECK(j["achievements"]["counters"].value("configReloads", 0) == 2);
    CHECK(j["achievements"]["unlocked"]["config_reloads"].value("tier", 0) == 1);
    host.shutdown();
}

TEST_CASE("achievements: [Achievements] visible=0 still earns and stores, but queues nothing") {
    cleanSaveDir();
    const char* saveWin = kSaveWin;
    const std::string& iniPath = kIniPath;

    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(saveWin);
        seed.save();
        seed.shutdown();
    }
    // Turn the master off in the file the next startup loads.
    std::string ini = ini::readFile(iniPath);
    REQUIRE(ini.find("[Achievements]") != std::string::npos);
    const size_t section = ini.find("[Achievements]");
    REQUIRE(section != std::string::npos);
    const size_t key = ini.find("visible=1", section);
    REQUIRE(key != std::string::npos);
    ini.replace(key, 9, "visible=0");
    ini::writeFile(iniPath, ini);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.configReloaded();
    CHECK(host.achievementTier("config_reloads") >= 1);   // earned regardless
    CHECK(host.achievementToastsQueued() == 0u);          // shown never
    host.session(1, 0, 0);
    host.runInit(1);
    host.draw();
    CHECK_FALSE(host.achievementToastShowing());
    host.shutdown();
}

TEST_CASE("achievements: [Achievements] devScale multiplies every number for a test session, and only lingers while on") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.save();
        seed.shutdown();
    }
    // The dev knob, typed in by hand under [Achievements].
    std::string ini = ini::readFile(kIniPath);
    const size_t section = ini.find("[Achievements]\n");
    REQUIRE(section != std::string::npos);
    ini.insert(section + std::string("[Achievements]\n").size(), "devScale=10\n");
    ini::writeFile(kIniPath, ini);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    // Each switch counts as ten, so ten of them earn Profile Hopper (a one-shot
    // at a hundred) and the row reads a hundred. The stored counter is still
    // ten. Tinkerer cannot show this - it is a one-shot at ONE, and a scale has
    // nothing to reach past a bar already cleared by the first event.
    REQUIRE(host.hasExplorationSet());
    host.explorationSet("profileSwitches", 10.0);
    CHECK(host.achievementValue("profile_hopper") == doctest::Approx(100.0));
    CHECK(host.achievementTier("profile_hopper") == 1);
    // Written back while it is on, so an auto-save does not drop it mid-test.
    host.save();
    CHECK(ini::readFile(kIniPath).find("devScale=10") != std::string::npos);
    host.runDeinit();
    auto j = readStats(kStatsPath);
    REQUIRE(j.is_object());
    // The file holds the truth: ten switches, not a hundred. devScale
    // multiplies what the ROW reads, never what is stored.
    CHECK(j["exploration"].value("profileSwitches", 0.0) == doctest::Approx(10.0));
    host.shutdown();
}

// A backflip driven through the real detector, the way fmx_test.cpp does it:
// 100 Hz telemetry on the injectable Fmx clock, 1.5 s airborne at -300 deg/s,
// then a landing held through the grace and the chain window.
TEST_CASE("achievements: a landed backflip feeds the FMX lifetime totals and toasts on chain completion") {
    cleanSaveDir();
    const char* saveWin = kSaveWin;
    const std::string& statsPath = kStatsPath;

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.hasFmx());
    // MX Bikes (the test DLL) has FMX; a game without it would hide these rows,
    // which the tier query reports as an unknown id.
    REQUIRE(host.achievementTier("fmx_backflips") == 0);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(1);
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
    airTicks(150, -300.0f);          // the backflip, 1.5 s in the air
    groundTicks(80);                 // land, hold through the grace: banked into the chain
    // Landed but not yet COMPLETE: nothing lifetime, and no TRICK toast. The
    // jump itself is now measured as a flight (height, distance, airtime)
    // whether or not a trick comes off it, so the queue may already hold a card
    // for that - anchor the counts below to it instead of to zero, or every
    // future row that a jump can move breaks this test rather than the thing it
    // guards.
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(0.0));
    const unsigned flightCards = host.achievementToastsQueued();

    groundTicks(210);                // the chain window expires: the chain completes
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_backflips") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_kinds") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_airtime") > 1.0);     // the 1.5 s jump
    CHECK(host.achievementValue("fmx_airtime") < 2.0);     // ...under Hang Time's first tier
    CHECK(host.achievementValue("fmx_points") > 0.0);
    CHECK(host.achievementTier("fmx_backflips") == 1);      // Flipper Bronze
    CHECK(host.achievementTier("fmx_airtime") == 0);
    CHECK(host.achievementToastsQueued() == flightCards + 1);
    CHECK(host.achievementLastToast() == "Flipper - Bronze|Land a backflip (@Lynds)");
    const double flipChain = host.achievementValue("fmx_chain");
    CHECK(flipChain > 0.0);
    CHECK(host.achievementValue("fmx_points") == doctest::Approx(flipChain));

    // A six-second wheelie at 10 m/s: front wheel up, nose 20 degrees past the
    // 15-degree entry threshold, then the front wheel down ends it and the chain
    // runs out. The balance rows read the trick's own duration and distance.
    auto wheelieTicks = [&](int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0;
            r.pitch = -20.0f;
            tick(r);
        }
    };
    wheelieTicks(600);
    groundTicks(80);
    groundTicks(210);
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(2.0));
    CHECK(host.achievementValue("fmx_kinds") == doctest::Approx(2.0));
    // ~5.5 s of the 6: the detector's own start debounce comes off the front,
    // and the row reads what the detector banked, not the raw hold.
    CHECK(host.achievementValue("fmx_wheelie") > 5.0);
    CHECK(host.achievementValue("fmx_wheelie") < 6.0);
    CHECK(host.achievementValue("fmx_wheelie_km") == doctest::Approx(0.055).epsilon(0.1));
    CHECK(host.achievementTier("fmx_wheelie") == 1);        // Wheelie King Bronze, 5 s
    CHECK(host.achievementValue("fmx_backflips") == doctest::Approx(1.0));
    // The best chain is whichever scored higher; the points are both together.
    const double bestChain = host.achievementValue("fmx_chain");
    CHECK(bestChain >= flipChain);
    CHECK(host.achievementValue("fmx_points") > flipChain);
    CHECK(host.achievementValue("fmx_points") >= bestChain);
    CHECK(host.achievementToastsQueued() == flightCards + 2);
    CHECK(host.achievementLastToast() == "Wheelie King - Bronze|Land a 5.0s wheelie");

    // A frontflip: the same jump the other way round. Its own row, and a third
    // kind for the collector.
    airTicks(150, 300.0f);
    groundTicks(80);
    groundTicks(210);
    CHECK(host.achievementValue("fmx_frontflips") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_backflips") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(3.0));
    CHECK(host.achievementValue("fmx_kinds") == doctest::Approx(3.0));
    CHECK(host.achievementTier("fmx_frontflips") == 1);   // Somersault Bronze
    // TWO toasts here, not one: this flip takes the best chain past 250 as well
    // (Chain Reaction Bronze). An air trick's duration and distance are the
    // FLIGHT's now rather than the part after it classified, so the air bonus
    // sees the whole jump and a chain scores a little higher than it used to.
    CHECK(host.achievementTier("fmx_chain") == 1);
    CHECK(host.achievementToastsQueued() == flightCards + 4);
    // "- Bronze" in the title, which a one-shot's toast does not carry: the row
    // is a ladder now (1 / 3 / 13 / 25, a quarter of Flipper's), so its first
    // flip earns a metal rather than simply Earned.
    CHECK(host.achievementLastToast() == "Somersault - Bronze|Land a frontflip");

    // The leave-track flush writes the block, by name.
    host.runStop();
    host.runDeinit();
    auto j = readStats(statsPath);
    REQUIRE(j.is_object());
    REQUIRE(j.contains("fmx"));
    CHECK(j["fmx"].value("tricksLanded", 0) == 3);
    CHECK(j["fmx"].value("backflips", 0) == 1);
    CHECK(j["fmx"].value("frontflips", 0) == 1);
    CHECK(j["fmx"]["kinds"].size() == 3u);   // sorted by name
    CHECK(j["fmx"]["kinds"][0].get<std::string>() == "Backflip");
    CHECK(j["fmx"]["kinds"][1].get<std::string>() == "Frontflip");
    CHECK(j["fmx"]["kinds"][2].get<std::string>() == "Wheelie");
    CHECK(j["fmx"].value("longestWheelieSec", 0.0) > 5.0);
    CHECK(j["fmx"].value("bestChainScore", 0) == static_cast<int>(host.achievementValue("fmx_chain")));
    CHECK(j["achievements"]["unlocked"]["fmx_backflips"].value("tier", 0) == 1);
    CHECK(j["achievements"]["unlocked"]["fmx_wheelie"].value("tier", 0) == 1);
    host.shutdown();
}

// The named air tricks each keep a lifetime count of their own: a yaw in the
// air with the nose level is a whip, a roll is a scrub. Both persist by name
// beside the flips. (An oppo or a turn down needs the nose past 55 degrees in world
// space on top of the yaw, which this flat telemetry cannot pose; its count
// rides the same sample field and is pinned by the persistence read below.)
TEST_CASE("achievements: whips and scrubs count by kind, and Somersault is a hidden row") {
    cleanSaveDir();
    // Each host in its own block: two loaded in one scope unload in the wrong
    // order at exit and the process never returns.
    {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasFmx());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(1);
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
    auto groundTicks = [&](int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); };
    auto airTicks = [&](int n, float yawVel, float rollVel) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.yawVel = yawVel; r.rollVel = rollVel;
            tick(r);
        }
    };
    groundTicks(20);
    airTicks(150, 60.0f, 0.0f);      // 90 degrees of yaw, nose level: a whip
    groundTicks(80);
    groundTicks(210);                // the chain completes
    CHECK(host.achievementValue("fmx_whips") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_scrubs") == doctest::Approx(0.0));
    airTicks(150, 0.0f, 60.0f);      // 90 degrees of roll: a scrub
    groundTicks(80);
    groundTicks(210);
    CHECK(host.achievementValue("fmx_scrubs") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_whips") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(2.0));
    CHECK(host.achievementValue("fmx_kinds") == doctest::Approx(2.0));
    // Bronze asks ten of either; the rows moved, not the tier. No TRICK card is
    // queued - any card here is the flight the jump was (see the backflip case).
    CHECK(host.achievementTier("fmx_whips") == 0);
    CHECK(host.achievementTier("fmx_scrubs") == 0);

    host.runStop();
    host.runDeinit();
    auto j = readStats(kStatsPath);
    REQUIRE(j.is_object());
    REQUIRE(j.contains("fmx"));
    CHECK(j["fmx"].value("whips", 0) == 1);
    CHECK(j["fmx"].value("scrubs", 0) == 1);
    CHECK(j["fmx"].value("oppos", -1) == 0);
    CHECK(j["fmx"].value("turnDowns", -1) == 0);
    host.shutdown();
    }

    // A file carrying the counts loads them back, and Somersault (hidden now)
    // is granted from it without being listed.
    ini::writeFile(kStatsPath,
        "{ \"version\": 1, \"fmx\": { \"tricksLanded\": 3, \"whips\": 12, \"scrubs\": 2, "
        "\"oppos\": 1, \"turnDowns\": 1, \"frontflips\": 1 } }\n");
    {
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    again.startup(kSaveWin);
    CHECK(again.achievementValue("fmx_whips") == doctest::Approx(12.0));
    CHECK(again.achievementTier("fmx_whips") == 1);      // twelve: Bronze at ten
    CHECK(again.achievementTier("fmx_oppos") == 1);
    CHECK(again.achievementTier("fmx_turn_downs") == 1);
    CHECK(again.achievementTier("fmx_frontflips") == 1);
    again.shutdown();
    }
}

TEST_CASE("achievements: a bike counts once it has been round, not once it is loaded") {
    // SELECTING a bike used to be enough: the per-track-and-bike record is
    // created the moment a context is set, and these rows counted the records.
    // So every bike ever loaded counted as ridden, and every track ever opened
    // counted as visited - which is how a streamer arrived at "apparently I've
    // ridden 328 different tracks". One valid lap is the smallest thing that
    // means you actually rode it, and it is what all three rows want now.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Load a bike, ride nothing: no row moves.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    CHECK(host.achievementValue("bikes") == doctest::Approx(0.0));
    CHECK(host.achievementValue("tracks") == doctest::Approx(0.0));
    CHECK(host.achievementValue("bike_classes") == doctest::Approx(0.0));

    // One lap on it, and all three count it.
    auto lapOn = [&](const char* bike, const char* cls) {
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, bike, cls, "TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, 1, 0);
        host.addEntry(10, "Alice", bike);
        host.runInit(RACE1);
        host.raceLap(RACE1, 10, 1, 90000);
        host.runDeinit();
        host.eventDeinit();
    };
    lapOn("Test 450", "MX1");
    CHECK(host.achievementValue("bikes") == doctest::Approx(1.0));
    CHECK(host.achievementValue("tracks") == doctest::Approx(1.0));
    CHECK(host.achievementValue("bike_classes") == doctest::Approx(1.0));

    // A second class, ridden: Class Act's Bronze.
    lapOn("Test 250", "MX2");
    CHECK(host.achievementValue("bike_classes") == doctest::Approx(2.0));
    CHECK(host.achievementTier("bike_classes") == 1);
    CHECK(host.achievementLastToast() == "Class Act - Bronze|Ride bikes from 2 classes (@Aiden)");

    // A third bike in a class already ridden is a bike, not a class.
    lapOn("Test 125", "MX2");
    CHECK(host.achievementValue("bikes") == doctest::Approx(3.0));
    CHECK(host.achievementValue("bike_classes") == doctest::Approx(2.0));
    // ...and all three were on one track, which is still one track.
    CHECK(host.achievementValue("tracks") == doctest::Approx(1.0));
    host.shutdown();
}

TEST_CASE("achievements: rain, a big grid, the longest session, and distinct tracks and bikes") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // A wet twenty-rider race, three laps, on the first track and bike.
    winRace(host, { .track = "Track A", .bike = "Bike A", .riders = 20, .laps = 3, .conditions = 2 });
    CHECK(host.achievementTier("rain_races") == 1);      // Mudder Bronze
    CHECK(host.achievementTier("big_grids") == 1);       // Crowd Surfer Bronze
    CHECK(host.achievementValue("session_laps") == doctest::Approx(3.0));
    CHECK(host.achievementValue("tracks") == doctest::Approx(1.0));
    CHECK(host.achievementValue("bikes") == doctest::Approx(1.0));

    // A dry two-rider race, two laps, on a second track and bike: the rain and
    // grid counts hold, the longest session is a max rather than a last, and
    // the distinct counts grow.
    winRace(host, { .track = "Track B", .bike = "Bike B", .riders = 2, .laps = 2 });
    CHECK(host.achievementValue("rain_races") == doctest::Approx(1.0));
    CHECK(host.achievementValue("big_grids") == doctest::Approx(1.0));
    CHECK(host.achievementValue("session_laps") == doctest::Approx(3.0));
    CHECK(host.achievementValue("tracks") == doctest::Approx(2.0));
    CHECK(host.achievementValue("bikes") == doctest::Approx(2.0));
    CHECK(host.achievementValue("races") == doctest::Approx(2.0));

    // The first track on the first bike again: nothing new to count.
    winRace(host, { .track = "Track A", .bike = "Bike A", .riders = 2, .laps = 1 });
    CHECK(host.achievementValue("tracks") == doctest::Approx(2.0));
    CHECK(host.achievementValue("bikes") == doctest::Approx(2.0));

    auto j = readStats(kStatsPath);
    REQUIRE(j.is_object());
    CHECK(j["global"].value("rainRaceCount", 0) == 1);
    CHECK(j["global"].value("bigGridRaceCount", 0) == 1);
    CHECK(j["global"].value("maxSessionLaps", 0) == 3);
    host.shutdown();
}

// The totals behind Shift Happens, Skill Issue and Long Hauler are a cache
// that a gear shift, a crash edge and the odometer bump IN PLACE while it is
// clean (so a shift never walks every record), and a lap dirties for a full
// recompute. Both roads must give the same numbers.
TEST_CASE("achievements: Local Hero is the most laps at one track over its bikes, Loyal one bike's own odometer") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Three laps at A on one bike, two at B, then two more at A on another
    // bike: A's laps add up across its bikes (five), and B's do not join them.
    winRace(host, { .track = "Track A", .bike = "Bike A", .laps = 3 });
    CHECK(host.achievementValue("track_laps") == doctest::Approx(3.0));
    winRace(host, { .track = "Track B", .bike = "Bike B", .laps = 2 });
    CHECK(host.achievementValue("track_laps") == doctest::Approx(3.0));
    winRace(host, { .track = "Track A", .bike = "Bike B", .laps = 2 });
    CHECK(host.achievementValue("track_laps") == doctest::Approx(5.0));
    CHECK(host.achievementValue("laps") == doctest::Approx(7.0));

    // 120 m on each of two bikes (the odometer pattern of the totals case
    // above: 24 frames a quarter second apart at 20 m/s, past the 100 m mark
    // that evaluates the continuous metrics): the longest single odometer is
    // one bike's 120 m, while the distance row has both.
    long long t = 1'000'000;
    for (const char* bike : { "Bike A", "Bike B" }) {
        host.eventInit("Track A", "Alice", 1600.0f, 2, bike, "MX1", "Track A");
        host.raceEvent("Track A");
        host.session(1, 0, 480000);
        host.addEntry(10, "Alice", bike);
        host.runInit(1);
        host.runStart();
        host.statsSetNowUs(t);
        for (int i = 0; i < 24; ++i) {
            t += 250'000;
            host.statsSetNowUs(t);
            host.telemetry(20.0f, 3, t / 1.0e6f);
        }
        host.runDeinit();
        host.eventDeinit();
        t += 10'000'000;
    }
    CHECK(host.achievementValue("bike_km") == doctest::Approx(0.12).epsilon(0.05));
    CHECK(host.achievementValue("distance") == doctest::Approx(0.24).epsilon(0.05));
    host.shutdown();
}

TEST_CASE("achievements: lifetime totals stay exact through clean-cache bumps and a recompute") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(1, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(1);
    host.runStart();

    // 24 frames a quarter second apart at 20 m/s: 120 m, past the 100 m mark,
    // and a gear change on every frame after the first: 23 shifts.
    long long t = 1'000'000;
    host.statsSetNowUs(t);
    for (int i = 0; i < 24; ++i) {
        t += 250'000;
        host.statsSetNowUs(t);
        host.telemetry(20.0f, (i % 2) ? 4 : 3, t / 1.0e6f);
    }
    CHECK(host.achievementValue("gear_shifts") == doctest::Approx(23.0));
    CHECK(host.achievementValue("distance") == doctest::Approx(0.12).epsilon(0.05));
    // One crash edge: the crash flag the stats read is the player's track
    // position (RaceTrackPosition), and a crash held over two frames counts
    // once. Gear stays where the loop left it, so no shift rides along.
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 1 } });
    host.telemetry(20.0f, 4, t / 1.0e6f);
    host.telemetry(20.0f, 4, t / 1.0e6f);
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.1f, .crashed = 0 } });
    host.telemetry(20.0f, 4, t / 1.0e6f);
    CHECK(host.achievementValue("crashes") == doctest::Approx(1.0));

    // A lap dirties the cache; the recompute must land on the same numbers.
    host.raceLap(1, 10, 1, 90000);
    CHECK(host.achievementValue("laps") == doctest::Approx(1.0));
    CHECK(host.achievementValue("gear_shifts") == doctest::Approx(23.0));
    CHECK(host.achievementValue("crashes") == doctest::Approx(1.0));
    CHECK(host.achievementValue("distance") == doctest::Approx(0.12).epsilon(0.05));

    // Clean again: four more shifts (from 4th gear: 3, 4, 3, 4) bump in place.
    for (int i = 0; i < 4; ++i) {
        t += 250'000;
        host.statsSetNowUs(t);
        host.telemetry(20.0f, (i % 2) ? 4 : 3, t / 1.0e6f);
    }
    CHECK(host.achievementValue("gear_shifts") == doctest::Approx(27.0));
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("achievements: a toast waits while the hide-all-HUDs hotkey keeps the widget off screen") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
    host.runInit(1);

    host.setHudsEnabled(false);
    host.configReloaded();                         // Tinkerer Bronze, queued
    CHECK(host.achievementToastsQueued() == 1u);
    host.draw();
    CHECK_FALSE(host.achievementToastShowing());   // not taken: its clock has not started

    host.setHudsEnabled(true);
    host.draw();
    CHECK(host.achievementToastShowing());         // shown now, for its full duration
    host.shutdown();
}

TEST_CASE("achievements: Clean Sheet is the best penalty-free run, Leap Forward a PB beaten by a second") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // Three clean finishes: a run of three, and the row reads it.
    winRace(host, RaceSpec{});
    winRace(host, RaceSpec{});
    winRace(host, RaceSpec{});
    CHECK(host.achievementValue("penalty_free") == doctest::Approx(3.0));
    // A finish with a penalty ends the run; the best stays at three and the
    // penalty itself feeds Rule Bender.
    winRace(host, { .penaltySeconds = 5 });
    CHECK(host.achievementValue("penalty_free") == doctest::Approx(3.0));
    CHECK(host.achievementValue("penalties") == doctest::Approx(1.0));
    // A clean finish starts a new run of one; the best is still three.
    winRace(host, RaceSpec{});
    CHECK(host.achievementValue("penalty_free") == doctest::Approx(3.0));
    {
        auto j = readStats(kStatsPath);
        REQUIRE(j.is_object());
        CHECK(j["global"].value("penaltyFreeStreak", 0) == 1);
        CHECK(j["global"].value("bestPenaltyFreeStreak", 0) == 3);
    }

    // PBs: the game flags a lap as a personal best (best=1) and the plugin
    // stores it. The first lap sets one (Personal Best), a lap 0.4 s quicker
    // beats it but is no leap, a lap 1.2 s quicker than THAT is one.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(1, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(1);
    host.raceLap(1, 10, 1, 95000, /*best=*/1);
    host.raceLap(1, 10, 2, 94600, /*best=*/1);
    CHECK(host.achievementValue("personal_bests") == doctest::Approx(2.0));   // both laps were PBs
    CHECK(host.achievementValue("pb_leaps") == doctest::Approx(0.0));
    host.raceLap(1, 10, 3, 93400, /*best=*/1);
    CHECK(host.achievementValue("pb_leaps") == doctest::Approx(1.0));
    CHECK(host.achievementTier("pb_leaps") == 1);
    host.runDeinit();
    {
        auto j = readStats(kStatsPath);
        REQUIRE(j.is_object());
        CHECK(j["global"].value("pbLeaps", 0) == 1);
    }
    host.shutdown();
}

TEST_CASE("achievements: Iron Butt reads the longest DAY from the file and keeps it") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.shutdown();
    }
    // Ninety minutes in a day, as a previous run recorded it. The row moved off
    // the per-SESSION figure precisely because a session ends at a crash, a pit
    // visit or a track exit - so the marathon it means to reward was the thing
    // most likely to reset it.
    ini::writeFile(kStatsPath,
        "{ \"version\": 1, \"exploration\": { \"dayRideHours\": 1.5 } }\n");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementValue("session_time") == doctest::Approx(1.5));
    CHECK(host.achievementTier("session_time") == 1);   // Iron Butt Bronze, one hour
    // A short ride now does not lower the record: the row is a high-water mark.
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 480000);
    host.runInit(1);
    for (int s = 0; s < 30; ++s) {
        host.explorationTick(/*spectating=*/false, /*rumbleLive=*/false, /*onTrack=*/true, 60, 0);
    }
    CHECK(host.achievementValue("session_time") == doctest::Approx(1.5));
    host.runDeinit();
    auto j = readStats(kStatsPath);
    REQUIRE(j.is_object());
    CHECK(j["exploration"].value("dayRideHours", 0.0) == doctest::Approx(1.5));
    host.shutdown();
}

TEST_CASE("achievements: halfway to Gold and Platinum gets one milestone toast, once, not on load") {
    cleanSaveDir();
    // Regular: 7, 30, 100, 365. Silver at thirty; halfway to Gold at
    // sixty-five. Driven by setting the signal outright, because the machinery
    // under test is what the manager does with a value once it moves - the feed
    // itself is exploration_test's business, and no cheap event hook drives a
    // four-tier row.
    //
    // This was Profile Hopper until the mxbmrp3 page went all-one-shot: a row
    // on that page cannot demonstrate a halfway card any more, because it has
    // no Gold to be halfway to. Regular is a four-tier Exploration row that
    // survives that change, which is the only property this case needs.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasExplorationSet());
        host.explorationSet("daysUsed", 7.0);                 // Bronze
        host.explorationSet("daysUsed", 30.0);                // Silver
        host.explorationSet("daysUsed", 64.0);                // just under the midpoint
        CHECK(host.achievementTier("regular") == 2);
        const unsigned before = host.achievementToastsQueued();   // Bronze, Silver
        CHECK(before == 2u);
        host.explorationSet("daysUsed", 65.0);                // the midpoint
        CHECK(host.achievementToastsQueued() == before + 1);
        CHECK(host.achievementLastToast() == "Regular - halfway to Gold|65 / 100");
        host.explorationSet("daysUsed", 66.0);                // no repeat
        CHECK(host.achievementToastsQueued() == before + 1);
        host.statsSave();
        auto j = readStats(kStatsPath);
        REQUIRE(j.is_object());
        CHECK(j["achievements"]["unlocked"]["regular"].value("halfway", 0) == 3);
        host.shutdown();
    }
    // The next startup finds a rider past the midpoint and says nothing; the
    // next move does not repeat it either.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        CHECK(host.achievementToastsQueued() == 0u);
        host.explorationSet("daysUsed", 70.0);
        CHECK(host.achievementToastsQueued() == 0u);
        host.shutdown();
    }
    // No halfway card on the way to Bronze or Silver: twenty from a fresh file
    // is past half of thirty, and only Bronze is toasted.
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasExplorationSet());
        host.explorationSet("daysUsed", 20.0);
        CHECK(host.achievementToastsQueued() == 1u);
        CHECK(host.achievementLastToast() == "Regular - Bronze|Show up on 7 different days");
        host.shutdown();
    }
}

TEST_CASE("achievements: a stored tier above a row's own count is clamped to the row") {
    // A row cut down to a one-shot (Rage Quit was, mid-development) leaves a
    // stored 4 behind; evaluate() only raises, so the load is the one place to
    // pull it back, else the row's tag reads Platinum on a row whose only tier
    // is Earned.
    //
    // The earned-units check below used to corroborate that by reading 1
    // instead of 4. Rage Quit is a MISFORTUNE row now, so it counts toward no
    // summary at all and that figure reads 0 whatever the clamp did - which
    // makes it a check on the exclusion rather than on the clamp. Both are
    // worth having, so both are here, each saying which it is.
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.shutdown();
    }
    ini::writeFile(kStatsPath,
        "{ \"version\": 1, \"achievements\": { \"unlocked\": { \"rage_quit\": { \"tier\": 4, \"halfway\": 4 } } } }\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    CHECK(host.achievementTier("rage_quit") == 1);   // the clamp
    CHECK(host.achievementUnits().first == 0);       // and a misfortune is not progress
    host.shutdown();
}

// ONE SET BEHIND BOTH FIGURES. The tab counts achievements over the counted
// rows; the analytics ach_pct counts TIERS, and used to count them over every
// non-hidden row - so the plugin's own pages and the Tinkering rows sat in a
// completion figure the tab said they were outside of, and the two numbers told
// different stories about the same install. The resolution differs on purpose,
// the set may not.
TEST_CASE("achievements: the tier figure counts the same rows the tab does") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.shutdown();
    }
    // A row from each page that is listed but does not count: the plugin's own
    // and the Tinkering one. Both are ordinary rows a player can see.
    ini::writeFile(kStatsPath,
        "{ \"version\": 1, \"achievements\": { \"unlocked\": {"
        " \"grand_tour\": { \"tier\": 1, \"halfway\": 0 },"
        " \"decorator\": { \"tier\": 1, \"halfway\": 0 } } } }\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.achievementTier("grand_tour") == 1);
    REQUIRE(host.achievementTier("decorator") == 1);
    // Earned by the tab (rows) and by the analytics (tiers): both say none.
    CHECK(host.achievementRows().first == 0);
    CHECK(host.achievementUnits().first == 0);
    // They are not lost, they are the "(+n)" beside the percentage.
    CHECK(host.achievementBonus() == 2);
    host.shutdown();
}

TEST_CASE("achievements: a click on a toast opens the menu on the Achievements tab and ends the card") {
    cleanSaveDir();
    {
        PluginHost seed(dllPath());   // creates <save>\mxbmrp3\ on a clean tree
        REQUIRE(seed.loaded());
        seed.startup(kSaveWin);
        seed.shutdown();
    }
    // A pre-achievements file: the one row it grants (Racer, ten races) is
    // queued on load as its own card, and the widget takes it on the first draw.
    ini::writeFile(kStatsPath, "{ \"version\": 1, \"global\": { \"raceCount\": 10 } }\n");
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    if (!host.hasInjectedMouse() || !host.hasScreenEdges()) { MESSAGE("build without the hooks"); return; }
    host.draw();
    REQUIRE(host.achievementToastShowing());
    REQUIRE_FALSE(host.settingsVisible());
    const PluginHost::ScreenEdges e = host.hudScreenEdges("achievement_widget");
    REQUIRE(e.r > e.l);
    host.clickAt(static_cast<float>(e.l + e.r) * 0.5e-6f, static_cast<float>(e.t + e.b) * 0.5e-6f);
    CHECK(host.settingsVisible());
    CHECK(host.activeTab() == "Achievements");
    CHECK_FALSE(host.achievementToastShowing());
    std::string group;
    CHECK(host.achievementLastToast() == "Racer - Silver|Finish 10 races");
    // The card's own page, by the GROUP it landed on rather than by its index:
    // pages are ENTRIES_PER_PAGE-row chunks of the catalogue, so pinning an absolute number
    // here makes every future row addition a failure in this test instead of
    // in the thing it guards.
    const int cardPage = host.achievementsPage(&group);
    CHECK(group == "Racing");

    // A row's toast opens the page holding that row, wherever the list was
    // left: page through first, then earn Tinkerer (the Tinkering page).
    float nx = 0.0f, ny = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.next", &nx, &ny));
    host.clickAt(nx, ny);
    host.clickAt(nx, ny);
    CHECK(host.achievementsPage(&group) == cardPage + 2);   // two on from Racing's
    host.showSettings(false);
    host.injectMouse(false);
    // The menu's clicks counted the default setup as they happened (Tyre
    // Kicker's first tier, say), and those cards come first: click each away
    // so the next card up is Tinkerer's.
    for (int guard = 0; guard < 8; ++guard) {
        host.draw();
        if (!host.achievementToastShowing()) break;
        const PluginHost::ScreenEdges c = host.hudScreenEdges("achievement_widget");
        host.clickAt(static_cast<float>(c.l + c.r) * 0.5e-6f, static_cast<float>(c.t + c.b) * 0.5e-6f);
        host.injectMouse(false);
        host.showSettings(false);
    }
    host.configReloaded();
    host.draw();
    REQUIRE(host.achievementToastShowing());
    const PluginHost::ScreenEdges t = host.hudScreenEdges("achievement_widget");
    host.clickAt(static_cast<float>(t.l + t.r) * 0.5e-6f, static_cast<float>(t.t + t.b) * 0.5e-6f);
    CHECK(host.settingsVisible());
    host.draw();                                          // the menu lays its page out
    CHECK(host.achievementsPage(&group) > cardPage);
    CHECK(group == "Tinkering");
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("achievements: the toast defaults to the right corner above Speed and Gear, and grows leftward") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    if (!host.hasScreenEdges()) { MESSAGE("build without the hook"); return; }
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
    host.runInit(1);

    // One grid cell, in the hook's 1e6 fixed point; the widgets are laid out on
    // whole cells, so this is the unit their gaps are measured in.
    const int cellW = 5500, cellH = 11000;   // 0.02 * 0.275, 0.02 * 1.1 / 2
    const PluginHost::ScreenEdges speed = host.hudScreenEdges("speed_widget");
    const PluginHost::ScreenEdges gear  = host.hudScreenEdges("gear_widget");
    REQUIRE(speed.r > speed.l);
    REQUIRE(gear.r > gear.l);

    host.configReloaded();   // Tinkerer - Bronze | Reload the config
    host.draw();
    REQUIRE(host.achievementToastShowing());
    const PluginHost::ScreenEdges a = host.hudScreenEdges("achievement_widget");
    REQUIRE(a.r > a.l);
    // Right edge on Speed's, on screen; the bottom one cell above the widget row,
    // never over either of them.
    CHECK(std::abs(a.r - speed.r) <= 2);
    CHECK(a.r <= 1000000);
    CHECK(a.b <= speed.t - cellH + 2);
    CHECK(a.b <= gear.t - cellH + 2);
    CHECK(a.b > speed.t - 2 * cellH);   // and not floating: within a cell of the gap

    // A wider toast keeps the right edge and grows LEFT: Profile Hopper's
    // line ("Switch profiles 100 times") is longer than Tinkerer's
    // ("Reload the config"). The card up is ended by its click (which opens the
    // menu), so the next queued one is taken.
    if (host.hasInjectedMouse() && host.hasExplorationSet()) {
        host.clickAt(static_cast<float>(a.l + a.r) * 0.5e-6f, static_cast<float>(a.t + a.b) * 0.5e-6f);
        host.injectMouse(false);
        REQUIRE_FALSE(host.achievementToastShowing());
        host.explorationSet("profileSwitches", 100.0);
        CHECK(host.achievementTier("profile_hopper") == 1);
        host.draw();
        REQUIRE(host.achievementToastShowing());
        const PluginHost::ScreenEdges b = host.hudScreenEdges("achievement_widget");
        CHECK(std::abs(b.r - a.r) <= 2);
        CHECK(b.l < a.l - cellW);
        CHECK(b.t == a.t);
        CHECK(b.b == a.b);
    }
    host.shutdown();
}

TEST_CASE("achievements: the pager's buttons page through, and an end you cannot pass takes no click") {
    // The pager is two real buttons around "Page x/y" (SettingsLayoutContext::
    // addPager): an end that cannot be pressed has no region, so its tooltip id
    // is absent from the built regions -- which is how this reads the state.
    // Every click below lands on the coordinates read on page one: the pager
    // sits at the foot of a FULL page, so a shorter page must not move it.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    if (!host.hasInjectedMouse()) { MESSAGE("build without the mouse stand-in"); return; }
    host.showSettings(true);
    host.setActiveTab("Achievements");
    host.draw();
    float x = 0.0f, y = 0.0f;
    CHECK_FALSE(host.settingsRegionCenter("pager.prev", &x, &y));   // page one: nothing before it
    REQUIRE(host.settingsRegionCenter("pager.next", &x, &y));
    host.clickAt(x, y);
    CHECK(host.settingsRegionCenter("pager.prev", &x, &y));         // page two: both ends live
    REQUIRE(host.settingsRegionCenter("pager.next", &x, &y));
    host.clickAt(x, y);
    REQUIRE(host.settingsRegionCenter("pager.prev", &x, &y));
    host.clickAt(x, y);
    host.clickAt(x, y);
    CHECK_FALSE(host.settingsRegionCenter("pager.prev", &x, &y));   // back on page one

    // The buttons hold still when the page number gains a digit: the label's
    // slot is sized for the last page's text, so "Page 9/N" and "Page 10/N"
    // put both ends in the same place. Walk to page nine, then step once.
    float nx = 0.0f, ny = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.next", &nx, &ny));
    for (int p = 1; p < 9; ++p) host.clickAt(nx, ny);
    float px9 = 0.0f, py9 = 0.0f, nx9 = 0.0f, ny9 = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.prev", &px9, &py9));
    REQUIRE(host.settingsRegionCenter("pager.next", &nx9, &ny9));
    host.clickAt(nx9, ny9);                                          // page ten
    float px10 = 0.0f, py10 = 0.0f, nx10 = 0.0f, ny10 = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.prev", &px10, &py10));
    CHECK(px10 == doctest::Approx(px9));
    CHECK(py10 == doctest::Approx(py9));
    if (host.settingsRegionCenter("pager.next", &nx10, &ny10)) {    // absent on a last page
        CHECK(nx10 == doctest::Approx(nx9));
        CHECK(ny10 == doctest::Approx(ny9));
    }
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("achievements: a hidden row is the (+n), not a total it is not in") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    const auto fresh = host.achievementUnits();
    const auto freshRows = host.achievementRows();
    CHECK(fresh.first == 0);
    CHECK(fresh.second > 0);
    CHECK(host.achievementBonus() == 0);
    // Night Owl is hidden: a session at three in the morning earns it, and
    // NEITHER figure moves - it is reported as the bonus instead. This used to
    // count in the earned half of a total it was not in, so the tab read past
    // 100% ("50 / 45"); the overshoot was meant as the reward for finding a
    // secret and read as a counting fault instead.
    host.setLocalTime(2026, 6, 16, 3);
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 480000);
    host.runInit(1);
    CHECK(host.achievementTier("night_owl") == 1);
    const auto after = host.achievementUnits();
    CHECK(after.first == fresh.first);              // tiers: unmoved
    CHECK(after.second == fresh.second);
    const auto rows = host.achievementRows();
    CHECK(rows.first == freshRows.first);           // achievements: unmoved
    CHECK(rows.second == freshRows.second);
    CHECK(rows.second < after.second);              // achievements, not tiers
    // ...and it is here instead, which is what the tab draws as "(+1)".
    CHECK(host.achievementBonus() == 1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("achievements: a misfortune moves neither half of the summary") {
    // THE OTHER UNLISTED GROUP, and the one place it parts company with Hidden.
    // Both are out of the TOTAL. A hidden row's earned tier still counts, on
    // top, so finding a secret pushes the figure past 100% - the case above
    // pins that. A Misfortune row counts nowhere: crashing ninety-nine times
    // is not progress, and a figure it could move would let a bad session
    // flatter the same number a good one moves.
    //
    // Driven through Rule Bender, which needs no crash state, so this reads the
    // same on every game that runs the suite. Its bar is a hundred penalties,
    // and a communication each is the cheapest way to the row -- five seconds
    // apiece leaves Time Served (ten minutes) short, so the "(+n)" below is
    // this row alone.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    const auto fresh = host.achievementUnits();
    const auto freshRows = host.achievementRows();

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, 2, 0, 16, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    std::vector<ClassRow> mid;
    mid.push_back({ .num = 10, .best = 90000, .laps = 0, .gap = 0 });
    host.classify(RACE1, 1000, mid);
    for (int i = 0; i < 100; ++i) host.communication(10, 0, /*communication=*/2, /*seconds=*/5);

    // Earned, shown, toasted - and invisible to every summary figure.
    CHECK(host.achievementValue("penalties") == doctest::Approx(100.0));
    CHECK(host.achievementTier("penalties") == 1);
    const auto after = host.achievementUnits();
    const auto afterRows = host.achievementRows();
    CHECK(after.first == fresh.first);
    CHECK(after.second == fresh.second);
    CHECK(afterRows.first == freshRows.first);
    CHECK(afterRows.second == freshRows.second);
    // It is in the "(+n)" though - out of the percentage, not out of sight.
    CHECK(host.achievementBonus() == 1);

    host.runDeinit();
    host.shutdown();
}

// A SWEEP'S NUMBER IS THE OTHER ROWS' TIERS, so every row it counts has to have
// been evaluated before it. evaluate() walked the catalogue once in order, and
// four counted rows sit AFTER the Completion block - so finishing the ladder on
// one of those four left the Sweep, and with it the Prestige button, waiting for
// whatever moved a number next. `steady_hands` is one of the four.
TEST_CASE("achievements: a Sweep sees the row that finishes the ladder, in the same pass") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasAchForceTiers());
    REQUIRE(host.hasExplorationSet());

    // A ladder one row short of finished. Forced straight into the states, which
    // is the only part of this a test may fake: the row left out has to arrive
    // through the real evaluation or there is nothing here to test.
    host.achievementForceTiers(4, "steady_hands");
    REQUIRE(host.achievementTier("steady_hands") == 0);
    REQUIRE(host.achievementTier("all_platinum") == 0);

    // ...and it arrives. ONE evaluation: half an hour without crashing is all
    // that row has, and every Sweep has to see it before it is asked.
    host.explorationSet("steadyHands", 1800.0);
    CHECK(host.achievementTier("steady_hands") == 1);
    CHECK(host.achievementTier("all_bronze") == 1);
    CHECK(host.achievementTier("all_silver") == 1);
    CHECK(host.achievementTier("all_gold") == 1);
    CHECK(host.achievementTier("all_platinum") == 1);

    host.shutdown();
}
