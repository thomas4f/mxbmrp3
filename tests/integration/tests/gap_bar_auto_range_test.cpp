// ============================================================================
// tests/integration/tests/gap_bar_auto_range_test.cpp
// THE GAP BAR'S AUTO RANGE FITS THE LAP'S LARGEST GAP.
//
// Auto (the default, GapBarHud::RANGE_AUTO) draws the fill against the round
// step that holds the largest gap of the lap so far (PluginUtils::niceGapScaleMs,
// the Delta Trace's scale), never under 1 s. It grows with a bigger gap, does
// not shrink back within the lap (a fill that rescaled with every swing would
// read as the gap changing), and starts over once a lap is committed - or once
// the gap comes back live after the pits, since the out-lap is never
// committed. A fixed range still draws against itself.
//
// The fill's width over the half bar is the ratio gap / range, measured off the
// quad the way center_stack_theme_test measures it at full deflection.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cmath>

typedef void (*PFN_LL)(long long);

namespace {
constexpr int RACE1 = 1;
}

TEST_CASE("gap bar auto range: fits the lap's largest gap, holds it, starts over with the next lap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\gap_bar_auto_range\\") >= 0);
    REQUIRE(host.hasQuadRects());
    auto SetNowUs = host.sym<PFN_LL>("MXBMRP3_Test_LapTimerSetNowUs");
    auto SetRange = host.sym<void (*)(int)>("MXBMRP3_Test_GapBarRange");
    REQUIRE_MESSAGE((SetNowUs && SetRange), "test hooks not exported (test build?)");
    REQUIRE(host.setHudVisible("gap_bar_hud", true));
    host.clearTheme();

    host.eventInit("RangeTrack", "Alice");
    host.raceEvent("RangeTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // A committed lap: what sets the PB the fill is gated on, and what starts
    // the Auto peak over.
    long long lapStartUs = 1000000000LL;
    auto lap = [&](int lapNum, int lapMs, int best) {
        SetNowUs(lapStartUs - 20000);
        host.raceTrackPosition({ { 10, 0.99f } });
        SetNowUs(lapStartUs);
        host.raceTrackPosition({ { 10, 0.00f } });
        for (int p = 1; p <= 197; ++p) {
            SetNowUs(lapStartUs + static_cast<long long>(p) * (lapMs / 200) * 1000LL);
            host.raceTrackPosition({ { 10, p / 200.0f } });
        }
        lapStartUs += static_cast<long long>(lapMs) * 1000LL;
        SetNowUs(lapStartUs);
        host.classify(RACE1, lapMs * lapNum, { { .num = 10, .laps = lapNum, .gap = 0 } });
        host.raceTrackPosition({ { 10, 0.00f } });
        host.raceLap(RACE1, 10, lapNum, lapMs, best, lapMs / 3, lapMs * 2 / 3);
        host.drawWithState(0);
        host.drawWithState(0);
    };
    lap(1, 60000, /*best=*/1);

    const auto panel = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    const double pl = panel.l / 1e6, pr = panel.r / 1e6;
    const double mid = (pl + pr) / 2.0;
    const double half = (pr - pl) / 2.0;

    // The fill as a fraction of its half: the widest quad with an edge on the
    // centre line that is no wider than the half.
    auto fillRatio = [&](int gapMs) {
        REQUIRE(host.gapBarForceGap(gapMs, true));
        host.draw();
        double best = 0.0;
        for (const auto& q : host.hudQuadRects(PluginHost::HUD_GAPBAR)) {
            const double l = std::min(q.l, q.r), r = std::max(q.l, q.r);
            const bool onCentre = std::abs(l - mid) < 1e-4 || std::abs(r - mid) < 1e-4;
            if (onCentre && r - l <= half + 1e-4) best = std::max(best, r - l);
        }
        return best / half;
    };

    CHECK(fillRatio(400) == doctest::Approx(0.4).epsilon(0.01));    // 1 s floor
    CHECK(fillRatio(1500) == doctest::Approx(0.75).epsilon(0.01));  // grows to 2 s
    CHECK(fillRatio(-400) == doctest::Approx(0.2).epsilon(0.01));   // holds 2 s this lap

    lap(2, 61000, /*best=*/0);
    CHECK(fillRatio(400) == doctest::Approx(0.4).epsilon(0.01));    // a new lap starts over

    // A crash that costs 20 s widens the range to 30 s; the rider pits. The
    // out-lap is reported as a 0 ms lap and never committed, so without the
    // live-flip reset the first flying lap would still draw against 30 s.
    CHECK(fillRatio(20000) == doctest::Approx(20.0 / 30.0).epsilon(0.01));
    host.runStop();
    host.runDeinit();
    // The planted gap outranks the computed one (see MXBMRP3_Test_GapBarForceGap),
    // so the pits' "no gap" is planted too; the next plant is the live flip.
    REQUIRE(host.gapBarForceGap(0, false));
    host.draw();
    host.runInit(RACE1);
    host.runStart();
    for (int p = 176; p <= 199; ++p) {
        SetNowUs(lapStartUs + 120000000LL + static_cast<long long>(p - 176) * 300000LL);
        host.raceTrackPosition({ { 10, p / 200.0f } });
    }
    lapStartUs += 120000000LL + 24 * 300000LL;
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(RACE1, 10, 3, 0, /*best=*/0, 0, 0);
    SetNowUs(lapStartUs + 3000000LL);
    host.raceTrackPosition({ { 10, 0.05f } });
    host.drawWithState(0);
    CHECK(fillRatio(400) == doctest::Approx(0.4).epsilon(0.01));    // back to the floor

    // A fixed range draws against itself, whatever the lap held.
    SetRange(5000);
    CHECK(fillRatio(2500) == doctest::Approx(0.5).epsilon(0.01));

    host.shutdown();
}
