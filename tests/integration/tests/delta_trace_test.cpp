// ============================================================================
// tests/integration/tests/delta_trace_test.cpp
// The Delta Trace HUD (hud/delta_trace_hud.h) plots the gap the Gap Bar reads,
// sampled across the lap from the PB gap tracker's tables. This drives a real
// reference lap and half of a second one through the callbacks and checks
// that the line covers exactly the half lap ridden, restarts with the next
// lap while the lap just completed stays up ahead of the rider past a gap,
// keeps what it drew when the rider rides back, marks the rider with a square
// that moves between profile points without a rebuild,
// shows all of that lap once the gap stops being live (the pits), comes back
// live after the out-lap with that lap still kept, and is gone with a new
// session until the new session has a lap of its own. And that every quad is wound the way the game
// draws: it culls the other order, so the red (behind) fill, built line-first
// below the zero line, vanished in game (Thomas, 1.32) while the companion
// window, which does not cull, still showed it.
//
// The lap timer is wall-clock anchored, so the clock is simulated through
// MXBMRP3_Test_LapTimerSetNowUs: each position sample is sent at the elapsed
// time the scripted lap has there.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

typedef void (*PFN_LL)(long long);

namespace {
constexpr int RACE1 = 1;
constexpr int LapDeltaProfileGap = 6;   // LapDeltaProfile::OVERWRITE_GAP
}

TEST_CASE("delta trace: plots the lap in progress against the reference, then the lap just completed") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\delta_trace\\") >= 0);

    auto Points       = host.sym<int (*)()>("MXBMRP3_Test_DeltaTracePoints");
    auto Shape        = host.sym<void (*)(int*, int*, int*, float*, int*)>("MXBMRP3_Test_DeltaTraceShape");
    auto SetNowUs     = host.sym<PFN_LL>("MXBMRP3_Test_LapTimerSetNowUs");
    REQUIRE_MESSAGE((Points && Shape && SetNowUs), "test hooks not exported (test build?)");
    auto previous = [&]() { int p = -1, r = -1, m = -1; Shape(&p, &r, &m, nullptr, nullptr); return p; };
    auto reversed = [&]() { int p = -1, r = -1, m = -1; Shape(&p, &r, &m, nullptr, nullptr); return r; };
    auto marker   = [&]() { int p = -1, r = -1, m = -1; Shape(&p, &r, &m, nullptr, nullptr); return m; };
    auto markerX  = [&]() { float x = -1.0f; Shape(nullptr, nullptr, nullptr, &x, nullptr); return x; };
    auto rebuilds = [&]() { int n = -1; Shape(nullptr, nullptr, nullptr, nullptr, &n); return n; };

    host.setHudVisible("delta_trace_hud", true);

    host.eventInit("TraceTrack", "Alice");
    host.raceEvent("TraceTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");

    host.runInit(RACE1);
    host.runStart();

    long long lapStartUs = 1000000000LL;
    // One sample per 0.5% of the lap (the trace's own resolution) at `msPerStep`
    // ms each, from step `from` to `to` of 200.
    auto ride = [&](int from, int to, int msPerStep) {
        for (int p = from; p <= to; ++p) {
            SetNowUs(lapStartUs + static_cast<long long>(p) * msPerStep * 1000LL);
            host.raceTrackPosition({ { 10, p / 200.0f } });
        }
    };
    auto frame = [&]() { host.drawWithState(0); host.drawWithState(0); return Points(); };

    // The S/F crossing anchors the timer and makes the lap start observed.
    SetNowUs(lapStartUs - 20000);
    host.raceTrackPosition({ { 10, 0.99f } });
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    ride(1, 197, 300);                             // lap 1: 60 s, even pace
    CHECK(frame() == 0);                           // no reference yet

    lapStartUs += 60000000LL;
    SetNowUs(lapStartUs);
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);

    ride(1, 100, 310);                             // lap 2: slower, half way round
    const int half = frame();
    INFO("points plotted at half lap: " << half);
    CHECK(half >= 95);                             // one per step ridden (plus slot 0)
    CHECK(half <= 102);                            // and none beyond the rider
    CHECK(previous() == 0);                        // lap 1 had nothing to compare with
    CHECK(reversed() == 0);                        // behind: the fill sits below zero
    CHECK(marker() == 1);                          // the rider's square on the live gap

    // Between profile points only the marker moves, at the pace of the
    // position updates: the trace itself is not rebuilt.
    {
        const float x0 = markerX();
        const int built = rebuilds();
        SetNowUs(lapStartUs + 100LL * 310 * 1000LL + 930000LL);
        host.raceTrackPosition({ { 10, 100.6f / 200.0f } });   // 0.3% on, same profile point
        CHECK(frame() == half);
        CHECK(markerX() > x0);
        CHECK(rebuilds() == built);                    // moved in place, not rebuilt
        CHECK(reversed() == 0);
    }

    // Over the line the trace starts again with the new lap.
    ride(101, 197, 310);
    const int full = frame();
    lapStartUs += 62000000LL;
    SetNowUs(lapStartUs);
    host.classify(RACE1, 122000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    // Wrapped, not yet completed (the line window): the picture holds rather
    // than flashing the lap before and snapping back.
    CHECK(frame() == full);
    host.raceLap(RACE1, 10, 2, 62000, /*best=*/0, 20500, 41000);
    ride(1, 10, 290);                              // ahead of the PB this time: fills above zero
    const int restart = frame();
    INFO("points plotted early in lap 3: " << restart);
    CHECK(restart >= 8);
    CHECK(restart <= 12);
    // Lap 2 is still there from past the rider and the gap to the line.
    const int kept = previous();
    INFO("previous-lap points ahead of the rider: " << kept);
    CHECK(kept >= 175);
    CHECK(kept <= 200 - 10 - LapDeltaProfileGap);
    CHECK(reversed() == 0);                        // ahead (lap 3) and behind (lap 2) both

    // Riding back (a crash, a missed jump) keeps what the lap already drew.
    for (int p = 9; p >= 4; --p) {
        SetNowUs(lapStartUs + 2900000LL + static_cast<long long>(10 - p) * 400000LL);
        host.raceTrackPosition({ { 10, p / 200.0f } });
    }
    const int back = frame();
    INFO("points plotted after riding back to 2%: " << back);
    CHECK(back >= restart);
    CHECK(previous() == kept);                     // the sweep still starts past the furthest point
    CHECK(marker() == 1);
    CHECK(reversed() == 0);                        // the resampled slots fall behind: zero crossings

    // Into the pits: no live gap, so the trace shows the lap just completed.
    host.runStop();
    host.runDeinit();
    const int pitted = frame();
    INFO("points plotted in the pits: " << pitted);
    CHECK(pitted >= 190);
    CHECK(previous() == 0);                        // nothing live to sweep over it
    CHECK(marker() == 0);                          // nor a rider on it
    CHECK(reversed() == 0);

    // Back out of the pits most of a lap on: the run to the line is a partial,
    // so nothing is live yet and the lap kept in the pits stays up. The game
    // reports the out-lap as a 0 ms RaceLap at the line; the next full lap
    // reads live again, with that kept lap still ahead of the rider.
    host.runInit(RACE1);
    host.runStart();
    lapStartUs += 120000000LL;
    for (int p = 176; p <= 199; ++p) {
        SetNowUs(lapStartUs + static_cast<long long>(p - 176) * 300000LL);
        host.raceTrackPosition({ { 10, p / 200.0f } });
    }
    CHECK(marker() == 0);
    CHECK(frame() >= 190);
    lapStartUs += 24 * 300000LL;
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1, 10, 4, 0, /*best=*/0, 0, 0);
    ride(1, 100, 305);
    const int rejoined = frame();
    INFO("points plotted half way round the lap after the out-lap: " << rejoined);
    CHECK(rejoined >= 95);
    CHECK(marker() == 1);
    CHECK(previous() >= 80);                       // lap 2 still ahead of the rider

    // A new session drops every reference: the trace empties...
    host.session(RACE1 + 1, 10, 0);
    CHECK(frame() == 0);

    // ...and fills again once the new session has a lap to compare with.
    lapStartUs += 100LL * 305 * 1000LL + 20000000LL;
    SetNowUs(lapStartUs - 20000);
    host.raceTrackPosition({ { 10, 0.99f } });
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    ride(1, 197, 300);
    CHECK(frame() == 0);                           // the new session's first lap: no reference yet
    lapStartUs += 60000000LL;
    SetNowUs(lapStartUs);
    host.classify(RACE1 + 1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1 + 1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    ride(1, 50, 300);
    const int fresh = frame();
    INFO("points plotted a quarter into the new session's second lap: " << fresh);
    CHECK(fresh >= 45);
    CHECK(marker() == 1);

    SetNowUs(-1);
}
