// ============================================================================
// tests/integration/tests/whats_new_test.cpp
// THE "NEW" MARKERS APPEAR, DISMISS THE WAY THEY SAY THEY DO, AND STAY DISMISSED.
//
// The markers point players at features that landed in a settings row they have
// no reason to open (hud/settings/whats_new.h). Three things can go wrong and
// only the last is visible in a screenshot:
//
//   - they never show, and the feature stays as buried as it was;
//   - they never clear, so the menu is permanently decorated and the tag stops
//     meaning anything;
//   - they clear but do not PERSIST, so every restart re-decorates the menu --
//     which reads as the plugin having forgotten, and is the most annoying of
//     the three because it never stops.
//
// The dismissal rules are asymmetric on purpose and the asymmetry is the part
// worth pinning: opening a tab clears its TAG ("I know there is something here")
// while leaving the row BANDS, and hovering a row clears that row's band ("I have
// found it"). A test that treated them as one switch would pass against an
// implementation that cleared everything on the first click, which is the
// behaviour the split exists to avoid.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>
#include <vector>

TEST_CASE("what's-new markers show, dismiss per rule, and persist") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\whats_new\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE_MESSAGE(host.hasWhatsNew(), "MXBMRP3_Test_WhatsNew* not exported (test build?)");

    // A FRESH PLAYER: nothing dismissed, so every marker in the table is live.
    host.whatsNewReset();
    const int total = host.whatsNewLiveCount();
    REQUIRE_MESSAGE(total > 0,
                    "no live markers -- either the table is empty or every version "
                    "in it has fallen behind the build (check_whats_new.sh)");

    SUBCASE("every marker points at a row this build actually draws") {
        // THE SILENT FAILURE. A marker names a tab and a row TOOLTIP ID, and the
        // renderer bands whichever click region carries that id. Name an id no row
        // on that tab registers -- a typo, or a row that was renamed since -- and
        // nothing happens: no band, no log line, no assert. The tab still gets its
        // "New" tag, because the tag only asks whether the tab has a live marker.
        // So the menu says "something new here" and then shows nothing new, which
        // is worse than not marking it at all.
        //
        // Shared ids make this easy to get wrong in the other direction too. The pit
        // board's Texture row registers "common.texture", the id EVERY HUD's Texture
        // row uses, which is why a Marker carries a tab as well as an id -- and why
        // it is worth checking that the pairing resolves rather than assuming it.
        // A synthetic theme first: the Appearance tab HIDES its Panel Theme row when
        // no themes are installed, and a headless run has no mxbmrp3_data\\themes\\.
        // Without this the theme marker reads as unresolved for a reason that has
        // nothing to do with the table.
        host.installTheme("synthetic", 0.0f, 0.0f, /*titleBand=*/1, /*card=*/1);

        REQUIRE(host.whatsNewMarkerCount() == total);
        for (int i = 0; i < host.whatsNewMarkerCount(); ++i) {
            INFO("marker " << i << ": " << host.whatsNewMarkerName(i));
            CHECK(host.whatsNewMarkerResolves(i));
        }
        // Opening the tabs above dismissed their tags; put the set back for the
        // cases that follow (doctest re-runs the body per subcase, but the DLL's
        // state is process-global).
        host.clearTheme();
        host.whatsNewReset();
    }

    SUBCASE("a marked tab carries the tag, and opening it clears only the tag") {
        REQUIRE(host.whatsNewTabTagged("Widgets"));

        REQUIRE(host.openSettingsTab("Widgets"));
        // The TAG is gone...
        CHECK_FALSE(host.whatsNewTabTagged("Widgets"));
        // ...and the rows on it are NOT. This is the asymmetry: opening the tab
        // says you know something is here, not that you found it.
        CHECK(host.whatsNewLiveCount() == total);
    }

    SUBCASE("hovering a marked row clears that row and no other") {
        REQUIRE(host.openSettingsTab("Widgets"));
        const int before = host.whatsNewLiveCount();
        REQUIRE(before >= 2);   // Widgets carries two; the case needs both

        host.hoverSettingsRow("widgets.crashes");
        CHECK(host.whatsNewLiveCount() == before - 1);

        // Hovering it again is not a second dismissal.
        host.hoverSettingsRow("widgets.crashes");
        CHECK(host.whatsNewLiveCount() == before - 1);

        // An unmarked row on the same tab dismisses nothing.
        host.hoverSettingsRow("widgets.speed");
        CHECK(host.whatsNewLiveCount() == before - 1);
    }

    SUBCASE("a dismissal survives a save and reload") {
        // THROUGH THE PATH A PLAYER ACTUALLY TAKES: mark-dirty then flushIfDirty,
        // which is what RunStop/RunDeinit call when they leave the track. Written
        // against host.save() this case passed while the feature was broken in the
        // field -- save() writes unconditionally, so it proved the SERIALIZER works
        // and said nothing about whether a dismissal ever asks to be written.
        // Hovering a row touches nothing else in the panel, so if it does not mark
        // the settings dirty the flush is a no-op and every marker returns on the
        // next launch, forever. That is exactly what shipped, and only this shape of
        // the case can see it.
        REQUIRE_MESSAGE(host.hasMarkDirty(), "MXBMRP3_Test_FlushIfDirty/IsDirty absent");
        host.setAutoSave(true);
        REQUIRE(host.openSettingsTab("Widgets"));

        // SAVE FIRST, to clear the flag. Opening a tab persists [Profiles] activeTab
        // and marks dirty by itself, so a dirty check straight after the click passes
        // whatever the dismissal did -- it did, against a build with the mark removed.
        // From a clean flag the hover is the only thing that can set it again.
        host.save();
        REQUIRE_FALSE(host.isDirty());

        host.hoverSettingsRow("widgets.crashes");
        const int afterDismiss = host.whatsNewLiveCount();
        REQUIRE(afterDismiss == total - 1);
        CHECK_MESSAGE(host.isDirty(),
                      "dismissing a row left the settings clean -- nothing will ever "
                      "write whatsNewSeen, so the marker comes back next launch");

        host.flushIfDirty();
        // Wipe the in-memory set, then load: if the dismissal never reached the
        // file this reads back as a fresh player and the count returns to `total`.
        host.whatsNewReset();
        REQUIRE(host.whatsNewLiveCount() == total);
        host.loadSettings(saveWin);
        CHECK(host.whatsNewLiveCount() == afterDismiss);
        CHECK_FALSE(host.whatsNewTabTagged("Widgets"));   // the tab dismissal too
    }

    SUBCASE("starting up does not silently dismiss a tab") {
        // SettingsHud's constructor ends with hide() so the panel starts hidden, and
        // hide() dismisses whatever tab is open -- which at construction is
        // TAB_GENERAL, before any settings file has loaded. Unguarded, every launch
        // pre-dismissed General: an INI carrying whatsNewSeen overwrote it on load
        // and hid the fault, while an INI WITHOUT that key (fresh install, or any
        // file from before this feature) kept it and saved it, spending the General
        // tab's next marker on a player who was never shown one.
        //
        // Driven through the MECHANISM rather than through the startup instant: the
        // constructor runs once, before any test line, and by the second subcase the
        // process-global set is legitimately non-empty -- a case that read it at
        // startup would pass vacuously on the first run and fail spuriously after.
        // What the guard actually says is "hide() dismisses only a panel that WAS
        // open", so that is what is asserted, and hiding an already-hidden panel is
        // exactly what the constructor does.
        //
        // Read through the dismissed SET rather than a live count: no marker names
        // TAB_GENERAL today, so the stray key is invisible to every other accessor.
        // The trap is armed for whoever adds one.
        host.whatsNewReset();
        host.showSettings(false);           // hide an ALREADY-hidden panel
        const std::string afterNoOpHide = host.whatsNewSerialize();
        CHECK_MESSAGE(afterNoOpHide.empty(),
                      "hiding an already-hidden panel dismissed '" << afterNoOpHide
                      << "' -- the constructor's own hide() does this before the "
                      "settings file has loaded, spending that tab's next marker");

        // ...and a REAL close still dismisses, so the check above is not passing
        // because the whole path is dead.
        host.showSettings(true);
        host.showSettings(false);
        CHECK_FALSE(host.whatsNewSerialize().empty());
    }

    SUBCASE("Reset Everything does NOT bring the markers back") {
        // A dismissal is a record of what this player has already been SHOWN, not a
        // setting they chose, so a factory reset leaves it alone -- the same treatment
        // developer mode gets, and for the same reason. Restoring it would re-decorate
        // the menu with tags for features they went and looked at, which reads as the
        // plugin having forgotten rather than as a reset.
        //
        // It takes explicit code to get this right: the snapshot reset replays is
        // captured BEFORE the settings file loads, so its whatsNewSeen is empty and
        // replaying it would clear the set. replayGlobalDefaults saves and restores it
        // around the replay. Without that line this case goes red.
        //
        // resetEverything(), not resetAll(): the menu's button is TWO calls -- the
        // global snapshot replay and the per-profile reset -- and only the first goes
        // anywhere near this. Written against resetAll() alone, a test here would pass
        // while checking half a button.
        REQUIRE_MESSAGE(host.hasResetGlobals(), "MXBMRP3_Test_ResetGlobals not exported");
        REQUIRE(host.openSettingsTab("Widgets"));
        host.hoverSettingsRow("widgets.crashes");
        REQUIRE(host.whatsNewLiveCount() == total - 1);
        REQUIRE_FALSE(host.whatsNewTabTagged("Widgets"));

        host.resetEverything();
        CHECK(host.whatsNewLiveCount() == total - 1);
        CHECK_FALSE(host.whatsNewTabTagged("Widgets"));
    }

    host.shutdown();
}
