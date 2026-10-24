// ============================================================================
// tests/integration/tests/reference_caption_test.cpp
// A TITLE ROW NAMES THE LAP ITS GAP IS AGAINST.
//
// The HUDs a player can point at another lap (Settings > General > Reference,
// or each HUD's own row) name it right-aligned on their title row, in the
// column-header style and the spelling Settings lists it by
// (PbGapTracker::refName): the Gap Bar, the Delta Trace, the Lap Log while its
// gap row is on, the Timing HUD while its Gap section is on, and the Dash's
// Timing, Gap bar and Delta trace pages after their parents. A HUD on
// Default names General's reference, so changing General changes every one;
// with the title off there is nowhere to put it.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr int RACE1 = 6;

// The note on the title row: right of the title (the first string) and level
// with it. Timing's comparison rows and the Dash's Timing corners carry the
// same names ("Session PB", "Last lap") further down, so the text alone is not
// enough.
const PluginHost::StringRow* find(PluginHost& host, const char* hud, const std::string& text) {
    static std::vector<PluginHost::StringRow> rows;
    rows = host.hudStringRows(hud);
    if (rows.empty()) return nullptr;
    for (const auto& r : rows)
        if (r.text == text && r.x > rows.front().x && std::abs(r.y - rows.front().y) < 0.02) return &r;
    return nullptr;
}

bool shows(PluginHost& host, const char* hud, const std::string& text) {
    return find(host, hud, text) != nullptr;
}

void start(PluginHost& host, const std::string& saveWin, const char* body) {
    REQUIRE(host.startup(saveWin.c_str()) >= 0);
    const std::string dir = saveWin + "mxbmrp3";
    std::filesystem::create_directories(dir);
    std::ofstream ini(dir + "\\mxbmrp3_settings.ini", std::ios::trunc);
    REQUIRE(ini.is_open());
    ini << "[Settings]\nversion=6\n\n[Messages]\ndashTried=1\n\n" << body;
    ini.close();
    host.loadSettings(saveWin.c_str());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.draw();
    host.draw();
}

}  // namespace

TEST_CASE("reference titles: each title row names General's reference, and follows it") {
    for (const auto& [general, name] : { std::pair{ 0, "Session PB" }, std::pair{ 1, "All-time PB" },
                                         std::pair{ 2, "Last lap" } }) {
        CAPTURE(name);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        std::string ini = "[General]\ndefaultReference=" + std::to_string(general) + "\n\n"
            "[GapBarHud]\nvisible=1\nshowTitle=1\n\n[LapLogHud]\nvisible=1\n\n"
            "[DeltaTraceHud]\nvisible=1\n\n[TimingHud]\nvisible=1\nshowTitle=1\ngapRow=3\n";
        start(host, "Z:\\tmp\\mxbmrp3-tests\\ref_title\\", ini.c_str());
        for (const char* hud : { "gap_bar_hud", "lap_log_hud", "delta_trace_hud", "timing_hud" }) {
            CAPTURE(hud);
            CHECK(shows(host, hud, name));
        }
        host.shutdown();
    }
}

TEST_CASE("reference titles: nothing named with the gap row or Gap off, or the title off") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\ref_title_off\\",
          "[LapLogHud]\nvisible=1\nshowGapRow=0\n\n[TimingHud]\nvisible=1\nshowTitle=1\n\n"
          "[DeltaTraceHud]\nvisible=1\nshowTitle=0\n");
    CHECK_FALSE(shows(host, "lap_log_hud", "Session PB"));
    CHECK_FALSE(shows(host, "timing_hud", "Session PB"));
    CHECK_FALSE(shows(host, "delta_trace_hud", "Session PB"));
    host.shutdown();
}

TEST_CASE("reference titles: the Dash's pages name their parents' reference, at one width") {
    double width = -1.0;
    for (const char* page : { "timing", "gapbar", "deltatrace", "laps" }) {
        CAPTURE(page);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        const std::string ini = std::string("[General]\ndefaultReference=1\n\n"
            "[TimingHud]\ngapRow=3\n\n[DashHud]\nvisible=1\nshowTitle=1\nnotice_takeover=0\npage=") + page + "\n";
        start(host, "Z:\\tmp\\mxbmrp3-tests\\ref_title_dash\\", ini.c_str());
        // Laps names none: no reference behind it.
        CHECK(shows(host, "dash_hud", "All-time PB") == (std::string(page) != "laps"));
        // The panel is planned for the longest title and note, so stepping
        // pages never changes its width.
        const auto edges = host.hudScreenEdges(PluginHost::HUD_DASH);
        if (width < 0.0) width = edges.r - edges.l;
        CHECK(edges.r - edges.l == doctest::Approx(width));
        host.shutdown();
    }
}
