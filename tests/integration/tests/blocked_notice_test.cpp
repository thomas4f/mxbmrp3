// ============================================================================
// tests/integration/tests/blocked_notice_test.cpp
// BLOCKED-STATE NOTICE: a panel whose feature cannot work right now keeps its
// normal footprint and replaces its content with a headline over a hint
// (BaseHud::addBlockedNotice) -- the shape the Gamepad widget and Stream Chat
// established, so a player learns it once.
//
// Two panels had drifted from it and are pinned here:
//  - FRIENDS with Steam off / unreachable drew a card sized to its message, in
//    MUTED, and while its settings tab was open the preview text REPLACED the
//    message -- so the tab a player opens to find the Steam switch hid the fact
//    that Steam was off. Now: the notice in NEGATIVE with its hint, the same while
//    previewing, and no "Preview" text anywhere.
//  - RUMBLE drew flat 0% traces with rumble switched off or no controller, which
//    reads as "working, nothing happening". Now: a notice per cause, in the same
//    footprint the graph takes, so the panel does not jump when it starts working.
//
// A test build has no Steam: switching it on lands in "not available", which
// with "off" is both Friends states this asserts.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>
#include <string>
#include <vector>

static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\blocked_notice\\";
static const std::string kIniPath =
    "Z:\\tmp\\mxbmrp3-tests\\blocked_notice\\mxbmrp3\\mxbmrp3_settings.ini";

namespace {

constexpr int kSlotMuted = 3;     // ColorSlot::MUTED
constexpr int kSlotNegative = 8;  // ColorSlot::NEGATIVE

// Index of the first drawn string equal to `text`, or -1.
int findString(PluginHost& host, const char* hud, const char* text) {
    const std::vector<PluginHost::StringRow> rows = host.hudStringRows(hud);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text == text) return static_cast<int>(i);
    }
    return -1;
}

bool anyStringContains(PluginHost& host, const char* hud, const char* needle) {
    for (const PluginHost::StringRow& r : host.hudStringRows(hud)) {
        if (r.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

// The headline is drawn in `slot` and the hint under it in MUTED.
void checkNotice(PluginHost& host, const char* hud, const char* headline, const char* hint,
                 int slot) {
    const int h = findString(host, hud, headline);
    INFO(hud << " headline '" << headline << "'");
    REQUIRE(h >= 0);
    CHECK(host.stringColor(hud, h) == host.effectiveColor(slot));
    const int t = findString(host, hud, hint);
    INFO(hud << " hint '" << hint << "'");
    REQUIRE(t >= 0);
    CHECK(host.stringColor(hud, t) == host.effectiveColor(kSlotMuted));
    const std::vector<PluginHost::StringRow> rows = host.hudStringRows(hud);
    CHECK(rows[static_cast<size_t>(t)].y > rows[static_cast<size_t>(h)].y);   // hint below
    CHECK(rows[static_cast<size_t>(t)].x == rows[static_cast<size_t>(h)].x);  // one centre
}

struct Size { int w = 0, h = 0; };
Size sizeOf(PluginHost& host, const char* hud) {
    const PluginHost::ScreenEdges e = host.hudScreenEdges(hud);
    return { e.r - e.l, e.b - e.t };
}

}  // namespace

TEST_CASE("blocked notice: Friends names Steam's state, the same live and in its tab") {
    std::remove(kIniPath.c_str());
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasInkHooks());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasWhatsNew());        // openSettingsTab
    REQUIRE(host.hasSteamSetEnabled());

    const char* kHud = "friends_hud";
    REQUIRE(host.setHudVisible(kHud, true));
    host.steamSetEnabled(true);
    host.draw();

    // Default show mode is "With friends": no friends, nothing on screen.
    CHECK(sizeOf(host, kHud).w == 0);

    // Its tab open, it stands in with what "Always" would draw: the notice, not a
    // preview line. Steam is on but unreachable here.
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Friends"));
    host.draw();
    checkNotice(host, kHud, "Steam not available", "Launch the game via Steam", kSlotNegative);
    CHECK_FALSE(anyStringContains(host, kHud, "Preview"));
    const Size unavailable = sizeOf(host, kHud);
    CHECK(unavailable.w > 0);

    // Switched off: the other notice, in the same footprint -- the panel is the
    // table's size whatever it says, not its message's.
    host.steamSetEnabled(false);
    host.draw();
    checkNotice(host, kHud, "Steam integration off", "Check MXBMRP3 Settings > General", kSlotNegative);
    CHECK_FALSE(anyStringContains(host, kHud, "Preview"));
    const Size off = sizeOf(host, kHud);
    CHECK(off.w == unavailable.w);
    CHECK(off.h == unavailable.h);

    // Back on, with no tab change in between: the panel follows the switch.
    host.steamSetEnabled(true);
    host.draw();
    CHECK(findString(host, kHud, "Steam not available") >= 0);

    host.showSettings(false);
    host.draw();
    CHECK(sizeOf(host, kHud).w == 0);

    host.runDeinit();
    host.shutdown();
}

TEST_CASE("blocked notice: Rumble says why nothing can rumble, in the graph's footprint") {
    std::remove(kIniPath.c_str());
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasInkHooks());
    REQUIRE(host.hasScreenEdges());

    const char* kHud = "rumble_hud";
    REQUIRE(host.setHudVisible(kHud, true));
    host.fakeGamepad(false);
    host.rumbleSetEnabled(false);
    host.draw();

    // Rumble off by default.
    checkNotice(host, kHud, "Rumble off", "Check MXBMRP3 Settings > Rumble", kSlotNegative);
    const Size offSize = sizeOf(host, kHud);
    CHECK(offSize.w > 0);

    // On, but the selected controller is not there.
    host.rumbleSetEnabled(true);
    host.draw();
    checkNotice(host, kHud, "Controller 1 Not Connected", "Check MXBMRP3 Settings > General",
                kSlotNegative);
    CHECK_FALSE(anyStringContains(host, kHud, "Rumble off"));

    // Controller present: the graph, no notice, and the same box as the notice had.
    host.fakeGamepad(true);
    host.draw();
    CHECK_FALSE(anyStringContains(host, kHud, "Not Connected"));
    CHECK_FALSE(anyStringContains(host, kHud, "Check MXBMRP3 Settings"));
    CHECK(findString(host, kHud, "100%") >= 0);
    const Size working = sizeOf(host, kHud);
    CHECK(working.w == offSize.w);
    CHECK(working.h == offSize.h);

    host.fakeGamepad(false);
    host.rumbleSetEnabled(false);
    host.runDeinit();
    host.shutdown();
}
