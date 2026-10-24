// ============================================================================
// tests/integration/tests/rpm_widget_test.cpp
// The RPM widget's shift-light strip (hud/rpm_widget.h):
//   - by default it spans the Gear and Speed widgets, left edge to right edge,
//     and sits under them
//   - segments light left to right over the top half of the rev range: green,
//     amber, then red from the shift point
//   - on the limiter the red segments flash, on Motion's clock, and the green
//     and amber ones stay lit
//   - spectating, the shift point is not the rider's: plain rpm, no flash
//   - a vehicle without a limiter (KRP's direct-drive karts report 0) lights
//     over its max rpm instead, and never flashes
//   - [RpmWidget] segments / segmentGaps / width / vertical (INI-only) reshape
//     the strip; vertical fills bottom to top at the Gear widget's height
//   - the newest light fades up over its stretch of revs in four steps, from
//     where a whole light would come on: the first red one shows at the shift
//     point, a quarter of the way up; nothing extra is drawn for it
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int SHIFT = 10500;
constexpr int LIMITER = 12000;
const char* const kRpm = "rpm_widget";

struct Segment { PluginHost::QuadRect rect; unsigned long color = 0; };

// The strip's segments: the last `count` quads the widget drew (a background,
// when one is drawn, comes first).
std::vector<Segment> segments(PluginHost& host, size_t count) {
    host.drawWithState(0);   // on track: the telemetry is the player's own
    host.drawWithState(0);
    const auto rects = host.hudQuadRects(kRpm);
    std::vector<Segment> out;
    if (rects.size() < count) return out;
    for (size_t i = rects.size() - count; i < rects.size(); ++i)
        out.push_back({ rects[i], host.quadColor(kRpm, static_cast<int>(i)) });
    return out;
}

unsigned alpha(unsigned long c) { return static_cast<unsigned>((c >> 24) & 0xFF); }
unsigned long rgb(unsigned long c) { return c & 0x00FFFFFFul; }

// An unlit segment is drawn at RpmWidget::UNLIT_OPACITY (0x3F); the newest
// lit one at least a step above it.
constexpr unsigned UNLIT_ALPHA = 0x3F;
bool on(const Segment& seg) { return alpha(seg.color) > UNLIT_ALPHA + 8; }

// The lit segments, whole or fading up.
int litCount(const std::vector<Segment>& s) {
    int n = 0;
    for (const auto& seg : s) if (on(seg)) ++n;
    return n;
}

void ride(PluginHost& host, int rpm) {
    TelemetryRow row;
    row.gear = 3;
    row.rpm = rpm;
    host.telemetryFrame(row);
}

void setUp(PluginHost& host, const char* saveWin, const std::string& rpmSection = "") {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasInkHooks());
    REQUIRE(host.hasMotion());
    host.writeSettingsFile(saveWin, "[Settings]\nversion=10\n\n[RpmWidget]\nvisible=1\n" + rpmSection);
    host.loadSettings(saveWin);
    host.setMotion(0);
    host.eventInit("RpmTrack", "Player", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 0.0f, SHIFT, LIMITER);
    host.drawWithState(0);
}
}  // namespace

TEST_CASE("rpm widget: by default it spans Gear and Speed and sits under them") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_span\\");
    REQUIRE(host.hasScreenEdges());
    host.drawWithState(0);
    const PluginHost::ScreenEdges gear = host.hudScreenEdges("gear_widget");
    const PluginHost::ScreenEdges speed = host.hudScreenEdges("speed_widget");
    const PluginHost::ScreenEdges rpm = host.hudScreenEdges(kRpm);
    REQUIRE(rpm.r > rpm.l);
    CHECK(rpm.l == gear.l);
    CHECK(rpm.r == speed.r);
    CHECK(rpm.t >= gear.b);
    CHECK(rpm.t >= speed.b);
    host.shutdown();
}

TEST_CASE("rpm widget: segments light green, amber, then red from the shift point") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_light\\");

    ride(host, 4000);   // under the strip's range
    auto s = segments(host, 15);
    REQUIRE(s.size() == 15);
    CHECK(litCount(s) == 0);

    ride(host, LIMITER / 2);   // the range starts: one light
    s = segments(host, 15);
    CHECK(litCount(s) == 1);

    ride(host, LIMITER - 1);   // just under the limiter: all of them, no flash yet
    s = segments(host, 15);
    CHECK(litCount(s) == 15);
    // Left to right: the first is green, the last red, and there is an amber
    // band between them.
    const unsigned long first = rgb(s.front().color), last = rgb(s.back().color);
    CHECK(first != last);
    bool amber = false;
    for (const auto& seg : s)
        if (rgb(seg.color) != first && rgb(seg.color) != last) amber = true;
    CHECK(amber);
    // The first red segment is the one the shift point lights.
    int firstRed = -1;
    for (int i = 0; i < 15; ++i)
        if (rgb(s[static_cast<size_t>(i)].color) == last) { firstRed = i; break; }
    REQUIRE(firstRed > 0);
    ride(host, SHIFT - 1);
    CHECK(litCount(segments(host, 15)) == firstRed);
    ride(host, SHIFT);
    CHECK(litCount(segments(host, 15)) == firstRed + 1);

    // Left to right, without overlapping.
    for (size_t i = 1; i < s.size(); ++i) CHECK(s[i].rect.l >= s[i - 1].rect.r);
    host.shutdown();
}

TEST_CASE("rpm widget: on the limiter only the red segments flash") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_limiter\\");
    ride(host, LIMITER - 1);
    const auto under = segments(host, 15);
    const unsigned long red = rgb(under.back().color);
    int reds = 0;
    for (const auto& seg : under) if (rgb(seg.color) == red) ++reds;
    REQUIRE(reds > 0);
    REQUIRE(reds < 15);

    host.setMotionNowUs(1000000);
    ride(host, LIMITER);
    auto s = segments(host, 15);
    CHECK(litCount(s) == 15);
    // The colour bands stay as they are; the limiter does not repaint the strip.
    for (size_t i = 0; i < s.size(); ++i) CHECK(rgb(s[i].color) == rgb(under[i].color));

    host.setMotionNowUs(1000000 + 100000);   // half a period on: the red ones dark
    s = segments(host, 15);
    CHECK(litCount(s) == 15 - reds);
    for (const auto& seg : s)
        CHECK((alpha(seg.color) == 0xFF) == (rgb(seg.color) != red));

    host.setMotionNowUs(1000000 + 200000);   // and lit again
    CHECK(litCount(segments(host, 15)) == 15);

    host.setMotionNowUs(-1);
    host.shutdown();
}

TEST_CASE("rpm widget: spectating shows plain rpm, without the flash") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_spectate\\");
    host.raceEvent("RpmTrack");
    host.session(6, 10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*curSelection=*/1);   // camera on #22

    // The spectated rider's segments: the last 15 quads of a SPECTATE draw.
    const auto watched = [&host](int rpm) {
        host.raceVehicleData(22, 30.0f, 4, rpm, 1.0f, 0.0f, 0.0f);
        host.draw();
        host.draw();
        const auto rects = host.hudQuadRects(kRpm);
        std::vector<Segment> out;
        for (size_t i = rects.size() >= 15 ? rects.size() - 15 : 0; i < rects.size(); ++i)
            out.push_back({ rects[i], host.quadColor(kRpm, static_cast<int>(i)) });
        return out;
    };

    auto s = watched(LIMITER / 2);   // the range starts: one light
    REQUIRE(s.size() == 15);
    CHECK(litCount(s) == 1);
    CHECK(litCount(watched(LIMITER - 1)) == 15);

    // On the limiter the strip stays lit through both halves of the flash.
    host.setMotionNowUs(1000000);
    CHECK(litCount(watched(LIMITER)) == 15);
    host.setMotionNowUs(1000000 + 100000);
    CHECK(litCount(watched(LIMITER)) == 15);

    host.setMotionNowUs(-1);
    host.shutdown();
}

TEST_CASE("rpm widget: without a limiter it lights over the max rpm, without the flash") {
    // KRP's direct-drive karts report no limiter, only a max rpm; the strip
    // stayed dark there.
    constexpr int MAX = 16000;
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_nolimiter\\");
    host.eventInit("RpmTrack", "Player", 1600.0f, 2, "Test Kart", "KZ2", "", 0, "", 0.0f, 0, 0, MAX);

    ride(host, MAX / 2);   // the range starts: one light
    auto s = segments(host, 15);
    REQUIRE(s.size() == 15);
    CHECK(litCount(s) == 1);
    ride(host, MAX - 1);
    CHECK(litCount(segments(host, 15)) == 15);

    // At and past the max rpm the strip stays lit through both halves of the flash.
    host.setMotionNowUs(1000000);
    ride(host, MAX);
    CHECK(litCount(segments(host, 15)) == 15);
    host.setMotionNowUs(1000000 + 100000);
    CHECK(litCount(segments(host, 15)) == 15);

    host.setMotionNowUs(-1);
    host.shutdown();
}

TEST_CASE("rpm widget: without a limiter the Bars R column fills over the max rpm too") {
    // The same blind spot as the strip: R measured against the limiter alone, so
    // on a kart without one it never filled.
    constexpr int MAX = 16000;
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_bars\\", "\n[BarsWidget]\nvisible=1\n");
    host.eventInit("RpmTrack", "Player", 1600.0f, 2, "Test Kart", "KZ2", "", 0, "", 0.0f, 0, 0, MAX);
    const auto barsQuads = [&host](int rpm) {
        ride(host, rpm);
        host.drawWithState(0);
        host.drawWithState(0);
        return host.hudQuadRects("bars_widget").size();
    };
    const size_t idle = barsQuads(0);
    REQUIRE(idle > 0);
    CHECK(barsQuads(MAX / 2) > idle);   // R gained its filled part
    host.shutdown();
}

TEST_CASE("rpm widget: segments, gaps and width come from the INI") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_ini\\", "segments=10\nsegmentGaps=0\nwidth=30\n");
    REQUIRE(host.hasScreenEdges());
    ride(host, LIMITER - 1);
    const auto s = segments(host, 10);
    REQUIRE(s.size() == 10);
    CHECK(host.hudQuadRects(kRpm).size() < 15);   // not the default count
    CHECK(litCount(s) == 10);
    // No gaps: each segment starts where the last one ended.
    for (size_t i = 1; i < s.size(); ++i) CHECK(s[i].rect.l == doctest::Approx(s[i - 1].rect.r).epsilon(1e-4));
    // Wider than the default span.
    const PluginHost::ScreenEdges gear = host.hudScreenEdges("gear_widget");
    const PluginHost::ScreenEdges speed = host.hudScreenEdges("speed_widget");
    const PluginHost::ScreenEdges rpm = host.hudScreenEdges(kRpm);
    CHECK(rpm.r - rpm.l > speed.r - gear.l);
    host.shutdown();
}

TEST_CASE("rpm widget: vertical stands the strip on its end at Gear's height") {
    // The horizontal strip's segment height, to compare the turned one against.
    double rowH = 0.0;
    {
        PluginHost host(dllPath());
        setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_horizontal\\");
        ride(host, LIMITER - 1);
        const auto s = segments(host, 15);
        REQUIRE(s.size() == 15);
        rowH = s[0].rect.b - s[0].rect.t;
        host.shutdown();
    }
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_vertical\\", "vertical=1\n");
    REQUIRE(host.hasScreenEdges());
    ride(host, LIMITER / 2);   // one light: the bottom one
    const auto s = segments(host, 15);
    REQUIRE(s.size() == 15);
    CHECK(litCount(s) == 1);
    CHECK(on(s[0]));
    // Bottom to top, stacked without overlapping, in one column.
    for (size_t i = 1; i < s.size(); ++i) {
        CHECK(s[i].rect.b <= s[i - 1].rect.t + 1e-6);
        CHECK(s[i].rect.l == doctest::Approx(s[0].rect.l));
    }
    // As wide on screen as the horizontal strip is tall (x is in 16:9 units).
    CHECK((s[0].rect.r - s[0].rect.l) * 16.0 / 9.0 == doctest::Approx(rowH).epsilon(0.02));
    // As tall as the Gear widget.
    const PluginHost::ScreenEdges gear = host.hudScreenEdges("gear_widget");
    const PluginHost::ScreenEdges rpm = host.hudScreenEdges(kRpm);
    CHECK(rpm.b - rpm.t == gear.b - gear.t);
    host.shutdown();
}

TEST_CASE("rpm widget: the newest light fades up over its stretch of revs") {
    // SHIFT 10500 / LIMITER 12000 over 15 lights: the range starts at 6000, 4
    // are red, and the 11 below share [6000, 10500), ~409 rpm each. The third
    // light (index 2) comes on at ~6818, its stretch running to ~7227.
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_fade\\");
    ride(host, 7220);   // the end of its stretch: whole
    auto s = segments(host, 15);
    REQUIRE(s.size() == 15);
    const size_t quads = host.hudQuadRects(kRpm).size();
    CHECK(alpha(s[2].color) == 0xFF);
    const unsigned dim = alpha(s[3].color);
    CHECK(dim == UNLIT_ALPHA);

    // A quarter, then three quarters of the way from unlit to lit, with the
    // ones before it whole and nothing drawn besides the segments.
    for (const auto& [rpm, share] : { std::pair{ 6830, 0.25 }, std::pair{ 7030, 0.75 } }) {
        CAPTURE(rpm);
        ride(host, rpm);
        s = segments(host, 15);
        CHECK(host.hudQuadRects(kRpm).size() == quads);
        CHECK(alpha(s[1].color) == 0xFF);
        CHECK(alpha(s[2].color) == doctest::Approx(dim + (0xFF - dim) * share).epsilon(0.03));
        CHECK(rgb(s[2].color) == rgb(s[1].color));
        CHECK(alpha(s[3].color) == dim);
    }
    host.shutdown();
}

TEST_CASE("rpm widget: the first red light still shows at the shift point") {
    PluginHost host(dllPath());
    setUp(host, "Z:\\tmp\\mxbmrp3-tests\\rpm_fade_shift\\");
    ride(host, SHIFT - 1);   // the last amber one whole, no red
    auto s = segments(host, 15);
    REQUIRE(s.size() == 15);
    for (size_t i = 0; i < 11; ++i) CHECK(alpha(s[i].color) == 0xFF);
    CHECK(alpha(s[11].color) == UNLIT_ALPHA);
    ride(host, SHIFT);
    s = segments(host, 15);
    CHECK(on(s[11]));   // the red one, a quarter of the way up
    CHECK(alpha(s[11].color) < 0xFF);
    host.shutdown();
}
