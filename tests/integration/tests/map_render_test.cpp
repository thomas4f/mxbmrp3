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
#include <filesystem>
#include <fstream>
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

    // Golden quad count for this fixed scenario. History: the pre-cache renderTrack
    // and the world-ribbon refactor both emitted 647 here (pinning that refactor as
    // byte-identical). The detail-scale rework re-baselined it to 521: the ribbon
    // builder now DEDUPES the duplicated sample at every segment joint, which used
    // to emit one degenerate zero-area quad per boundary per pass — 63 boundaries
    // x 2 passes = 126 quads of pure per-quad overhead, gone with zero visual
    // change (647 - 126 = 521 exactly; the non-degenerate geometry is untouched —
    // adaptive 100% keeps the old AUTO's sample positions). If a deliberate
    // LOD/geometry change moves it, re-baseline this number.
    CHECK(base.count == 521);

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

TEST_CASE("map: detail scale drives quad count; adaptive normalizes across track length") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\mapdetail\\") >= 0);

    auto MapVisible  = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapPct      = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetDetailPct");
    auto MapAdaptive = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetAdaptive");
    auto MapStatsFn  = host.sym<PFN_MapQuadStats>("MXBMRP3_Test_MapQuadStats");
    REQUIRE(MapVisible);
    REQUIRE(MapPct);
    REQUIRE(MapAdaptive);
    REQUIRE(MapStatsFn);

    host.eventInit("PerfTrack", "Player");
    host.raceEvent("PerfTrack");
    host.session(6, 2);
    host.addEntry(1, "Rider 1");
    host.classify(6, 120000, { { .num = 1, .best = 90000, .gap = 0 } });
    host.raceTrackPosition({ { .num = 1, .trackPos = 0.10f, .posX = 100.0f, .posZ = 50.0f, .yaw = 45.0f } });
    MapVisible(1);

    auto count = [&]() {
        host.draw();
        double sx, sy; int bad = 0;
        int c = MapStatsFn(&sx, &sy, &bad);
        CHECK(bad == 0);
        return c;
    };

    // --- Detail scale is a monotonic density dial (adaptive mode) -------------
    host.trackCenterline(circleTrack(), { 800.0f, 400.0f, 1200.0f, 0.0f });
    MapAdaptive(1);
    MapPct(20);  int d20  = count();
    MapPct(100); int d100 = count();
    MapPct(200); int d200 = count();
    INFO("adaptive quad counts: 20%=" << d20 << " 100%=" << d100 << " 200%=" << d200);
    CHECK(d20 > 0);
    CHECK(d100 > d20);
    CHECK(d200 > d100);
    // Density scales linearly with the percentage NOMINALLY (10x from 20% to
    // 200%), but the per-segment floor of one step inflates low-end counts on
    // this 64-segment circle, and quads that aren't the ribbon (background,
    // markers, rider) add a constant. The CONTRACT asserted is "the dial has
    // real range": coarse ratios, not exact linearity.
    CHECK(d200 >= d20 * 3);
    CHECK(d100 >= d20 * 2);

    // --- Adaptive normalizes quad count across track length -------------------
    // A 3x longer loop drawn in the same map box must land within ~15% of the
    // short loop's quad count at the same detail scale (screen-space density is
    // the invariant). Fixed mode is the contrast: same meters-per-quad -> the
    // longer track gets ~3x the quads.
    MapPct(100);
    int shortAdaptive = count();
    host.trackCenterline(circleTrack(64, 4800.0f), { 2400.0f, 1200.0f, 3600.0f, 0.0f });
    int longAdaptive = count();
    INFO("adaptive: short=" << shortAdaptive << " long=" << longAdaptive);
    CHECK(longAdaptive > shortAdaptive * 0.85);
    CHECK(longAdaptive < shortAdaptive * 1.15);

    MapAdaptive(0);   // fixed meters-per-quad
    int longFixed = count();
    host.trackCenterline(circleTrack(), { 800.0f, 400.0f, 1200.0f, 0.0f });
    int shortFixed = count();
    INFO("fixed: short=" << shortFixed << " long=" << longFixed);
    CHECK(longFixed > shortFixed * 2);   // ~3x nominal; assert the coarse ratio

    // --- Outline: one control for on/off + rim width ---------------------------
    // Off drops the whole outline pass (roughly half the ribbon quads); changing
    // the WIDTH re-emits the same tessellation (same count) with different vertex
    // positions (different checksum).
    auto MapOutline = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetOutline");
    auto MapSumSq   = host.sym<int(*)(double*, double*)>("MXBMRP3_Test_MapQuadSumSq");
    REQUIRE(MapOutline);
    REQUIRE(MapSumSq);
    MapAdaptive(1);
    MapOutline(100);
    host.draw();
    double onX2 = 0, onY2 = 0;
    int onCount = MapSumSq(&onX2, &onY2);

    MapOutline(0);
    int offCount = count();
    INFO("outline: on=" << onCount << " off=" << offCount);
    CHECK(offCount < onCount);
    CHECK(offCount * 2 > onCount * 0.8);   // the drop is ~the outline pass, not everything

    MapOutline(200);
    host.draw();
    double wideX2 = 0, wideY2 = 0;
    int wideCount = MapSumSq(&wideX2, &wideY2);
    CHECK(wideCount == onCount);                      // same tessellation, wider quads
    // The plain vertex SUM is blind to width (symmetric edges cancel); the
    // squared sum keeps the width term. The rim is small in normalized units, so
    // the delta is well below Approx's relative epsilon — exact inequality is
    // right here: the rebuild is deterministic, so equal bits == nothing changed.
    CHECK(wideX2 != onX2);
    MapOutline(100);

    host.shutdown();
}

TEST_CASE("map: legacy detail=AUTO/HIGH/LOW INI values migrate to scale/adaptive") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\maplegacy\\") >= 0);

    auto DetailState = host.sym<int(*)()>("MXBMRP3_Test_MapDetailState");
    REQUIRE(DetailState);

    // Factory default (no INI yet): adaptive ON at the lean 50% — deliberately
    // NOT the 100% that legacy AUTO migrates to (upgraders keep their old look,
    // fresh installs get the lighter budget).
    CHECK(DetailState() == 1050);

    // Write a settings INI carrying the given [MapHud] detail lines, then reload
    // settings from it (the hand-edit + RELOAD_CONFIG workflow).
    auto loadWithMapSection = [&](const char* lines) {
        namespace fs = std::filesystem;
        fs::create_directories("Z:\\tmp\\mxbmrp3-tests\\maplegacy\\mxbmrp3");
        std::ofstream f("Z:\\tmp\\mxbmrp3-tests\\maplegacy\\mxbmrp3\\mxbmrp3_settings.ini",
                        std::ios::binary | std::ios::trunc);
        f << "[Settings]\nversion=4\n\n[MapHud]\n" << lines << "\n";
        f.close();
        host.loadSettings("Z:\\tmp\\mxbmrp3-tests\\maplegacy\\");
    };

    // DetailState encodes percent + 1000*adaptive.
    loadWithMapSection("detail=HIGH");
    CHECK(DetailState() == 200);    // fixed, 200% (the old 1.0m)

    loadWithMapSection("detail=LOW");
    CHECK(DetailState() == 60);     // fixed, 60% (~the old 4.0m)

    loadWithMapSection("detail=AUTO");
    CHECK(DetailState() == 1100);   // adaptive, 100% (the old AUTO exactly)

    // New keys win over a stale legacy key in the same file.
    loadWithMapSection("detail=LOW\ndetailScale=1.4\ndetailAdaptive=1");
    CHECK(DetailState() == 1140);

    // Out-of-range values clamp instead of aborting the section.
    loadWithMapSection("detailScale=9.9\ndetailAdaptive=0");
    CHECK(DetailState() == 200);

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
