// ============================================================================
// tests/integration/tests/dash_test.cpp
// The Dash: one card cycling compact pages, drawn from the central data rather
// than by hosting other HUDs. Pins:
//
//   2. The card shows the SAME notice the Notices HUD does: both read the one
//      NoticeTracker, which HudManager steps once a frame. A second copy of the
//      state machine would race the Notices HUD for each timed notice's
//      PluginData flag (whoever consumed it first, the other never showed it).
//   3. With the Notices HUD off, the Dash alone keeps the tracker stepping, so
//      its notice still shows.
//   4. One panel (one quad unthemed) and an icon per page that fits the strip.
//   5. A notice is drawn as the Notices HUD draws it: on a slab whose colour is
//      its meaning (BLUE FLAG on the real flag blue), the caption in the ink
//      that reads on it.
//   6. Dash + / Dash - step the card through its pages, wrapping both ways.
//   7. The Standings page: the displayed rider's row in the middle, between the
//      riders just ahead (negative) and behind (positive): position, race
//      number, short name and official gap; "-" with nobody there. Spectating,
//      the spectated rider is in the middle.
//   8. ...decided by the Standings HUD's planGap(): no number before the first
//      split, a retired rider's state instead of a gap.
//   9. The Timing page: the central lap timer's running time and under it the
//      live gap to the reference lap, whatever the Timing HUD's Gap toggle,
//      and no other comparison (the title row names the reference),
//      "-" for each until there is one; through a split's freeze, the held official gap, as
//      the Timing HUD and Gap Bar show.
//  10. The panel lines up with the HUDs around it: as wide as Ideal Lap,
//      the page's section at least the Speed widget's card tall, a whole number
//      of grid rows, and by default centred under the Timing HUD.
//  12. The Laps page: the three newest laps, newest first, each with its gap
//      to the best; a struck lap muted with no gap.
//  11. With Motion on, a step crossfades the card and slides the strip;
//      a step mid-fade never brings the page that was fading in to full, and
//      one back to the page fading out never dips it; the other pages' icons
//      are dimmed.
//  17. The RPM page lights the RPM widget's segments from the same
//      readLights(), the revs to the nearest 50 under them at the right; it
//      and the Fuel page take their widgets' colours.
//  18. The Gap bar page: the Gap Bar's fill from the card's centre, left
//      behind and right ahead, at the Gap Bar's opacity, under the gap as the
//      Timing page shows it; last and best lap either side under the bar.
//  21. The strip is a ring: stepping past the last page slides on to the first.
//  22. The Fuel page: the Fuel widget's rows with its labels; N/A off track;
//      with all four rows on, Used gives way; beside them, a tank bar lit to
//      the share of the tank left.
//  23. Page names: a step shows the page's name, held, then its content; a
//      notice taking over, or the page coming back after, shows none.
//  24. Settings > Dash > Appearance > Notice focus off: no takeover, and no page
//      shows the notice (Notices is not among the pages).
//  25. Themed, the page and the strip are each a section card, the strip's
//      icons inside the second.
//  28. The Standings page's own row: the best lap where Standings labels it.
//  29. The Telemetry page: the Telemetry HUD's grid and traces, from the same history,
//      which PluginData fills for the Dash alone while the page is up (and not
//      on another page: the Dash ships on, so it must not cost everyone that).
//  30. Settings survive a restart through the real serializer (page, pages,
//      options), and a file with every page off loads with all of them on.
//  31. Appearance > UI icons off: the strip draws each page's two-letter mark
//      instead of its icon, as every UI icon falls back to text.
//  16. The Time page: the session clock as the game sends it; in overtime the
//      label, which a second passing does not rebuild.
//  15. The Ideal page: last and best lap with their gap to the ideal lap (the
//      best sectors summed), and the ideal itself.
//  13. Settings > Dash > Pages: the steps skip a page switched off, and the
//      strip drops its icon.
//  14. With Notice focus on, a new notice takes the card over
//      and the rider's page comes back when it ends; a step during the takeover
//      dismisses that notice and brings the rider's page straight back. The
//      strip stays on the rider's page through the takeover.
//  32. The Delta Trace page draws the Delta Trace HUD's chart (the same
//      DeltaTracePlot): point for point the line the HUD draws.
//  33. The Event Log page shows the Event Log's newest events.
//  34. The Session page shows the Session HUD's rows, as that HUD writes them.
//  35. On by default with Dash - / Dash + on Page Up / Page Down, and until the
//      first step ever the card says so; not again after.
//  36. The Flat map page: the riders along the card as the Gap Bar's markers
//      (never its ghost), the race leader's crowned (races only).
//  37. Appearance > Page strip: the strip on any side of the page, or none;
//      down a side, its icons stay inside its card through a step, and the
//      strip's card is as tall as the page's.
//  38. With the title on, it names the page: "Dash: Timing".
//  39. A PB notice on the card carries a second line: the lap and its gain on
//      the best it beat (the lap alone for a first best); the Notices HUD's
//      one-row slab leaves it out.
//  40. While the Standings HUD's status column is on, the Standings page draws
//      its status icons before the position (a tracked rider's shape).
//  41. The Standings page tints the gaps either side, ahead red and behind
//      green, and, with the Standings HUD's penalty column on, shows a rider's
//      penalty before the gap, the name cut to make room.
//  42. A fresh install leaves RPM out of the cycle (the RPM widget is on
//      instead), and its Gap Bar, switched on, sits under the Dash, clear of
//      every HUD shown (unthemed, as a fresh install is).
//  43. With custom timing segments set and the player on track, the Timing
//      page is the Timing HUD's segment line (TimingHud::segmentLine): the
//      segment time, then the summed segment bests as the target, then on a
//      repeat the held delta to them; the title row names "Best".
//  44. A page whose icon file is missing draws its mark, not a filled square.
//      (The other cases start from a tree holding the strip's icons, startDash:
//      the harness ships none.)
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

bool shows(PluginHost& host, const char* hud, const std::string& text) {
    const auto rows = host.hudStringRows(hud);
    return std::any_of(rows.begin(), rows.end(),
                       [&](const PluginHost::StringRow& r) { return r.text == text; });
}

// The strip's page icons. The harness ships no icons, and a missing one draws
// the page's two letters instead (the letters case below), so every case starts
// from a discovery tree of its own that holds them: the CWD-relative tree
// (plugins\\mxbmrp3_data) for the length of Startup, the one call that
// discovers assets, as lap_log_status_test stages its fonts. Not the save dir's
// icons folder: Startup copies that INTO the shared tree, where it would stay for
// every later case and test binary in the run -- each HUD's title icon included.
// The Dash's own title icon (hud-dash) is left out, so its quad counts stay the
// strip's.
int startDash(PluginHost& host, const std::string& saveWin, bool icons = true) {
    if (!icons) return host.startup(saveWin.c_str());
    static const std::filesystem::path tree = [] {
        const std::filesystem::path root = "Z:\\tmp\\mxbmrp3-tests\\dash_icon_tree";
        const std::filesystem::path icons = root / "plugins" / "mxbmrp3_data" / "icons";
        std::filesystem::create_directories(icons);
        for (const char* icon : { "hud-standings", "hud-timing", "hud-laplog", "hud-clock", "hud-fuel",
                                  "hud-gapbar", "hud-flatmap", "hud-ideallap", "hud-gauge", "hud-deltatrace",
                                  "hud-sessioncharts", "hud-session", "hud-telemetry", "hud-eventlog",
                                  "hud-notices" }) {
            const std::string src = std::string(MXB_REPO_DATA_DIR "/icons/") + icon + ".tga";
            const std::string dst = (icons / (std::string(icon) + ".tga")).string();
            REQUIRE_MESSAGE(CopyFileA(src.c_str(), dst.c_str(), FALSE) != 0, "shipped icon missing: " << src);
        }
        return root;
    }();
    const std::filesystem::path prev = std::filesystem::current_path();
    std::filesystem::current_path(tree);
    const int result = host.startup(saveWin.c_str());
    std::filesystem::current_path(prev);   // integration_main's status sentinel is CWD-relative
    return result;
}

// On track with no setup name, which raises the DEFAULT SETUP notice and keeps it
// up for the notice duration -- long enough for the assertions.
void onTrack(PluginHost& host) {
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
}

// Starts from a hand-written INI (the rest at defaults) in a fresh save dir.
// `icons` false: the shared tree, with none (see startDash).
void startWithIni(PluginHost& host, const std::string& saveWin, const char* body, bool icons = true) {
    REQUIRE(startDash(host, saveWin, icons) >= 0);
    const std::string dir = saveWin + "mxbmrp3";
    std::filesystem::create_directories(dir);
    std::ofstream ini(dir + "\\mxbmrp3_settings.ini", std::ios::trunc);
    REQUIRE(ini.is_open());
    // The first-step hint already dropped: these tests read the pages themselves.
    // On the first page, Standings, with every page in the cycle, unless the test
    // says otherwise: they count their steps from there (a fresh install opens on
    // Timing without Standings, Time and RPM, DashHud::DEFAULT_PAGE / DEFAULT_PAGES).
    // The strip under the page, as the geometry checks measure it (a fresh
    // install has it at the right, DashHud::DEFAULT_STRIP_PLACE).
    std::string ini_body = body;
    std::string dash;
    if (ini_body.find("\npage=") == std::string::npos) dash += "page=standings\n";
    if (ini_body.find("strip=") == std::string::npos) dash += "strip=bottom\n";
    for (const char* key : { "page_standings=", "page_time=", "page_rpm=" }) {
        if (ini_body.find(key) == std::string::npos) dash += std::string(key) + "1\n";
    }
    const size_t at = ini_body.find("[DashHud]\n");
    if (at == std::string::npos) ini_body += "\n[DashHud]\n" + dash;
    else ini_body.insert(at + 10, dash);
    ini << "[Settings]\nversion=6\n\n[Messages]\ndashTried=1\n\n" << ini_body;
    ini.close();
    host.loadSettings(saveWin.c_str());
}

// The strip's icons: the Dash's quads from the first icon on.
std::vector<PluginHost::QuadRect> stripIcons(PluginHost& host) {
    const auto quads = host.hudQuadRects("dash_hud");
    const int first = host.dashFirstIcon();
    REQUIRE(first >= 0);
    return std::vector<PluginHost::QuadRect>(quads.begin() + std::min<size_t>(first, quads.size()), quads.end());
}

}  // namespace

TEST_CASE("dash: the notice up, from the shared data") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, "Z:\\tmp\\mxbmrp3-tests\\dash\\") >= 0);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.setHudVisible("dash_hud", true));
    REQUIRE(host.setHudVisible("notices_hud", true));   // off by default

    host.draw();
    // No notice yet: the rider's page, under the first-step hint (a fresh start).
    CHECK_FALSE(shows(host, "dash_hud", "DEFAULT SETUP"));

    onTrack(host);
    host.telemetry(20.0f, 3);
    host.draw();

    // The same notice on both panels.
    CHECK(shows(host, "notices_hud", "DEFAULT SETUP"));
    CHECK(shows(host, "dash_hud", "DEFAULT SETUP"));

    // The panel (one quad unthemed), the notice's slab, and the three icons
    // that fit down the strip at the right (the default) round the page
    // showing; the rest wait off its ends.
    CHECK(host.hudQuadRects("dash_hud").size() == 5);
    CHECK(stripIcons(host).size() == 3);

    host.shutdown();
}

TEST_CASE("dash: with the Notices HUD off, the Dash alone keeps the notice coming") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, "Z:\\tmp\\mxbmrp3-tests\\dash_alone\\") >= 0);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.setHudVisible("notices_hud", false));
    REQUIRE(host.setHudVisible("dash_hud", true));

    onTrack(host);
    host.draw();
    CHECK(shows(host, "dash_hud", "DEFAULT SETUP"));

    host.shutdown();
}

TEST_CASE("dash: a blue flag is drawn on a slab in the flag's own blue") {
    // The blue flag notice ships OFF (Settings > Notices), so switch it on in the
    // saved INI and start again on it.
    const char* saveDir = "Z:\\tmp\\mxbmrp3-tests\\dash_blue\\";
    const std::string iniPath = "Z:\\tmp\\mxbmrp3-tests\\dash_blue\\mxbmrp3\\mxbmrp3_settings.ini";
    std::remove(iniPath.c_str());
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\dash_blue", nullptr);
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\dash_blue\\mxbmrp3", nullptr);
    {
        PluginHost first(dllPath());
        REQUIRE(first.loaded());
        REQUIRE(startDash(first, saveDir) >= 0);
        REQUIRE(first.save());
        first.shutdown();
    }
    {
        std::string text = ini::readFile(iniPath);
        const std::string off = "notice_blue_flag=0";
        size_t at = 0;
        int switched = 0;
        while ((at = text.find(off, at)) != std::string::npos) {
            text[at + off.size() - 1] = '1';
            ++switched;
        }
        REQUIRE(switched > 0);
        REQUIRE(ini::writeFile(iniPath, text));
    }

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, saveDir) >= 0);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.setHudVisible("dash_hud", true));

    // The player (R2) a lap down, with the leader (R1) just behind: blue-flagged.
    host.eventInit("TestTrack", "Backmarker");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Backmarker");   // first entry after EventInit = the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.addEntry(4, "Second");
    host.session(6, /*numLaps=*/10);   // Race1, running
    host.classify(6, 120000, {
        { .num = 1, .laps = 3 }, { .num = 4, .laps = 3 },
        { .num = 2, .laps = 1 }, { .num = 3, .laps = 1 },
    });
    host.raceTrackPosition({
        { .num = 1, .trackPos = 0.50f }, { .num = 2, .trackPos = 0.53f },
        { .num = 3, .trackPos = 0.20f }, { .num = 4, .trackPos = 0.10f },
    });
    REQUIRE(host.isRiderBlueFlagged(2));
    host.draw();

    const auto rows = host.hudStringRows("dash_hud");
    int found = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].text == "BLUE FLAG") found = static_cast<int>(i);
    REQUIRE(found >= 0);
    // ColorPalette::BLUE, #0000ff, packed ABGR: only the blue byte set. The slab
    // carries it, as on the Notices HUD; the caption is the ink that reads on it.
    REQUIRE(host.hasInkHooks());
    const size_t quads = host.hudQuadRects("dash_hud").size();
    int slab = -1;
    for (size_t i = 0; i < quads; ++i)
        if ((host.quadColor("dash_hud", static_cast<int>(i)) & 0x00FFFFFFul) == 0x00FF0000ul)
            slab = static_cast<int>(i);
    CHECK(slab >= 0);
    CHECK((host.stringColor("dash_hud", found) & 0x00FFFFFFul) != 0x00FF0000ul);

    host.shutdown();
}

TEST_CASE("dash: Dash + and Dash - cycle the card, and Standings shows the riders either side") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Page names off: the steps here check the content straight after.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gaps\\", "[DashHud]\npage_names=0\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    REQUIRE(host.setHudVisible("dash_hud", true));

    // The player (R2) second: 1.5 s behind the leader, 2.5 s ahead of third.
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Averyveryverylongname");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10);   // Race1, running
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
    });
    host.draw();
    CHECK(host.dashPage() == 0);   // Standings

    auto step = [&](const char* hotkey) {
        REQUIRE(host.injectHotkey(hotkey));
        host.draw();
        host.draw();
    };

    auto standings = [&]() {
        CHECK(shows(host, "dash_hud", "1"));   // the race numbers
        CHECK(shows(host, "dash_hud", "3"));
        CHECK(shows(host, "dash_hud", "Ave"));   // the short name, as Standings' Short
        CHECK(shows(host, "dash_hud", "2"));    // the player in the middle
        CHECK(shows(host, "dash_hud", "Me"));
        CHECK(shows(host, "dash_hud", "-1.500"));
        CHECK(shows(host, "dash_hud", "Thi"));
        CHECK(shows(host, "dash_hud", "+2.500"));
    };
    standings();

    step("dash_next");
    CHECK(host.dashPage() == 1);   // Timing
    step("dash_next");
    CHECK(host.dashPage() == 2);   // Laps
    step("dash_next");
    CHECK(host.dashPage() == 3);   // Time
    CHECK(shows(host, "dash_hud", "02:00"));   // the session clock, from classify
    step("dash_next");
    CHECK(host.dashPage() == 4);   // Fuel
    step("dash_next");
    CHECK(host.dashPage() == 5);   // Gap bar
    step("dash_next");
    CHECK(host.dashPage() == 6);   // Flat map
    step("dash_next");
    CHECK(host.dashPage() == 7);   // Ideal
    step("dash_next");
    CHECK(host.dashPage() == 8);   // RPM
    step("dash_next");
    CHECK(host.dashPage() == 9);   // Delta trace
    step("dash_next");
    CHECK(host.dashPage() == 10);  // Gap chart
    step("dash_next");
    CHECK(host.dashPage() == 11);  // Session
    step("dash_next");
    CHECK(host.dashPage() == 12);  // Telemetry
    step("dash_next");
    CHECK(host.dashPage() == 13);  // Event log
    step("dash_next");   // wraps forward, Notices not among the pages
    CHECK(host.dashPage() == 0);
    step("dash_prev");   // wraps back
    CHECK(host.dashPage() == 13);
    step("dash_prev");
    step("dash_next");
    step("dash_next");
    CHECK(host.dashPage() == 0);
    standings();

    // Leading: the two behind, the own row without a best lap a "-".
    host.classify(6, 121000, {
        { .num = 2, .laps = 3, .gap = 0 },
        { .num = 1, .laps = 3, .gap = 800 },
        { .num = 3, .laps = 3, .gap = 4000 },
    });
    host.draw();
    CHECK(shows(host, "dash_hud", "-"));
    CHECK(shows(host, "dash_hud", "2"));
    CHECK(shows(host, "dash_hud", "+0.800"));

    host.shutdown();
}

TEST_CASE("dash: alone, Standings shows the rider's own position, name and best lap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_alone\\", "[DashHud]\npage_names=0\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.setHudVisible("dash_hud", true));

    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(7, "Me");
    host.session(2, /*numLaps=*/0);   // practice, running
    host.classify(2, 600000, { { .num = 7, .best = 83456, .laps = 3 } });
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 0);   // Standings, the first page
    CHECK(shows(host, "dash_hud", "1"));
    CHECK(shows(host, "dash_hud", "7"));   // the race number
    CHECK(shows(host, "dash_hud", "Me"));
    CHECK(shows(host, "dash_hud", "1:23.456"));

    host.shutdown();
}

TEST_CASE("dash: Standings guards its gaps as the Standings HUD does") {
    // The first Standings page subtracted the two riders' gaps bare: before anyone
    // crossed a split every gap was 0 and the page showed a zero gap both ways, and a
    // retired rider got a number instead of their state. It now goes through
    // StandingsGap::planGap(), the Standings HUD's own adjacent-gap decision.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Page names off: the steps here check the content straight after.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_guards\\", "[DashHud]\npage_names=0\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    REQUIRE(host.setHudVisible("dash_hud", true));   // on Standings, the first page

    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10);   // Race1, running

    // Off the start, before the first split: no gaps yet, so no numbers.
    host.classify(6, 5000, {
        { .num = 1, .laps = 0 }, { .num = 2, .laps = 0 }, { .num = 3, .laps = 0 },
    });
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 0);   // Standings
    CHECK(shows(host, "dash_hud", "1"));
    CHECK(shows(host, "dash_hud", "3"));
    CHECK_FALSE(shows(host, "dash_hud", "0.0"));
    CHECK_FALSE(shows(host, "dash_hud", "+0.000"));
    CHECK_FALSE(shows(host, "dash_hud", "-0.000"));
    {
        const auto rows = host.hudStringRows("dash_hud");
        CHECK(std::count_if(rows.begin(), rows.end(),
                            [](const PluginHost::StringRow& r) { return r.text == "-"; }) >= 2);
    }

    // Under way, the rider behind retired: their state, not a gap.
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 2, .gap = 4000, .state = 3 },   // RiderState::RETIRED
    });
    host.draw();
    CHECK(shows(host, "dash_hud", "-1.500"));
    CHECK(shows(host, "dash_hud", "RET"));
    CHECK_FALSE(shows(host, "dash_hud", "+2.500"));

    host.shutdown();
}

TEST_CASE("dash: Timing shows the running lap and the gap under it") {
    // The gap whatever the Timing HUD's Gap toggle (off by default).
    bool gapOn = false;
    SUBCASE("Timing HUD's Gap on") { gapOn = true; }
    SUBCASE("Timing HUD's Gap off") { gapOn = false; }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Page names off: the steps here check the content straight after.
    startWithIni(host, gapOn ? "Z:\\tmp\\mxbmrp3-tests\\dash_timing_gap\\" : "Z:\\tmp\\mxbmrp3-tests\\dash_timing\\",
                 gapOn ? "[DashHud]\npage_names=0\n[TimingHud]\ngapRow=3\n" : "[DashHud]\npage_names=0\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    REQUIRE(host.setHudVisible("dash_hud", true));

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);   // display rider = #4
    host.telemetry(20.0f, 3);   // so gear and speed are no placeholders

    REQUIRE(host.injectHotkey("dash_next"));   // Standings -> Timing
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 1);

    // No lap timer and no reference lap yet: a placeholder each, and no
    // comparison beside the gap (one reference at a time, as every gap).
    auto placeholders = [&] {
        const auto rows = host.hudStringRows("dash_hud");
        return std::count_if(rows.begin(), rows.end(),
                             [](const PluginHost::StringRow& r) { return r.text == "-"; });
    };
    CHECK(placeholders() == 2);
    CHECK_FALSE(shows(host, "dash_hud", "Session PB"));
    CHECK_FALSE(shows(host, "dash_hud", "All-time PB"));

    // Across the line the timer runs; a planted gap stands in for a reference lap
    // (a real one needs a full lap of position samples).
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
    REQUIRE(host.elapsedLapTime() >= 0);
    REQUIRE(host.gapBarForceGap(-1234, true));
    host.draw();
    CHECK(placeholders() == 0);
    // To the millisecond, as the Timing HUD.
    CHECK(shows(host, "dash_hud", "-1.234"));

    host.shutdown();
}

TEST_CASE("dash: Gap bar fills from the centre toward the side the gap is on") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Notice focus off, or the DEFAULT SETUP notice of runInit takes it over.
    // The Gap Bar at an opacity of its own, which its page's fill takes.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gapbar\\",
                 "[DashHud]\nvisible=1\npage=gapbar\nnotice_takeover=0\n"
                 "[GapBarHud]\nbackgroundOpacity=0.4\n");
    REQUIRE(host.hasStringRows());
    host.draw();
    REQUIRE(host.dashPage() == 5);   // Gap bar

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);   // display rider = #4
    host.telemetry(20.0f, 3);
    host.draw();
    // No reference lap: the placeholder and no fill (the panel, then the icons).
    // Last and best lap sit under the bar, a dash each while no lap is done.
    CHECK(shows(host, "dash_hud", "-"));
    CHECK(shows(host, "dash_hud", "Last"));
    CHECK(shows(host, "dash_hud", "Best"));
    const auto bare = host.hudQuadRects("dash_hud");

    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
    REQUIRE(host.elapsedLapTime() >= 0);

    // The fill: one quad more, after the panel, edged on the card's centre.
    auto fill = [&] {
        const auto quads = host.hudQuadRects("dash_hud");
        REQUIRE(quads.size() == bare.size() + 1);
        return quads[1];
    };
    const auto card = bare[0];
    const double centre = (card.l + card.r) * 0.5;

    REQUIRE(host.gapBarForceGap(1234, true));   // behind: left of centre
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "+1.234"));   // to the millisecond, as the Gap Bar
    auto q = fill();
    // The fill at the Gap Bar's opacity (0.4 of 255), as the Gap Bar draws it.
    CHECK((host.quadColor("dash_hud", 1) >> 24) == 102);
    CHECK(std::abs(q.r - centre) < 1e-6);
    CHECK(q.l < centre - 1e-3);
    CHECK(q.l > card.l);

    REQUIRE(host.gapBarForceGap(-1234, true));   // ahead: right of centre
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "-1.234"));
    q = fill();
    CHECK(std::abs(q.l - centre) < 1e-6);
    CHECK(q.r > centre + 1e-3);
    CHECK(q.r < card.r);

    host.shutdown();
}

TEST_CASE("dash: Timing holds the official gap through a split's freeze") {
    // The first Timing page read the live gap raw, so at every split it showed a
    // different number from the Timing HUD and Gap Bar beside it, which hold the
    // official one for the freeze (General's, 5 s). The ride is pb_gap_test's:
    // lap 1 60.000 (S1 20.000), lap 2 65.000, then lap 3's S1 at 23.456 -- 3.456
    // behind the session best's.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Notice focus off, or the DEFAULT SETUP notice of runInit takes it over.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_freeze\\",
                 "[DashHud]\nvisible=1\npage=timing\nnotice_takeover=0\n[TimingHud]\ngapRow=3\n");
    REQUIRE(host.hasStringRows());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);   // Race1
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.runStart();

    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(6, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(6, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(6, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(6, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(/*on track*/ 0);
    host.raceSplit(6, 10, 2, 0, 25000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(6, 10, 2, 1, 45000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(6, 125000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(6, 10, 2, 65000, /*best=*/0, 25000, 45000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    host.raceSplit(6, 10, 3, 0, 23456);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.drawWithState(0);

    REQUIRE(host.dashPage() == 1);   // Timing
    int liveGap = 0;
    REQUIRE(host.liveGapRef(/*session PB*/ 0, liveGap));
    CHECK_MESSAGE(liveGap != 3456, "the live gap " << liveGap << " must differ from the held one");
    CHECK(shows(host, "dash_hud", "+3.456"));   // the official gap, held

    host.shutdown();
}

TEST_CASE("dash: the panel takes the sizes of the HUDs around it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // The strip under the page, so the panel is the page's width alone.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_sizes\\", "[DashHud]\nstrip=bottom\n");
    REQUIRE(host.hasQuadRects());
    for (const char* hud : { "dash_hud", "speed_widget", "timing_hud", "ideal_lap_hud" })
        REQUIRE(host.setHudVisible(hud, true));
    host.draw();
    host.draw();

    const auto quads = host.hudQuadRects("dash_hud");   // the panel, then the icons
    REQUIRE(quads.size() >= 2);
    PluginHost::QuadRect card, panel, speedCard, speed, timing, ideal;
    REQUIRE(host.hudCardRect("dash_hud", &card, &panel));
    REQUIRE(host.hudCardRect("speed_widget", &speedCard, &speed));
    REQUIRE(host.hudCardRect("timing_hud", nullptr, &timing));
    REQUIRE(host.hudCardRect("ideal_lap_hud", nullptr, &ideal));
    auto w = [](const PluginHost::QuadRect& r) { return r.r - r.l; };
    auto h = [](const PluginHost::QuadRect& r) { return r.b - r.t; };
    // As wide as the Ideal Lap HUD (and Standings, FMX); the page's section at
    // least as tall as the Speed widget's card, so its rows line up beside it,
    // and the strip's icons under it.
    CHECK(w(panel) == doctest::Approx(w(ideal)).epsilon(1e-4));
    const auto icons = stripIcons(host);
    REQUIRE(!icons.empty());
    for (const auto& icon : icons)
        CHECK(icon.t - quads[0].t >= card.t - panel.t + h(speedCard) - 1e-6);
    // A whole number of grid rows tall, so the HUDs around it tile with it.
    REQUIRE(host.hasLayoutCells());
    const double cellH = host.layoutCells().cellH;
    REQUIRE(cellH > 0.0);
    const double rows = h(panel) / cellH;
    CHECK(rows == doctest::Approx(std::round(rows)).epsilon(1e-4));
    // DashHud::DEFAULT_WIDTH_CELLS its real width.
    const double across = host.layoutCells().cellW;   // one cell (the Dash at 100%)
    CHECK(w(panel) == doctest::Approx(29.0 * across).epsilon(1e-4));

    host.shutdown();
}

TEST_CASE("dash: by default it sits on top of the centre stack, Timing under it, its strip at the right") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, "Z:\\tmp\\mxbmrp3-tests\\dash_default_place\\") >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasLayoutCells());
    for (const char* hud : { "dash_hud", "timing_hud" })
        REQUIRE(host.setHudVisible(hud, true));
    host.draw();
    host.draw();

    const auto dash = host.hudQuadRects("dash_hud");
    const auto timingQuads = host.hudQuadRects("timing_hud");
    REQUIRE(!dash.empty());
    REQUIRE(!timingQuads.empty());
    const PluginHost::QuadRect panel = dash[0], timing = timingQuads[0];   // the backgrounds
    // The strip beside the page: the panel's icons right of its middle.
    const auto icons = stripIcons(host);
    REQUIRE(!icons.empty());
    for (const auto& icon : icons)
        CHECK(icon.l > panel.l + (panel.r - panel.l) * 0.5);
    // Centred across the screen at the centre stack's top (one cell down), and
    // DashHud::DEFAULT_PANEL_WIDTH_CELLS (which places it) its real width.
    const double across = host.layoutCells().cellW;
    CHECK((panel.r - panel.l) == doctest::Approx(32.25 * across).epsilon(1e-4));
    CHECK((panel.l + panel.r) * 0.5 == doctest::Approx(0.5).epsilon(1e-4));
    CHECK(panel.t == doctest::Approx(host.layoutCells().cellH).epsilon(1e-4));
    // The Timing HUD stacks under it now (hud/center_stack.h), not in its spot.
    CHECK(timing.t > panel.b);

    host.shutdown();
}

// The Gap Bar's default spot used to lie inside the Dash's (both at the centre
// stack's top), so a freshly enabled Gap Bar was hidden under it and could not
// be grabbed. It now sits under the Dash, the centre stack's two cells apart,
// clear of every HUD a fresh install shows. UNTHEMED, which a fresh install is
// and the stored defaults are derived for: a theme grows the Dash into the gap
// while the stored top stays put, the stack's accepted cost (see the themed
// note in center_stack_theme_test).
TEST_CASE("dash: a fresh install's Gap Bar sits under the Dash, clear of every HUD shown") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_gapbar_default\\";
    {
        const std::string dir = std::string(saveWin) + "mxbmrp3";
        std::filesystem::create_directories(dir);
        std::remove((dir + "\\mxbmrp3_settings.ini").c_str());
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasLayoutCells());
    onTrack(host);
    REQUIRE(host.hudVisible("gap_bar_hud") == 0);   // still off by default
    REQUIRE(host.setHudVisible("gap_bar_hud", true));
    host.draw();
    host.draw();

    const auto g = host.hudScreenEdges("gap_bar_hud");
    const auto d = host.hudScreenEdges("dash_hud");
    INFO("gapbar [" << g.l << ".." << g.r << " x " << g.t << ".." << g.b << "] dash ["
         << d.l << ".." << d.r << " x " << d.t << ".." << d.b << "]");
    REQUIRE(g.r > g.l);
    REQUIRE(d.r > d.l);
    // Under the Dash and centred with it, two cells of air
    // (CenterStack::DASH_DEFAULT_BOTTOM_CELL is the Dash's bottom).
    CHECK(std::abs((g.l + g.r) - (d.l + d.r)) <= 4);
    const int twoCells = static_cast<int>(2.0 * host.layoutCells().cellH * 1e6);
    CHECK(std::abs((g.t - d.b) - twoCells) <= 2);

    // Every HUD and widget but the full-screen layers and the settings menu.
    const char* const shown[] = {
        "standings_hud", "map_hud", "radar_hud", "lap_log_hud", "friends_hud", "ideal_lap_hud",
        "telemetry_hud", "performance_hud", "delta_trace_hud", "pitboard_hud", "records_hud",
        "session_charts_hud", "fmx_hud", "stats_hud", "event_log_hud", "stream_chat_hud",
        "lap_widget", "position_widget", "time_widget", "session_widget", "speed_widget",
        "gear_widget", "crash_widget", "rpm_widget", "speedo_widget", "tacho_widget",
        "timing_hud", "bars_widget", "notices_hud", "spotter_widget",
        "fuel_widget", "dash_hud", "achievement_widget", "gamepad_widget", "lean_widget",
        "gforce_widget", "compass_widget", "clock_widget", "prestige_widget",
        "tyre_temp_widget", "ecu_widget", "toggle_strip_widget",
    };
    int measured = 0;
    for (const char* name : shown) {
        if (host.hudVisible(name) != 1) continue;
        const auto e = host.hudScreenEdges(name);
        if (e.r <= e.l || e.b <= e.t) continue;   // nothing drawn yet
        ++measured;
        CAPTURE(name);
        INFO(name << " [" << e.l << ".." << e.r << " x " << e.t << ".." << e.b << "]");
        CHECK((e.r <= g.l || e.l >= g.r || e.b <= g.t || e.t >= g.b));
    }
    CHECK(measured >= 6);   // the Dash, Standings, Map and the corner widgets at least
    host.shutdown();
}

TEST_CASE("dash: with Motion on, a step crossfades the card and the strip follows") {
    constexpr long long T0 = 1000000000ll;   // Motion's clock, in microseconds
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    // Page names off: the steps here check the content straight after.
    // The Timing HUD's Gap on, so the Timing page carries the planted gap.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_motion\\",
                 "[DashHud]\npage_names=0\n[TimingHud]\ngapRow=3\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    REQUIRE(host.setHudVisible("dash_hud", true));
    // Something on each page to tell it by (the pages carry no title): the
    // DEFAULT SETUP notice, the rider ahead's "1", a planted "-1.234" gap.
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10);
    host.runInit(6);
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
    });
    REQUIRE(host.gapBarForceGap(-1234, true));
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);
    host.draw();
    REQUIRE(shows(host, "dash_hud", "DEFAULT SETUP"));

    // A page's alpha, by its marker, or -1 when the page is not drawn.
    auto labelAlpha = [&](const char* page) {
        const std::string mark = std::string(page) == "Notices" ? "DEFAULT SETUP"
                               : std::string(page) == "Standings" ? "1" : "-1.234";
        const auto rows = host.hudStringRows("dash_hud");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == mark)
                return static_cast<int>(host.stringColor("dash_hud", static_cast<int>(i)) >> 24);
        return -1;
    };
    auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };

    REQUIRE(host.injectHotkey("dash_next"));   // Notices -> Standings, at T0
    at(T0);
    at(T0 + 120000);   // halfway: both pages up, neither at full
    const int out = labelAlpha("Notices"), in = labelAlpha("Standings");
    CHECK(out > 0x10);
    CHECK(out < 0xF0);
    CHECK(in > 0x10);
    CHECK(in < 0xF0);
    at(T0 + 300000);   // over
    CHECK(labelAlpha("Notices") == -1);
    CHECK(labelAlpha("Standings") == 0xFF);

    // The other pages' icons dimmed.
    {
        const int first = host.dashFirstIcon();
        const int icons = static_cast<int>(host.hudQuadRects("dash_hud").size()) - first;
        REQUIRE(icons == 7);   // the ones that fit under the card
        int full = 0, dim = 0;
        for (int i = 0; i < icons; ++i) {
            const int a = static_cast<int>(host.quadColor("dash_hud", first + i) >> 24);
            if (a == 0xFF) ++full;
            else if (a > 0 && a < 0xA0) ++dim;   // the outermost fade further toward the ends
        }
        CHECK(full == 1);
        CHECK(dim == 6);
    }

    // A step back mid-fade: the page that had only started to come in fades out
    // from where it got to, never popping to full.
    const long long t1 = T0 + 1000000;
    host.setMotionNowUs(t1);
    REQUIRE(host.injectHotkey("dash_next"));   // Standings -> Timing
    at(t1);
    at(t1 + 60000);    // Timing a quarter of the way in
    const int reached = labelAlpha("Timing");
    REQUIRE(reached > 0);
    REQUIRE(host.injectHotkey("dash_prev"));   // back to Standings
    at(t1 + 60000);
    at(t1 + 61000);
    CHECK(labelAlpha("Timing") <= reached);
    at(t1 + 400000);
    CHECK(labelAlpha("Timing") == -1);
    CHECK(labelAlpha("Standings") == 0xFF);

    // ...and stepping straight back to the page fading out brings it back from
    // where it was, without dipping.
    const long long t2 = T0 + 2000000;
    host.setMotionNowUs(t2);
    REQUIRE(host.injectHotkey("dash_next"));   // Standings -> Timing
    at(t2);
    at(t2 + 30000);    // Standings barely faded
    const int before = labelAlpha("Standings");
    REQUIRE(before > 0xC0);
    REQUIRE(host.injectHotkey("dash_prev"));   // back to Standings
    at(t2 + 30000);
    at(t2 + 31000);
    CHECK(labelAlpha("Standings") >= before - 8);

    host.shutdown();
}

TEST_CASE("dash: Laps shows the newest three laps against the best") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Notice focus off, or the DEFAULT SETUP notice of runInit takes it over.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_laps\\",
                 "[DashHud]\nvisible=1\npage=laps\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    host.draw();
    REQUIRE(host.dashPage() == 2);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.draw();
    {   // No laps yet: a placeholder row each.
        const auto rows = host.hudStringRows("dash_hud");
        CHECK(std::count_if(rows.begin(), rows.end(),
                            [](const PluginHost::StringRow& r) { return r.text == "-"; }) >= 6);
    }

    host.raceLap(6, 10, 1, 60000, /*best=*/1);
    host.raceLap(6, 10, 2, 65000);
    host.raceLap(6, 10, 3, 61000, 0, -1, -1, /*invalid=*/true);
    host.raceLap(6, 10, 4, 62000);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "L4"));
    CHECK(shows(host, "dash_hud", "1:02.000"));
    CHECK(shows(host, "dash_hud", "+2.000"));
    CHECK(shows(host, "dash_hud", "1:01.000"));   // struck: shown, muted, no gap
    CHECK_FALSE(shows(host, "dash_hud", "+1.000"));
    CHECK(shows(host, "dash_hud", "1:05.000"));
    CHECK(shows(host, "dash_hud", "+5.000"));
    CHECK_FALSE(shows(host, "dash_hud", "1:00.000"));   // the best, a fourth lap back
    // In the Lap Log's Order (oldest first by default): the newest lap last.
    auto rowY = [&](const char* text) {
        for (const auto& r : host.hudStringRows("dash_hud")) if (r.text == text) return r.y;
        return -1.0;
    };
    CHECK(rowY("L4") > rowY("L2"));

    host.shutdown();
}

TEST_CASE("dash: Laps marks one best when two laps tie") {
    // The best is the lap, not the time: a later lap equalling it is shown
    // against it (+0.000), not marked best as well.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_laps_tie\\",
                 "[DashHud]\nvisible=1\npage=laps\nnotice_takeover=0\n");
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.raceLap(6, 10, 1, 61000, /*best=*/1);
    host.raceLap(6, 10, 2, 61000);
    host.raceLap(6, 10, 3, 62000);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "+0.000"));
    CHECK(shows(host, "dash_hud", "+1.000"));
    host.shutdown();
}

TEST_CASE("dash: Laps follows the Lap Log's Order, without its live row") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_laps_order\\",
                 "[LapLogHud]\ndisplayOrder=1\n\n[DashHud]\nvisible=1\npage=laps\nnotice_takeover=0\n");
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.raceLap(6, 10, 1, 60000, /*best=*/1);
    host.raceLap(6, 10, 2, 65000);
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 2);
    auto rowY = [&](const char* text) {
        for (const auto& r : host.hudStringRows("dash_hud")) if (r.text == text) return r.y;
        return -1.0;
    };
    // Newest first, as set on the Lap Log.
    REQUIRE(rowY("L2") >= 0.0);
    CHECK(rowY("L2") < rowY("L1"));

    // The lap under way gets no row (the Lap Log's live row is left off).
    host.classify(6, 200000, { { .num = 10, .laps = 2, .gap = 0, .pit = 0 } });
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.03f } }); host.draw();
    host.draw();
    CHECK_FALSE(shows(host, "dash_hud", "L3"));

    host.shutdown();
}

TEST_CASE("dash: the steps skip a page switched off, and so does the strip") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMessages());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_pages\\",
                 "[DashHud]\nvisible=1\nnotice_takeover=0\npage_timing=0\npage_gapbar=0\npage_flatmap=0\npage_time=0\npage_ideal=0\npage_rpm=0\n"
                 "page_fuel=0\npage_telemetry=0\npage_deltatrace=0\npage_gapchart=0\npage_eventlog=0\npage_session=0\n");
    host.draw();
    REQUIRE(host.dashPage() == 0);   // Standings
    // The panel and an icon for each page left on.
    CHECK(host.hudQuadRects("dash_hud").size() == 3);

    auto step = [&](const char* hotkey) {
        REQUIRE(host.injectHotkey(hotkey));
        host.draw();
        host.draw();
    };
    step("dash_next");
    CHECK(host.dashPage() == 2);   // Laps, past Timing
    step("dash_next");
    CHECK(host.dashPage() == 0);
    step("dash_prev");
    CHECK(host.dashPage() == 2);

    host.shutdown();
}

TEST_CASE("dash: a notice takes the card over, then the rider's page comes back") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMessages());
    // The shortest notice duration, so this one ends within the test.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_takeover\\",
                 "[NoticesHud]\npbDurationMs=1000\n\n[DashHud]\nvisible=1\npage=laps\n");
    REQUIRE(host.setHudVisible("notices_hud", true));   // off by default
    REQUIRE(host.hasStringRows());
    host.draw();
    REQUIRE(host.dashPage() == 2);   // Laps
    auto icons = [&] { return stripIcons(host); };
    const auto onLaps = icons();
    REQUIRE(!onLaps.empty());

    onTrack(host);   // raises DEFAULT SETUP
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 14);   // Notices, for as long as it is up
    CHECK(shows(host, "dash_hud", "DEFAULT SETUP"));
    {   // The strip stays on the rider's page: a notice is not a page they chose.
        const auto held = icons();
        REQUIRE(held.size() == onLaps.size());
        for (size_t i = 0; i < held.size(); ++i) {
            CHECK(std::abs(held[i].l - onLaps[i].l) < 1e-9);
            CHECK(std::abs(held[i].r - onLaps[i].r) < 1e-9);
        }
    }

    Sleep(1200);   // past the notice
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 2);   // Laps again

    // A step during a takeover dismisses it: the rider's own page at once (not
    // the page after it), and that notice stays off the card.
    host.runInit(1);   // DEFAULT SETUP again
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 14);
    REQUIRE(host.injectHotkey("dash_next"));
    host.draw();
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 2);   // Laps, and it stays
    CHECK(shows(host, "notices_hud", "DEFAULT SETUP"));

    host.shutdown();
}

TEST_CASE("dash: Ideal shows the last and best lap against the ideal") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Notice focus off, or the DEFAULT SETUP notice of runInit takes it over.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_ideal\\",
                 "[DashHud]\nvisible=1\npage=ideal\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    host.draw();
    REQUIRE(host.dashPage() == 7);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.draw();
    CHECK(shows(host, "dash_hud", "Ideal"));
    CHECK(shows(host, "dash_hud", "-"));

    // ideal_lap_test's ride: sectors 30.0/32.0/28.0 then 29.0/33.0/27.0, so the
    // ideal (29.0 + 32.0 + 27.0) beats both laps.
    host.raceLap(6, 10, 1, 90000, /*best=*/1, 30000, 62000);
    host.raceLap(6, 10, 2, 89000, /*best=*/1, 29000, 62000);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "1:29.000"));   // last and best
    CHECK(shows(host, "dash_hud", "+1.000"));       // each against the ideal
    CHECK(shows(host, "dash_hud", "1:28.000"));   // the ideal

    host.shutdown();
}

TEST_CASE("dash: RPM shows the revs over the RPM widget's segments") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Notice focus off, or the DEFAULT SETUP notice of runInit takes it over.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_rpm\\",
                 "[DashHud]\nvisible=1\npage=rpm\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    host.draw();
    REQUIRE(host.dashPage() == 8);   // RPM

    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.runInit(6);
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
    });
    TelemetryRow row;
    row.speed = 20.0f;
    row.rpm = 8237;
    host.telemetryFrame(row);
    host.draw();
    host.draw();

    // The panel, the RPM widget's lights (15 by default), then the strip's
    // icons; under the lights, the revs.
    const auto quads = host.hudQuadRects("dash_hud");
    CHECK(quads.size() >= 1 + 15 + 5);
    CHECK(shows(host, "dash_hud", "8250"));   // 8237 to the nearest 50
    // The lights all share one height, unlike the card.
    int lights = 0;
    for (size_t i = 1; i < quads.size(); ++i)
        if (std::abs(quads[i].t - quads[1].t) < 1e-9 && std::abs(quads[i].b - quads[1].b) < 1e-9) ++lights;
    CHECK(lights == 15);
    // One row tall (an icon away from the page showing is one row), on the middle
    // of the card's three rows: a whole empty row above them.
    const auto icons = stripIcons(host);
    REQUIRE(!icons.empty());
    const double rowH = icons[0].b - icons[0].t;
    CHECK(std::abs((quads[1].b - quads[1].t) - rowH) < 1e-6);
    CHECK(quads[1].t - quads[0].t >= rowH - 1e-6);

    host.shutdown();
}

TEST_CASE("dash: the Fuel page shows the Fuel widget's rows") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_fuel_page\\",
                 "[DashHud]\nvisible=1\npage=fuel\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    onTrack(host);
    TelemetryRow row;
    row.speed = 20.0f;
    row.fuel = 4.96f;
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
    REQUIRE(host.dashPage() == 4);
    // The widget's default rows (Used is INI-only there), its rounding, and no
    // laps-left figure before a lap is timed.
    CHECK(shows(host, "dash_hud", "Fuel"));
    CHECK(shows(host, "dash_hud", "Per lap"));
    CHECK(shows(host, "dash_hud", "Laps left"));
    CHECK_FALSE(shows(host, "dash_hud", "Used"));
    CHECK(shows(host, "dash_hud", "5.0L"));   // the widget's "%.1f%s"
    CHECK(shows(host, "dash_hud", "-"));

    // Spectating, the tank is not the player's: N/A, as on the widget.
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "N/A"));
    CHECK_FALSE(shows(host, "dash_hud", "5.0L"));

    host.shutdown();
}

TEST_CASE("dash: the Fuel page's tank bar lights the share of the tank left, the top one fading") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_fuel_bar\\",
                 "[DashHud]\nvisible=1\npage=fuel\nnotice_takeover=0\n");
    // An 8 l tank, as the game sends it at EventInit.
    host.eventInit("Southwick", "Thomas", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 8.0f);
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    TelemetryRow row;
    row.speed = 20.0f;
    row.fuel = 4.96f;   // 62% of the tank: six of ten segments and a fifth of one
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
    REQUIRE(host.dashPage() == 4);

    // After the panel, the bar's ten segments, one column, bottom up; lit at
    // full strength, the seventh faded up a quarter of the way (as an RPM light
    // a step into its stretch), the rest dimmed as the RPM widget's unlit lights.
    const auto quads = host.hudQuadRects("dash_hud");
    REQUIRE(quads.size() > 11);
    auto alphaOf = [&](int i) { return static_cast<int>(host.quadColor("dash_hud", i) >> 24); };
    int lit = 0, dim = 0;
    for (int i = 1; i <= 10; ++i) {
        CHECK(std::abs(quads[i].l - quads[1].l) < 1e-9);
        if (i > 1) CHECK(quads[i].b < quads[i - 1].t + 1e-9);
        if (alphaOf(i) == 0xFF) ++lit;
        else if (alphaOf(i) == 0x3F) ++dim;
    }
    CHECK(lit == 6);
    CHECK(dim == 3);
    CHECK(alphaOf(7) == 0x6F);   // 0.25 + 0.75 x 0.25 of 255
    // Three quarters into the seventh: three quarters up.
    row.fuel = 5.3f;
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
    CHECK(alphaOf(6) == 0xFF);
    CHECK(alphaOf(7) == 0xCF);   // 0.25 + 0.75 x 0.75 of 255
    CHECK(alphaOf(8) == 0x3F);
    // The rows sit right of it.
    for (const auto& r : host.hudStringRows("dash_hud"))
        if (r.text == "Fuel") CHECK(r.x > quads[1].r);

    host.shutdown();
}

TEST_CASE("dash: with all four Fuel rows on, the Fuel page drops Used, not Laps left") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_fuel_rows\\",
                 "[FuelWidget]\nrow_used=1\n\n[DashHud]\nvisible=1\npage=fuel\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    onTrack(host);
    TelemetryRow row;
    row.speed = 20.0f;
    row.fuel = 4.96f;
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
    REQUIRE(host.dashPage() == 4);
    // The card holds three rows; laps left is the reading that matters.
    CHECK(shows(host, "dash_hud", "Fuel"));
    CHECK(shows(host, "dash_hud", "Per lap"));
    CHECK(shows(host, "dash_hud", "Laps left"));
    CHECK_FALSE(shows(host, "dash_hud", "Used"));

    host.shutdown();
}

TEST_CASE("dash: past the last page the strip slides on round to the first") {
    constexpr long long T0 = 1000000000ll;   // Motion's clock, in microseconds
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    // Seven pages, on the last of them.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_ring\\",
                 "[DashHud]\nvisible=1\npage=rpm\nnotice_takeover=0\npage_fuel=0\npage_telemetry=0\n"
                 "page_deltatrace=0\npage_gapchart=0\npage_eventlog=0\npage_session=0\n");
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);
    host.draw();
    REQUIRE(host.dashPage() == 8);   // RPM

    const auto bare = host.hudQuadRects("dash_hud");
    const double centre = (bare[0].l + bare[0].r) * 0.5;

    REQUIRE(host.injectHotkey("dash_next"));   // RPM -> Standings, round the ring
    host.setMotionNowUs(T0 + 1000000);
    host.draw();
    REQUIRE(host.dashPage() == 0);   // Standings
    host.setMotionNowUs(T0 + 1000000 + 120000);   // halfway
    host.draw();
    // The panel, the fading RPM lights, then the icons in page order:
    // Standings's first.
    const auto icons = stripIcons(host);
    REQUIRE(!icons.empty());
    const auto& gaps = icons[0];
    const double gapsX = (gaps.l + gaps.r) * 0.5;
    const double iconW = gaps.r - gaps.l;
    // Coming in from one icon right of the middle, not from the far left end
    // of the strip six icons away.
    CHECK(gapsX > centre);
    CHECK(gapsX - centre < iconW * 3.0);

    host.shutdown();
}

TEST_CASE("dash: with Page names on, a step shows the page's name, then its content") {
    constexpr long long T0 = 1000000000ll;   // Motion's clock, in microseconds
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    // The Timing HUD's Gap on: the Timing page's two strings (time, gap).
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_names\\",
                 "[DashHud]\nvisible=1\npage=standings\npage_names=1\n[TimingHud]\ngapRow=3\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    auto alphaOf = [&](const char* text) {
        const auto rows = host.hudStringRows("dash_hud");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == text)
                return static_cast<int>(host.stringColor("dash_hud", static_cast<int>(i)) >> 24);
        return -1;
    };
    auto at = [&](long long us) { host.setMotionNowUs(us); host.draw(); };
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);

    // A notice taking the card over is not a step: straight to the notice.
    onTrack(host);
    at(T0);
    at(T0 + 1000);
    REQUIRE(host.dashPage() == 14);   // Notices
    CHECK(alphaOf("Notices") == -1);
    // Nor is the rider's page coming back after it.
    REQUIRE(host.injectHotkey("dash_next"));
    at(T0 + 2000);
    REQUIRE(host.dashPage() == 0);
    CHECK(alphaOf("Standings") == -1);

    // A step: the name fades in with the page, holds, then gives way.
    const long long t1 = T0 + 1000000;
    host.setMotionNowUs(t1);
    REQUIRE(host.injectHotkey("dash_next"));   // Standings -> Timing
    at(t1);
    REQUIRE(host.dashPage() == 1);
    at(t1 + 300000);   // the page is in, the name held
    CHECK(alphaOf("Timing") == 0xFF);
    // The content under it is not drawn: one string where its two go (the
    // time and the gap).
    const size_t held = host.hudStringRows("dash_hud").size();
    at(t1 + 600000 + 120000);   // halfway into the content
    const int half = alphaOf("Timing");
    CHECK(half > 0x10);
    CHECK(half < 0xF0);
    at(t1 + 600000 + 300000);   // over
    CHECK(alphaOf("Timing") == -1);
    CHECK(host.hudStringRows("dash_hud").size() == held + 1);

    // A step back mid-fade: the page fading out comes back with its content from
    // where it got to, and no name pops up over it.
    const long long t3 = T0 + 2000000;
    host.setMotionNowUs(t3);
    REQUIRE(host.injectHotkey("dash_prev"));   // Timing -> Standings
    at(t3);
    at(t3 + 60000);
    REQUIRE(host.injectHotkey("dash_next"));   // back to Timing, still fading out
    at(t3 + 60000);
    CHECK(alphaOf("Timing") == -1);
    at(t3 + 300000);
    CHECK(alphaOf("Timing") == -1);
    CHECK(host.hudStringRows("dash_hud").size() == held + 1);   // its content, in

    // With Motion off: the name, then a cut to the content, without a redraw
    // from anything else.
    host.setMotion(/*Off*/ 0);
    const long long t2 = T0 + 3000000;
    host.setMotionNowUs(t2);
    REQUIRE(host.injectHotkey("dash_next"));   // Timing -> Laps
    at(t2);
    CHECK(alphaOf("Laps") == 0xFF);
    at(t2 + 590000);
    CHECK(alphaOf("Laps") == 0xFF);
    at(t2 + 610000);
    CHECK(alphaOf("Laps") == -1);

    host.shutdown();
}

TEST_CASE("dash: with Notice focus off, a notice stays off the card") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_focus\\",
                 "[DashHud]\nvisible=1\npage=standings\nnotice_takeover=0\npage_names=0\n");
    REQUIRE(host.setHudVisible("notices_hud", true));   // off by default
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    onTrack(host);   // raises DEFAULT SETUP
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 0);   // still Standings
    CHECK_FALSE(shows(host, "dash_hud", "DEFAULT SETUP"));
    // And no page shows it: Notices is not among them.
    REQUIRE(host.injectHotkey("dash_prev"));
    host.draw();
    host.draw();
    CHECK(host.dashPage() == 13);   // Event log, round the ring
    CHECK_FALSE(shows(host, "dash_hud", "DEFAULT SETUP"));
    CHECK(shows(host, "notices_hud", "DEFAULT SETUP"));

    host.shutdown();
}

TEST_CASE("dash: the Telemetry page draws the Telemetry HUD's traces, and fills the history itself") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasTelemetrySurfaces());
    auto pump = [&](int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.rpm = 6000 + i * 10;
            r.throttle = 0.25f + 0.01f * static_cast<float>(i % 50);
            host.telemetryFrame(r);
        }
    };

    SUBCASE("on the Telemetry page: the history fills and the page traces it") {
        startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_telemetry\\",
                     "[DashHud]\nvisible=1\npage=telemetry\npage_names=0\n");
        host.tmSetVisible(false);   // the Telemetry HUD off: the Dash alone asks
        host.session(1, 0);
        host.draw();
        REQUIRE(host.dashPage() == 12);
        const size_t bare = host.hudQuadRects("dash_hud").size();   // card, strip, icons, grid
        // The Telemetry HUD's grid lines at 100/50/0%, there before any trace.
        int gridLines = 0;
        for (const auto& q : host.hudQuadRects("dash_hud"))
            if (q.b - q.t < 0.005f && q.r - q.l > 0.1f) ++gridLines;
        CHECK(gridLines == 3);
        pump(120);
        CHECK(host.tmHistoryDepth() > 0);
        host.draw();
        // A segment between each two throttle samples, at the least.
        CHECK(host.hudQuadRects("dash_hud").size() >= bare + 100);
    }
    SUBCASE("on another page, Telemetry among them: nothing is recorded") {
        startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_telemetry_off\\",
                     "[DashHud]\nvisible=1\npage=standings\n");
        host.tmSetVisible(false);
        host.tmClearHistory();
        host.session(1, 0);
        host.draw();
        pump(120);
        CHECK(host.tmHistoryDepth() == 0);
    }

    host.shutdown();
}

TEST_CASE("dash: in overtime the Time page shows the laps to go, and a second passing rebuilds nothing") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_overtime\\",
                 "[DashHud]\nvisible=1\npage=time\npage_names=0\n");
    host.bmSetVisible(true);   // the profiler keeps the rebuild count
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Me");
    host.addEntry(22, "Other");
    host.session(6, /*numLaps=*/2, /*lengthMs=*/480000);   // Race1: 8:00 + 2 laps
    host.classify(6, 5000, { { .num = 10, .laps = 3 }, { .num = 22, .laps = 3, .gap = 2000 } });
    host.classify(6, -1000, { { .num = 10, .laps = 3 }, { .num = 22, .laps = 3, .gap = 2000 } });
    host.classify(6, -3000, { { .num = 10, .laps = 4 }, { .num = 22, .laps = 3, .gap = 2000 } });
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 3);
    CHECK(shows(host, "dash_hud", "2 TO GO"));

    const int before = host.hudRebuildCount("dash_hud");
    REQUIRE(before >= 0);
    host.classify(6, -4500, { { .num = 10, .laps = 4 }, { .num = 22, .laps = 3, .gap = 2000 } });
    host.draw();
    host.draw();
    CHECK(host.hudRebuildCount("dash_hud") == before);
    CHECK(shows(host, "dash_hud", "2 TO GO"));
    // The label changing does rebuild it.
    host.classify(6, -6000, { { .num = 10, .laps = 5 }, { .num = 22, .laps = 4, .gap = 2000 } });
    host.draw();
    host.draw();
    CHECK(host.hudRebuildCount("dash_hud") > before);
    CHECK(shows(host, "dash_hud", "FINAL LAP"));

    host.shutdown();
}

TEST_CASE("dash: spectating, the Standings page puts the spectated rider in the middle") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_spectate\\",
                 "[DashHud]\nvisible=1\npage=standings\npage_names=0\n");
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.addEntry(4, "Fourth");
    host.session(6, /*numLaps=*/10);
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
        { .num = 4, .laps = 3, .gap = 6000 },
    });
    host.draw();
    host.spectateVehicles({ { 3, "Third" } }, /*curSelection=*/0);
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 0);
    // Third in the middle: the player above, Fourth below, the leader not shown.
    CHECK(shows(host, "dash_hud", "3"));
    CHECK(shows(host, "dash_hud", "Thi"));
    CHECK(shows(host, "dash_hud", "Me"));
    CHECK(shows(host, "dash_hud", "Fou"));
    CHECK_FALSE(shows(host, "dash_hud", "Lea"));

    host.shutdown();
}

TEST_CASE("dash: until the first step, the card shows the keys that step it") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_hint\\";
    {
        const std::string dir = std::string(saveWin) + "mxbmrp3";
        std::filesystem::create_directories(dir);
        std::remove((dir + "\\mxbmrp3_settings.ini").c_str());
    }
    {
        // A fresh install: the Dash on, Page Up / Down bound, the hint over the page.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(startDash(host, saveWin) >= 0);
        REQUIRE(host.hasStringRows());
        host.draw();
        host.draw();
        CHECK(host.dashPage() == 1);   // Timing, the default page, under the hint
        // ...standing in for the Timing and Notices HUDs, off on a fresh install.
        CHECK(host.hudVisible("timing_hud") == 0);
        CHECK(host.hudVisible("notices_hud") == 0);
        // The RPM widget is on instead, so its page is out of the cycle (below).
        CHECK(host.hudVisible("rpm_widget") == 1);
        CHECK(shows(host, "dash_hud", "Page Up / Page Down"));
        CHECK(shows(host, "dash_hud", "to turn the pages"));
        CHECK(shows(host, "dash_hud", "Rebind in Settings > Hotkeys"));
        // A step drops it and does only that: the page it covered stays.
        REQUIRE(host.injectHotkey("dash_next"));
        host.draw();
        host.draw();
        CHECK(host.dashPage() == 1);
        CHECK_FALSE(shows(host, "dash_hud", "Page Up / Page Down"));
        // Standings and Time are out of a fresh install's cycle: back from Timing
        // goes round to the Event log.
        REQUIRE(host.injectHotkey("dash_prev"));
        host.draw();
        host.draw();
        CHECK(host.dashPage() == 13);
        // RPM is out too: back from the Delta trace skips it to Ideal.
        for (int i = 0; i < 5; ++i) {
            REQUIRE(host.injectHotkey("dash_prev"));
            host.draw();
            host.draw();
        }
        CHECK(host.dashPage() == 7);
        host.shutdown();   // saves
    }
    {
        // For good: not back at the next launch.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(startDash(host, saveWin) >= 0);
        host.draw();
        host.draw();
        CHECK_FALSE(shows(host, "dash_hud", "Page Up / Page Down"));
        host.shutdown();
    }
}

TEST_CASE("dash: under the first-step hint, the running clock does not rebuild the card") {
    // A fresh install opens on Timing under the hint, which covers the page: the
    // clock ticking under it must not rebuild (and reformat the hint) each frame.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_hint_idle\\";
    {
        const std::string dir = std::string(saveWin) + "mxbmrp3";
        std::filesystem::create_directories(dir);
        std::remove((dir + "\\mxbmrp3_settings.ini").c_str());
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, saveWin) >= 0);
    host.bmSetVisible(true);   // the profiler keeps the rebuild count
    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
    REQUIRE(host.elapsedLapTime() >= 0);
    host.draw();
    REQUIRE(host.dashPage() == 1);   // Timing, under the hint
    REQUIRE(shows(host, "dash_hud", "to turn the pages"));
    const int before = host.hudRebuildCount("dash_hud");
    REQUIRE(before >= 0);
    // Long enough for the shown clock to step several tenths.
    for (int i = 0; i < 6; ++i) {
        Sleep(120);
        host.draw();
    }
    CHECK(host.hudRebuildCount("dash_hud") == before);
    host.shutdown();
}

TEST_CASE("dash: Page Down / Page Up give way to keys the player already uses") {
    // An INI from before the Dash has no Dash keys, so the shipped Page Down and
    // Page Up arrive with the upgrade; where the player already has another
    // action on them, the Dash's step goes unbound instead of firing both.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_key_clash\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(startDash(host, saveWin) >= 0);
    {
        const std::string dir = std::string(saveWin) + "mxbmrp3";
        std::filesystem::create_directories(dir);
        std::ofstream ini(dir + "\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        // VK_NEXT (34) on the Map toggle, VK_PRIOR (33) on the Radar toggle.
        ini << "[Settings]\nversion=6\n\n[Hotkeys]\nmap_key=34\nmap_mod=0\nradar_key=33\nradar_mod=0\n";
    }
    host.loadSettings(saveWin);
    REQUIRE(host.setHudVisible("dash_hud", true));
    host.draw();
    host.draw();
    // The first-step hint asks for keys rather than naming the player's.
    CHECK(shows(host, "dash_hud", "Bind Dash + / Dash -"));
    CHECK_FALSE(shows(host, "dash_hud", "Page Up / Page Down"));
    host.shutdown();
}

TEST_CASE("dash: its settings survive a restart, and all-off loads as all-on") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_persist\\";
    // Two pages, on Laps.
    auto check = [](PluginHost& host) {
        REQUIRE(host.setHudVisible("dash_hud", true));
        host.draw();
        host.draw();
        CHECK(host.dashPage() == 2);   // Laps
        // The panel and an icon each for Standings and Laps, down the left.
        const auto quads = host.hudQuadRects("dash_hud");
        REQUIRE(quads.size() == 3);
        for (size_t i = 1; i < 3; ++i) CHECK(quads[i].r < quads[0].l + (quads[0].r - quads[0].l) * 0.25);
    };
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startWithIni(host, saveWin,
                     "[DashHud]\nvisible=1\npage=laps\nstrip=left\n"
                     "notice_takeover=0\npage_timing=0\npage_gapbar=0\npage_flatmap=0\npage_time=0\npage_ideal=0\n"
                     "page_rpm=0\npage_fuel=0\npage_telemetry=0\npage_deltatrace=0\npage_gapchart=0\npage_eventlog=0\n"
                     "page_session=0\n");
        check(host);
        host.shutdown();   // saves from the live state
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(startDash(host, saveWin) >= 0);   // loads what the last one saved
        check(host);

        // Every page off (a hand-edited file): all back on.
        host.writeSettingsFile(saveWin,
            "[Settings]\nversion=6\n\n[DashHud]\nvisible=1\n"
            "notice_takeover=0\npage_standings=0\npage_timing=0\npage_gapbar=0\npage_flatmap=0\npage_time=0\npage_laps=0\n"
            "page_ideal=0\npage_rpm=0\npage_fuel=0\npage_telemetry=0\npage_deltatrace=0\npage_gapchart=0\n"
            "page_eventlog=0\npage_session=0\n");
        host.loadSettings(saveWin);
        host.draw();
        host.draw();
        // The panel and the three icons that fit down the strip at the right.
        CHECK(host.hudQuadRects("dash_hud").size() == 4);
        host.shutdown();
    }
}

TEST_CASE("dash: with UI icons off the strip draws each page's mark") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_icons_off\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, saveWin, "[DashHud]\nvisible=1\n");
    host.draw();
    host.draw();
    CHECK_FALSE(shows(host, "dash_hud", "ST"));   // icons on: quads, no marks
    const size_t withIcons = host.hudQuadRects("dash_hud").size();

    host.writeSettingsFile(saveWin, "[Settings]\nversion=10\n\n[Display]\ntitleIcons=0\n\n[DashHud]\nvisible=1\n");
    host.loadSettings(saveWin);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "ST"));   // the page showing
    CHECK(shows(host, "dash_hud", "TI"));
    CHECK(shows(host, "dash_hud", "EL"));   // round the ring, behind Standings
    CHECK_FALSE(shows(host, "dash_hud", "NO"));   // Notices is not a page
    // The icons' quads gone: the panel alone.
    CHECK(host.hudQuadRects("dash_hud").size() == 1);
    CHECK(withIcons > 1);

    host.shutdown();
}

// An icons folder without the strip's icons (a plugin updated without its data
// folder): the marks again, not a filled square per page -- sprite 0 is the
// solid colour, which is what the strip drew before.
TEST_CASE("dash: a page whose icon is missing draws its mark") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\dash_icon_missing\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);   // the shared tree: no icons
    host.writeSettingsFile(saveWin, "[Settings]\nversion=10\n\n[Messages]\ndashTried=1\n\n"
                                    "[DashHud]\nvisible=1\nstrip=bottom\n");
    host.loadSettings(saveWin);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "TI"));   // a fresh install opens on Timing
    CHECK(shows(host, "dash_hud", "LA"));
    CHECK(host.hudQuadRects("dash_hud").size() == 1);   // the panel alone, no squares

    host.shutdown();
}

namespace {
// Three riders on the Standings page, the player second; `extra` is appended
// to the INI (Standings HUD keys the page follows). No icons: with them a
// tracked rider's shape adds a quad the plate counts would have to allow for.
void standingsTrio(PluginHost& host, const char* dir, const std::string& extra) {
    startWithIni(host, dir, ("[DashHud]\nvisible=1\npage=standings\npage_names=0\n\n" + extra).c_str(), /*icons=*/false);
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Leaderboardking");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10);
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
    });
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 0);
}
}  // namespace

TEST_CASE("dash: the Standings page follows the Standings HUD's short-name length, cut to the card") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_short_names\\", "[StandingsHud]\nshortNameChars=2\n");
        CHECK(shows(host, "dash_hud", "Le"));
        CHECK(shows(host, "dash_hud", "Th"));
        CHECK_FALSE(shows(host, "dash_hud", "Lea"));
        host.shutdown();
    }
    {
        // 31 asked; the card leaves room for six beside a lap time, after the
        // Standings HUD's status column (on by default).
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_short_names_cap\\", "[StandingsHud]\nshortNameChars=31\n");
        CHECK(shows(host, "dash_hud", "Leader"));
        CHECK_FALSE(shows(host, "dash_hud", "Leaderb"));
        host.shutdown();
    }
}

TEST_CASE("dash: the Standings page draws the race-number plates as the Standings HUD") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_plates\\", "");
    const size_t modern = host.hudQuadRects("dash_hud").size();


    // A tracked rider's plate takes the rider's colour, picked up without a
    // standings change (the tracked list's own revision).
    REQUIRE(host.hasInkHooks());
    auto colours = [&] {
        std::vector<unsigned long> out;
        const int n = static_cast<int>(host.hudQuadRects("dash_hud").size());
        for (int i = 0; i < n; ++i) out.push_back(host.quadColor("dash_hud", i));
        return out;
    };
    const auto before = colours();
    REQUIRE(host.trackRider("Third"));
    host.draw();
    host.draw();
    const auto after = colours();
    REQUIRE(after.size() == before.size());
    int changed = 0;
    for (size_t i = 0; i < after.size(); ++i) changed += after[i] != before[i] ? 1 : 0;
    CHECK(changed == 1);   // Third's plate, nothing else

    host.shutdown();

    // The classic layout: no plates, no brand marks (three of each fewer).
    PluginHost classic(dllPath());
    REQUIRE(classic.loaded());
    standingsTrio(classic, "Z:\\tmp\\mxbmrp3-tests\\dash_plates_classic\\", "[StandingsHud]\nclassicLayout=1\n");
    CHECK(classic.hudQuadRects("dash_hud").size() == modern - 6);
    classic.shutdown();

    // The player-row band off: one quad fewer (the band; the name takes the accent).
    PluginHost noBand(dllPath());
    REQUIRE(noBand.loaded());
    standingsTrio(noBand, "Z:\\tmp\\mxbmrp3-tests\\dash_plates_noband\\", "[StandingsHud]\nplayerRowHighlight=0\n");
    CHECK(noBand.hudQuadRects("dash_hud").size() < modern);
    noBand.shutdown();
}

TEST_CASE("dash: leading or last, the Standings page fills its three rows") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_rows_edges\\",
                 "[DashHud]\nvisible=1\npage=standings\npage_names=0\n");
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // first entry after EventInit = the player
    host.addEntry(1, "Alpha");
    host.addEntry(3, "Bravo");
    host.addEntry(4, "Charlie");
    host.session(6, /*numLaps=*/10);
    // Leading: the two behind, both gaps on the behind side.
    host.classify(6, 120000, {
        { .num = 2, .laps = 3, .gap = 0 },
        { .num = 1, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000 },
        { .num = 4, .laps = 3, .gap = 6000 },
    });
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 0);
    CHECK(shows(host, "dash_hud", "Me"));
    CHECK(shows(host, "dash_hud", "Alp"));
    CHECK(shows(host, "dash_hud", "Bra"));
    CHECK_FALSE(shows(host, "dash_hud", "Cha"));
    CHECK(shows(host, "dash_hud", "+4.000"));   // two behind keeps its gap to the player

    // Last: the two ahead.
    host.classify(6, 121000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 3, .laps = 3, .gap = 1500 },
        { .num = 4, .laps = 3, .gap = 4000 },
        { .num = 2, .laps = 3, .gap = 6000 },
    });
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "Me"));
    CHECK(shows(host, "dash_hud", "Bra"));
    CHECK(shows(host, "dash_hud", "Cha"));
    CHECK_FALSE(shows(host, "dash_hud", "Alp"));
    CHECK(shows(host, "dash_hud", "-4.500"));   // two ahead

    host.shutdown();
}

TEST_CASE("dash: the Timing page holds the lap's official time, and INVALID for a cut lap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_timing_hold\\",
                 "[DashHud]\nvisible=1\npage=timing\nnotice_takeover=0\npage_names=0\n");   // no PB notice over it
    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);
    host.classify(6, 200000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();   // S/F -> anchor
    REQUIRE(host.dashPage() == 1);

    // A clean lap: its official time held, as on the Timing HUD.
    host.raceLap(6, 4, /*lapNum=*/1, /*lapTime=*/91000, /*best=*/91000,
                 /*split0=*/30000, /*split1=*/61000, /*invalid=*/false);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "1:31.000"));

    // A cut lap: INVALID.
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
    host.raceLap(6, 4, /*lapNum=*/2, /*lapTime=*/92000, /*best=*/91000,
                 /*split0=*/30500, /*split1=*/61500, /*invalid=*/true);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "INVALID"));

    host.shutdown();
}

TEST_CASE("dash: the Standings page plans its gaps with the Standings HUD's gap settings") {
    {
        // Gap reference Leader: the leader's row labelled, the rest against it.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gap_leader\\", "[StandingsHud]\ngapReferenceMode=LEADER\n");
        CHECK(shows(host, "dash_hud", "Leader"));
        CHECK(shows(host, "dash_hud", "+1.500"));
        CHECK(shows(host, "dash_hud", "+4.000"));
        // A gap to the leader has no side: no ahead/behind tint, the plain ink
        // the rider's name has.
        REQUIRE(host.hasInkHooks());
        auto colourOf = [&](const char* text) {
            const auto rows = host.hudStringRows("dash_hud");
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].text == text) return host.stringColor("dash_hud", static_cast<int>(i));
            return 0ul;
        };
        CHECK(colourOf("+4.000") == colourOf("Thi"));
        host.shutdown();
    }
    {
        // Gap mode Player: only the player's own row carries one.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gap_player\\", "[StandingsHud]\ngapMode=PLAYER\n");
        CHECK_FALSE(shows(host, "dash_hud", "-1.500"));
        CHECK_FALSE(shows(host, "dash_hud", "+2.500"));
        host.shutdown();
    }
    {
        // Gap mode Off: no gaps at all.
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gap_off\\", "[StandingsHud]\ngapMode=OFF\n");
        CHECK_FALSE(shows(host, "dash_hud", "-1.500"));
        CHECK_FALSE(shows(host, "dash_hud", "+2.500"));
        host.shutdown();
    }
}

TEST_CASE("dash: the Fuel and RPM pages take their widgets' colours") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInkHooks());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_page_ink\\",
                 "[FuelWidget]\ncolor_tertiary=0xFF00FF00\n\n"
                 "[RpmWidget]\ncolor_positive=0xFF0000FF\n\n"
                 "[DashHud]\nvisible=1\npage_names=0\nnotice_takeover=0\npage=fuel\npage_gapbar=0\npage_flatmap=0\npage_ideal=0\n");
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 4);   // Fuel
    auto inkOf = [&](const char* text) -> unsigned long {
        const auto rows = host.hudStringRows("dash_hud");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == text) return host.stringColor("dash_hud", static_cast<int>(i));
        return 0;
    };
    CHECK(inkOf("Fuel") == 0xFF00FF00ul);   // the Fuel widget's tertiary, on the row's label

    // The RPM page's first light, unlit, in the RPM widget's positive colour
    // (dimmed as that widget dims an unlit light).
    REQUIRE(host.injectHotkey("dash_next"));   // Fuel -> RPM, past the two pages off
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 8);
    CHECK((host.quadColor("dash_hud", 1) & 0x00FFFFFFul) == 0x000000FFul);
    host.shutdown();
}

TEST_CASE("dash: themed, the page and the strip are each a section card") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasSectionCards());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_theme\\", "[DashHud]\nvisible=1\npage_names=0\n");
    host.installTheme("dash_theme", /*inset=*/1.0f, /*cardBorder=*/1.0f, /*titleBand=*/1, /*card=*/1);
    host.draw();
    host.draw();
    // Two cards, the page's on top, the strip's under it.
    const auto cards = host.sectionCards("dash_hud");
    REQUIRE(cards.size() == 2);
    CHECK(cards[0].bottom <= cards[1].top + 1e-6);
    // The strip's icons sit inside the second card (the quads carry the HUD's
    // offset and the spans do not: measure from the panel's own top quad).
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("dash_hud", &card, &panel));
    const auto quads = host.hudQuadRects("dash_hud");
    REQUIRE(!quads.empty());
    const double offsetY = quads[0].t - panel.t;   // the frame's first quad sits on the panel's top
    const auto icons = stripIcons(host);
    REQUIRE(!icons.empty());
    for (const auto& icon : icons) {
        CHECK(icon.t - offsetY >= cards[1].top - 1e-6);
        CHECK(icon.b - offsetY <= cards[1].bottom + 1e-6);
    }
    host.clearTheme();
    host.shutdown();
}

TEST_CASE("dash: the Gap chart page draws the rider and the riders around them, and follows a spectate switch") {
    // Four laps each for the leader, the player and the rider behind, with a
    // fourth rider further back: three lines of three segments under the
    // leader's edge, over the Grid's line at each lap. Spectating the last rider swaps the lines at once (no lap
    // needed): theirs and the player's, ahead of them.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // One point per lap (Sector points off: 47 = ELEM_DEFAULT before 1.33): the
    // counts below are per-lap dots.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gapchart\\",
                 "[DashHud]\nvisible=1\npage=gapchart\nnotice_takeover=0\npage_names=0\n\n"
                 "[SessionChartsHud]\nenabledElements=47\n");
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");   // the player
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.addEntry(4, "Fourth");
    host.session(6, /*numLaps=*/10);
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 10);
    CHECK(shows(host, "dash_hud", "-"));   // no laps yet
    const size_t bare = host.hudQuadRects("dash_hud").size();

    const int pace[4] = { 60000, 60500, 61000, 63000 };   // riders 1..4
    for (int lap = 1; lap <= 4; ++lap)
        for (int r = 1; r <= 4; ++r) host.raceLap(6, r, lap, pace[r - 1] + lap * 10 * r);
    host.classify(6, 120000, {
        { .num = 1, .laps = 4, .gap = 0 },
        { .num = 2, .laps = 4, .gap = 2000 },
        { .num = 3, .laps = 4, .gap = 4000 },
        { .num = 4, .laps = 4, .gap = 12000 },
    });
    host.draw();
    host.draw();
    CHECK_FALSE(shows(host, "dash_hud", "-"));
    CHECK(host.hudQuadRects("dash_hud").size() == bare + 9 + 4);   // and a lap line at each of the four laps
    // The parent's Line labels (on by default): each line's race number at its
    // end, the three a small row apart although the lines end close together.
    CHECK(shows(host, "dash_hud", "#1"));
    CHECK(shows(host, "dash_hud", "#2"));
    CHECK(shows(host, "dash_hud", "#3"));
    CHECK_FALSE(shows(host, "dash_hud", "#4"));
    {
        REQUIRE(host.hasLayoutCells());
        const double cellH = host.layoutCells().cellH;
        std::vector<double> ys;
        for (const auto& r : host.hudStringRows("dash_hud"))
            if (!r.text.empty() && r.text[0] == '#') ys.push_back(r.y);
        std::sort(ys.begin(), ys.end());
        REQUIRE(ys.size() == 3);
        CHECK(ys[1] - ys[0] >= 1.4 * cellH);   // lineHeightSmall is 1.5 cells
        CHECK(ys[2] - ys[1] >= 1.4 * cellH);
    }
    // A lap logged with the same riders around: one more segment each.
    for (int r = 1; r <= 4; ++r) host.raceLap(6, r, 5, pace[r - 1]);
    host.draw();
    host.draw();
    CHECK(host.hudQuadRects("dash_hud").size() == bare + 12 + 5);

    host.spectateVehicles({ { 4, "Fourth" } }, /*curSelection=*/0);
    host.draw();
    host.draw();
    CHECK(host.hudQuadRects("dash_hud").size() == bare + 8 + 5);
    CHECK(shows(host, "dash_hud", "#3"));
    CHECK(shows(host, "dash_hud", "#4"));
    CHECK_FALSE(shows(host, "dash_hud", "#1"));
    CHECK_FALSE(shows(host, "dash_hud", "#2"));
    host.shutdown();
}

TEST_CASE("dash: the Gap chart page leaves the race numbers out when the Charts HUD's Line labels is off") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // ELEM_DEFAULT without ELEM_LEGEND (1 << 2): Grid, Axis labels, Zero line, Filter outliers.
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gapchart_nolabels\\",
                 "[DashHud]\nvisible=1\npage=gapchart\nnotice_takeover=0\npage_names=0\n\n"
                 "[SessionChartsHud]\nenabledElements=43\n");
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(2, "Me");
    host.addEntry(1, "Leader");
    host.addEntry(3, "Third");
    host.session(6, /*numLaps=*/10);
    for (int lap = 1; lap <= 3; ++lap)
        for (int r = 1; r <= 3; ++r) host.raceLap(6, r, lap, 60000 + r * 500);
    host.classify(6, 120000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 3000 },
    });
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 10);
    CHECK_FALSE(shows(host, "dash_hud", "-"));   // the lines are drawn
    for (const auto& r : host.hudStringRows("dash_hud")) {
        CAPTURE(r.text);
        CHECK((r.text.empty() || r.text[0] != '#'));
    }
    host.shutdown();
}

TEST_CASE("dash: the Delta Trace page draws the Delta Trace HUD's chart") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_delta\\",
                 "[DashHud]\nvisible=1\npage=deltatrace\nnotice_takeover=0\n");
    auto HudPoints = host.sym<int (*)()>("MXBMRP3_Test_DeltaTracePoints");
    auto DashPoints = host.sym<int (*)()>("MXBMRP3_Test_DashDeltaPoints");
    auto SetNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
    REQUIRE_MESSAGE((HudPoints && DashPoints && SetNowUs), "test hooks not exported (test build?)");
    host.setHudVisible("delta_trace_hud", true);
    host.bmSetVisible(true);   // the profiler keeps the rebuild count
    host.draw();
    REQUIRE(host.dashPage() == 9);   // Delta trace

    host.eventInit("TraceTrack", "Alice");
    host.raceEvent("TraceTrack");
    host.session(1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(1);
    host.runStart();
    long long lapStartUs = 1000000000LL;
    auto ride = [&](int from, int to, int msPerStep) {
        for (int p = from; p <= to; ++p) {
            SetNowUs(lapStartUs + static_cast<long long>(p) * msPerStep * 1000LL);
            host.raceTrackPosition({ { 10, p / 200.0f } });
        }
    };
    SetNowUs(lapStartUs - 20000);
    host.raceTrackPosition({ { 10, 0.99f } });
    SetNowUs(lapStartUs);
    host.raceTrackPosition({ { 10, 0.00f } });
    ride(1, 197, 300);                              // the reference lap
    lapStartUs += 60000000LL;
    SetNowUs(lapStartUs);
    host.classify(1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.00f } });
    host.raceLap(1, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);
    ride(1, 100, 310);                              // half of a slower lap
    host.drawWithState(0);
    host.drawWithState(0);

    // The same line on both: the lap ridden so far, point for point.
    INFO("HUD " << HudPoints() << ", Dash " << DashPoints());
    CHECK(DashPoints() >= 95);
    CHECK(DashPoints() == HudPoints());
    // The page carries no axis labels; its peak label is the HUD's.
    CHECK_FALSE(shows(host, "dash_hud", "+1s"));

    // Within a profile point the rider's marker glides, moved in place without
    // a rebuild, as on the Delta Trace HUD.
    const int rebuilds = host.hudRebuildCount("dash_hud");
    REQUIRE(rebuilds >= 0);
    const auto before = host.hudQuadRects("dash_hud");
    SetNowUs(lapStartUs + 100LL * 310 * 1000LL + 500000LL);
    host.raceTrackPosition({ { 10, 100.6f / 200.0f } });
    host.drawWithState(0);
    const auto after = host.hudQuadRects("dash_hud");
    CHECK(host.hudRebuildCount("dash_hud") == rebuilds);
    REQUIRE(after.size() == before.size());
    int moved = 0;
    for (size_t i = 0; i < after.size(); ++i)
        if (std::abs(after[i].l - before[i].l) > 1e-6) ++moved;
    CHECK(moved == 1);
    host.shutdown();
}

TEST_CASE("dash: the Event Log page shows the Event Log's newest events") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_events\\",
                 "[DashHud]\nvisible=1\npage=eventlog\nnotice_takeover=0\n");
    host.eventLogSetVisible(true);
    host.draw();
    REQUIRE(host.dashPage() == 13);   // Event log

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    });
    host.raceLap(1, /*raceNum=*/22, /*lap=*/1, /*lapTime=*/90000, /*best=*/2);
    host.draw();
    host.draw();

    // The fastest lap's time, the detail of the newest event, on both.
    CHECK(shows(host, "event_log_hud", "1:30.000"));
    CHECK(shows(host, "dash_hud", "1:30.000"));
    host.shutdown();
}

TEST_CASE("dash: the Event Log page shows the player's PB rows, under the Event Log's filter") {
    for (const bool on : { true, false }) {
        CAPTURE(on);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        const std::string ini = std::string("[DashHud]\nvisible=1\npage=eventlog\nnotice_takeover=0\n"
                                            "[EventLogHud]\nevent_alltime_pb=") + (on ? "1" : "0") + "\n";
        startWithIni(host, on ? "Z:\\tmp\\mxbmrp3-tests\\dash_events_pb\\"
                              : "Z:\\tmp\\mxbmrp3-tests\\dash_events_pb_off\\", ini.c_str());
        host.eventLogSetVisible(true);
        host.eventInit("TestTrack", "Alice");
        host.raceEvent("TestTrack");
        host.session(1, /*numLaps=*/0, /*lengthMs=*/600000);
        host.addEntry(10, "Alice");   // first entry after EventInit = the player
        host.runInit(1);
        // No stats in a fresh save dir: the first clean lap is an all-time PB.
        host.raceLap(1, /*raceNum=*/10, /*lap=*/1, /*lapTime=*/90000, /*best=*/2);
        host.draw();
        host.draw();
        REQUIRE(host.dashPage() == 13);   // Event log
        CHECK(shows(host, "event_log_hud", "All-time PB:") == on);
        CHECK(shows(host, "dash_hud", "All-time PB:") == on);
        host.shutdown();
    }
}

TEST_CASE("dash: the Session page shows the Session HUD's rows") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_session\\",
                 "[DashHud]\nvisible=1\npage=session\nnotice_takeover=0\n");
    REQUIRE(host.setHudVisible("session_widget", true));
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.draw();
    host.draw();
    REQUIRE(host.dashPage() == 11);   // Session

    // Server (offline: "Testing"), track and format, each as the Session HUD
    // writes it.
    const auto hudRows = host.hudStringRows("session_widget");
    int matched = 0;
    for (const auto& r : hudRows) {
        if (r.text.empty() || r.text == "Session") continue;
        CHECK_MESSAGE(shows(host, "dash_hud", r.text), "missing on the Dash: " << r.text);
        ++matched;
    }
    CHECK(matched == 3);
    CHECK(shows(host, "dash_hud", "TestTrack"));
    host.shutdown();
}

TEST_CASE("dash: Flat map lays the riders along the card, the race leader crowned") {
    // Gold as PluginConstants::PodiumColors::GOLD (#ffd700), packed ABGR.
    constexpr unsigned long GOLD_BGR = 0x0000D7FFul;
    // The gold quads' centres, as a share of the page card's width.
    auto goldAt = [](PluginHost& host) {
        std::vector<double> at;
        PluginHost::QuadRect card, panel;
        REQUIRE(host.hudCardRect("dash_hud", &card, &panel));
        // The card is in the HUD's own space; its quads are on screen, the
        // panel's background first.
        const auto quads = host.hudQuadRects("dash_hud");
        REQUIRE(!quads.empty());
        const double offset = quads[0].l - panel.l;
        for (size_t i = 0; i < quads.size(); ++i) {
            if ((host.quadColor("dash_hud", static_cast<int>(i)) & 0x00FFFFFFul) != GOLD_BGR) continue;
            at.push_back(((quads[i].l + quads[i].r) / 2.0 - offset - card.l) / (card.r - card.l));
        }
        return at;
    };
    auto ride = [](PluginHost& host, int session) {
        host.eventInit("CircleTrack", "Player");
        host.raceEvent("CircleTrack");
        host.addEntry(2, "Player");   // first entry after EventInit = the player
        host.addEntry(1, "Leader");
        host.addEntry(3, "Third");
        host.session(session, 10);
        host.classify(session, 120000, {
            { .num = 1, .laps = 3, .gap = 0 },
            { .num = 2, .laps = 3, .gap = 1500 },
            { .num = 3, .laps = 3, .gap = 4000 },
        });
        host.raceTrackPosition({
            { .num = 1, .trackPos = 0.80f, .posX = 0.0f, .posZ = 0.0f, .yaw = 0.0f },
            { .num = 2, .trackPos = 0.50f, .posX = 0.0f, .posZ = 0.0f, .yaw = 0.0f },
            { .num = 3, .trackPos = 0.20f, .posX = 0.0f, .posZ = 0.0f, .yaw = 0.0f },
        });
        // The page redraws at the Gap Bar's rate (DashHud::FLAT_MAP_INTERVAL_MS).
        host.draw();
        Sleep(30);
        host.draw();
    };
    // The harness stages no icon set, so a synthetic theme lends the crown a
    // sprite before the Dash first looks it up (RiderFlagIcons resolves it once).
    auto start = [](PluginHost& host, const char* dir) {
        REQUIRE(host.loaded());
        startWithIni(host, dir, "[DashHud]\nvisible=1\nnotice_takeover=0\npage=flatmap\n");
        REQUIRE(host.hasIconTheming());
        host.installTheme("crown", 0.0f, 0.0f, /*titleBand=*/0, /*card=*/0);
        REQUIRE(host.setThemeIcon("crown", 900003, 0));
        REQUIRE(host.dashPage() == 6);   // Flat map
    };
    SUBCASE("race: the leader's marker is the crown, where the leader is") {
        PluginHost host(dllPath());
        start(host, "Z:\\tmp\\mxbmrp3-tests\\dash_flatmap\\");
        ride(host, 6);   // Race1
        const std::vector<double> gold = goldAt(host);
        REQUIRE(gold.size() == 1);
        CHECK(gold[0] == doctest::Approx(0.8).epsilon(0.06));
        host.shutdown();
    }
    SUBCASE("practice: no crown") {
        PluginHost host(dllPath());
        start(host, "Z:\\tmp\\mxbmrp3-tests\\dash_flatmap_practice\\");
        ride(host, 2);
        CHECK(goldAt(host).empty());
        host.shutdown();
    }
}

TEST_CASE("dash: the page strip sits on any side of the page, or nowhere") {
    struct Place { const char* key; int side; };   // 0 top, 1 right, 2 bottom, 3 left, -1 off
    const Place places[] = { { "top", 0 }, { "right", 1 }, { "bottom", 2 }, { "left", 3 }, { "off", -1 } };
    for (const Place& place : places) {
        CAPTURE(place.key);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        const std::string body = std::string("[DashHud]\nvisible=1\nnotice_takeover=0\nstrip=") + place.key + "\n";
        startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_strip\\", body.c_str());
        host.draw();
        host.draw();
        const auto quads = host.hudQuadRects("dash_hud");
        REQUIRE(!quads.empty());
        const PluginHost::QuadRect panel = quads[0];   // the background first
        const double midX = (panel.l + panel.r) / 2.0, midY = (panel.t + panel.b) / 2.0;
        const auto icons = stripIcons(host);
        if (place.side < 0) {
            CHECK(icons.empty());
            host.shutdown();
            continue;
        }
        const bool column = place.side == 1 || place.side == 3;
        // Down a side, three: the page showing's and one either side.
        if (column) REQUIRE(icons.size() == 3);
        else REQUIRE(icons.size() >= 2);
        for (const auto& icon : icons) {
            const double x = (icon.l + icon.r) / 2.0, y = (icon.t + icon.b) / 2.0;
            if (place.side == 0) CHECK(y < midY);
            if (place.side == 2) CHECK(y > midY);
            if (place.side == 1) CHECK(x > panel.r - (panel.r - panel.l) * 0.2);
            if (place.side == 3) CHECK(x < panel.l + (panel.r - panel.l) * 0.2);
            // Inside the panel either way.
            CHECK(x > panel.l);
            CHECK(x < panel.r);
            CHECK(y > panel.t);
            CHECK(y < panel.b);
        }
        // Above or below the page, the icons run across the strip; beside it, down.
        // (Within the harness's micro-unit rounding of each edge.)
        if (!column) CHECK(std::abs(icons.back().t + icons.back().b - icons.front().t - icons.front().b) < 3e-6);
        else CHECK(std::abs(icons.back().l + icons.back().r - icons.front().l - icons.front().r) < 3e-6);
        // The page as wide as ever: the strip beside it widens the panel.
        if (column) CHECK(panel.r - panel.l > 0.16);
        host.shutdown();
    }
}

TEST_CASE("dash: down a side the strip's icons stay inside its card through a step") {
    constexpr long long T0 = 1000000000ll;   // Motion's clock, in microseconds
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_strip_slide\\",
                 "[DashHud]\nvisible=1\nnotice_takeover=0\nstrip=left\n");
    host.setMotionNowUs(T0);
    host.setMotion(/*Normal: 240 ms*/ 2);
    host.draw();
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("dash_hud", &card, &panel));
    REQUIRE(host.injectHotkey("dash_next"));
    for (long long ms = 0; ms <= 240; ms += 20) {
        CAPTURE(ms);
        host.setMotionNowUs(T0 + 1000000 + ms * 1000);
        host.draw();
        const auto quads = host.hudQuadRects("dash_hud");
        REQUIRE(!quads.empty());
        // The card is in the HUD's own space, its quads on screen.
        const double dy = quads[0].t - panel.t;
        for (const auto& icon : stripIcons(host)) {
            CHECK(icon.t >= card.t + dy - 1e-6);
            CHECK(icon.b <= card.b + dy + 1e-6);
        }
    }
    host.shutdown();
}

TEST_CASE("dash: down a side the strip's card is as tall as the page's") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasSectionCards());
    const char* place = "right";
    float inset = 1.0f;
    SUBCASE("right") { place = "right"; }
    SUBCASE("left") { place = "left"; }
    // Thin insets put the panel's sum a float hair over a whole cell at some
    // scales, which once left the cards a cell apart until the next rebuild.
    SUBCASE("right, inset 0") { inset = 0.0f; }
    SUBCASE("right, inset 0.5") { inset = 0.5f; }
    CAPTURE(place);
    CAPTURE(inset);
    char ini[128];
    snprintf(ini, sizeof(ini), "[DashHud]\nvisible=1\npage_names=0\nstrip=%s\n", place);
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_strip_cards\\", ini);
    host.installTheme("dash_theme", inset, /*cardBorder=*/1.0f, /*titleBand=*/1, /*card=*/1);
    host.draw();
    // Side by side: the same top and the same bottom, so neither card ends
    // short, on the very first draw after each size change.
    for (const float scale : { 1.0f, 0.5f, 0.9f, 1.25f, 2.0f, 1.0f }) {
        CAPTURE(scale);
        REQUIRE(host.setHudScale("dash_hud", scale));
        host.draw();
        const auto cards = host.sectionCards("dash_hud");
        REQUIRE(cards.size() == 2);
        CHECK(std::abs(cards[0].top - cards[1].top) < 1e-6);
        CHECK(std::abs(cards[0].bottom - cards[1].bottom) < 1e-5);   // a hundredth of a pixel
    }
    // Three icons down the side.
    CHECK(stripIcons(host).size() == 3);
    host.clearTheme();
    host.shutdown();
}

TEST_CASE("dash: with the title on, it names the page") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_title\\",
                 "[DashHud]\nvisible=1\nshowTitle=1\npage_names=0\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "Dash: Standings"));
    REQUIRE(host.injectHotkey("dash_next"));
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "Dash: Timing"));
    CHECK(!shows(host, "dash_hud", "Dash: Standings"));
    host.shutdown();
}

TEST_CASE("dash: a PB notice shows the lap and its gain under it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_pb_detail\\", "[DashHud]\nvisible=1\npage_names=0\n");
    REQUIRE(host.setHudVisible("notices_hud", true));   // off by default
    REQUIRE(host.hasStringRows());
    host.eventInit("TestTrack", "Thomas", 1600.0f, 2, "Bike A", "MX1", /*trackId=*/"TestTrack");
    host.raceEvent("TestTrack");
    host.session(1, /*numLaps=*/0, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.runInit(1);
    host.draw();

    // A first best: the lap alone, nothing to have beaten.
    host.raceLap(1, /*raceNum=*/4, /*lap=*/1, 90000, /*best=*/1);
    host.draw();
    host.draw();
    REQUIRE(shows(host, "dash_hud", "ALL-TIME PB"));
    CHECK(shows(host, "dash_hud", "1:30.000"));

    // Beaten while the notice is up: the same caption, the new lap and its gain.
    host.raceLap(1, 4, 2, 89000, 1);
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "ALL-TIME PB"));
    CHECK(shows(host, "dash_hud", "1:29.000  -1.000"));
    // The Notices HUD's slab: the caption alone.
    CHECK(shows(host, "notices_hud", "ALL-TIME PB"));
    CHECK_FALSE(shows(host, "notices_hud", "1:29.000  -1.000"));
    host.shutdown();
}

TEST_CASE("dash: the Standings page draws the Standings HUD's status icons while that HUD does") {
    // The harness stages no icon set, so a synthetic theme lends the wrench a
    // sprite. Third into the pits: the wrench before the position, one quad more.
    // (Before the first draw: the Standings HUD looks its icons up once.)
    auto pitQuads = [](const char* dir, const std::string& extra) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startWithIni(host, dir, ("[DashHud]\nvisible=1\npage=standings\npage_names=0\n\n" + extra).c_str());
        REQUIRE(host.hasIconTheming());
        host.installTheme("status", 0.0f, 0.0f, /*titleBand=*/0, /*card=*/0);
        REQUIRE(host.setThemeIcon("wrench", 900004, 0));
        host.eventInit("TestTrack", "Me");
        host.raceEvent("TestTrack");
        host.addEntry(2, "Me");
        host.addEntry(1, "Leaderboardking");
        host.addEntry(3, "Third");
        host.session(6, /*numLaps=*/10);
        auto classify = [&](int thirdPit) {
            host.classify(6, 120000, {
                { .num = 1, .laps = 3, .gap = 0 },
                { .num = 2, .laps = 3, .gap = 1500 },
                { .num = 3, .laps = 3, .gap = 4000, .pit = thirdPit },
            });
            host.draw();
            host.draw();
            return host.hudQuadRects("dash_hud").size();
        };
        const size_t before = classify(0);
        const size_t after = classify(1);
        host.clearTheme();
        host.shutdown();
        return static_cast<int>(after) - static_cast<int>(before);
    };
    CHECK(pitQuads("Z:\\tmp\\mxbmrp3-tests\\dash_status\\", "") == 1);
    // With the Standings HUD's status column off, none.
    CHECK(pitQuads("Z:\\tmp\\mxbmrp3-tests\\dash_status_off\\", "[StandingsHud]\ncol_tracked=0\n") == 0);
}

TEST_CASE("dash: the Standings page tints the gaps either side and shows penalties") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasInkHooks());
    standingsTrio(host, "Z:\\tmp\\mxbmrp3-tests\\dash_gap_tint\\", "[StandingsHud]\ncol_penalty=1\n");
    auto colourOf = [&](const char* text) {
        const auto rows = host.hudStringRows("dash_hud");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].text == text) return host.stringColor("dash_hud", static_cast<int>(i));
        return 0ul;
    };
    // Ahead and behind in two colours, neither the self row's best-lap ink.
    const unsigned long ahead = colourOf("-1.500");
    const unsigned long behind = colourOf("+2.500");
    REQUIRE(ahead != 0);
    REQUIRE(behind != 0);
    CHECK(ahead != behind);
    CHECK(ahead != colourOf("Me"));
    CHECK(behind != colourOf("Me"));

    // Third takes a five-second penalty: "+5s" before the gap, the name cut.
    CHECK_FALSE(shows(host, "dash_hud", "+5s"));
    host.classify(6, 121000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000, .penalty = 5000 },
    });
    host.draw();
    host.draw();
    CHECK(shows(host, "dash_hud", "+5s"));
    CHECK(shows(host, "dash_hud", "Th"));
    CHECK_FALSE(shows(host, "dash_hud", "Thi"));
    host.shutdown();

    // With the penalty column off (its default), no penalty.
    PluginHost off(dllPath());
    REQUIRE(off.loaded());
    standingsTrio(off, "Z:\\tmp\\mxbmrp3-tests\\dash_gap_tint_off\\", "");
    off.classify(6, 121000, {
        { .num = 1, .laps = 3, .gap = 0 },
        { .num = 2, .laps = 3, .gap = 1500 },
        { .num = 3, .laps = 3, .gap = 4000, .penalty = 5000 },
    });
    off.draw();
    off.draw();
    CHECK_FALSE(off.hudStringRows("dash_hud").empty());
    CHECK_FALSE(shows(off, "dash_hud", "+5s"));
    off.shutdown();
}

TEST_CASE("dash: with custom segments set, the Timing page times them as the Timing HUD") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startWithIni(host, "Z:\\tmp\\mxbmrp3-tests\\dash_segments\\",
                 "[DashHud]\nvisible=1\nshowTitle=1\npage=timing\npage_names=0\nnotice_takeover=0\n");
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasMessages());
    onTrack(host);
    // On track (view state 0): segments are the player's own training tool.
    auto at = [&](float pos) {
        host.raceTrackPosition({ { .num = 4, .trackPos = pos } });
        host.telemetry(20.0f, 3, 0.0f, pos);
        host.drawWithState(0);
    };
    auto count = [&](auto pred) {
        const auto rows = host.hudStringRows("dash_hud");
        return std::count_if(rows.begin(), rows.end(), pred);
    };
    auto isPlaceholder = [](const PluginHost::StringRow& r) { return r.text == "-"; };
    // A delta: a sign and seconds, never a lap time's colon.
    auto isDelta = [](const PluginHost::StringRow& r) {
        return !r.text.empty() && (r.text[0] == '+' || r.text[0] == '-') && r.text.size() > 1 &&
               r.text.find(':') == std::string::npos;
    };
    at(0.05f);
    CHECK_FALSE(shows(host, "dash_hud", "Best"));

    // Points at 0.10 and 0.50: one segment between them.
    at(0.10f);
    REQUIRE(host.injectHotkey("segment_add"));
    host.drawWithState(0);
    at(0.50f);
    REQUIRE(host.injectHotkey("segment_add"));
    host.drawWithState(0);
    host.drawWithState(0);
    CHECK(shows(host, "dash_hud", "Best"));   // the title row's reference

    // Round to the start and into the segment: its time runs, no target yet.
    at(0.95f);
    at(0.05f);
    at(0.15f);
    CHECK(count(isPlaceholder) == 1);
    CHECK(count(isDelta) == 0);

    // Through its end: the time held, and the new best as the target. The
    // segment is timed off the wall clock, so it takes a moment: a zero best
    // is no target, as on the Timing HUD.
    Sleep(30);
    at(0.55f);
    host.drawWithState(0);
    CHECK(count(isPlaceholder) == 0);
    CHECK(count(isDelta) == 0);

    // A second run: held with its delta to that best.
    at(0.95f);
    at(0.05f);
    at(0.15f);
    Sleep(30);
    at(0.55f);
    host.drawWithState(0);
    CHECK(count(isPlaceholder) == 0);
    CHECK(count(isDelta) == 1);

    host.shutdown();
}
