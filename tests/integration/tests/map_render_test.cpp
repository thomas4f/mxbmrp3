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
#include <string>
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
    // adaptive 100% keeps the old AUTO's sample positions). The PanelBox port
    // re-baselined it to 520: the map's content rect moved by its [panel] padding,
    // which shifts the per-quad view-cull window, and one boundary quad that
    // straddled the old clip edge now culls in one of the two ribbon passes. An
    // ODD delta can only be culling — a centerline-sample change moves both passes
    // together and is always even — so the ribbon geometry itself is untouched.
    // If a deliberate LOD/geometry change moves it, re-baseline this number.
    CHECK(base.count == 520);

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

// ============================================================================
// THE PLAYER'S MARKER IS CLAMPED TO THE MAP EDGE, NOT DROPPED.
//
// Ride off the map -- crash into the scenery, get turned around, take a wrong
// line out of a rut -- and the map is suddenly the one panel that could point
// you back. It used to show nothing: every marker whose centre fell outside the
// clip rect was skipped, the player's included, so the panel went blank in
// exactly the situation it was worth looking at.
//
// The player's marker is now pinned to the nearest edge, per axis, keeping its
// heading. Per-axis is what makes it useful rather than decorative: off the left
// of the map, x pins to the left edge while y still tracks the rider up and down
// it, so the icon sits beside where they actually are.
//
// Asserted through the map's own quad count, which is the only thing here that
// can tell "drawn at the edge" from "not drawn": a rect check alone passes
// trivially when the marker is missing, and the aggregate centroid moves for a
// dozen reasons. The count is paired with a bounds check so a marker that IS
// drawn but hangs off the panel still fails.
// ============================================================================
TEST_CASE("map: the player's marker clamps to the edge when off-map") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map_clamp\\") >= 0);

    auto MapVisible = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapStatsFn = host.sym<PFN_MapQuadStats>("MXBMRP3_Test_MapQuadStats");
    REQUIRE(MapVisible);
    REQUIRE(MapStatsFn);
    REQUIRE(host.hasScreenEdges());

    host.eventInit("CircleTrack", "Player");
    host.raceEvent("CircleTrack");
    host.session(6, 2);
    host.addEntry(1, "Player");
    host.trackCenterline(circleTrack(), {});
    host.classify(6, 120000, { { .num = 1, .best = 90000, .gap = 0 } });
    MapVisible(1);

    auto quadCount = [&]() {
        host.draw();
        double sx = 0, sy = 0; int bad = 0;
        const int n = MapStatsFn(&sx, &sy, &bad);
        CHECK(bad == 0);
        return n;
    };

    // ON the track: the marker is drawn like any other.
    host.raceTrackPosition({ { .num = 1, .trackPos = 0.10f,
                               .posX = 0.0f, .posZ = 160.0f, .yaw = 0.0f } });
    const int onMap = quadCount();
    REQUIRE(onMap > 0);

    // FAR off it, in each direction in turn. The count must not move: a dropped
    // marker is exactly one quad lighter, which is the bug this pins.
    struct Corner { const char* what; float x, z; };
    const Corner corners[] = {
        { "west",  -100000.0f,      0.0f },
        { "east",   100000.0f,      0.0f },
        { "north",       0.0f, -100000.0f },
        { "south",       0.0f,  100000.0f },
        { "far corner", 100000.0f, 100000.0f },
    };
    const auto panel = host.hudScreenEdges("map_hud");
    for (const Corner& c : corners) {
        // yaw 45 degrees: the rotated icon's half-extent is at its maximum there
        // (h * (|cos| + |sin|) = 1.41h), so a clamp that inset by the unrotated
        // half-size lets a corner of the sprite hang outside the panel.
        host.raceTrackPosition({ { .num = 1, .trackPos = 0.10f,
                                   .posX = c.x, .posZ = c.z, .yaw = 45.0f } });
        INFO("off-map to the " << std::string(c.what));
        CHECK(quadCount() == onMap);

        // ...and it landed INSIDE the panel rather than merely being emitted.
        for (const auto& q : host.hudQuadRects("map_hud")) {
            CHECK(q.l >= panel.l / 1e6 - 1e-4);
            CHECK(q.t >= panel.t / 1e6 - 1e-4);
            CHECK(q.r <= panel.r / 1e6 + 1e-4);
            CHECK(q.b <= panel.b / 1e6 + 1e-4);
        }
    }

    host.shutdown();
}

// ============================================================================
// ZOOM MODE: AN ARROW POINTS BACK AT THE TRACK WHEN THE TRACK LEAVES THE VIEW.
//
// The clamp above is the FULL-TRACK view's answer and does nothing here, because
// zoom centres the view on the player (calculateZoomBounds): the player's icon
// cannot leave the panel, so there is nothing to clamp. What leaves is the track.
// Ride far enough off and the map is an empty box with your own arrow in the
// middle of it -- exactly when it is worth looking at, and exactly when it stopped
// saying anything.
//
// Counted rather than located: the pointer is one extra quad, and its absence is
// the regression. Its DIRECTION is asserted separately below, by putting the
// player on each side of the track in turn and checking the arrow lands on the
// matching edge -- an arrow pinned to the wrong edge is worse than none, and a
// count alone cannot tell the two apart.
// ============================================================================
TEST_CASE("map: zoom mode points back at the track when the track is off-view") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map_pointer\\") >= 0);

    auto MapVisible = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapZoom    = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetZoom");
    auto MapStatsFn = host.sym<PFN_MapQuadStats>("MXBMRP3_Test_MapQuadStats");
    REQUIRE(MapVisible);
    REQUIRE(MapZoom);
    REQUIRE(MapStatsFn);

    host.eventInit("CircleTrack", "Player");
    host.raceEvent("CircleTrack");
    host.session(6, 2);
    host.addEntry(1, "Player");
    host.trackCenterline(circleTrack(), {});
    host.classify(6, 120000, { { .num = 1, .best = 90000, .gap = 0 } });
    MapVisible(1);
    MapZoom(1);

    auto quadCount = [&]() {
        host.draw();
        double sx = 0, sy = 0; int bad = 0;
        const int n = MapStatsFn(&sx, &sy, &bad);
        CHECK(bad == 0);
        return n;
    };

    // ON the track: the centerline is right under the player, so no pointer.
    //
    // (0,0), NOT a point computed from the radius. circleTrack() integrates 64
    // curve segments from the ORIGIN, so the circle passes through (0,0) with its
    // centre a radius away -- it is not centred on the origin, and a first attempt
    // here that assumed it was put the "on track" player a full diameter off the
    // ribbon and measured a map with no track in it at all.
    host.raceTrackPosition({ { .num = 1, .trackPos = 0.0f,
                               .posX = 0.0f, .posZ = 0.0f, .yaw = 0.0f } });
    const int onTrack = quadCount();
    REQUIRE_MESSAGE(onTrack > 1,
                    "zoom view drew " << onTrack << " quad(s) with the player ON the "
                    "centerline -- the track is not in view, so this case would be "
                    "asserting nothing");

    // WAY off it, further than the zoom range (default 100m, so 50m each way).
    // Every quad must still be inside the panel: the arrow is pinned to the edge,
    // which is only useful if "the edge" means the panel's and not somewhere past it.
    const auto panel = host.hudScreenEdges("map_hud");
    host.raceTrackPosition({ { .num = 1, .trackPos = 0.0f,
                               .posX = 0.0f, .posZ = 4000.0f, .yaw = 45.0f } });
    // EXACTLY TWO: the player's own marker, and the pointer. (Not a background --
    // an unthemed map draws none, which is why the count is this small at all.)
    // Not "onTrack + 1" -- a
    // first attempt asserted that and was wrong in an instructive way: when the
    // player is 4km out, the whole ribbon culls away, so the count DROPS from 126
    // to 2 rather than rising by one. The track quads going is the premise of the
    // feature, not a regression.
    //
    // 2 is what makes this discriminating: without the pointer the same frame emits
    // exactly 1 (measured), so the assertion fails by one quad in the direction that
    // matters and cannot be satisfied by the ribbon coming back.
    const int offTrack = quadCount();
    CHECK_MESSAGE(offTrack == 2,
                  "expected the player's marker + the pointer with the track "
                  "off-view; got " << offTrack << " quad(s)");
    const auto quads = host.hudQuadRects("map_hud");
    for (const auto& q : quads) {
        CHECK(q.l >= panel.l / 1e6 - 1e-4);
        CHECK(q.t >= panel.t / 1e6 - 1e-4);
        CHECK(q.r <= panel.r / 1e6 + 1e-4);
        CHECK(q.b <= panel.b / 1e6 + 1e-4);
    }

    // NEAR THE PLAYER, AND CLEAR OF THEM. Two failures to pin, in opposite
    // directions, and the bounds check above catches neither:
    //
    //   * ON TOP of the player -- what a broken clamp produces, and what a
    //     screenshot of this scene first looked like. "Inside the panel" is
    //     perfectly satisfied by an arrow sitting under the rider's own icon.
    //   * OUT AT THE EDGE -- where this feature started. On a 390px map that put
    //     the arrow ~240px from the rider, almost the furthest away it could be,
    //     reading as an unrelated marker in a corner rather than as a direction
    //     from the player. That version passed an "is it at an edge" assertion,
    //     which is why that assertion is gone rather than loosened.
    //
    // The pointer is the LAST quad (drawn after the riders); the player's marker is
    // the other one and zoom centres it, so the two are measured against each other
    // and against the panel's half-extent.
    REQUIRE(quads.size() == 2);
    const auto& mark = quads.front();
    const auto& arrow = quads.back();
    const double cx = (panel.l + panel.r) / 2e6, cy = (panel.t + panel.b) / 2e6;
    const double halfW = (panel.r - panel.l) / 2e6, halfH = (panel.b - panel.t) / 2e6;
    auto offCentre = [&](const PluginHost::QuadRect& q) {
        return std::max(std::fabs((q.l + q.r) * 0.5 - cx) / halfW,
                        std::fabs((q.t + q.b) * 0.5 - cy) / halfH);
    };
    INFO("player off-centre " << offCentre(mark) << ", arrow " << offCentre(arrow));
    CHECK(offCentre(mark) < 0.10);     // zoom centres the player, by construction
    CHECK(offCentre(arrow) > offCentre(mark));   // the arrow is OUTSIDE the player...
    CHECK(offCentre(arrow) < 0.60);              // ...but nowhere near the edge

    // NOT TOUCHING: the rects must not overlap, which is the "clear of the icon and
    // the number" requirement stated as geometry. The ring radius is built from the
    // player's boosted icon, the label gap and the label's own line, so this is the
    // assertion that fails if any of those terms is dropped from the sum.
    const bool overlaps = !(arrow.r < mark.l || arrow.l > mark.r ||
                            arrow.b < mark.t || arrow.t > mark.b);
    CHECK_FALSE_MESSAGE(overlaps,
                        "arrow " << arrow.l << "," << arrow.t << ".." << arrow.r << ","
                        << arrow.b << " overlaps the player's marker "
                        << mark.l << "," << mark.t << ".." << mark.r << "," << mark.b);

    // ...and it TRACKS the direction rather than sitting wherever it first landed.
    // Asserted as a symmetry -- leaving the track far one way and far the other puts
    // the arrow on OPPOSITE halves of the panel -- rather than as "+Z means the top
    // edge". The claim that matters is that the arrow follows where the track went;
    // which screen edge a world axis maps to is the view transform's business, and
    // writing this test against my reading of that convention would pin the reading
    // rather than the behaviour.
    const double midY = (panel.t + panel.b) / 2e6;
    auto pointerY = [&](float posZ) {
        host.raceTrackPosition({ { .num = 1, .trackPos = 0.0f,
                                   .posX = 0.0f, .posZ = posZ, .yaw = 0.0f } });
        host.draw();
        // The pointer is the LAST quad the map emits (drawn after the riders).
        const auto quads = host.hudQuadRects("map_hud");
        REQUIRE_FALSE(quads.empty());
        const auto& q = quads.back();
        return (q.t + q.b) * 0.5;
    };
    const double north = pointerY(4000.0f);
    const double south = pointerY(-4000.0f);
    INFO("arrow y: +Z=" << north << " -Z=" << south << " mid=" << midY);
    CHECK(((north < midY) != (south < midY)));   // opposite halves
    CHECK(std::fabs(north - south) > 1e-3);      // ...and genuinely apart

    host.shutdown();
}

// ============================================================================
// THE POINTER DOES NOT JITTER WHEN RIDING ALONGSIDE THE TRACK.
//
// Reported from play: driving parallel to the track but off it -- which is when
// this arrow is most used -- it twitched. Two independent causes, both of which
// this case would have caught:
//
//   * VERTEX QUANTISATION. Aiming at the nearest ribbon SAMPLE means the winner
//     changes as you pass each vertex, so the bearing sawtooths and snaps back.
//     Fixed by projecting onto the segment.
//   * LOBE FLIPPING. The coarse probe can change which stretch of track wins
//     between frames when two are at similar distance. Fixed by hysteresis.
//
// Measured as the FRAME-TO-FRAME CHANGE in the arrow's bearing about the panel
// centre, along a straight parallel run. The bearing must sweep smoothly, so
// what is asserted is that no single step is a large fraction of the whole
// sweep: a sawtooth or a lobe flip shows up as one step dwarfing its neighbours,
// which an average or a total would hide completely.
// ============================================================================
TEST_CASE("map: the off-track pointer sweeps smoothly along a parallel run") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map_render\\") >= 0);

    auto MapVisible = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetVisible");
    auto MapZoom    = host.sym<PFN_MapI>("MXBMRP3_Test_MapSetZoom");
    REQUIRE(MapVisible);
    REQUIRE(MapZoom);
    REQUIRE(host.hasScreenEdges());

    host.eventInit("CircleTrack", "Player");
    host.raceEvent("CircleTrack");
    host.session(6, 2);
    host.addEntry(1, "Player");
    host.trackCenterline(circleTrack(), {});
    host.classify(6, 120000, { { .num = 1, .best = 90000, .gap = 0 } });
    MapVisible(1);
    MapZoom(1);

    const auto panel = host.hudScreenEdges("map_hud");
    const double cx = (panel.l + panel.r) / 2e6, cy = (panel.t + panel.b) / 2e6;

    // A straight run PAST the track, parallel to it, 60m off.
    //
    // THE GEOMETRY IS THE EXPERIMENT, and a first attempt at it proved nothing: with
    // 5m steps against a ribbon sampled every couple of metres, the player skips
    // whole vertices each frame and the sawtooth averages out -- that version passed
    // with both fixes disabled. Jitter needs steps SMALLER than the vertex spacing,
    // so several frames fall between two vertices and the handover shows as one big
    // step among small ones. Hence 0.25m.
    //
    // circleTrack integrates from the origin, so the circle's centre is (r, 0) and
    // its far side is at x = 2r. Sitting 60m beyond that, the nearest track point is
    // on the far side and the local tangent is vertical -- so stepping in z is a
    // genuine parallel run, and the bearing should barely move except for the sweep.
    // 60m also puts the whole ribbon outside the 100m zoom window, so the pointer is
    // the only thing drawn.
    const double r = 1600.0 / (2.0 * 3.14159265);
    std::vector<double> bearings;
    for (int i = 0; i <= 160; ++i) {
        const float x = static_cast<float>(2.0 * r + 60.0);
        const float z = -20.0f + i * 0.25f;
        host.raceTrackPosition({ { .num = 1, .trackPos = 0.0f,
                                   .posX = x, .posZ = z, .yaw = 0.0f } });
        host.draw();
        const auto quads = host.hudQuadRects("map_hud");
        REQUIRE_MESSAGE(quads.size() == 2,
                        "expected the player's marker + the pointer at step " << i
                        << "; got " << quads.size() << " quad(s)");
        const auto& arrow = quads.back();
        bearings.push_back(std::atan2((arrow.t + arrow.b) * 0.5 - cy,
                                      (arrow.l + arrow.r) * 0.5 - cx));
    }

    // Steps, unwrapped across the +/-pi seam so a legitimate sweep through it is
    // not read as a jump.
    double maxStep = 0.0, totalSweep = 0.0;
    for (size_t i = 1; i < bearings.size(); ++i) {
        double d = bearings[i] - bearings[i - 1];
        while (d >  3.14159265) d -= 2 * 3.14159265;
        while (d < -3.14159265) d += 2 * 3.14159265;
        totalSweep += std::fabs(d);
        maxStep = std::max(maxStep, std::fabs(d));
    }
    const double meanStep = totalSweep / static_cast<double>(bearings.size() - 1);
    // Reported unconditionally, not just on failure: this is a SMOOTHNESS number and
    // it can drift a long way while still passing a 6x ratio, so the log is where a
    // slow regression would be visible before the gate trips.
    MESSAGE("pointer sweep: max step " << maxStep << " rad, mean " << meanStep
            << ", ratio " << (maxStep / meanStep) << ", total " << totalSweep);
    INFO("max step " << maxStep << " rad, mean " << meanStep
         << ", total sweep " << totalSweep);
    REQUIRE(totalSweep > 0.05);          // the arrow really did move; else this proves nothing

    // AN ABSOLUTE BOUND is the primary claim, not a ratio against the mean. The
    // mean here is ~0.0014 rad, so a ratio test divides by a near-zero and swings
    // wildly for reasons that have nothing to do with smoothness -- it measured
    // 5.1x after the fix against a 6x threshold, which would have made this gate a
    // tripwire for unrelated changes. What a player can actually see is the SIZE of
    // the worst single jump, and that is what this pins.
    //
    // 0.03 rad is 1.7 degrees. Measured: 0.007 (0.4 deg) with the projection and
    // hysteresis in place, against 0.169 (9.7 deg) with both disabled -- so this sits
    // a factor of four above the fixed behaviour and a factor of five below the
    // broken one, which is the room a threshold wants on both sides.
    CHECK_MESSAGE(maxStep < 0.03,
                  "one frame turned the arrow " << maxStep << " rad ("
                  << (maxStep * 57.2958) << " deg) -- a sawtooth or a lobe flip, "
                  "not a sweep");
}
