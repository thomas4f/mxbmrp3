// ============================================================================
// tests/integration/tests/theme_override_test.cpp
// The per-HUD panel-theme override must CLEAR when the settings carry no theme key.
//
// THE BUG THIS PINS. The override is captured sparsely -- written only when the HUD
// has diverged from the global Appearance theme -- so absence means "follow the
// global theme" and has to be applied as such. The apply side assigned only on key
// PRESENCE and had no else-branch, so nothing ever cleared it:
//
//   pin Standings to "none"  ->  Reset to Defaults  ->  still pinned to "none"
//
// because the factory snapshot was captured at startup with an empty override and
// therefore carries no theme key at all. Profile switching had the same hole.
//
// The companion-instance keys four lines below it in settings_serde.h are the model
// this was meant to copy and only half did -- `if (compConfigured) ... else
// clearCompanionState()` -- and those ARE pinned (companion_decouple_test.cpp),
// which is why this one went unnoticed. CLAUDE.md states the rule by name: "capture
// that persists only when the companion has diverged, and apply that clears
// authoritatively when absent".
//
// Deliberately asserts through RESET and through a PROFILE SWITCH, because those are
// two different callers of the same applier and either could have been patched alone.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("per-HUD theme override clears on reset and on profile switch") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\theme-override\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();                                   // a default INI on disk to load back

    // Nothing pinned to begin with: the HUD follows the global Appearance theme.
    CHECK(host.standingsTheme() == "");

    SUBCASE("Reset to Defaults unpins it") {
        host.setStandingsTheme("none");
        REQUIRE(host.standingsTheme() == "none");

        host.resetAll();

        // The factory snapshot has no theme key for this HUD, and absence is
        // authoritative -- so reset means "back to following the global theme",
        // not "keep whatever was pinned because nothing said otherwise".
        CHECK(host.standingsTheme() == "");
    }

    SUBCASE("entering a profile captured without an override unpins it") {
        host.switchProfile(0);
        REQUIRE(host.standingsTheme() == "");

        // Pin it on profile 0, then move to profile 1 -- whose cache was captured at
        // startup with no override, so it carries no theme key. Entering it must
        // therefore UNPIN the HUD.
        //
        // Note the direction: switching AWAY captures the live state first, so
        // profile 0 legitimately keeps the pin. Coming BACK restoring it is the
        // feature working, not the bug -- the bug is a profile with no theme key
        // failing to clear one.
        host.setStandingsTheme("none");
        host.switchProfile(1);

        CHECK(host.standingsTheme() == "");
    }

    // A real theme name still round-trips: the fix must clear only what is ABSENT,
    // never overwrite what was saved. Without this the "fix" could be a blanket
    // clear on every apply, which would pass both cases above and break the feature.
    SUBCASE("a set override survives a save/load round trip") {
        host.setStandingsTheme("debug");
        host.save();
        host.loadSettings(saveWin);
        CHECK(host.standingsTheme() == "debug");
    }
}

// ============================================================================
// THE OVERRIDE REACHES THE PANEL'S PLAN, not just its paint.
//
// THE BUG THIS PINS. The memoised panel plan is keyed on the GLOBAL theme
// generation but computed from the RESOLVED theme's box terms — and a per-HUD
// override changes which theme resolves without bumping the global generation.
// invalidateThemeCache() cleared only the theme memo, never m_planCacheValid,
// so cycling the per-HUD Theme row served the PREVIOUS theme's geometry
// silently. The collision needs two themes whose difference is invisible to
// the rest of the key: borders leak in via dim.padding, so the themes here
// share every border and differ only in `[panel] gap` — a term only the plan
// reads. Latent with the shipped themes (they share one geometry by design);
// live for any third-party theme.
// ============================================================================
TEST_CASE("per-HUD theme override re-plans the panel, not just its paint") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme-override-plan\\");
    host.showAllHuds(true);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, 5, 0);
    host.addEntry(10, "Alice");
    host.addEntry(11, "Bob");
    host.classify(6, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
                               { .num = 11, .best = 91000, .laps = 1, .gap = 1000 } });

    // Two themes, EQUAL borders. "ovr-wide" states a fatter [panel] gap; the
    // second install ("ovr-tight", the defaults) is the globally selected one.
    host.installTheme("ovr-wide", /*inset=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*card=*/1);
    REQUIRE(host.setThemeGap(3.0f));
    host.installTheme("ovr-tight", 1.0f, 1.0f, 1, 1);
    host.draw();
    const auto tight = host.hudScreenEdges(PluginHost::HUD_STANDINGS);
    REQUIRE(tight.b > tight.t);

    // The override cycle is the one path that changes the resolved theme with
    // NO generation bump — the exact hole the plan key cannot see on its own.
    host.setStandingsTheme("ovr-wide");
    host.draw();
    const auto wide = host.hudScreenEdges(PluginHost::HUD_STANDINGS);
    CHECK_MESSAGE(wide.b - wide.t > tight.b - tight.t,
                  "overriding to a wider-gap theme did not change the panel's "
                  "height (" << (tight.b - tight.t) << " -> " << (wide.b - wide.t)
                  << ") -- the plan memo served the previous theme's geometry");

    // ...and back, so the fix cannot be a one-way invalidation.
    host.setStandingsTheme("");
    host.draw();
    const auto back = host.hudScreenEdges(PluginHost::HUD_STANDINGS);
    CHECK(back.b - back.t == tight.b - tight.t);

    host.shutdown();
}
