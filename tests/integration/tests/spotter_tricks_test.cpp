// ============================================================================
// tests/integration/tests/spotter_tricks_test.cpp
// The spotter's two opt-in categories end to end: FmxManager and
// AchievementManager reach the cue log through the real callbacks, and the
// gates that keep them quiet hold.
//
// Pinned, for tricks (driven on the simulated Fmx clock, as fmx_test.cpp):
//  1. The shipped defaults: Tricks OFF, Achievements ON (the spotter itself
//     ships off, which is what keeps a fresh install quiet).
//  2. Off, a whole backflip says nothing.
//  3. A plain jump with no rotation is never called: on a motocross track
//     every jump is an "Air" trick.
//  4. A wheelie is called only once it is half done - a short pop is not -
//     and a backflip is called MID-AIR, once, not every frame. Neither call
//     carries the trick's name: only the landing does.
//  5. The landing is called a moment AFTER touchdown, not at it (the hard
//     crash usually comes just after the wheels do), and still inside the
//     grace, before the trick is confirmed.
//  6. Three tricks chained bank as a combo (trick_combo, {combo_count}).
//  7. A crash right at touchdown is a boo with no cheer; one later in the
//     grace follows the cheer - the joke the timing keeps; one between the
//     tricks of a chain is a boo too. No boo names a trick.
//  8. A race session says nothing at all, whatever the switch says.
// And for achievements: a live unlock speaks its own achievement_<id> line,
// only with the Achievements switch on.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

constexpr int PRACTICE = 1;
constexpr int RACE1 = 6;
constexpr unsigned kRacingMask = 0x1F;            // the five racing categories
constexpr unsigned kTricksBit = 1u << 5;          // SpotterPhrase::Category::Tricks
constexpr unsigned kAchievementsBit = 1u << 6;    // ...::Achievements

const char* const kPack =
    "[Cues]\n"
    "trick_started = Called[ {trick}].\n"
    "trick_landed = Landed {trick}.\n"
    "trick_crashed = Crashed[ {trick}].\n"
    "trick_combo = Combo {combo_count}.\n"
    "achievement_config_reloads = Earned {achievement}[, {tier}].\n";

size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}

// The fmx_test.cpp rig: 100 Hz telemetry on the injectable Fmx clock.
struct Rider {
    PluginHost& host;
    long long t = 1'000'000;
    float x = 0.0f;
    void tick(TelemetryRow r) {
        t += 10'000;
        x += r.speed * 0.01f;
        r.posX = x;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    }
    void ground(int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); }
    void air(int n, float pitchVel) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = pitchVel;
            tick(r);
        }
    }
    void crash() { TelemetryRow r; r.crashed = 1; tick(r); }
    // Front wheel up, nose 20 degrees past the 15-degree entry threshold.
    void wheelie(int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0;
            r.pitch = -20.0f;
            tick(r);
        }
    }
};

// A fresh install each time: a stats file left by the last run has Tinkerer
// earned already, and an earned tier is not earned again.
void freshSaveDir(const std::string& saveWin) {
    std::remove((saveWin + "mxbmrp3\\mxbmrp3_stats.json").c_str());
    std::remove((saveWin + "mxbmrp3\\mxbmrp3_settings.ini").c_str());
}

void startRiding(PluginHost& host, int session, const char* dir) {
    REQUIRE(host.loaded());
    freshSaveDir(dir);
    host.startup(dir);
    REQUIRE(host.hasFmx());
    host.spotterInstallPack(kPack);
    host.spotterEnable(true);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(session, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(session);
    host.runStart();
}

}  // namespace

TEST_CASE("spotter tricks: the shipped defaults are Tricks off, Achievements on") {
    const std::string dir = "Z:\\tmp\\mxbmrp3-tests\\spotter-tricks-defaults\\";
    CreateDirectoryA(dir.c_str(), nullptr);   // save() writes into it; nothing else creates it
    freshSaveDir(dir);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(dir.c_str());
    host.save();
    const std::string ini = ini::readFile(dir + "mxbmrp3\\mxbmrp3_settings.ini");
    CHECK(ini.find("cat_tricks=0") != std::string::npos);
    CHECK(ini.find("cat_achievements=1") != std::string::npos);
    host.shutdown();
}

TEST_CASE("spotter tricks: named once half done, landing called a moment after touchdown, combos and crashes") {
    PluginHost host(dllPath());
    startRiding(host, PRACTICE, "Z:\\tmp\\mxbmrp3-tests\\spotter-tricks\\");
    Rider rider{host};
    rider.ground(20);

    // --- 2. Tricks off: a whole backflip, silence.
    host.spotterCategoryMask(kRacingMask);
    rider.air(150, -300.0f);
    rider.ground(300);
    CHECK(host.spotterCueLog().find("backflip") == std::string::npos);

    host.spotterCategoryMask(kRacingMask | kTricksBit);

    // --- 3. A rotation-free jump is plain Air: never named, never called.
    rider.air(100, 0.0f);
    rider.ground(300);
    CHECK(host.spotterCueLog().find("Called") == std::string::npos);
    CHECK(host.spotterCueLog().find("Landed") == std::string::npos);

    // --- 4. A one-second wheelie is a pop, not a trick worth naming...
    rider.wheelie(100);
    rider.ground(300);
    CHECK(host.spotterCueLog().find("Called.") == std::string::npos);
    // ...and one held well past half its bar is called, once.
    rider.wheelie(250);
    CHECK(count(host.spotterCueLog(), "Called.") == 1);
    rider.ground(300);

    // A backflip: called in the air, once - and WITHOUT its name, which a
    // pack's [ {trick}] group proves by dropping.
    rider.air(150, -300.0f);
    {
        const std::string log = host.spotterCueLog();
        CHECK(count(log, "Called.") == 2);
        CHECK(log.find("Called backflip") == std::string::npos);
        CHECK(log.find("Landed backflip.") == std::string::npos);
    }
    // --- 5. Just down: not called yet...
    rider.ground(10);
    CHECK(host.spotterCueLog().find("Landed backflip.") == std::string::npos);
    // ...but half a second in, still inside the 0.75 s grace, it is.
    rider.ground(40);
    CHECK(count(host.spotterCueLog(), "Landed backflip.") == 1);

    // --- 6. Two more in the chain window, then let the chain bank.
    rider.ground(30);
    rider.air(150, -300.0f);
    rider.ground(80);
    rider.air(150, -300.0f);
    rider.ground(300);
    {
        const std::string log = host.spotterCueLog();
        CHECK(count(log, "Landed backflip.") == 3);
        CHECK(count(log, "Combo three.") == 1);
    }

    // --- 7. Down at touchdown: a boo, and no cheer before it.
    rider.air(150, -300.0f);
    rider.ground(10);
    rider.crash();
    rider.ground(20);
    {
        const std::string log = host.spotterCueLog();
        CHECK(count(log, "Landed backflip.") == 3);
        CHECK(count(log, "Crashed.") == 1);   // the boo names no trick
    }
    // Down later in the grace: the boo follows the cheer.
    rider.ground(300);
    rider.air(150, -300.0f);
    rider.ground(60);
    rider.crash();
    rider.ground(20);
    {
        const std::string log = host.spotterCueLog();
        CHECK(count(log, "Landed backflip.") == 4);
        CHECK(count(log, "Crashed.") == 2);
        CHECK(log.rfind("Crashed.") > log.rfind("Landed backflip."));
    }
    // Down between the tricks of a chain: the last one landed fine, so the
    // boo names no trick - the chain is what was lost.
    rider.ground(300);
    rider.air(150, -300.0f);
    rider.ground(90);     // past the 0.75 s grace: the chain window
    rider.crash();
    rider.ground(20);
    {
        const std::string log = host.spotterCueLog();
        CHECK(count(log, "Landed backflip.") == 5);
        CHECK(count(log, "Crashed.") == 3);
    }

    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("spotter tricks: a race session never calls a trick") {
    PluginHost host(dllPath());
    startRiding(host, RACE1, "Z:\\tmp\\mxbmrp3-tests\\spotter-tricks-race\\");
    host.spotterCategoryMask(kRacingMask | kTricksBit);
    Rider rider{host};
    rider.ground(20);
    rider.air(150, -300.0f);
    rider.ground(10);
    rider.crash();
    rider.ground(20);
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Called") == std::string::npos);
    CHECK(log.find("Landed") == std::string::npos);
    CHECK(log.find("Crashed") == std::string::npos);
    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("spotter achievements: a live unlock speaks its own line, only with the switch on") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    freshSaveDir("Z:\\tmp\\mxbmrp3-tests\\spotter-achievements\\");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-achievements\\");
    host.spotterInstallPack(kPack);
    host.spotterEnable(true);

    // Switched off: Tinkerer is earned, and the spotter says nothing.
    host.spotterCategoryMask(kRacingMask);
    host.configReloaded();
    REQUIRE(host.achievementTier("config_reloads") == 1);
    CHECK(host.spotterCueLog().find("Tinkerer") == std::string::npos);
    host.shutdown();

    // A fresh install with the switch on: the row's own line, and a one-shot
    // has no tier, so its bracket drops.
    PluginHost again(dllPath());
    REQUIRE(again.loaded());
    freshSaveDir("Z:\\tmp\\mxbmrp3-tests\\spotter-achievements-on\\");
    again.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-achievements-on\\");
    again.spotterInstallPack(kPack);
    again.spotterEnable(true);
    again.spotterCategoryMask(kRacingMask | kAchievementsBit);
    again.configReloaded();
    CHECK(count(again.spotterCueLog(), "Earned Tinkerer.") == 1);
    // A second reload is a count, not a tier: nothing new to say.
    again.configReloaded();
    CHECK(count(again.spotterCueLog(), "Earned Tinkerer.") == 1);
    again.shutdown();
}
