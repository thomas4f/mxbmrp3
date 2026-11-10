// ============================================================================
// tests/integration/tests/slide_widget_test.cpp
// The Slide widget (hud/slide_widget.h), driven through real RunTelemetry frames:
//   - it opens the same size as the Lean widget, the sibling it is drawn after
//   - a slide with the rear out to the right (travel right of the nose) fills the
//     RIGHT half of the bowl, the mirror slide the LEFT half, and riding straight
//     fills neither
//   - airborne, the same heading/velocity split is a whip, not a slide: no fill
//   - spectating, the game sends no chassis velocity for the rider watched, so
//     the bowl stays empty rather than showing the player's own last slide
// The angle itself (signs, wrap, speed floor) is unit-tested in test_slip_angle.cpp;
// this pins that the widget reads it and puts it on the right side.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <string>
#include <vector>

namespace {
const char* const kSlide = "slide_widget";

void setUp(PluginHost& host, const char* saveWin) {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(saveWin, "[Settings]\nversion=11\n\n[SlideWidget]\nvisible=1\n\n"
                                    "[LeanWidget]\nvisible=1\n");
    host.loadSettings(saveWin);
    host.setMotion(0);
    host.eventInit("SlideTrack", "Player");
    host.drawWithState(0);
}

// One telemetry frame heading north (yaw 0) and travelling `pathDeg` clockwise of
// it at 15 m/s, then enough frames for the needle's smoothing to settle.
void ride(PluginHost& host, float pathDeg, bool grounded = true, int drawState = 0) {
    TelemetryRow row;
    row.speed = 15.0f;
    row.yaw = 0.0f;
    const float rad = pathDeg * 3.14159265f / 180.0f;
    row.velX = 15.0f * std::sin(rad);
    row.velZ = 15.0f * std::cos(rad);
    row.frontMaterial = grounded ? 1 : 0;
    row.rearMaterial = grounded ? 1 : 0;
    host.telemetryFrame(row);
    for (int i = 0; i < 40; ++i) host.drawWithState(drawState);
}

// The quads drawn beyond the idle widget's (the fill, while the slide is still
// building, so no peak marker is up yet), as their horizontal centres.
std::vector<double> fillCentres(PluginHost& host, size_t idleCount) {
    const auto rects = host.hudQuadRects(kSlide);
    std::vector<double> out;
    for (size_t i = idleCount; i < rects.size(); ++i)
        out.push_back((rects[i].l + rects[i].r) * 0.5);
    return out;
}

double panelCentre(PluginHost& host) {
    const auto e = host.hudScreenEdges(kSlide);
    return (e.l + e.r) * 0.5 / 1e6;
}
}  // namespace

TEST_CASE("slide widget: opens the same size as the Lean widget") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\slide_size\\");
    ride(host, 0.0f);
    const auto slide = host.hudScreenEdges(kSlide);
    const auto lean = host.hudScreenEdges("lean_widget");
    REQUIRE(slide.r > slide.l);
    REQUIRE(lean.r > lean.l);
    CHECK(slide.r - slide.l == lean.r - lean.l);
    CHECK(slide.b - slide.t == lean.b - lean.t);
    host.shutdown();
}

TEST_CASE("slide widget: the fill swings to the side the rear steps out to") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\slide_sides\\");

    ride(host, 0.0f);   // straight: the empty bowl
    const size_t idle = host.hudQuadRects(kSlide).size();
    REQUIRE(idle > 0);
    const double centre = panelCentre(host);

    ride(host, 20.0f);  // travelling right of the nose: rear out to the right
    auto fill = fillCentres(host, idle);
    REQUIRE(!fill.empty());
    for (double x : fill) CHECK(x > centre);

    ride(host, 0.0f);   // back to straight: settle, and let the right peak lapse
    for (int i = 0; i < 120; ++i) host.drawWithState(0);
    CHECK(host.hudQuadRects(kSlide).size() == idle);

    ride(host, -20.0f); // the mirror slide
    fill = fillCentres(host, idle);
    REQUIRE(!fill.empty());
    for (double x : fill) CHECK(x < centre);
    host.shutdown();
}

TEST_CASE("slide widget: no slide in the air, and none shown while spectating") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\slide_gates\\");
    ride(host, 0.0f);
    const size_t idle = host.hudQuadRects(kSlide).size();

    ride(host, 30.0f, /*grounded=*/false);
    CHECK(host.hudQuadRects(kSlide).size() == idle);

    // Spectating: the player's own telemetry is not the rider on screen.
    ride(host, 30.0f, /*grounded=*/true, /*drawState=*/1);
    CHECK(host.hudQuadRects(kSlide).size() == idle);
    host.shutdown();
}
