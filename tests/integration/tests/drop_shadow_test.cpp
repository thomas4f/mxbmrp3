// ============================================================================
// tests/integration/tests/drop_shadow_test.cpp
// The settings panel never drop-shadows, and every other panel still does.
//
// WHY THIS FILE EXISTS. A drop shadow is a SECOND STRING emitted underneath the
// first (collectSurface), so switching it on doubles the plugin's string count --
// and a string is the most expensive primitive we hand the engine, ~2.7x a quad. In
// a measured session that was ~830 us per frame.
//
// The settings panel always draws its own opaque background, so a shadow behind its
// text is a second string rendered under a surface that hides it: invisible work, on
// the heaviest string emitter in the plugin (84 strings against the next panel's 44).
// It is now suppressed by policy.
//
// THE FAILURE THIS GUARDS is not "the shadow reappears" -- it is that the policy
// gets implemented as a stored SETTING rather than as behaviour. BaseHud already has
// a per-HUD dropShadow override, and it is captured into the INI and sparse-saved,
// so the authoritative apply CLEARS it whenever the file does not mention it. A
// panel that set the override on itself would look right until the first settings
// round-trip and then quietly start shadowing again. Hence alwaysSkipDropShadow() is
// a virtual, and hence the round-trip case below.
//
// Observed through the handed-over string count, which is exactly what the engine is
// billed for -- there is no need to inspect the strings themselves.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <utility>

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum

void openSession(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.classify(RACE, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    host.draw();
}

// Strings handed to the game with the shadow setting at `on`.
int stringsWithShadow(PluginHost& host, bool on) {
    host.setDropShadow(on);
    host.draw();
    host.draw();          // the setting dirties every HUD; let the rebuild land
    return host.lastGameStrings();
}

// Quads handed to the game with the shadow setting at `on`.
int quadsWithShadow(PluginHost& host, bool on) {
    host.setDropShadow(on);
    host.draw();
    host.draw();
    return host.lastGameQuads();
}

}  // namespace

TEST_CASE("drop shadow: on doubles the strings an ordinary panel emits") {
    // The control. If this stopped being true the suppression case below would pass
    // vacuously -- it would be asserting that a knob which does nothing does nothing.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\drop_shadow_on\\");
    host.showAllHuds(true);
    host.showSettings(false);
    openSession(host);

    const int off = stringsWithShadow(host, false);
    const int on = stringsWithShadow(host, true);
    REQUIRE(off > 0);
    CHECK_MESSAGE(on > off,
                  "drop shadow emits a second string per string, so the handed-over "
                  "count must rise -- it is the most expensive primitive we submit");

    host.shutdown();
}

TEST_CASE("drop shadow: the settings panel contributes no shadow strings") {
    // Asserted as a DIFFERENCE OF DIFFERENCES, not as a flat count, because the frame
    // is never only the settings panel: with every HUD hidden, something else still
    // draws a string or two and those legitimately shadow. Measuring the shadow-driven
    // increase with the panel CLOSED and again with it OPEN isolates its contribution
    // exactly -- and needs no tolerance, which is the thing that would rot.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\drop_shadow_settings\\");
    host.showAllHuds(false);
    openSession(host);

    host.showSettings(false);
    const int closedOff = stringsWithShadow(host, false);
    const int closedOn = stringsWithShadow(host, true);

    host.showSettings(true);
    const int openOff = stringsWithShadow(host, false);
    const int openOn = stringsWithShadow(host, true);

    REQUIRE_MESSAGE(openOff > closedOff,
                    "opening the settings panel added no strings - nothing is being tested");
    CHECK_MESSAGE(openOn - openOff == closedOn - closedOff,
                  "the settings panel added " << (openOn - openOff) - (closedOn - closedOff)
                  << " shadow strings. Every one renders underneath an opaque panel "
                  << "that hides it, on the heaviest string emitter in the plugin");

    host.shutdown();
}

TEST_CASE("drop shadow: the settings panel's suppression survives a settings round-trip") {
    // THE CASE THAT MATTERS, and the reason this is a virtual rather than a call to
    // setDropShadowOverride() in the constructor. That override is a stored setting:
    // captured into the INI, sparse-saved, and cleared by the authoritative apply
    // whenever the loaded file does not mention it. A panel that set it on itself
    // would shadow again after the first save/load -- correct on a fresh install,
    // wrong forever after, and invisible in any test that never round-trips.
    const char* kPath = "Z:\\tmp\\mxbmrp3-tests\\drop_shadow_roundtrip\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kPath);
    host.showAllHuds(false);
    host.showSettings(true);
    openSession(host);

    const int before = stringsWithShadow(host, true);

    host.save();
    host.loadSettings(kPath);
    host.showAllHuds(false);
    host.showSettings(true);
    host.draw();
    const int after = stringsWithShadow(host, true);

    CHECK_MESSAGE(after == before,
                  "a settings round-trip changed the settings panel's string count ("
                  << before << " -> " << after << ") -- if the suppression is stored as "
                  << "a preference instead of being behaviour, the authoritative apply "
                  << "clears it and the shadows come back");

    host.shutdown();
}

// ============================================================================
// A MARKER LABEL IS ONE STRING, AND THE SHADOW BEHIND IT IS THE GLOBAL ONE.
//
// MapHud, RadarHud and GapBarHud all draw the same rider label, and all three
// once drew a hand-rolled outline: the string four times in black at +-5% of the
// font, then the label on top, every one of them with skipShadow=true. Map moved
// to the standard shadow; the gap bar did not, and its comment went on saying
// "like MapHud" while the two rendered differently -- four extra strings per
// label, a shadow the [Display] dropShadowOffsetX/Y could not move, and an
// outline that stayed on with drop shadows switched OFF.
//
// Counted on the HUD's OWN strings rather than the handed-over total, because
// the shadow is added downstream by collectRenderData: the fault is that the HUD
// emitted five where one was wanted, and only its own list can see that.
//
// RadarHud was the last holdout, and the fade was why: its labels dim as riders
// leave proximity range, and the shared shadow used to write the configured
// colour verbatim -- a solid shadow behind half-visible text. It modulates by the
// string's own alpha now (PluginUtils::modulateAlpha, pinned in the unit suite),
// so the radar draws one string too.
// ============================================================================
TEST_CASE("drop shadow: a marker label is one string, on the gap bar and the radar") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\gapbar_label_shadow\\";
    host.startup(save);
    REQUIRE_MESSAGE(host.hasStringRows(), "MXBMRP3_Test_HudStringRows not exported");

    // markerMode 2 = GHOST_OPPONENTS (opponents drawn), labelMode 2 = RACE_NUM --
    // a label that needs no classification order to have text.
    host.writeSettingsFile(save,
        "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nlabelMode=2\nmarkerMode=2\n"
        // RACE_NUM, spelled the way each HUD stores it: the radar serialises its
        // label mode as a NAME and the gap bar as an int. (Same enum, two encodings --
        // the one place these three still disagree on disk.)
        "\n[RadarHud]\nvisible=1\nlabelMode=RACE_NUM\n");
    host.loadSettings(save);

    openSession(host);
    host.addEntry(22, "Bob");
    // The OPPONENT, drawn by BOTH HUDs, which is what makes 22 the label under test:
    // the player (10) is skipped by the radar entirely (it IS the radar's centre).
    // The radar takes a rider only when it is close in WORLD metres AND close along
    // the track -- the second gate is what stops riders on a parallel straight
    // showing, and with no track length in this scenario it falls back to 5% of a lap.
    // So 22 sits a few metres away and a couple of percent up the road; put it across
    // the track instead and the radar silently draws nothing.
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.200f, .posX = 0.0f, .posZ = 0.0f },
                             { .num = 22, .trackPos = 0.205f, .posX = 4.0f, .posZ = 3.0f } });
    host.draw();

    auto labelCount = [&](PluginHost::HudId which) {
        int n = 0;
        for (const auto& r : host.hudStringRows(which)) if (r.text == "22") ++n;
        return n;
    };
    INFO("gap bar emitted " << labelCount(PluginHost::HUD_GAPBAR)
         << " string(s), radar " << labelCount(PluginHost::HUD_RADAR)
         << ", for one marker label");
    CHECK(labelCount(PluginHost::HUD_GAPBAR) == 1);   // was 5: four outline copies + label
    CHECK(labelCount(PluginHost::HUD_RADAR) == 1);    // ...same

    host.shutdown();
}

// ============================================================================
// ONE KEY, THREE HUDS: `labelAnchor` moves the marker label on the map, the
// radar and the gap bar alike.
//
// MapHud has shipped this as an INI-only setting for a while and the other two
// were hardcoded BELOW. They all place through MarkerLabel::place() now, so the
// anchor is a parameter rather than three switch statements -- and giving the
// other two the same key costs one serde line each.
//
// Asserted through the DRAWN label rather than the getter, because a getter that
// stores the value while the HUD keeps drawing where it always did is exactly
// the failure this is worth testing for. BELOW puts the label under the icon and
// ABOVE puts it over: the label's y crosses the marker.
// ============================================================================
TEST_CASE("label anchor: the same key moves the label on the radar and the gap bar") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\label_anchor\\";
    host.startup(save);
    REQUIRE(host.hasStringRows());

    auto labelY = [&](PluginHost::HudId which) {
        for (const auto& r : host.hudStringRows(which)) if (r.text == "22") return r.y;
        return -1.0;
    };

    auto drawWith = [&](const char* anchor) {
        std::string ini =
            "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nlabelMode=2\nmarkerMode=2\n"
            "labelAnchor=" + std::string(anchor) + "\n"
            "\n[RadarHud]\nvisible=1\nlabelMode=RACE_NUM\nlabelAnchor=" + anchor + "\n";
        host.writeSettingsFile(save, ini);
        host.loadSettings(save);
        host.raceTrackPosition({ { .num = 10, .trackPos = 0.200f, .posX = 0.0f, .posZ = 0.0f },
                                 { .num = 22, .trackPos = 0.205f, .posX = 4.0f, .posZ = 3.0f } });
        host.draw();
    };

    openSession(host);
    host.addEntry(22, "Bob");

    drawWith("BELOW");
    const double gapBelow = labelY(PluginHost::HUD_GAPBAR);
    const double radBelow = labelY(PluginHost::HUD_RADAR);
    REQUIRE_MESSAGE(gapBelow >= 0, "gap bar drew no marker label");
    REQUIRE_MESSAGE(radBelow >= 0, "radar drew no marker label");

    drawWith("ABOVE");
    const double gapAbove = labelY(PluginHost::HUD_GAPBAR);
    const double radAbove = labelY(PluginHost::HUD_RADAR);
    REQUIRE(gapAbove >= 0);
    REQUIRE(radAbove >= 0);

    INFO("gap bar label y BELOW " << gapBelow << " -> ABOVE " << gapAbove
         << "; radar " << radBelow << " -> " << radAbove);
    CHECK(gapAbove < gapBelow);   // y grows downward: ABOVE is higher on screen
    CHECK(radAbove < radBelow);

    host.shutdown();
}

// ============================================================================
// THE DASH'S PAGE ICONS TAKE THE SHADOW, ONE QUAD EACH, LIKE A TITLE ICON.
//
// Identity icons are flat art drawn to sit on a drop shadow (assets/icons/README.md),
// and collectSurface shadowed only the one beside a HUD's title - so the same icons
// down the Dash's strip were the only ones drawn without it. The Dash now marks
// each strip icon (BaseHud::shadowLastIcon) for the same path. Counted as handed-over
// quads with only the Dash up (its title off, so no title icon in the sum): the
// shadow must add exactly one quad per strip icon.
// ============================================================================
TEST_CASE("drop shadow: the Dash's page icons take one, like a title icon") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\dash_icon_shadow\\";
    // The strip's icons, which the harness does not ship (a missing one draws
    // its page's letters): from a discovery tree of this case's own, for the
    // length of Startup -- dash_test's startDash says why not the save dir.
    {
        const std::filesystem::path tree = "Z:\\tmp\\mxbmrp3-tests\\dash_icon_shadow_tree";
        const std::filesystem::path icons = tree / "plugins" / "mxbmrp3_data" / "icons";
        std::filesystem::create_directories(icons);
        for (const char* icon : { "hud-standings", "hud-timing", "hud-laplog", "hud-clock", "hud-fuel",
                                  "hud-gapbar", "hud-flatmap", "hud-ideallap", "hud-gauge", "hud-deltatrace",
                                  "hud-sessioncharts", "hud-session", "hud-telemetry", "hud-eventlog" }) {
            const std::string src = std::string(MXB_REPO_DATA_DIR "/icons/") + icon + ".tga";
            REQUIRE(CopyFileA(src.c_str(), (icons / (std::string(icon) + ".tga")).string().c_str(), FALSE) != 0);
        }
        const std::filesystem::path prev = std::filesystem::current_path();
        std::filesystem::current_path(tree);
        host.startup(save);
        std::filesystem::current_path(prev);   // integration_main's sentinel is CWD-relative
    }
    host.writeSettingsFile(save,
        "[Settings]\nversion=6\n\n[Messages]\ndashTried=1\n\n[DashHud]\nvisible=1\nstrip=bottom\n");
    host.loadSettings(save);
    host.showAllHuds(false);
    REQUIRE(host.setHudVisible("dash_hud", true));
    host.showSettings(false);
    openSession(host);

    const int off = quadsWithShadow(host, false);
    const int on = quadsWithShadow(host, true);
    const int icons = static_cast<int>(host.hudQuadRects("dash_hud").size()) - host.dashFirstIcon();
    REQUIRE_MESSAGE(icons > 0, "the Dash drew no strip icons - nothing is being tested");
    CHECK_MESSAGE(on - off == icons,
                  "drop shadow added " << (on - off) << " quads for " << icons
                  << " strip icons - each identity icon takes one shadow quad");

    host.shutdown();
}

// ============================================================================
// AN ICON BESIDE SHADOWED TEXT TAKES THE TEXT'S SHADOW, ONE QUAD EACH.
//
// Before 1.33 only the title icon and the Dash strip did: the Session HUD's row
// icons (and the Event Log's, the Standings carets, Stream Chat's badges...) sat
// flat beside text that cast a shadow. Each now marks its icons
// (BaseHud::shadowLastIcon) for core/hud_manager_shadow.cpp. The Session HUD
// stands for them: with only it up, the shadow's extra quads with its row icons
// on, less those with them off (the title icon, if any, cancels out), must be
// exactly one per row icon.
// ============================================================================
TEST_CASE("drop shadow: an icon beside shadowed text takes one, like a title icon") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\body_icon_shadow\\";
    {
        const std::filesystem::path tree = "Z:\\tmp\\mxbmrp3-tests\\body_icon_shadow_tree";
        const std::filesystem::path icons = tree / "plugins" / "mxbmrp3_data" / "icons";
        std::filesystem::create_directories(icons);
        for (const char* icon : { "location-dot", "clock", "temperature-low" }) {
            const std::string src = std::string(MXB_REPO_DATA_DIR "/icons/") + icon + ".tga";
            REQUIRE(CopyFileA(src.c_str(), (icons / (std::string(icon) + ".tga")).string().c_str(), FALSE) != 0);
        }
        const std::filesystem::path prev = std::filesystem::current_path();
        std::filesystem::current_path(tree);
        host.startup(save);
        std::filesystem::current_path(prev);   // integration_main's sentinel is CWD-relative
    }
    // Shadow quads with the Session HUD's row icons on or off.
    auto shadowQuads = [&](bool rowIcons) {
        host.writeSettingsFile(save, std::string("[Settings]\nversion=11\n\n[SessionHud]\nvisible=1\nshowIcons=") +
                                     (rowIcons ? "1" : "0") + "\n");
        host.loadSettings(save);
        host.showAllHuds(false);
        REQUIRE(host.setHudVisible("session_widget", true));
        host.showSettings(false);
        openSession(host);
        const int off = quadsWithShadow(host, false);
        const int quads = static_cast<int>(host.hudQuadRects("session_widget").size());
        const int on = quadsWithShadow(host, true);
        return std::make_pair(on - off, quads);
    };
    const auto withIcons = shadowQuads(true);
    const auto without = shadowQuads(false);
    const int icons = withIcons.second - without.second;
    REQUIRE_MESSAGE(icons > 0, "the Session HUD drew no row icons - nothing is being tested");
    CHECK_MESSAGE(withIcons.first - without.first == icons,
                  "drop shadow added " << (withIcons.first - without.first) << " quads for " << icons
                  << " row icons - each icon beside shadowed text takes one shadow quad");

    host.shutdown();
}

// ============================================================================
// THE TOGGLES STRIP'S ICONS TAKE THE SHADOW, ONE QUAD EACH.
//
// The strip floats bare over the scene, so its icons were the one set left flat
// beside a shadowed hover name and shadowed letters (UI icons off). The settings
// sidebar's icons sit on the menu's panel and stay flat. With every other HUD
// hidden (showAllHuds leaves the strip and the pointer alone), the shadow's
// extra quads with UI icons on, less those with them off (the strip then draws
// letters, no quads), must be exactly one per strip icon.
// ============================================================================
TEST_CASE("drop shadow: the Toggles strip's icons take one, like a title icon") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\toggle_icon_shadow\\";
    // Every shipped icon, which the harness does not ship (a missing one draws
    // its switch's letters), for the length of Startup.
    {
        const std::filesystem::path tree = "Z:\\tmp\\mxbmrp3-tests\\toggle_icon_shadow_tree";
        const std::filesystem::path icons = tree / "plugins" / "mxbmrp3_data" / "icons";
        std::filesystem::create_directories(icons);
        for (const auto& e : std::filesystem::directory_iterator(MXB_REPO_DATA_DIR "/icons")) {
            if (e.path().extension() != ".tga") continue;
            REQUIRE(CopyFileA(e.path().string().c_str(), (icons / e.path().filename()).string().c_str(),
                              FALSE) != 0);
        }
        const std::filesystem::path prev = std::filesystem::current_path();
        std::filesystem::current_path(tree);
        host.startup(save);
        std::filesystem::current_path(prev);   // integration_main's sentinel is CWD-relative
    }
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasToggleStrip()), "build without the hooks");
    // Shadow quads, and the strip's own quads, with UI icons on or off.
    auto shadowQuads = [&](bool uiIcons) {
        host.writeSettingsFile(save, std::string("[Settings]\nversion=11\n\n[Display]\ntitleIcons=") +
                                     (uiIcons ? "1" : "0") + "\n");
        host.loadSettings(save);
        host.showAllHuds(false);
        host.showSettings(false);
        openSession(host);
        // Wake the cursor away from the strip, so it shows with no row hovered.
        host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
        host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
        REQUIRE(host.toggleStripRow(0) > 10);
        const int off = quadsWithShadow(host, false);
        const int quads = static_cast<int>(host.hudQuadRects("toggle_strip_widget").size());
        const int on = quadsWithShadow(host, true);
        return std::make_pair(on - off, quads);
    };
    const auto withIcons = shadowQuads(true);
    const auto without = shadowQuads(false);
    const int icons = withIcons.second - without.second;
    REQUIRE_MESSAGE(icons == host.toggleStripRow(0), "the strip drew " << icons << " icons for "
                    << host.toggleStripRow(0) << " rows - an icon is missing from the tree");
    CHECK_MESSAGE(withIcons.first - without.first == icons,
                  "drop shadow added " << (withIcons.first - without.first) << " quads for " << icons
                  << " strip icons - each takes one shadow quad");

    host.injectMouse(false);
    host.shutdown();
}
