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
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

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
