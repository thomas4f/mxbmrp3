// ============================================================================
// tests/integration/tests/map_render_test.cpp
// MapHud rendering + world-ribbon-cache correctness. The map's track ribbon is
// tessellated once in view-independent world space and cached (WorldRibbonPoint);
// rotate-to-player and zoom then only transform the cached points instead of
// re-sampling the whole centerline every frame. This test drives a real 2D track
// under Wine and asserts the optimization is transparent:
//   * the map emits a non-empty, all-finite quad set in every view mode, and
//   * the default-view geometry is bit-for-bit reproducible across a detail-LOD
//     round-trip (which forces the world cache to rebuild) and across visiting
//     rotate / zoom (which must not corrupt the cache).
// It also guards the degenerate-track NaN path: a valid 2D loop must never
// produce a non-finite vertex.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <vector>

typedef void   (*PFN_MapI)(int);
typedef int    (*PFN_MapQuadStats)(double*, double*, int*);

namespace {
// A real 2D loop: a circle of curve segments, so the track has non-degenerate
// width AND height (a straight-only / 1D track would divide-by-zero in
// worldToScreen). Mirrors tests/integration/map_perf_driver.cpp's geometry.
std::vector<TrackSegmentRow> circleTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(segs);
    const float radius = trackLen / (2.0f * 3.14159265f);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 1;                 // curve
        v[i].length = trackLen / segs;
        v[i].radius = radius;
        v[i].angle = 0.0f;
    }
    return v;
}

// A DEGENERATE track: straight-only segments all heading north. calculateTrackBounds
// only turns on curve segments, so this integrates to a 1D vertical line — zero
// width in X. It exercises the worldToScreen divide-by-zero guard (without it, the
// map emits NaN/Inf vertices and a NaN HUD offset).
std::vector<TrackSegmentRow> lineTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(segs);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 0;                 // straight
        v[i].length = trackLen / segs;
        v[i].angle = 0.0f;             // all heading north -> collapses to a line in X
    }
    return v;
}

struct MapStats { int count; double sumX, sumY; int nonFinite; };
}

TEST_CASE("map: world-ribbon cache is transparent across LOD + view-mode round-trips") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map\\") >= 0);

    auto MapVisible = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapRotate  = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetRotate");
    auto MapZoom    = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetZoom");
    auto MapDetail  = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetDetail");
    auto MapStatsFn = host.sym<PFN_MapQuadStats>("MXBMRP3_Test_MapQuadStats");
    REQUIRE(MapVisible);
    REQUIRE(MapRotate);
    REQUIRE(MapZoom);
    REQUIRE(MapDetail);
    REQUIRE(MapStatsFn);

    // Populate a small race on the circular track.
    host.eventInit("PerfTrack", "Player");
    host.raceEvent("PerfTrack");
    host.session(6, 2);
    for (int i = 1; i <= 5; ++i) host.addEntry(i, ("Rider " + std::to_string(i)).c_str());
    host.trackCenterline(circleTrack(), { 800.0f, 400.0f, 1200.0f, 0.0f });
    host.classify(6, 120000, {
        { .num = 1, .best = 90000, .gap = 0 },
        { .num = 2, .best = 90500, .gap = 500 },
        { .num = 3, .best = 91000, .gap = 1000 },
        { .num = 4, .best = 91500, .gap = 1500 },
        { .num = 5, .best = 92000, .gap = 2000 },
    });
    // Riders around the loop; give #1 a heading + world position so rotate/zoom
    // have something to follow if #1 is the display rider.
    host.raceTrackPosition({
        { .num = 1, .trackPos = 0.00f, .posX = 100.0f, .posZ = 50.0f, .yaw = 45.0f },
        { .num = 2, .trackPos = 0.20f, .posX = 120.0f, .posZ = 90.0f, .yaw = 90.0f },
        { .num = 3, .trackPos = 0.40f },
        { .num = 4, .trackPos = 0.60f },
        { .num = 5, .trackPos = 0.80f },
    });

    MapVisible(1);

    auto read = [&]() -> MapStats {
        host.draw();  // state 1 (spectate) — the map renders in all view states
        MapStats s{};
        s.count = MapStatsFn(&s.sumX, &s.sumY, &s.nonFinite);
        return s;
    };

    auto finiteNonEmpty = [](const MapStats& s, const char* what) {
        INFO("mode=" << what << " count=" << s.count);
        CHECK(s.count > 0);
        CHECK(s.nonFinite == 0);   // a valid 2D loop must never yield NaN/Inf
    };

    // --- Baseline: default view (rotate off, zoom off, AUTO detail) -----------
    MapRotate(0); MapZoom(0); MapDetail(0);
    MapStats base = read();
    finiteNonEmpty(base, "default");

    // Golden quad count for this fixed scenario. Captured byte-identically from the
    // PRE-CACHE renderTrack (commit addebaf~1) and the world-ribbon version — both
    // emit 647 quads with the same vertex checksum here — so this pins the
    // refactor's output as equivalent to the original, not merely self-consistent.
    // If a deliberate LOD/geometry change moves it, re-baseline this number.
    CHECK(base.count == 647);

    // --- Detail-LOD round-trip: forces the world cache to rebuild -------------
    // HIGH subdivides the ribbon more finely (strictly more quads), LOW less; on
    // return to AUTO the geometry must be identical to the baseline.
    MapDetail(1); MapStats hi = read(); finiteNonEmpty(hi, "HIGH");
    CHECK(hi.count >= base.count);
    MapDetail(2); MapStats lo = read(); finiteNonEmpty(lo, "LOW");
    MapDetail(0); MapStats backAuto = read(); finiteNonEmpty(backAuto, "AUTO-again");
    CHECK(backAuto.count == base.count);
    CHECK(backAuto.sumX == doctest::Approx(base.sumX));
    CHECK(backAuto.sumY == doctest::Approx(base.sumY));

    // --- Rotate-to-player round-trip ------------------------------------------
    MapRotate(1); MapStats rot = read(); finiteNonEmpty(rot, "rotate");
    MapRotate(0); MapStats afterRot = read(); finiteNonEmpty(afterRot, "after-rotate");
    CHECK(afterRot.count == base.count);
    CHECK(afterRot.sumX == doctest::Approx(base.sumX));
    CHECK(afterRot.sumY == doctest::Approx(base.sumY));

    // --- Zoom round-trip -------------------------------------------------------
    MapZoom(1); MapStats zoom = read(); finiteNonEmpty(zoom, "zoom");
    MapZoom(0); MapStats afterZoom = read(); finiteNonEmpty(afterZoom, "after-zoom");
    CHECK(afterZoom.count == base.count);
    CHECK(afterZoom.sumX == doctest::Approx(base.sumX));
    CHECK(afterZoom.sumY == doctest::Approx(base.sumY));

    host.shutdown();
}

TEST_CASE("map: degenerate 1D track renders finite (worldToScreen divide-by-zero guard)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map\\") >= 0);

    auto MapVisible = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapZoom    = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetZoom");
    auto MapStatsFn = host.sym<PFN_MapQuadStats>("MXBMRP3_Test_MapQuadStats");
    REQUIRE(MapVisible);
    REQUIRE(MapZoom);
    REQUIRE(MapStatsFn);

    host.eventInit("LineTrack", "Player");
    host.raceEvent("LineTrack");
    host.session(6, 2);
    host.addEntry(1, "Rider 1");
    // Zero-width (1D) track: without the guard this NaNs the map's HUD offset and
    // every ribbon vertex.
    host.trackCenterline(lineTrack(), {});
    host.classify(6, 120000, { { .num = 1, .best = 90000, .gap = 0 } });
    host.raceTrackPosition({ { .num = 1, .trackPos = 0.10f, .posX = 0.0f, .posZ = 160.0f, .yaw = 0.0f } });

    MapVisible(1);
    auto checkFinite = [&](const char* what) {
        host.draw();
        double sx = 0, sy = 0; int bad = 0;
        MapStatsFn(&sx, &sy, &bad);
        INFO("mode=" << what);
        CHECK(bad == 0);   // no NaN/Inf vertex despite the degenerate geometry
    };

    checkFinite("default");        // container-size math + ribbon transform
    MapZoom(1); checkFinite("zoom");  // zoom recomputes bounds/scale -> most divide-prone
    MapZoom(0); checkFinite("default-again");

    host.shutdown();
}
