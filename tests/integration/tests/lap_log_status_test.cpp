// ============================================================================
// tests/integration/tests/lap_log_status_test.cpp
// The Lap Log says WHY a lap has no time, on that lap's own row: PIT for a lap
// that went through the pits, INVALID for one the game struck out without a
// time (a cut outside a race), the plain - placeholder for anything else.
// A lap that kept its time (a race cut) shows the time, muted, never a word.
// The Timing panel's INVALID for that lap is held to the same font rule.
//
// Before this, PIT existed (added with LapLogEntry::viaPits) but a timeless cut
// lap showed the bare placeholder, so the two looked unrelated. The rule is one
// cell: the Time column of the lap row, never the gap row beneath it. The text
// isn't in /api/state, so the rows are read via MXBMRP3_Test_HudStringRows and
// matched to their lap by the "L<n>" label on the same line.
//
// PIT and INVALID are words, so they are set in the Normal font, never Digits
// (MXBMRP3_Test_HudStringFont). Normal and Digits ship as the same face, so the
// test stages two font files (discovery reads names only) and pins Digits to the
// other one, or the two categories would be indistinguishable.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
constexpr int PRACTICE = 1;
constexpr int RACE1 = 6;

struct Cell { std::string text; int font = 0; };

// The right-most string on the row whose lap label is `lapLabel` ("L2"): the
// Time cell. Empty if that lap has no row.
Cell timeCell(PluginHost& host, const char* lapLabel) {
    const auto rows = host.hudStringRows("lap_log_hud");
    double y = -1.0;
    for (const auto& r : rows) if (r.text == lapLabel) { y = r.y; break; }
    if (y < 0.0) return {};
    Cell cell;
    double x = -1.0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        // Half a row: the STRONG-font label sits a little off the data baseline.
        if (std::fabs(r.y - y) > 0.01 || r.text.empty()) continue;
        if (r.x > x) {
            x = r.x;
            cell = { r.text, host.stringFont("lap_log_hud", static_cast<int>(i)) };
        }
    }
    return cell;
}

// Every non-empty string on the row whose lap label is `lapLabel`, label included.
std::vector<std::string> rowTexts(PluginHost& host, const char* lapLabel) {
    const auto rows = host.hudStringRows("lap_log_hud");
    double y = -1.0;
    for (const auto& r : rows) if (r.text == lapLabel) { y = r.y; break; }
    std::vector<std::string> out;
    if (y < 0.0) return out;
    for (const auto& r : rows) {
        if (std::fabs(r.y - y) <= 0.01 && !r.text.empty()) out.push_back(r.text);
    }
    return out;
}

bool rowHas(PluginHost& host, const char* lapLabel, const char* needle) {
    for (const std::string& t : rowTexts(host, lapLabel)) {
        if (t.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Run from a staged tree whose plugins\\mxbmrp3_data\\fonts holds the two faces;
// restore the CWD after (integration_main's status sentinel is CWD-relative).
struct StagedFonts {
    std::filesystem::path prev = std::filesystem::current_path();
    explicit StagedFonts(const char* dir) {
        const std::filesystem::path fonts = std::filesystem::path(dir) / "plugins" / "mxbmrp3_data" / "fonts";
        std::filesystem::create_directories(fonts);
        for (const char* face : { "RobotoMono-Regular", "RobotoMono-Bold", "IBMPlexMono-Regular" }) {
            std::ofstream(fonts / (std::string(face) + ".fnt"), std::ios::trunc);
        }
        std::filesystem::current_path(dir);
    }
    ~StagedFonts() { std::filesystem::current_path(prev); }
};

void setUp(PluginHost& host, const char* dir, int session) {
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasStringFont());
    host.startup(dir);
    {
        const std::string saveDir = std::string(dir) + "mxbmrp3\\";
        std::filesystem::create_directories(saveDir);
        std::ofstream ini(saveDir + "mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[Fonts]\ndigits=IBMPlexMono-Regular\n";
    }
    host.loadSettings(dir);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(session, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(session);
    host.runStart();
    REQUIRE(host.setHudVisible("lap_log_hud", true));
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });   // S/F: a lap is being timed
}
}  // namespace

TEST_CASE("lap log: a timeless lap says why on its own row") {
    const char* dir = "Z:\\tmp\\mxbmrp3-tests\\lap_log_status\\";
    PluginHost host(dllPath());   // loads from the CWD, so before staging
    StagedFonts fonts(dir);
    setUp(host, dir, PRACTICE);

    // L1: clean.
    host.raceLap(PRACTICE, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    // L2: cut in practice -- the game sends no time and the invalid flag.
    host.raceLap(PRACTICE, 10, 2, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.02f } });
    // L3: into the pits from the menu mid-lap, back out, and the re-entry's line.
    host.runStop();
    host.runDeinit();
    host.runInit(PRACTICE);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.88f } });
    host.raceLap(PRACTICE, 10, 3, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.draw();

    const Cell l1 = timeCell(host, "L1"), l2 = timeCell(host, "L2"), l3 = timeCell(host, "L3");
    CHECK(l1.text == "1:00.000");
    CHECK(l2.text == "INVALID");
    CHECK(l3.text == "PIT");
    // Words, not digits: neither status shares the timed lap's Digits font.
    REQUIRE(l1.font != 0);
    CHECK(l2.font != l1.font);
    CHECK(l3.font != l1.font);
    CHECK(l2.font == l3.font);
    host.shutdown();
}

TEST_CASE("lap log: every missing time is the one plain dash") {
    // Empty rows, the live lap's sectors not reached yet and a closed lap's missing
    // sector all read "-". Before, the first two drew -:--.--- and the third "-",
    // two forms for the same absence in one table.
    const char* dir = "Z:\\tmp\\mxbmrp3-tests\\lap_log_dash\\";
    PluginHost host(dllPath());   // loads from the CWD, so before staging
    StagedFonts fonts(dir);
    setUp(host, dir, PRACTICE);

    // L1: cut, no time and no splits. L2: live, S1 crossed.
    host.raceLap(PRACTICE, 10, /*lapNum=*/1, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(PRACTICE, 10, /*lapNum=*/1, /*split=*/0, 20500);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.draw();

    int dashes = 0;
    for (const auto& r : host.hudStringRows("lap_log_hud")) {
        CHECK_MESSAGE(r.text.find(":--") == std::string::npos, r.text);
        if (r.text == "-") ++dashes;
    }
    const auto l1 = rowTexts(host, "L1");
    CHECK(std::count(l1.begin(), l1.end(), std::string("-")) >= 3);   // its sectors
    const auto l2 = rowTexts(host, "L2");
    CHECK(rowHas(host, "L2", "20.500"));
    CHECK(std::count(l2.begin(), l2.end(), std::string("-")) >= 1);   // S3 not reached yet
    CHECK(dashes > 4);   // the empty rows beneath too
    host.shutdown();
}

TEST_CASE("lap log: a cut lap that kept its time shows the time, not a word") {
    const char* dir = "Z:\\tmp\\mxbmrp3-tests\\lap_log_status_race\\";
    PluginHost host(dllPath());   // loads from the CWD, so before staging
    StagedFonts fonts(dir);
    setUp(host, dir, RACE1);
    REQUIRE(host.setHudVisible("timing_hud", true));

    host.classify(RACE1, 61000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, 61000, /*best=*/0, 20000, 40000, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.draw();

    const Cell l1 = timeCell(host, "L1");
    CHECK(l1.text == "1:01.000");

    // The Timing panel's INVALID for the same lap is a word too: not Digits.
    REQUIRE(host.timingInvalidShown());
    const auto rows = host.hudStringRows("timing_hud");
    int invalidFont = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text == "INVALID") invalidFont = host.stringFont("timing_hud", static_cast<int>(i));
    }
    REQUIRE(invalidFont != 0);
    REQUIRE(l1.font != 0);
    CHECK(invalidFont != l1.font);
    host.shutdown();
}

TEST_CASE("lap log: a lap through the pits says PIT from pit entry, keeping its crossed sectors") {
    // The sequence of an in-game capture (forest_short_race2_practice_gaps.tape:
    // S1 crossed, Esc to the pits, back out, the line): the game closes that lap
    // with time 0 and split 1 intact. Before, the live row froze on the paused
    // time until the line, and the PIT row it became had dropped the S1 the
    // rider had actually ridden.
    const char* dir = "Z:\\tmp\\mxbmrp3-tests\\lap_log_status_pit\\";
    PluginHost host(dllPath());   // loads from the CWD, so before staging
    StagedFonts fonts(dir);
    setUp(host, dir, PRACTICE);

    // L1: clean. L2: S1 crossed, then Esc to the pits.
    host.raceLap(PRACTICE, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(PRACTICE, 10, /*lapNum=*/1, /*split=*/0, 20500);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.draw();
    REQUIRE(rowHas(host, "L2", "20.500"));
    host.runStop();
    host.runDeinit();
    host.draw();

    // In the pits: the live row says PIT already, in the text font, S1 kept.
    Cell live = timeCell(host, "L2");
    CHECK(live.text == "PIT");
    CHECK(live.font != timeCell(host, "L1").font);
    CHECK(rowHas(host, "L2", "20.500"));

    // Back out: still PIT, not a time and not a vanished row.
    host.runInit(PRACTICE);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.88f } });
    host.draw();
    CHECK(timeCell(host, "L2").text == "PIT");

    // The line: the game reports the lap as time 0 with split 1 intact.
    host.raceLap(PRACTICE, 10, 2, 0, /*best=*/0, 20500, 0);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.draw();
    CHECK(timeCell(host, "L2").text == "PIT");
    CHECK(rowHas(host, "L2", "20.500"));
    // The new lap's live row is a timed lap again.
    CHECK(timeCell(host, "L3").text != "PIT");
    host.shutdown();
}
