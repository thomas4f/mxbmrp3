// ============================================================================
// tests/integration/tests/director_home_test.cpp
// "Max shot = Off" — the broadcaster's forced-rotation switch. With it off the
// auto-director stops pacing the field on a timer and only ever cuts for a STORY
// (battle / overtake / incident / ...), returning to the rider the broadcaster
// put on camera once the story ends. This drives a real spectated race through
// the DLL with the director's injectable clock and asserts, end to end:
//
//   1. the home rider is adopted from whoever is spectated, and re-adopted on a
//      later manual pick;
//   2. with rotation OFF and no story running, the camera never leaves them —
//      with the same scenario and rotation ON it does (the negative control that
//      makes 1. mean something);
//   3. a story still takes the camera, and the shot comes HOME when it ends.
//
// All five forcedRotation() gates are covered, each against its own rotation-on
// control: the race variety cut, the lull round-robin, the non-race timing show's
// dip, the rider lock's camera cycle, and pickShot pinning cuts to the plain TV shot
// (Auto / Trackside). Plus the two states that hold a camera WITHOUT cutting - a
// rider lock, and a shot already dipped onto an onboard when the setting changed -
// which evaluate() must put back on the TV shot rather than leave on a bike cam.
//
// Timing note: the director evaluates on standings callbacks and coalesces to
// ~3x/sec, so each step advances the simulated clock by >= 300 ms; the shot cuts
// additionally honour the min-shot floor, set low here so a step crosses it.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int RACE = 6;       // PiBoSo Race1 session enum
constexpr int PRACTICE = 1;   // ...and Practice, which takes the non-race timing-show branch

// The field, in the order the game hands it to SpectateVehicles.
const std::vector<std::pair<int, std::string>> kField = {
    { 10, "Alice" }, { 22, "Bob" }, { 7, "Cara" }, { 3, "Dan" },
};

// Gaps-to-leader with everyone strung out well beyond the 2.5 s battle gap, so
// nothing on track is a story and the director has only its pacing to act on.
std::vector<ClassRow> quietGrid() {
    return {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 90500, .laps = 3, .gap = 6000 },
        { .num = 7,  .best = 91000, .laps = 3, .gap = 12000 },
        { .num = 3,  .best = 91500, .laps = 3, .gap = 18000 },
    };
}

// Same field with P1/P2 nose to tail (1.2 s < the 2.5 s battle gap) — a battle
// group the director is meant to cut to.
std::vector<ClassRow> battleGrid() {
    return {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 90500, .laps = 3, .gap = 1200 },
        { .num = 7,  .best = 91000, .laps = 3, .gap = 12000 },
        { .num = 3,  .best = 91500, .laps = 3, .gap = 18000 },
    };
}

// Put the game's camera on `raceNum`. In-game the engine applies the director's
// spectate request on its next SpectateVehicles callback; headless, nothing does
// that for us — and a stale selection is exactly what the director reads as "the
// caster took control", so every step has to mirror the request back.
void selectRider(PluginHost& host, int raceNum) {
    int idx = 0;
    for (size_t i = 0; i < kField.size(); ++i)
        if (kField[i].first == raceNum) idx = static_cast<int>(i);
    host.spectateVehicles(kField, idx);
}

// Stand up a spectated session with the director enabled, the camera on `homeNum`,
// and the given pacing (maxSec = 0 -> forced rotation off). `session` picks the
// branch under test: RACE runs the scored-story ladder, PRACTICE the non-race
// timing show. Returns the sim time of the last step.
long long openBroadcast(PluginHost& host, int homeNum, int minSec, int maxSec,
                        int session = RACE) {
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(session, /*numLaps=*/10, /*lengthMs=*/0);
    for (const auto& r : kField) host.addEntry(r.first, r.second.c_str());
    host.draw();                     // view state 1 = spectate, so the director directs
    selectRider(host, homeNum);      // the broadcaster's own pick, before enabling
    host.directorSetEnabled(true);
    host.directorSetShotSec(minSec, maxSec);

    const long long t = 1000;
    host.directorSetNowMs(t);
    host.classify(session, 200000, quietGrid());
    return t;
}

// One decision step: advance the clock, push standings, then mirror the
// director's (possibly new) subject back as the game's camera selection.
void step(PluginHost& host, long long t, const std::vector<ClassRow>& grid,
          int session = RACE) {
    host.directorSetNowMs(t);
    host.classify(session, 200000 + t, grid);
    selectRider(host, host.directorSubject());
}

// The camera role the director last requested, by name ("Auto" / "Trackside" /
// "Front Fender" / ...; "-" before the first cut). Unlike the advisory's subject
// this is NOT blanked while the director is paused or locked, which is exactly
// the state the rider-lock case below has to observe.
std::string cameraName(PluginHost& host) {
    const auto d = host.snapshot();
    return d.value("director", nlohmann::json::object()).value("camera", std::string("?"));
}

// Is `name` one of the onboard rig cams (anything that isn't the TV shot)?
bool isOnboard(const std::string& name) {
    return name != "Auto" && name != "Trackside" && name != "-" && name != "?";
}

}  // namespace

TEST_CASE("director home: Max shot Off holds the broadcaster's rider through a quiet race") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home\\");

    // #7 runs a lonely P3 — not the leader, so if the director fell back to its
    // usual leader baseline it would cut to #10 the moment the min shot elapsed.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/0);
    CHECK(host.directorHomeSubject() == 7);   // adopted from the spectated rider
    CHECK(host.directorSubject() == 7);

    // 20 s of quiet racing — well past any max shot the director could apply.
    for (int i = 0; i < 40; ++i) {
        t += 500;
        step(host, t, quietGrid());
    }
    CHECK(host.directorSubject() == 7);       // never wandered off

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: the same quiet race DOES rotate with Max shot on (control)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_control\\");

    // Identical scenario, only the setting differs: a 5 s max shot restores the
    // timer-driven pacing, so the camera must leave #7 on its own.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5);
    CHECK(host.directorHomeSubject() == 7);   // still recorded — just not honoured

    bool moved = false;
    for (int i = 0; i < 40 && !moved; ++i) {
        t += 500;
        step(host, t, quietGrid());
        if (host.directorSubject() != 7) moved = true;
    }
    CHECK(moved);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: a story still takes the camera, and the shot comes home after") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_story\\");

    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/0);
    REQUIRE(host.directorSubject() == 7);

    // P1/P2 close up into a battle: rotation being off must not make the director
    // inert — a story still outranks the home rider and wins the camera.
    bool cutToStory = false;
    for (int i = 0; i < 20 && !cutToStory; ++i) {
        t += 500;
        step(host, t, battleGrid());
        if (host.directorSubject() == 10) cutToStory = true;
    }
    CHECK(cutToStory);

    // The battle dissolves. With no story left the floor is the broadcaster's
    // rider, so the camera returns to #7 rather than settling on the leader.
    bool cameHome = false;
    for (int i = 0; i < 20 && !cameHome; ++i) {
        t += 500;
        step(host, t, quietGrid());
        if (host.directorSubject() == 7) cameHome = true;
    }
    CHECK(cameHome);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: a manual pick re-homes the broadcast") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_repick\\");

    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/0);
    REQUIRE(host.directorHomeSubject() == 7);

    // The caster switches the camera to #3 by hand. The director adopts it, yields
    // for its manual grace (6 s), and from then on #3 is the rider it returns to.
    t += 500;
    host.directorSetNowMs(t);
    selectRider(host, 3);
    host.classify(RACE, 200000 + t, quietGrid());
    CHECK(host.directorHomeSubject() == 3);

    // Ride out the grace, then a battle and back to quiet: the return lands on #3.
    for (int i = 0; i < 20; ++i) { t += 500; step(host, t, quietGrid()); }
    CHECK(host.directorSubject() == 3);

    bool cutToStory = false;
    for (int i = 0; i < 20 && !cutToStory; ++i) {
        t += 500;
        step(host, t, battleGrid());
        if (host.directorSubject() == 10) cutToStory = true;
    }
    REQUIRE(cutToStory);

    bool cameHome = false;
    for (int i = 0; i < 20 && !cameHome; ++i) {
        t += 500;
        step(host, t, quietGrid());
        if (host.directorSubject() == 3) cameHome = true;
    }
    CHECK(cameHome);

    host.directorSetNowMs(-1);
    host.shutdown();
}

// --- The other two timer-driven paths "Max shot = Off" has to reach. The cases
// above cover the race variety cut and the lull round-robin; these cover the
// non-race dip (runNonRaceShow) and the rider lock's camera cycle, so all four
// forcedRotation() gates are exercised rather than just the two in a race. ---

TEST_CASE("director home: the non-race timing show holds the broadcaster's rider too") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_practice\\");

    // Practice: rank is by best lap, so the "leader" is the pace-setter (#10) and
    // #7 is a slower rider the show would normally never sit on.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/0, PRACTICE);
    REQUIRE(host.directorHomeSubject() == 7);

    for (int i = 0; i < 40; ++i) {
        t += 500;
        step(host, t, quietGrid(), PRACTICE);
    }
    CHECK(host.directorSubject() == 7);   // no pace-setter pull, no variety dip

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: the non-race show DOES leave with Max shot on (control)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_practice_control\\");

    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5, PRACTICE);

    bool moved = false;
    for (int i = 0; i < 40 && !moved; ++i) {
        t += 500;
        step(host, t, quietGrid(), PRACTICE);
        if (host.directorSubject() != 7) moved = true;
    }
    CHECK(moved);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: Max shot Off puts a LOCKED shot back on the TV camera") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_lock\\");

    // Rotation ON first, and hold until the variety cadence has actually dipped the
    // camera onto an onboard — that dip is the state under test, so a run that never
    // reached one would make the assertion below vacuous.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5);
    for (int i = 0; i < 60 && !isOnboard(cameraName(host)); ++i) {
        t += 500;
        step(host, t, ((i / 6) % 2) ? battleGrid() : quietGrid());
    }
    REQUIRE(isOnboard(cameraName(host)));

    // Lock the rider (a lock makes no further cuts, so nothing would run pickShot),
    // then switch the max shot off. "Off = TV cameras only" has to hold anyway: the
    // camera must come off the onboard without waiting for a cut that never comes.
    host.directorToggleLock();
    REQUIRE(host.directorIsLocked());
    host.directorSetShotSec(2, 0);

    for (int i = 0; i < 10; ++i) {
        t += 500;
        step(host, t, quietGrid());
    }
    CHECK_FALSE(isOnboard(cameraName(host)));
    CHECK(cameraName(host) == "Auto");
    // ...and having landed there it stays put — no timer-driven angle cycling.
    const std::string settled = cameraName(host);
    for (int i = 0; i < 40; ++i) {
        t += 500;
        step(host, t, quietGrid());
    }
    CHECK(cameraName(host) == settled);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: switching Max shot to Off recovers an UNLOCKED onboard shot") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_recover\\");

    // Same end state without a lock. Note this case asserts the user-visible PROPERTY
    // ("with Off the camera is not on a bike cam"), not one mechanism: unlocked, a later
    // story cut may reach pickShot and pin it there anyway. The evaluate() correction is
    // what covers the case where no such cut ever comes, and the locked case above is
    // what pins it (verified failing with the correction disabled).
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5);
    for (int i = 0; i < 60 && !isOnboard(cameraName(host)); ++i) {
        t += 500;
        step(host, t, ((i / 6) % 2) ? battleGrid() : quietGrid());
    }
    REQUIRE(isOnboard(cameraName(host)));

    host.directorSetShotSec(2, 0);
    for (int i = 0; i < 10; ++i) {
        t += 500;
        step(host, t, quietGrid());
    }
    CHECK_FALSE(isOnboard(cameraName(host)));

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: the rider lock DOES cycle its camera with Max shot on (control)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_lock_control\\");

    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5);
    for (int i = 0; i < 20 && cameraName(host) == "-"; ++i) {
        t += 500;
        step(host, t, quietGrid());
    }
    REQUIRE(cameraName(host) != "-");

    host.directorToggleLock();
    REQUIRE(host.directorIsLocked());
    const std::string held = cameraName(host);

    bool rotated = false;
    for (int i = 0; i < 40 && !rotated; ++i) {
        t += 500;
        step(host, t, quietGrid());
        if (cameraName(host) != held) rotated = true;
    }
    CHECK(rotated);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: an out-of-range max shot clamps INTO the range, not to Off") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_clamp\\");

    // Only an explicit 0 means Off. Hand-editing the INI is a supported workflow and
    // its load path is this same setter, so a below-range `maxShotSec=3` has to read
    // as "cut fast" (clamped up to the 5 s floor) and NOT as its inverse, "never
    // rotate" — which is what collapsing sub-range values to Off would have meant.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/3);

    bool moved = false;
    for (int i = 0; i < 40 && !moved; ++i) {
        t += 500;
        step(host, t, quietGrid());
        if (host.directorSubject() != 7) moved = true;
    }
    CHECK(moved);   // rotation still on

    host.directorSetNowMs(-1);
    host.shutdown();
}

// --- Camera rule: a shot the caster chose stays on the plain TV camera. ---

namespace {

// Alternate battle / quiet blocks so the director keeps cutting — story out, home
// again — and collect every camera it lands on. That churn is what gives the
// variety cadence ("every Nth cut") chances to fire.
std::vector<std::string> camerasOverBroadcast(PluginHost& host, long long t) {
    std::vector<std::string> seen;
    for (int i = 0; i < 60; ++i) {
        t += 500;
        step(host, t, ((i / 6) % 2) ? battleGrid() : quietGrid());
        std::string c = cameraName(host);
        if (c != "-" && (seen.empty() || seen.back() != c)) seen.push_back(c);
    }
    return seen;
}

}  // namespace

TEST_CASE("director home: Max shot Off keeps every shot on Auto/Trackside") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_cams\\");

    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/0);
    const auto cams = camerasOverBroadcast(host, t);

    REQUIRE_FALSE(cams.empty());          // the director did cut, so this isn't vacuous
    int onboards = 0;
    for (const auto& c : cams) if (isOnboard(c)) ++onboards;
    CHECK(onboards == 0);                 // no helmet/fender dip on the caster's show

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director home: the same broadcast DOES dip to onboards with Max shot on (control)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_home_cams_control\\");

    // Same churn, same default "Onboard every" cadence — only the max shot differs.
    long long t = openBroadcast(host, /*homeNum=*/7, /*minSec=*/2, /*maxSec=*/5);
    const auto cams = camerasOverBroadcast(host, t);

    int onboards = 0;
    for (const auto& c : cams) if (isOnboard(c)) ++onboards;
    CHECK(onboards > 0);

    host.directorSetNowMs(-1);
    host.shutdown();
}
