// ============================================================================
// tests/integration/tests/gap_bar_splits_test.cpp
// THE GAP BAR'S SPLIT TICKS SIT WHERE THE SPLITS ARE.
//
// The bar is a flat map of the lap, and a tick marks each split on it: a short
// line at the top edge and one at the bottom, the middle left to the gap text and
// the rider markers. Where "where" comes from differs by game:
//
//   1. THE CENTERLINE'S MARKER DATA (MX Bikes, KRP): S/F and splits in meters,
//      there from the first frame. With the rider standing on S1, the tick and
//      the rider's own marker must share an x -- both are the same track
//      position on the same inner rect, so this pins the mapping, not a number.
//   2. LEARNED FROM A CROSSING (GP Bikes, whose centerline data is disabled in
//      gpb_api.cpp): no ticks until the display rider crosses a split, then one
//      at the position they crossed it.
//
// On by default; switched off it draws nothing.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int RACE1 = 6;
constexpr float TRACK_LEN = 1600.0f;

std::vector<TrackSegmentRow> circleTrack(int segs = 64) {
    std::vector<TrackSegmentRow> v(segs);
    const float radius = TRACK_LEN / (2.0f * 3.14159265f);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 1;
        v[i].length = TRACK_LEN / segs;
        v[i].radius = radius;
        v[i].angle = 0.0f;
    }
    return v;
}

// A tick is a thin quad much taller than it is wide; nothing else on the bar is.
std::vector<PluginHost::QuadRect> ticks(PluginHost& host) {
    std::vector<PluginHost::QuadRect> out;
    for (const auto& q : host.hudQuadRects("gap_bar_hud")) {
        const double w = std::abs(q.r - q.l), h = std::abs(q.b - q.t);
        if (h > 0.0 && w < h * 0.25) out.push_back(q);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.l < b.l; });
    return out;
}

double centreX(const PluginHost::QuadRect& q) { return (q.l + q.r) * 0.5; }

// The rider's own marker: the widest roughly-square quad on the bar that is not
// the background (the icon), found by being the one whose centre moves when the
// rider moves. Simpler here: the square quad whose centre is nearest `nearX`.
double markerCentreNear(PluginHost& host, double nearX) {
    double best = 1e9, x = -1.0;
    for (const auto& q : host.hudQuadRects("gap_bar_hud")) {
        const double w = std::abs(q.r - q.l), h = std::abs(q.b - q.t);
        if (h <= 0.0 || w < h * 0.3 || w > h * 3.0) continue;
        const double c = centreX(q);
        if (std::abs(c - nearX) < best) { best = std::abs(c - nearX); x = c; }
    }
    return x;
}

void setUp(PluginHost& host, const char* dir) {
    host.startup(dir);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    REQUIRE(host.setHudVisible("gap_bar_hud", true));
}

}  // namespace

TEST_CASE("gap bar split ticks: on by default, two per split at the centerline's splits; off draws none") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\gap_bar_splits\\");
    REQUIRE_MESSAGE(host.hasGapBarShowSplits(), "MXBMRP3_Test_GapBarShowSplits not exported");

    // S/F 100 m in, S1 at 500 m, S2 at 1100 m: S1 is 0.25 of the lap from the line.
    host.trackCenterline(circleTrack(), { 100.0f, 500.0f, 1100.0f, 0.0f });
    host.raceTrackPosition({ { 10, 0.25f } });   // standing on S1
    // A live gap, as the lap timer would plant in game: what rebuilds the bar
    // with the rider's marker where the position sample put it.
    REQUIRE(host.gapBarForceGap(-1500, true));   // wide: its fill is no thin quad
    host.draw();

    const auto t = ticks(host);   // on by default
    REQUIRE(t.size() == 4u);   // S1 top+bottom, S2 top+bottom
    // Each split's pair shares an x, one at the top edge and one at the bottom.
    CHECK(std::abs(centreX(t[0]) - centreX(t[1])) < 1e-4);
    CHECK(std::abs(centreX(t[2]) - centreX(t[3])) < 1e-4);
    CHECK(centreX(t[2]) > centreX(t[0]));
    CHECK(std::abs(std::min(t[0].t, t[0].b) - std::min(t[1].t, t[1].b)) > 1e-4);

    // The rider on S1 is drawn on the S1 tick.
    const double s1 = centreX(t[0]);
    const double rider = markerCentreNear(host, s1);
    INFO("S1 tick x " << s1 << ", rider marker x " << rider);
    CHECK(std::abs(rider - s1) < 1e-3);

    host.gapBarShowSplits(false);
    host.draw();
    CHECK(ticks(host).empty());
    host.shutdown();
}

TEST_CASE("gap bar split ticks: without centerline splits, a crossing is learned") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\gap_bar_splits_learn\\");

    host.trackCenterline(circleTrack());   // no marker data: GP Bikes' case
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.draw();
    CHECK(ticks(host).empty());   // nothing crossed yet

    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, /*lapNum=*/1, /*splitIndex=*/0, 20000);
    host.draw();
    const auto t = ticks(host);
    REQUIRE(t.size() == 2u);
    const double s1 = centreX(t[0]);
    CHECK(std::abs(markerCentreNear(host, s1) - s1) < 1e-3);   // learned where the rider was
    host.shutdown();
}
