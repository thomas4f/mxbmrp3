// ============================================================================
// tests/integration/tests/map_lap_delta_test.cpp
// The Map's lap delta (MapHud::LapDelta): the track fill blends toward green
// where the display rider gains on the reference lap and red where they lose,
// drawn with the game's own flat quads. Drives a real reference lap and half
// of a second one whose pace swings around it through the callbacks, and
// checks that:
//   - with the setting Off, no track quad is tinted (the default map is
//     unchanged)
//   - On, the half lap ridden is tinted; both directions show up: green over
//     the gaining stretches, red over the losing one
//   - the colours fade with the pace: many shades, not one green and one red
//     (that the shade moves smoothly between profile points is pinned by
//     tests/unit/test_lap_delta_rate.cpp)
//   - over the line the previous lap stays on ahead of the rider: early in
//     lap 3 more of the track is tinted than lap 2's half lap was, and in
//     the pits all of lap 2 shows (more again: no gap ahead of the rider)
//   - a new session drops the reference, and the tint with it
//   - with a reference, the outline and the fill share one world ribbon: a
//     rotating view re-walks the track zero times a frame, not twice (1.32
//     review: the outline asked for the untinted key, so every rebuild walked
//     the ribbon twice, ~1.4 ms a Draw zoomed on a 4 km track)
// The lap timer is wall-clock anchored, so the clock is simulated through
// MXBMRP3_Test_LapTimerSetNowUs: each position sample is sent at the elapsed
// time the scripted lap has there.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <vector>

typedef void (*PFN_I)(int);
typedef void (*PFN_LL)(long long);
typedef int (*PFN_DeltaQuads)(int*, int*, int*);
typedef long long (*PFN_LL0)();

namespace {
constexpr int RACE1 = 1;

// A circle of curve segments (as map_render_test), so the ribbon is dense.
std::vector<TrackSegmentRow> circleTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(segs);
    const float radius = trackLen / (2.0f * 3.14159265f);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 1;
        v[i].length = trackLen / segs;
        v[i].radius = radius;
        v[i].angle = 0.0f;
    }
    return v;
}
}  // namespace

TEST_CASE("map lap delta: tints the stretch ridden, green where gaining, red where losing") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map_lap_delta\\") >= 0);

    auto SetMode  = host.sym<PFN_I>("MXBMRP3_Test_MapSetLapDelta");
    auto Quads    = host.sym<PFN_DeltaQuads>("MXBMRP3_Test_MapLapDeltaQuads");
    auto SetNowUs = host.sym<PFN_LL>("MXBMRP3_Test_LapTimerSetNowUs");
    auto Visible  = host.sym<PFN_I>("MXBMRP3_Test_MapSetVisible");
    REQUIRE_MESSAGE((SetMode && Quads && SetNowUs && Visible), "test hooks not exported (test build?)");

    host.eventInit("DeltaTrack", "Alice");
    host.raceEvent("DeltaTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.trackCenterline(circleTrack(), { 1.0f, 400.0f, 1200.0f, 0.0f });
    Visible(1);
    host.mapFlatOverview();

    host.runInit(RACE1);
    host.runStart();

    long long lapStartUs = 1000000000LL;
    // One sample per 0.5% of the lap, step `p` at `elapsedMs(p)`.
    auto ride = [&](int from, int to, auto elapsedMs) {
        for (int p = from; p <= to; ++p) {
            SetNowUs(lapStartUs + static_cast<long long>(elapsedMs(p)) * 1000LL);
            host.raceTrackPosition({ { 10, p / 200.0f } });
        }
    };
    struct Read { int tinted, distinct, gaining, losing; };
    auto frame = [&]() {
        host.drawWithState(0);
        host.drawWithState(0);
        Read r{};
        r.tinted = Quads(&r.distinct, &r.gaining, &r.losing);
        return r;
    };

    // Lap 1: 60 s at an even pace, the reference.
    SetNowUs(lapStartUs - 20000);
    host.raceTrackPosition({ { 10, 0.99f } });
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    ride(1, 197, [](int p) { return p * 300; });
    lapStartUs += 60000000LL;
    SetNowUs(lapStartUs);
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);

    // Lap 2: a pace that swings smoothly around the reference's 300 ms per step
    // (275 to 325): gaining over the first and last eighth of the half lap
    // ridden, losing between them.
    auto lap2 = [](int p) {
        const double w = 2.0 * 3.14159265358979 / 100.0;
        return static_cast<int>(300.0 * p - 25.0 / w * std::sin(w * p));
    };
    ride(1, 100, lap2);

    SetMode(0);
    const Read off = frame();
    CHECK(off.tinted == 0);

    SetMode(1);   // session PB
    const Read on = frame();
    INFO("tinted=" << on.tinted << " distinct=" << on.distinct
         << " gaining=" << on.gaining << " losing=" << on.losing);
    CHECK(on.tinted > 20);
    CHECK(on.gaining > 5);
    CHECK(on.losing > 5);
    // Shades follow the pace rather than two flat colours.
    CHECK(on.distinct > 12);

    // Rotate-to-player: every frame is a screen-ribbon miss. The world ribbon
    // (the arc walk) stays cached, shared by the outline and the tinted fill.
    {
        auto Rotate = host.sym<PFN_I>("MXBMRP3_Test_MapSetRotate");
        auto Builds = host.sym<PFN_LL0>("MXBMRP3_Test_MapWorldRibbonBuilds");
        REQUIRE((Rotate && Builds));
        Rotate(1);
        auto turn = [&](int i) {
            host.raceTrackPosition({ { .num = 10, .trackPos = 100 / 200.0f, .yaw = 10.0f * i } });
            host.drawWithState(0);
        };
        for (int i = 0; i < 3; ++i) turn(i);
        const long long before = Builds();
        for (int i = 3; i < 23; ++i) turn(i);
        CHECK(Builds() - before == 0);
        CHECK(frame().tinted > 20);
        Rotate(0);
    }

    // Lap 2 completes; lap 3 starts. Its first 5% is tinted from lap 3 itself,
    // and past a gap the rest of the track still shows lap 2.
    ride(101, 197, lap2);
    lapStartUs += 60000000LL;
    SetNowUs(lapStartUs);
    host.classify(RACE1, 120000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1, 10, /*lapNum=*/2, 60000, /*best=*/0, 20000, 40000);
    ride(1, 10, lap2);
    const Read swept = frame();
    INFO("early in lap 3: tinted=" << swept.tinted << " gaining=" << swept.gaining << " losing=" << swept.losing);
    CHECK(swept.tinted > on.tinted);
    CHECK(swept.gaining > 5);
    CHECK(swept.losing > 5);

    // In the pits nothing is live: the whole of lap 2 stays.
    host.runStop();
    host.runDeinit();
    host.raceTrackPosition({ { 10, 11 / 200.0f } });   // something to repaint on
    const Read pits = frame();
    INFO("in the pits: tinted=" << pits.tinted);
    CHECK(pits.tinted > swept.tinted);

    // A new session drops every reference: the tint goes with it.
    host.session(RACE1 + 1, 10, 0);
    CHECK(frame().tinted == 0);

    SetMode(0);
    SetNowUs(-1);
}
