// ============================================================================
// tests/integration/tests/map_view_test.cpp
// The Map's views beyond the flat, whole-track map (hud/map_hud_view.cpp):
//   - EDGE FADE: zoomed, the track runs off the map. The ribbon is cut exactly
//     at the clip rect (no quad's centre lies outside it) and the quads near
//     the edge are faded; not zoomed, nothing is faded, so the default map is
//     unchanged.
//   - TILT (Map > Tilt, zoomed only): the track farther away (the top of the
//     map) is drawn narrower than the track close by (the bottom), and is cut
//     and faded at the edge like the flat zoomed map. At Mode Overview the
//     setting is greyed out and the map is flat (Thomas, 1.32: the whole track
//     tilted adds little).
//   - ADAPTIVE RANGE (Map > Adaptive range): Follow's window doubles at speed,
//     eased in over Motion's clock, and comes back to Range when slow.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <vector>

typedef void (*PFN_I)(int);
typedef int (*PFN_ViewQuads)(float*, int*, int*, float*);

namespace {
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

struct Read { int quads = 0, faded = 0, outside = 0; float top = 0.0f, bottom = 0.0f; };
}  // namespace

TEST_CASE("map views: the zoomed map fades at its edge, the tilted map narrows with distance") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\map_view\\") >= 0);

    auto Visible = host.sym<PFN_I>("MXBMRP3_Test_MapSetVisible");
    auto Zoom    = host.sym<PFN_I>("MXBMRP3_Test_MapSetZoom");
    auto SetTilt = host.sym<PFN_I>("MXBMRP3_Test_MapSetTilt");
    auto SetAdaptive = host.sym<PFN_I>("MXBMRP3_Test_MapSetAdaptiveRange");
    auto Quads   = host.sym<PFN_ViewQuads>("MXBMRP3_Test_MapViewQuads");
    REQUIRE_MESSAGE((Visible && Zoom && SetTilt && SetAdaptive && Quads && host.hasMotion()), "test hooks not exported (test build?)");

    host.eventInit("ViewTrack", "Alice");
    host.raceEvent("ViewTrack");
    host.session(1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(1, "Alice");
    host.addEntry(2, "Bob");
    host.trackCenterline(circleTrack(), { 1.0f, 400.0f, 1200.0f, 0.0f });
    host.raceTrackPosition({
        { .num = 1, .trackPos = 0.0f, .posX = 0.0f, .posZ = 0.0f, .yaw = 0.0f },   // on the centreline's start
        { .num = 2, .trackPos = 0.60f },
    });
    Visible(1);

    auto frame = [&]() {
        host.drawWithState(0);
        host.drawWithState(0);
        Read r;
        float w[2] = {};
        r.quads = Quads(nullptr, &r.faded, &r.outside, w);
        r.top = w[0];
        r.bottom = w[1];
        return r;
    };

    SUBCASE("flat") {
        SetTilt(0);
        Zoom(0);
        const Read full = frame();
        INFO("full: quads=" << full.quads << " faded=" << full.faded << " top=" << full.top << " bottom=" << full.bottom);
        REQUIRE(full.quads > 50);
        CHECK(full.faded == 0);                       // the whole-track map is as it was
        CHECK(full.outside == 0);
        CHECK(full.top == doctest::Approx(full.bottom).epsilon(0.05));

        Zoom(1);
        const Read zoomed = frame();
        INFO("zoomed: quads=" << zoomed.quads << " faded=" << zoomed.faded << " outside=" << zoomed.outside);
        REQUIRE(zoomed.quads > 10);
        CHECK(zoomed.faded > 4);                      // the stretch running off the map fades
        CHECK(zoomed.outside == 0);                   // and is cut at the clip rect
    }

    SUBCASE("tilted") {
        SetTilt(40);
        Zoom(0);
        const Read full = frame();
        INFO("full: quads=" << full.quads << " faded=" << full.faded << " top=" << full.top << " bottom=" << full.bottom);
        REQUIRE(full.quads > 50);
        CHECK(full.faded == 0);                       // Full range: the flat map, untouched
        CHECK(full.outside == 0);
        CHECK(full.top == doctest::Approx(full.bottom).epsilon(0.05));

        Zoom(1);
        const Read zoomed = frame();
        INFO("tilted zoomed: quads=" << zoomed.quads << " faded=" << zoomed.faded << " outside=" << zoomed.outside
             << " top=" << zoomed.top << " bottom=" << zoomed.bottom);
        REQUIRE(zoomed.quads > 10);
        REQUIRE(zoomed.bottom > 0.0f);
        CHECK(zoomed.top < 0.85f * zoomed.bottom);    // farther is narrower
        CHECK(zoomed.faded > 4);
        CHECK(zoomed.outside == 0);

        SetTilt(0);                                   // and back: the flat zoomed map again
        const Read flat = frame();
        CHECK(flat.quads > 10);
        CHECK(flat.outside == 0);
        CHECK(flat.top == doctest::Approx(flat.bottom).epsilon(0.05));   // not the cached tilted ribbon
    }

    SUBCASE("tilt is an amount") {
        Zoom(1);
        SetTilt(40);
        const Read full = frame();
        SetTilt(20);
        const Read half = frame();
        SetTilt(50);
        const Read most = frame();
        INFO("top/bottom at 20: " << half.top / half.bottom << ", 40: " << full.top / full.bottom
             << ", 50: " << most.top / most.bottom);
        REQUIRE(half.quads > 10);
        REQUIRE(most.quads > 10);
        REQUIRE(half.bottom > 0.0f);
        REQUIRE(most.bottom > 0.0f);
        // Farther narrows more the further the map lays down (its own ribbon, not the cache).
        CHECK(half.top / half.bottom > full.top / full.bottom + 0.05f);
        CHECK(most.top / most.bottom < full.top / full.bottom - 0.05f);
        CHECK(half.outside == 0);
        CHECK(most.outside == 0);
        CHECK(most.faded > 4);
    }

    SUBCASE("adaptive range grows with speed and eases back") {
        SetTilt(0);
        Zoom(1);
        SetAdaptive(1);
        // Motion's clock drives the ease: one 16 ms step per frame.
        long long now = 1000000;
        auto ride = [&](float speedMs, int frames) {
            Read r{};
            for (int i = 0; i < frames; ++i) {
                now += 16000;
                host.setMotionNowUs(now);
                host.telemetry(speedMs);
                r = frame();
            }
            return r;
        };
        const Read slow = ride(0.0f, 4);
        const Read first = ride(40.0f, 1);   // eases, does not jump
        const Read fast = ride(40.0f, 120);
        const Read back = ride(0.0f, 120);
        INFO("track width slow " << slow.bottom << ", first fast frame " << first.bottom
             << ", fast " << fast.bottom << ", slow again " << back.bottom);
        REQUIRE(slow.bottom > 0.0f);
        CHECK(first.bottom > 0.9f * slow.bottom);
        CHECK(fast.bottom == doctest::Approx(0.5f * slow.bottom).epsilon(0.1));   // twice the range
        CHECK(back.bottom == doctest::Approx(slow.bottom).epsilon(0.05));
        CHECK(fast.outside == 0);

        SetAdaptive(0);                              // off: Range again at any speed
        const Read off = ride(40.0f, 2);
        CHECK(off.bottom == doctest::Approx(slow.bottom).epsilon(0.05));
        host.setMotionNowUs(-1);
    }

    host.shutdown();
}
