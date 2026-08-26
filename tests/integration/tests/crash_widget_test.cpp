// ============================================================================
// tests/integration/tests/crash_widget_test.cpp
// The crash widget's tally: what makes it different from every other crash count
// in the plugin, and the box it shares with Speed and Gear.
//
// WHY THIS FILE EXISTS. StatsManager already counts crashes -- per track and bike,
// alongside the laps and the PBs, and scoped to that pairing. This widget's number
// is a different thing wearing the same word: a STREAMER's tally that runs across
// practice, races, server hops and game restarts, and moves only when the rider
// presses Reset. Every boundary that resets the per-track counters is a boundary
// this one must survive, which is exactly what makes it easy to break by wiring it
// to the wrong lifetime -- and a tally that quietly restarted at a track change
// would look completely correct on screen. So the cases below cross those
// boundaries deliberately: a track+bike change, then a full plugin unload/reload.
//
// The reset goes through the WIDGET (PluginHost::crashTallyReset -> resetCounter),
// not through StatsManager, so the button's and the hotkey's shared entry point is
// the one under test; and it must land on DISK immediately rather than at the next
// leave-track flush -- a count that came back after a crash-to-desktop is the one
// failure a streamer would actually notice.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include "ini.h"             // readFile
#include "nlohmann/json.hpp"
#include <cstdio>   // std::remove

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum
constexpr int PLAYER = 10;  // first active RaceAddEntry after EventInit = the player

// The runner pre-creates SAVE_ROOT/<test name minus _test> and the plugin does not
// mkdir for itself, so this path is not free-form: it must be this test's own dir.
const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\crash_widget\\";
const char* kStatsPath =
    "Z:\\tmp\\mxbmrp3-tests\\crash_widget\\mxbmrp3\\mxbmrp3_stats.json";

// The persisted tally, or -1 when the file has no global section at all. Note the
// save path OMITS the key at zero (a default), so "absent" reads as 0 here.
int persistedTally() {
    const std::string txt = ini::readFile(kStatsPath);
    if (txt.empty()) return -1;
    auto j = nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("global")) return -1;
    return j["global"].value("crashTally", 0);
}

// A crashed / not-crashed frame. THE CRASH FLAG DOES NOT COME FROM RunTelemetry:
// the handler reads it off the player's row in the RaceTrackPosition batch
// (SPluginsBikeData_t's own m_iCrashed is not what StatsManager sees), so a state
// change is a track-position batch followed by the telemetry frame that samples it.
void rideFrame(PluginHost& host, float t, bool crashed) {
    host.raceTrackPosition({ TrackRow{ PLAYER, /*trackPos=*/0.25f, crashed ? 1 : 0 } });
    TelemetryRow r;
    r.time = t;
    host.telemetryFrame(r);
}

// One crash: the tally follows the RISING EDGE, so a crash is a crashed frame
// after a clean one, and staying down does not keep counting.
void crashOnce(PluginHost& host, float t) {
    rideFrame(host, t, true);
    rideFrame(host, t + 0.5f, false);
}

void openSession(PluginHost& host, const char* track, const char* bike) {
    host.eventInit(track, bike);   // track+bike = the scope the per-track stats use
    host.raceEvent(track);
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(PLAYER, "Alice");
    host.runInit(RACE);
}

}  // namespace

TEST_CASE("crash widget: the tally survives a track+bike change and a plugin restart") {
    std::remove(kStatsPath);   // start clean; the only writer below is this test

    int afterFirstStint = 0;
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasCrashTally());

        openSession(host, "TrackA", "BikeOne");
        CHECK(host.crashTally() == 0);

        crashOnce(host, 1.0f);
        crashOnce(host, 3.0f);
        CHECK(host.crashTally() == 2);

        // A rider who STAYS down is still one crash -- the counter follows the
        // edge, not the state, or a fallen rider would tick once per frame.
        rideFrame(host, 5.0f, true);
        rideFrame(host, 5.5f, true);
        rideFrame(host, 6.0f, true);
        CHECK(host.crashTally() == 3);
        rideFrame(host, 6.5f, false);

        // A NEW TRACK AND BIKE. This is the boundary the per-track crashCount is
        // scoped by, and the one this tally must ignore.
        host.runDeinit();
        openSession(host, "TrackB", "BikeTwo");
        CHECK(host.crashTally() == 3);

        crashOnce(host, 10.0f);
        afterFirstStint = host.crashTally();
        CHECK(afterFirstStint == 4);

        host.runDeinit();          // leave-track flush writes the stats file
        host.shutdown();
    }
    CHECK(persistedTally() == afterFirstStint);

    // A FULL RESTART: new process-lifetime plugin, same save path. "Game restarts"
    // is the last boundary in the ask, and the only one a file round-trip covers.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        openSession(host, "TrackC", "BikeThree");
        CHECK(host.crashTally() == afterFirstStint);

        crashOnce(host, 1.0f);
        CHECK(host.crashTally() == afterFirstStint + 1);

        // RESET, through the widget's own entry point -- and it must reach disk at
        // once, WITHOUT a leave-track flush, so a crash-to-desktop can't resurrect
        // the count the streamer just cleared.
        CHECK(host.crashTallyReset() == 0);
        CHECK(persistedTally() == 0);

        // ...and counting resumes from zero rather than from where it left off.
        crashOnce(host, 5.0f);
        CHECK(host.crashTally() == 1);

        host.runDeinit();
        host.shutdown();
    }
}

TEST_CASE("crash widget: the count on screen is the count it holds") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasCrashTally());

    host.showAllHuds(true);   // the widget early-outs when hidden on both surfaces
    openSession(host, "TrackA", "BikeOne");
    host.crashTallyReset();   // whatever an earlier case left on disk

    // EVERY OTHER CASE HERE READS THE TALLY, not the widget. That is the number
    // StatsManager holds, and a widget subscribed to the wrong change type would
    // leave it perfectly correct while the panel showed a stale one -- the same
    // silent failure position_widget_test exists for. So this one reads the DRAWN
    // string, which is the only thing a viewer actually sees.
    auto drawnCount = [&]() {
        host.draw();
        for (const auto& row : host.hudStringRows(PluginHost::HUD_CRASH)) {
            if (row.text.empty()) continue;         // the hidden title's placeholder
            if (row.text == "Crashes") continue;    // the caption (off by default)
            if (row.text == "Reset") continue;      // the chip
            return row.text;
        }
        return std::string("<none>");
    };
    // The title ships OFF, like the rest of the widget rail the default position
    // tiles it into -- so the first draw must carry no caption at all.
    host.draw();
    for (const auto& row : host.hudStringRows(PluginHost::HUD_CRASH))
        CHECK(row.text != "Crashes");
    CHECK(drawnCount() == "0");

    crashOnce(host, 1.0f);
    crashOnce(host, 3.0f);
    CHECK(drawnCount() == "2");

    host.crashTallyReset();
    CHECK(drawnCount() == "0");

    host.runDeinit();
    host.shutdown();
}

TEST_CASE("crash widget: the Reset chip stays inside the content box") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasThemeGeometry());

    host.showAllHuds(true);
    openSession(host, "TrackA", "BikeOne");
    // The caption is this case's landmark for the content box, and it ships OFF
    // now -- switch it on rather than lose the measurement.
    REQUIRE(host.setHudTitle("crash_widget", true));
    host.draw();

    // The chip used to be sized from its LABEL - "Reset" plus a char of air plus
    // the theme's button insets - while the panel is eight characters wide. That
    // came out WIDER than the content box, so it hung over the panel padding by
    // half its width on each side while the caption and the count sat inside it.
    // Nothing else in the widget could show that: both of those are content, and
    // the panel rect is the same either way.
    const auto panel = host.hudScreenEdges(PluginHost::HUD_CRASH);
    const auto quads = host.hudQuadRects(PluginHost::HUD_CRASH);
    REQUIRE_MESSAGE(!quads.empty(), "no chip quad drawn - is the Reset button on?");

    // The caption is drawn LEFT-justified at the plan's content origin, so it is
    // the content box's left edge without a hook of its own.
    double captionX = -1.0;
    for (const auto& row : host.hudStringRows(PluginHost::HUD_CRASH)) {
        if (row.text == "Crashes") { captionX = row.x; break; }
    }
    REQUIRE_MESSAGE(captionX > 0.0, "caption not drawn - cannot locate the content box");

    const double panelL = panel.l / 1e6, panelR = panel.r / 1e6;
    const double padL = captionX - panelL;
    REQUIRE_MESSAGE(padL > 0.0, "caption is not inside the panel");

    // The PANEL BACKGROUND spans the panel by definition, so it is not a
    // candidate -- and it only started appearing here when the widget's default
    // opacity went to 1.0 (a fully transparent background is not submitted).
    // Skipped by what it IS, its own rect, rather than by an index that moves
    // the moment a theme adds nine-slice art.
    int chips = 0;
    for (const auto& q : quads) {
        const bool isPanelBg = std::fabs(q.l - panelL) < 1e-6 &&
                               std::fabs(q.r - panelR) < 1e-6;
        if (isPanelBg) continue;
        ++chips;
        INFO("chip " << q.l << ".." << q.r << " content " << captionX
             << ".." << (panelR - padL) << " panel " << panelL << ".." << panelR);
        CHECK(q.l >= captionX - 1e-6);
        CHECK(q.r <= panelR - padL + 1e-6);
    }
    CHECK_MESSAGE(chips > 0, "only the panel background was drawn - no chip to check");

    host.shutdown();
}

TEST_CASE("crash widget: shares its content box with Speed and Gear") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasThemeGeometry());

    host.showAllHuds(true);
    openSession(host, "TrackA", "BikeOne");

    // TITLES OFF ON ALL THREE. The caption is a per-HUD user setting and a title
    // band is real height, so leaving it in would compare "does the user have the
    // same title setting on all three" rather than the box they share -- and this
    // widget ships with its caption ON (a bare number means nothing) while the
    // other two ship without.
    REQUIRE(host.setHudTitle("speed_widget", false));
    REQUIRE(host.setHudTitle("gear_widget", false));
    REQUIRE(host.setHudTitle("crash_widget", false));
    host.draw();

    const PluginHost::PanelRect speed = host.hudPanelRect(PluginHost::HUD_SPEED);
    const PluginHost::PanelRect gear  = host.hudPanelRect(PluginHost::HUD_GEAR);
    const PluginHost::PanelRect crash = host.hudPanelRect(PluginHost::HUD_CRASH);

    // Speed and Gear are the pair the widget was sized to, so THEY are the premise:
    // if these two ever diverge the check below is meaningless and this says so
    // rather than passing vacuously against a moved target.
    REQUIRE(speed.h > 0);
    REQUIRE(speed.h == gear.h);

    // The claim: a third widget that tiles with them. Height is what matters --
    // the widths differ with the caption and the digits, but a row of three
    // reads as a row only if their boxes are the same height.
    CHECK(crash.h == speed.h);

    host.shutdown();
}
