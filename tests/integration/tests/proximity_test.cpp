// ============================================================================
// tests/integration/tests/proximity_test.cpp
// Roost end to end: real RaceTrackPosition batches through PluginData's
// geometry pass into StatsManager's accumulation. Side by Side was a row here
// too until it was cut; the classifier still separates the two bands, and the
// alongside case below is what holds it to that.
//
// The unit layer (tests/unit/test_roost_detect.cpp) pins the pairwise
// geometry. What can only be checked here is the wiring around it: that the
// seconds come off the REAL position callback rather than a HUD or the
// spotter, that they are gated to a race actually in progress, that a crash
// ends the interval instead of pausing it, and that the ~1s flush hands the
// total to the exploration sums without losing the remainder.
//
// The clock is injected (MXBMRP3_Test_StatsSetNowUs) and stepped a fixed
// amount per batch, the same technique odometer_test.cpp uses: without it a
// headless test firing callbacks back to back measures ~nothing.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cstdio>   // std::remove

static constexpr int RACE1 = 6;
static constexpr int PRACTICE = 1;
static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\proximity\\";
// raceEvent() is what sets the track length PluginData converts centreline
// positions with, and it reports 1600 m. Placing riders against anything else
// puts them somewhere other than where the test says they are.
static constexpr float kLap = 1600.0f;
static const std::string kStatsPath =
    "Z:\\tmp\\mxbmrp3-tests\\proximity\\mxbmrp3\\mxbmrp3_stats.json";

namespace {

// Every case starts from zero seconds: these are lifetime SUMS, so one case's
// racing would otherwise be the next one's starting total.
void cleanSaveDir() { std::remove(kStatsPath.c_str()); }

// The player at the origin, and one rival placed relative to them.
std::vector<TrackRow> pair(float rivalAlongM, float rivalLateralM = 0.0f, int playerCrashed = 0,
                           int rivalCrashed = 0) {
    return {
        { .num = 10, .trackPos = 0.5f, .crashed = playerCrashed, .posX = 0.0f, .posZ = 0.0f },
        { .num = 21, .trackPos = 0.5f + rivalAlongM / kLap, .crashed = rivalCrashed,
          .posX = rivalLateralM, .posZ = rivalAlongM },
    };
}

struct Session {
    PluginHost& host;
    long long t = 1'000'000;
    // 100ms per batch, so ten batches is a second and the flush is exact. The
    // telemetry frame is what says the bike is MOVING - the row says Ride, and
    // a position batch carries no speed (see StatsManager::recordProximity).
    void batch(const std::vector<TrackRow>& rows, float speedMs = 20.0f) {
        t += 100'000;
        host.statsSetNowUs(t);
        TelemetryRow r;
        r.speed = speedMs;
        host.telemetryFrame(r);
        host.raceTrackPosition(rows);
    }
    // riding=false is the replay and the pit wall: the same session and the
    // same batches, but no RunStart, so the player is not out there.
    void start(int session, int state = 16, bool riding = true) {
        host.eventInit("TestTrack", "Alice", kLap, 2, "Test 450", "MX1", "TestTrack");
        host.raceEvent("TestTrack");
        host.session(session, 3, 0, state);
        host.addEntry(10, "Alice");
        host.addEntry(21, "Rider");
        host.runInit(session);
        if (riding) host.runStart();
        host.classify(session, 1000, { { .num = 10, .laps = 0 }, { .num = 21, .laps = 0 } },
                      /*sessionState=*/state);
    }
    void finish() { host.runStop(); host.runDeinit(); host.eventDeinit(); }
};

}  // namespace

TEST_CASE("proximity: seconds behind a rider are roost, seconds beside one are not") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    // The first batch only takes a reference; the next eleven are 100ms each,
    // so 1.1s of measured time passes and the ~1s flush lands with all of it.
    for (int i = 0; i < 12; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(1.1).epsilon(0.02));

    // Pull ALONGSIDE and roost stops, rather than continuing to count the same
    // rider. Side by Side had a row of its own once and no longer does, but the
    // classifier still has to tell the two apart -- this is the assertion that
    // says so now that there is no second number to read it off.
    const double roostSoFar = host.achievementValue("roost");
    for (int i = 0; i < 11; ++i) s.batch(pair(1.0f, 1.5f));
    CHECK(host.achievementValue("roost") == doctest::Approx(roostSoFar));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: a crash ends the interval, it does not pause it") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    for (int i = 0; i < 12; ++i) s.batch(pair(8.0f));
    const double before = host.achievementValue("roost");
    REQUIRE(before > 0.5);

    // Down for a while, then back up. The time on the floor must not be
    // credited when measurement resumes - which is what a paused clock, rather
    // than an ended interval, would do.
    for (int i = 0; i < 30; ++i) s.batch(pair(8.0f, 0.0f, /*playerCrashed=*/1));
    // Twelve again: one to re-reference after the ended interval, eleven of
    // racing. The three seconds on the floor are simply gone.
    for (int i = 0; i < 12; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(before + 1.1).epsilon(0.05));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: a rider on the floor is scenery, not company") {
    // Roost is seconds spent behind someone THROWING it. A rider lying in the
    // landing is throwing nothing, and riding past them was paying out at the
    // full rate because only the player's own crashed flag was read.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    // Ten seconds sat 8 m behind a rival who is down: squarely inside the roost
    // band on every other measure, and worth nothing.
    for (int i = 0; i < 100; ++i) s.batch(pair(8.0f, 0.0f, /*playerCrashed=*/0, /*rivalCrashed=*/1));
    s.batch(pair(8.0f, 0.0f, 0, 1));
    CHECK(host.achievementValue("roost") == doctest::Approx(0.0));

    // The same ten seconds once they are up and riding again pays in full, so
    // the zero above is the crash and not the geometry.
    for (int i = 0; i < 100; ++i) s.batch(pair(8.0f));
    s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(10.0).epsilon(0.15));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: any session in progress counts, and a bike standing still does not") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };

    // PRACTICE COUNTS. Sitting behind someone learning a line is the same
    // riding the row is about, and its sentence never said race - the page it
    // sits on was the only thing that implied it.
    s.start(PRACTICE);
    for (int i = 0; i < 12; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(1.1).epsilon(0.02));
    const double afterPractice = host.achievementValue("roost");

    // ...and a bike that is not moving is not riding. Two of them parked a few
    // metres apart on track used to bank this for as long as they sat there.
    for (int i = 0; i < 30; ++i) s.batch(pair(8.0f), /*speedMs=*/0.0f);
    CHECK(host.achievementValue("roost") == doctest::Approx(afterPractice));
    s.finish();

    // A race that has not gone green: the whole grid is nose to tail on the
    // gate doing nothing, which would otherwise be the easiest roost time in
    // the game.
    s.start(RACE1, /*state=*/32);
    for (int i = 0; i < 30; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(afterPractice));
    s.finish();

    host.shutdown();
}

TEST_CASE("proximity: alone on track, and nobody near, count for nothing") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    for (int i = 0; i < 30; ++i) {
        s.batch({ { .num = 10, .trackPos = 0.5f, .posX = 0.0f, .posZ = 0.0f } });
    }
    CHECK(host.achievementValue("roost") == doctest::Approx(0.0));

    // A rival on track but half a lap away.
    for (int i = 0; i < 30; ++i) s.batch(pair(500.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(0.0));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: a race watched rather than ridden earns nothing") {
    // A loaded replay and a race watched from the pits deliver the same
    // in-progress race batches, with the player's own bike among them - so the
    // geometry says roost and only RunStart says whether the player is the one
    // riding it. Without that gate a race could be banked twice: once live,
    // once by watching the replay of it.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1, /*state=*/16, /*riding=*/false);

    // Three seconds sat squarely in the roost band, none of them ridden.
    for (int i = 0; i < 30; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(0.0));

    // Out on track, same geometry: pays in full, so the zero above is the gate
    // and not the placement. One batch to re-reference, eleven of racing.
    host.runStart();
    for (int i = 0; i < 12; ++i) s.batch(pair(8.0f));
    CHECK(host.achievementValue("roost") == doctest::Approx(1.1).epsilon(0.02));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: Pile-Up counts the riders down AROUND you, while you are down too") {
    // The one branch of the geometry pass that measures anything while the
    // player is on the floor: the flatten drops a crashed player and stops, so
    // this count has to happen on the way out of it.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    // The player down at the origin, with the others placed around them.
    auto heap = [&](int playerCrashed, const std::vector<std::pair<float, int>>& others) {
        std::vector<TrackRow> rows{
            { .num = 10, .trackPos = 0.5f, .crashed = playerCrashed, .posX = 0.0f, .posZ = 0.0f }
        };
        int num = 21;
        for (const auto& o : others) {
            rows.push_back({ .num = num++, .trackPos = 0.5f + o.first / kLap,
                             .crashed = o.second, .posX = 0.0f, .posZ = o.first });
        }
        return rows;
    };

    // THREE OTHERS DOWN, AND THE PLAYER UPRIGHT: riding past a heap is not
    // being in one. (It is also the roost pass's own rule - a rider on the
    // floor is scenery.) It IS Peace Out, which is the point of the pair.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, { { 3.0f, 1 }, { 5.0f, 1 }, { 7.0f, 1 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(0.0));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(3.0));

    // THE PLAYER DOWN, AND THE OTHERS UPRIGHT: a crash on your own is a crash
    // on your own, however close the racing was.
    for (int i = 0; i < 5; ++i) s.batch(heap(1, { { 3.0f, 0 }, { 5.0f, 0 }, { 7.0f, 0 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(0.0));

    // EVERYONE DOWN, BUT UP THE ROAD. Same incident needs to be the same
    // place: fifty metres away is the next corner's crash, not this one.
    for (int i = 0; i < 5; ++i) s.batch(heap(1, { { 50.0f, 1 }, { 60.0f, 1 }, { 70.0f, 1 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(0.0));

    // TWO DOWN WITHIN THE HEAP and one well clear of it: the row counts what is
    // actually around you, so this is a two and not a three.
    for (int i = 0; i < 5; ++i) s.batch(heap(1, { { 3.0f, 1 }, { 7.0f, 1 }, { 50.0f, 1 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(2.0));
    CHECK(host.achievementTier("pile_up") == 0);

    // ...and the third joins it.
    for (int i = 0; i < 5; ++i) s.batch(heap(1, { { 3.0f, 1 }, { 7.0f, 1 }, { 9.0f, 1 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(3.0));
    CHECK(host.achievementTier("pile_up") == 1);

    // A lifetime WORST, so a tidier crash afterwards does not walk it back.
    for (int i = 0; i < 5; ++i) s.batch(heap(1, { { 3.0f, 1 }, { 50.0f, 0 }, { 60.0f, 0 } }));
    CHECK(host.achievementValue("pile_up") == doctest::Approx(3.0));

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: Peace Out is the heap ridden PAST, and parking beside it is not") {
    // Pile-Up's mirror, off the same count: upright, moving, and three riders
    // down around you. Each near miss is asserted before the hit, because the
    // row is a lifetime worst-case maximum and would swallow them afterwards.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    auto heap = [&](int playerCrashed, const std::vector<std::pair<float, int>>& others) {
        std::vector<TrackRow> rows{
            { .num = 10, .trackPos = 0.5f, .crashed = playerCrashed, .posX = 0.0f, .posZ = 0.0f }
        };
        int num = 21;
        for (const auto& o : others) {
            rows.push_back({ .num = num++, .trackPos = 0.5f + o.first / kLap,
                             .crashed = o.second, .posX = 0.0f, .posZ = o.first });
        }
        return rows;
    };
    const std::vector<std::pair<float, int>> closeHeap{ { 3.0f, 1 }, { 5.0f, 1 }, { 7.0f, 1 } };

    // A HEAP UP THE ROAD. Three down fifty metres away is a corner you have not
    // reached, not one you rode through.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, { { 50.0f, 1 }, { 60.0f, 1 }, { 70.0f, 1 } }));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(0.0));

    // PARKED BESIDE IT. Same three riders, right there, and the bike stopped:
    // "ride through" is the row, and this is watching.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, closeHeap), /*speedMs=*/0.0f);
    CHECK(host.achievementValue("peace_out") == doctest::Approx(0.0));

    // PADDLING THROUGH IT. Moving, but at walking pace - which the roost rows'
    // "not parked" floor would wave through, and which is why this row does not
    // use that floor. A rider dabbing their way past a heap has not ridden
    // through one.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, closeHeap), /*speedMs=*/1.0f);
    CHECK(host.achievementValue("peace_out") == doctest::Approx(0.0));

    // (Going down in it yourself belongs to the test below, not here: it arms a
    // guard that would silently swallow both cases after it, which is how this
    // step was written the first time and what made the pair below read zero.)

    // TWO DOWN, RIDDEN PAST: counted, and short of the row.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, { { 3.0f, 1 }, { 7.0f, 1 }, { 50.0f, 1 } }));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(2.0));
    CHECK(host.achievementTier("peace_out") == 0);

    // ...and the third, upright and moving through it.
    for (int i = 0; i < 5; ++i) s.batch(heap(0, closeHeap));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(3.0));
    CHECK(host.achievementTier("peace_out") == 1);

    s.finish();
    host.shutdown();
}

TEST_CASE("proximity: climbing out of your OWN pile-up is not riding through one") {
    // The first-corner case, and the one the pair's exclusivity turns on: go
    // down in the heap, remount while the others are still on the floor, ride
    // off. Without a guard the SAME incident pays Pile-Up and Peace Out both.
    // Its own case rather than a step in the one above, because Peace Out is a
    // lifetime maximum - once earned honestly it can never read zero again, so
    // a shared case could not tell the guard working from the guard missing.
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    Session s{ host };
    s.start(RACE1);

    auto heap = [&](int playerCrashed) {
        return std::vector<TrackRow>{
            { .num = 10, .trackPos = 0.5f, .crashed = playerCrashed, .posX = 0.0f, .posZ = 0.0f },
            { .num = 21, .trackPos = 0.5f + 3.0f / kLap, .crashed = 1, .posX = 0.0f, .posZ = 3.0f },
            { .num = 22, .trackPos = 0.5f + 5.0f / kLap, .crashed = 1, .posX = 0.0f, .posZ = 5.0f },
            { .num = 23, .trackPos = 0.5f + 7.0f / kLap, .crashed = 1, .posX = 0.0f, .posZ = 7.0f },
        };
    };

    // Down in it, with three others. Pile-Up's row, and not this one.
    for (int i = 0; i < 5; ++i) s.batch(heap(/*playerCrashed=*/1));
    REQUIRE(host.achievementValue("pile_up") == doctest::Approx(3.0));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(0.0));

    // Up again and moving properly, the heap unchanged around us. Two seconds
    // of it: still our own crash site, so the row does not pay out. The first
    // batch after getting up is the one that matters - an earlier version of
    // the guard armed off the telemetry crash edge, which trails the position
    // batch by one, and that single batch credited the row.
    for (int i = 0; i < 20; ++i) s.batch(heap(/*playerCrashed=*/0));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(0.0));

    // A SPELL, NOT A LATCH. Past ten seconds from last being down, a heap is
    // somebody else's incident again and the row is live - which is what says
    // the zero above is the guard and not a wiring fault.
    for (int i = 0; i < 90; ++i) s.batch(heap(/*playerCrashed=*/0));
    CHECK(host.achievementValue("peace_out") == doctest::Approx(3.0));

    s.finish();
    host.shutdown();
}
