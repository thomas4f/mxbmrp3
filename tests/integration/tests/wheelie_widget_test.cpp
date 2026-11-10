// ============================================================================
// tests/integration/tests/wheelie_widget_test.cpp
// The Wheelie widget (hud/wheelie_widget.h), driven through real RunTelemetry
// callbacks:
//   - it reads the nose-up angle only while the bike is on its back wheel: the
//     same pitch with both wheels down (a hill) or both up (a jump) reads 0,
//     which is the shared rule in core/wheelie_angle.h reaching the screen
//   - spectating it shows N/A, because pitch and wheel contact are the player's
//     own telemetry only (the G-force widget's rule)
//   - it is exactly the Lean widget's size: the small gauges tile in a row
//   - its quarter ring fills the content box: the arc reaches as far right and
//     as high as the box allows, rather than sitting as a circle in a corner of
//     a box that is wider than it is tall
// The protractor arithmetic itself is pinned by tests/unit/test_wheelie_arc.cpp.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
const char* const kWheelie = "wheelie_widget";

void setUp(PluginHost& host, const char* saveWin) {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasStringRows());
    host.writeSettingsFile(saveWin,
        "[Settings]\nversion=10\n\n[WheelieWidget]\nvisible=1\n\n[LeanWidget]\nvisible=1\n");
    host.loadSettings(saveWin);
    host.eventInit("WheelieTrack", "Player", 1600.0f);
    host.runInit(1);
    host.drawWithState(0);
}

// Hold one posture long enough for the widget's smoothing to settle, then read
// the value it shows.
std::string ride(PluginHost& host, float pitch, int front, int rear, int state = 0) {
    for (int i = 0; i < 80; ++i) {
        TelemetryRow row;
        row.pitch = pitch;
        row.frontMaterial = front;
        row.rearMaterial = rear;
        host.telemetryFrame(row);
        host.drawWithState(state);
    }
    const auto rows = host.hudStringRows(kWheelie);
    return rows.empty() ? std::string() : rows.back().text;
}
}  // namespace

TEST_CASE("wheelie widget: the angle reads only on the back wheel") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\wheelie_value\\");
    CHECK(ride(host, -40.0f, 0, 1) == "40");   // a wheelie, nose 40 degrees up
    CHECK(ride(host, -40.0f, 1, 1) == "0");    // the same pitch up a hill
    CHECK(ride(host, -25.0f, 0, 1) == "25");
    CHECK(ride(host, -25.0f, 0, 0) == "0");    // the same pitch in the air
    host.shutdown();
}

TEST_CASE("wheelie widget: spectating shows N/A") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\wheelie_spectate\\");
    CHECK(ride(host, -40.0f, 0, 1, /*SPECTATE*/ 1) == "N/A");
    host.shutdown();
}

TEST_CASE("wheelie widget: the Lean widget's size, its arc filling the box") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\wheelie_size\\");
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasQuadRects());
    ride(host, 0.0f, 1, 1);
    const PluginHost::ScreenEdges lean = host.hudScreenEdges("lean_widget");
    const PluginHost::ScreenEdges wheelie = host.hudScreenEdges(kWheelie);
    REQUIRE(wheelie.r > wheelie.l);
    CHECK(wheelie.r - wheelie.l == lean.r - lean.l);
    CHECK(wheelie.b - wheelie.t == lean.b - lean.t);

    // The ring is drawn after the panel's own quads; with both wheels down it is
    // the background quarter alone, the last quads the widget drew. Its extent
    // spans the content box: as wide as the panel less its padding either side,
    // as tall as it less its padding above and below -- one box, filled.
    const auto rects = host.hudQuadRects(kWheelie);
    REQUIRE(rects.size() > 24);
    double l = 1e9, t = 1e9, r = -1e9, b = -1e9;
    for (size_t i = rects.size() - 24; i < rects.size(); ++i) {
        l = std::min(l, rects[i].l); t = std::min(t, rects[i].t);
        r = std::max(r, rects[i].r); b = std::max(b, rects[i].b);
    }
    // The panel's background is its first quad, in the same (offset) space.
    const PluginHost::QuadRect& panel = rects.front();
    const double padX = l - panel.l;
    const double padTop = t - panel.t;
    CAPTURE(padX);
    CAPTURE(padTop);
    CHECK(padX > 0.0);
    CHECK(padTop > 0.0);
    // The right gap equals the left one and the bottom gap the top one, to a
    // hair: the arc spans the box, it does not sit in its corner.
    CHECK(std::abs((panel.r - r) - padX) < 0.0005);
    CHECK(std::abs((panel.b - b) - padTop) < 0.0005);
    host.shutdown();
}
