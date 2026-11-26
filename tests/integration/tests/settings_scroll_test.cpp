// ============================================================================
// tests/integration/tests/settings_scroll_test.cpp
// THE SETTINGS PANEL'S SCROLLING LISTS (hud/settings/scroll_view.h), driven
// through the real panel with the injected mouse, on the Achievements list
// (the longest, and the tab that sets the panel's height).
//
// What a unit test of the geometry cannot see, each pinned here:
//   * a step moves the list whole rows at a time and stops at both ends --
//     the top, and the last rest;
//   * the scrollbar's end arrows step the list (its hit strip wider than the
//     drawn bar); a press on its track moves a
//     view's worth toward it; a held thumb dragged to the foot takes the list
//     to its last rest; a held arrow repeats, and stops once the pointer
//     leaves its cell;
//   * AT REST NOTHING HANGS OUT: no string of the list sits part-way across
//     the view's top or bottom edge (the game cannot clip, so a row is drawn
//     whole inside or not at all), wherever the list stands;
//   * the panel is the same height wherever the list stands;
//   * a list that fits (the tracked riders, empty) draws no scrollbar;
//   * every list's bar lane ends at the same x, tab to tab, and the list's
//     right-aligned text stays out of it;
//   * a group card the view cuts is left OPEN on that side: no closing corner
//     at the view's foot while the group's rows go on below it, nor at its
//     top while they go on above (else the card reads as ended and more rows
//     slide in from under it).
//   * THE WHEEL (core/mouse_wheel.h), on the harness's stand-in for the game
//     window (its procedure outlives the DLL): a notch over a list
//     steps it and never reaches the game; off the lists, with the panel
//     undrawn past the claim's deadline (the game's own menus, which make no
//     plugin calls), and once the panel closes, every notch is the game's --
//     and closing, Shutdown(), and an unload WITHOUT Shutdown() each give the
//     window its own procedure back. The first attach asks for the pin.
// Motion is Off in the test build, so every move lands on the next draw; the
// glide itself is test_scroll_view.cpp's.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"


#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int SERVER_PLAYERS = 0;   // ScrollView::SERVER_PLAYERS
constexpr int TRACKED_RIDERS = 1;   // ScrollView::TRACKED_RIDERS
constexpr int ACHIEVEMENTS = 2;     // ScrollView::ACHIEVEMENTS
constexpr int FIND_RESULTS = 3;     // ScrollView::FIND_RESULTS
constexpr int WIDGETS = 4;          // ScrollView::WIDGETS

void start(PluginHost& host, const char* dir) {
    REQUIRE(host.loaded());
    host.startup(dir);
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();
}

// Strings drawn (alpha > 0) whose row straddles the view's top or bottom edge.
int straddling(PluginHost& host, const PluginHost::ScrollView& v) {
    const float lh = v.h / v.viewLines;
    const float eps = lh * 0.05f;
    int n = 0;
    for (const PluginHost::DrawnText& t : host.lastStrings()) {
        if (t.alpha == 0 || t.x < v.x - eps || t.x > v.x + v.w + eps) continue;
        const bool overTop = t.y < v.y - eps && t.y > v.y - lh + eps;
        const bool overFoot = t.y > v.y + v.h - lh + eps && t.y < v.y + v.h - eps;
        if (overTop || overFoot) {
            MESSAGE("straddles the edge: \"" << t.text << "\" at y=" << t.y);
            ++n;
        }
    }
    return n;
}
}  // namespace

TEST_CASE("settings scroll: a step moves whole rows and stops at both ends") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_step\\");
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    REQUIRE(v.maxRest > 0.0f);
    CHECK(v.thumbH > 0.0f);
    CHECK(v.shown == doctest::Approx(0.0f));

    host.stepScroll(ACHIEVEMENTS, 1);
    const PluginHost::ScrollView one = host.settingsScroll(ACHIEVEMENTS);
    CHECK(one.target > 0.0f);
    CHECK(one.shown == doctest::Approx(one.target));   // Motion off: landed
    // A step: whole rows until two lines have passed -- the group's first
    // row, its name sharing it, three lines.
    CHECK(one.target <= 3.0f + 1e-3f);
    host.stepScroll(ACHIEVEMENTS, 1);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target > one.target);

    host.stepScroll(ACHIEVEMENTS, -50);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(0.0f));
    host.stepScroll(ACHIEVEMENTS, 500);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(v.maxRest));
    host.shutdown();
}

TEST_CASE("settings scroll: the arrows step, the track moves a view's worth, the thumb drags to the foot") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_bar\\");
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    REQUIRE(v.thumbH > 0.0f);
    const float lh = v.h / v.viewLines;
    const float barX = v.x + v.w - 0.0005f;   // the bar is the lane at the view's right end
    const float trackTop = v.y + lh;          // under the up arrow's cell
    const float trackFoot = v.y + v.h - lh;   // over the down arrow's cell

    // The down arrow: one step. The up arrow: back to the top.
    host.clickAt(barX, v.y + v.h - lh * 0.5f);
    const float stepped = host.settingsScroll(ACHIEVEMENTS).target;
    CHECK(stepped > 0.0f);
    CHECK(stepped <= 3.0f + 1e-3f);
    host.clickAt(barX, v.y + lh * 0.5f);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(0.0f));
    // The bar is thin, but its hit strip is wider: a press just past its
    // right edge, off the drawn bar, still takes the arrow.
    host.clickAt(v.x + v.w + 0.0015f, v.y + v.h - lh * 0.5f);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(stepped));
    host.clickAt(barX, v.y + lh * 0.5f);

    // The track under the thumb: a view's worth down, but never past a row
    // that was in view.
    host.clickAt(barX, trackFoot - 0.002f);
    const float paged = host.settingsScroll(ACHIEVEMENTS).target;
    CHECK(paged > 3.0f);
    CHECK(paged <= v.viewLines + 1e-3f);
    // ...and back up.
    host.clickAt(barX, trackTop + 0.002f);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(0.0f));

    // Hold the thumb, drag it past the foot, let go: the last rest.
    const float grabY = trackTop + v.thumbH * 0.5f;
    host.injectMouse(true, barX, grabY, 0); host.draw();
    host.injectMouse(true, barX, grabY, 1); host.draw();
    host.injectMouse(true, barX, v.y + v.h * 0.5f, 1); host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target > 0.0f);   // following the pointer
    host.injectMouse(true, barX, v.y + v.h * 2.0f, 1); host.draw();
    host.injectMouse(true, barX, v.y + v.h * 2.0f, 0); host.draw();
    host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(v.maxRest));
    host.injectMouse(false);
    host.shutdown();
}

// Held, an end arrow keeps stepping on the panel's hold curve (400 ms before the
// first repeat); slid off its cell with the button still down, it stops, as the
// panel's own hold-to-repeat buttons do.
TEST_CASE("settings scroll: a held arrow repeats, and stops when the pointer leaves it") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_hold\\");
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    REQUIRE(v.thumbH > 0.0f);
    const float lh = v.h / v.viewLines;
    const float barX = v.x + v.w - 0.0005f;
    const float downY = v.y + v.h - lh * 0.5f;
    const auto wait = [&host](int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        host.draw();
    };

    host.injectMouse(true, barX, downY, 0); host.draw();
    host.injectMouse(true, barX, downY, 1); host.draw();
    const float pressed = host.settingsScroll(ACHIEVEMENTS).target;
    CHECK(pressed > 0.0f);   // the press itself steps
    for (int i = 0; i < 8; ++i) wait(150);
    const float held = host.settingsScroll(ACHIEVEMENTS).target;
    CHECK(held > pressed);   // ...and holding repeats

    // Still down, but over the track: no more steps.
    host.injectMouse(true, barX, v.y + v.h * 0.5f, 1); host.draw();
    const float left = host.settingsScroll(ACHIEVEMENTS).target;
    for (int i = 0; i < 6; ++i) wait(150);
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(left));
    host.injectMouse(true, barX, v.y + v.h * 0.5f, 0); host.draw();
    host.injectMouse(false);
    host.shutdown();
}

// The bar's lane is the section box's right edge on every tab, so the bar sits
// at one x wherever it shows: each list's view ends there.
TEST_CASE("settings scroll: every list's scrollbar lane ends at the same x") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_lane\\");
    const float laneR = host.settingsScroll(ACHIEVEMENTS).x + host.settingsScroll(ACHIEVEMENTS).w;
    REQUIRE(laneR > 0.0f);
    const auto endsAtLane = [&](int id, const char* what) {
        const PluginHost::ScrollView v = host.settingsScroll(id);
        REQUIRE_MESSAGE(v.laidOut, what);
        CHECK_MESSAGE(v.x + v.w == doctest::Approx(laneR).epsilon(1e-4), what);
    };
    REQUIRE(host.openSettingsTab("Widgets"));
    host.draw();
    endsAtLane(WIDGETS, "Widgets");
    REQUIRE(host.openSettingsTab("Riders"));
    host.draw();
    endsAtLane(SERVER_PLAYERS, "Riders: server players");
    endsAtLane(TRACKED_RIDERS, "Riders: tracked riders");
    REQUIRE(host.settingsFind("gap") > 0);
    host.draw();
    endsAtLane(FIND_RESULTS, "Find");
    host.shutdown();
}

// The bar lives in the panel's padding, so the list keeps its full width: a
// right-aligned figure in the list ends where the section above it (the toasts'
// card) ends its own, never past it and never under the bar. (Achievements once
// measured its rows before the scroll view was laid out.)
TEST_CASE("settings scroll: the list's right-aligned text stays out of the bar's lane") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_lane_text\\");
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    const float lh = v.h / v.viewLines;
    float above = 0.0f, inList = 0.0f;
    for (const PluginHost::DrawnText& t : host.lastStrings()) {
        if (t.alpha == 0 || t.justify != 2) continue;
        if (t.y < v.y - lh) above = std::max(above, t.x);
        else if (t.y < v.y + v.h) inList = std::max(inList, t.x);
    }
    REQUIRE(above > 0.0f);
    REQUIRE(inList > 0.0f);
    CHECK(inList == doctest::Approx(above).epsilon(1e-4));
    CHECK(inList < v.x + v.w);   // short of the bar's right edge
    host.shutdown();
}

TEST_CASE("settings scroll: at rest no row hangs over an edge, and the panel keeps its height") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_edges\\");
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    const int height = host.hudPanelRect("settings_hud").h;
    REQUIRE(height > 0);
    int stops = 0;
    for (int guard = 0; guard < 120; ++guard, ++stops) {
        host.draw();
        const PluginHost::ScrollView at = host.settingsScroll(ACHIEVEMENTS);
        INFO("at line " << at.target);
        CHECK(straddling(host, at) == 0);
        CHECK(host.hudPanelRect("settings_hud").h == height);
        host.stepScroll(ACHIEVEMENTS, 1);
        if (host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(at.target)) break;
    }
    CHECK(stops > 5);
    host.shutdown();
}

// A drawn corner of a group card resting at the view's foot (or, with
// `atTop`, its top): a small quad in the column just left of the rows' text
// (the card's own left edge), in the band where a card closed at that edge
// puts its corner -- at the top, tight enough to miss the corner of the
// list's own section card, which sits still just above the view.
bool cornerAtEdge(PluginHost& host, const PluginHost::ScrollView& v, bool atTop) {
    const float lh = v.h / v.viewLines;
    const float eps = lh * 0.05f;
    const float foot = v.y + v.h;
    const auto quads = host.hudQuadRects("settings_hud");
    for (size_t i = 0; i < quads.size(); ++i) {
        const PluginHost::QuadRect& q = quads[i];
        if ((host.quadColor("settings_hud", static_cast<int>(i)) >> 24) == 0) continue;
        if (q.r > v.x + eps || q.l >= q.r) continue;
        // A corner of the list's own card: small, and starting within the
        // card's pad of the rows (the tab list's card ends further left).
        if (q.b - q.t > lh || q.l < v.x - 0.6f * lh) continue;
        if (!atTop && q.t > foot - lh * 0.5f && q.t < foot + lh) return true;
        if (atTop && q.b < v.y + lh * 0.5f && q.b > v.y - lh * 0.5f) return true;
    }
    return false;
}

TEST_CASE("settings scroll: a group card the view cuts stays open at that edge") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_cards\\");
    REQUIRE(host.hasInkHooks());
    REQUIRE(host.hasThemeGeometry());
    // A theme with card art: the build's own default draws group cards as air.
    host.installTheme("edges", /*inset=*/1.0f, /*cardBorder=*/1.0f, /*titleBand=*/1, /*card=*/1);
    // The group names drawn in the list (at its own left, where entry titles
    // never start), below its top edge.
    auto groupNames = [&host](const PluginHost::ScrollView& v) {
        const float lh = v.h / v.viewLines;
        std::vector<std::string> names;
        for (const PluginHost::DrawnText& t : host.lastStrings()) {
            if (t.alpha == 0 || std::abs(t.x - v.x) > lh * 0.05f) continue;
            if (t.y >= v.y - lh * 0.05f && t.y < v.y + v.h) names.push_back(t.text);
        }
        return names;
    };
    int open = 0, corners = 0;
    for (int guard = 0; guard < 120; ++guard) {
        host.draw();
        const PluginHost::ScrollView at = host.settingsScroll(ACHIEVEMENTS);
        const bool corner = cornerAtEdge(host, at, false);
        corners += corner ? 1 : 0;
        const std::vector<std::string> before = groupNames(at);
        host.stepScroll(ACHIEVEMENTS, 1);
        host.draw();
        const PluginHost::ScrollView next = host.settingsScroll(ACHIEVEMENTS);
        if (next.target <= at.target) break;
        // What the step revealed at the foot: rows of a new group (its name
        // came into view) mean the card at the foot had ended; rows and no new
        // name mean it went on, so it must have been open; nothing revealed
        // (only the seam between two cards) says neither.
        const float lh = at.h / at.viewLines;
        const float foot = at.y + at.h;
        const float moved = (next.target - at.target) * lh;
        bool revealed = false;
        for (const PluginHost::DrawnText& t : host.lastStrings()) {
            if (t.alpha == 0 || t.x < at.x - lh * 0.05f || t.x > at.x + at.w) continue;
            if (t.y + moved >= foot - lh * 0.05f && t.y < foot) revealed = true;
        }
        bool newGroup = false;
        for (const std::string& name : groupNames(next)) {
            if (std::find(before.begin(), before.end(), name) == before.end()) newGroup = true;
        }
        INFO("at line " << at.target);
        if (revealed && !newGroup) { ++open; CHECK_FALSE(corner); }
    }
    CHECK(open > 0);
    CHECK(corners > 0);   // the detector does see a card that ends at the foot

    // The top, walking back up: a rest whose top row is not a group's first
    // (no group name on it) has that group's earlier rows cut off above, so
    // its card must be open there; one whose top row starts a group closes.
    int openTop = 0, closedTop = 0;
    for (int guard = 0; guard < 120; ++guard) {
        host.draw();
        const PluginHost::ScrollView at = host.settingsScroll(ACHIEVEMENTS);
        const float lh = at.h / at.viewLines;
        bool startsGroup = false;
        for (const std::string& name : groupNames(at)) {
            for (const PluginHost::DrawnText& t : host.lastStrings()) {
                if (t.text == name && t.alpha != 0 && std::abs(t.x - at.x) <= lh * 0.05f &&
                    t.y < at.y + lh * 0.6f) startsGroup = true;
            }
        }
        const bool corner = cornerAtEdge(host, at, true);
        INFO("at line " << at.target);
        if (at.target > 0.0f && !startsGroup) { ++openTop; CHECK_FALSE(corner); }
        if (startsGroup && corner) ++closedTop;
        if (at.target <= 0.0f) break;
        host.stepScroll(ACHIEVEMENTS, -1);
    }
    CHECK(openTop > 0);
    CHECK(closedTop > 0);   // the detector does see a card that starts at the top
    host.shutdown();
}

TEST_CASE("settings scroll: a list that fits draws no scrollbar and does not move") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_fits\\");
    REQUIRE(host.openSettingsTab("Riders"));
    host.draw();
    const PluginHost::ScrollView v = host.settingsScroll(TRACKED_RIDERS);
    REQUIRE(v.laidOut);
    CHECK(v.maxRest == doctest::Approx(0.0f));
    CHECK(v.thumbH == doctest::Approx(0.0f));
    host.stepScroll(TRACKED_RIDERS, 3);
    CHECK(host.settingsScroll(TRACKED_RIDERS).target == doctest::Approx(0.0f));
    host.shutdown();
}

namespace {
constexpr int NOTCH = 120;   // WHEEL_DELTA

// The panel open on Achievements, the wheel pointed at the stand-in, the
// cursor over the list and one frame drawn: ours is in front.
PluginHost::ScrollView startWheel(PluginHost& host, const char* dir) {
    start(host, dir);
    REQUIRE_MESSAGE(host.useWheelStandIn(), "build without the wheel hooks, or no X server");
    host.forceActiveSurface(0);
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    REQUIRE(v.maxRest > 0.0f);
    host.injectMouse(true, v.x + v.w * 0.5f, v.y + v.h * 0.5f, 0);
    host.draw();
    REQUIRE((host.wheelState() & 1) == 1);
    REQUIRE_FALSE(PluginHost::gameOwnsWheelWindow());
    return v;
}
}  // namespace

TEST_CASE("settings scroll: the wheel steps the list under the cursor, and is the game's everywhere else") {
    PluginHost host(dllPath());
    const PluginHost::ScrollView v = startWheel(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_wheel\\");
    const float overX = v.x + v.w * 0.5f, overY = v.y + v.h * 0.5f;
    const int game0 = PluginHost::wheelGameCount();
    CHECK((host.wheelState() & 2) == 2);   // the first attach asks for the pin

    // Over the list: the notch kept, the list a step down.
    CHECK(PluginHost::wheelSend(-NOTCH) == game0);
    host.draw();
    const float one = host.settingsScroll(ACHIEVEMENTS).target;
    CHECK(one > 0.0f);
    // Two notches at once, two steps; then back up to the top, and no further.
    PluginHost::wheelSend(-2 * NOTCH); host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target > one);
    PluginHost::wheelSend(50 * NOTCH); host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(0.0f));
    // A fine-grained wheel's partial deltas add up to a notch.
    PluginHost::wheelSend(-NOTCH / 2); host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(0.0f));
    PluginHost::wheelSend(-NOTCH / 2); host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(one));
    CHECK(PluginHost::wheelGameCount() == game0);   // the game saw none of them

    // Off the lists: the game's, and the list stays.
    host.injectMouse(true, v.x - 0.2f, v.y - 0.2f, 0); host.draw();
    CHECK(PluginHost::wheelSend(-NOTCH) == game0 + 1);
    host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(one));

    // Over the list but no longer drawn (the game's menus make no plugin
    // calls): once the claim runs out, the game's.
    host.injectMouse(true, overX, overY, 0); host.draw();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(PluginHost::wheelSend(-NOTCH) == game0 + 2);
    host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target == doctest::Approx(one));

    // Closed: the window has its own procedure back, and the wheel is the game's.
    host.showSettings(false); host.draw();
    CHECK((host.wheelState() & 1) == 0);
    CHECK(PluginHost::gameOwnsWheelWindow());
    CHECK(PluginHost::wheelSend(-NOTCH) == game0 + 3);
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("settings scroll: Shutdown() with the panel open gives the game window back") {
    PluginHost host(dllPath());
    startWheel(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_wheel_shutdown\\");
    host.shutdown();
    CHECK(PluginHost::gameOwnsWheelWindow());
    const int game0 = PluginHost::wheelGameCount();
    CHECK(PluginHost::wheelSend(-NOTCH) == game0 + 1);
}

TEST_CASE("settings scroll: an unload WITHOUT Shutdown() with the panel open gives the game window back") {
    {
        PluginHost host(dllPath());
        startWheel(host, "Z:\\tmp\\mxbmrp3-tests\\settings_scroll_wheel_unload\\");
        host.skipShutdownOnDestroy();
    }   // FreeLibrary: ~MouseWheel is all that is left to restore it
    CHECK(PluginHost::gameOwnsWheelWindow());
    const int game0 = PluginHost::wheelGameCount();
    CHECK(PluginHost::wheelSend(-NOTCH) == game0 + 1);   // reaches the game, not unmapped code
}
