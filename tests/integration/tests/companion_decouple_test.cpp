// ============================================================================
// tests/integration/tests/companion_decouple_test.cpp
// Per-surface HUD decoupling: the companion window carries its OWN on/off +
// position for each HUD, independent of the in-game surface, while everything
// else (colors, fonts, sizes, columns) stays shared. Two things are pinned here:
//
//  1) Runtime semantics of the companion instance (base_hud.h), driven on the
//     live StandingsHud via the MXBMRP3_Test_Standings* hooks:
//       * MIRROR while unconfigured   — getCompanionVisible() follows the game
//         surface until the first companion-side edit (so an untouched HUD looks
//         identical on both windows, and a later game-side change is reflected).
//       * SNAPSHOT on first edit / DECOUPLE — setCompanionVisible() diverges the
//         companion without touching the game surface, and thereafter game-side
//         changes no longer move the companion (the whole point: standings on the
//         primary screen, telemetry on the companion, from one shared profile).
//       * CLEAR reverts to mirror     — clearCompanionState() drops the override
//         so the HUD follows the game again.
//
//  2) Persistence round-trip: a diverged companion state survives save -> load
//     through the REAL serializer (not just an INI-string check — the base
//     [HudName] section round-trips verbatim, so a string assertion can't tell a
//     captured value from a passed-through one; reading the live HUD back after a
//     reload can).
//
//  Also guards the default: a fresh profile writes NO companion keys (the
//  instance mirrors the game, so there is nothing to persist).
//
// Self-contained doctest, one process (singletons persist across cases). See
// run_tests.sh; the hooks live in core/test_hooks.cpp (MXBMRP3_TEST_BUILD only).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

TEST_CASE("companion decouple: mirror -> diverge -> clear, and persist across reload") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\companion_decouple\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\companion_decouple\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasCompanionDecouple(),
                    "MXBMRP3_Test_Standings* hooks not exported (test build?)");
    host.startup(saveWin);

    // Start from a known, mirroring baseline.
    host.stClearCompanion();
    host.stSetVisible(true);

    // --- Mirror while unconfigured ------------------------------------------
    {
        auto s = host.stCompanionState();
        CHECK(s.configured == 0);
        CHECK(s.gameVisible == 1);
        CHECK(s.companionVisible == 1);          // follows the game
    }
    host.stSetVisible(false);
    {
        auto s = host.stCompanionState();
        CHECK(s.configured == 0);                // a game-side change alone doesn't configure
        CHECK(s.gameVisible == 0);
        CHECK(s.companionVisible == 0);          // still mirrors the game
    }

    // --- First companion edit snapshots + decouples -------------------------
    host.stSetVisible(true);                      // game visible again...
    host.stSetCompanionVisible(false);            // ...but hide it on the companion
    {
        auto s = host.stCompanionState();
        CHECK(s.configured == 1);                 // now diverged
        CHECK(s.gameVisible == 1);                // in-game surface untouched
        CHECK(s.companionVisible == 0);           // companion is independently hidden
    }
    // Once configured, moving the game surface no longer drags the companion.
    host.stSetVisible(false);
    CHECK(host.stCompanionState().companionVisible == 0);
    host.stSetVisible(true);
    {
        auto s = host.stCompanionState();
        CHECK(s.gameVisible == 1);
        CHECK(s.companionVisible == 0);           // held its own value across both game flips
    }

    // --- Persistence: the divergence survives a save -> reload --------------
    host.save();
    // Perturb the LIVE state so a reload that restored from memory (rather than
    // disk) would be caught: drop the override, then reload from the saved file.
    host.stClearCompanion();
    REQUIRE(host.stCompanionState().configured == 0);
    host.loadSettings(saveWin);
    {
        auto s = host.stCompanionState();
        CHECK(s.configured == 1);                 // restored from disk
        CHECK(s.gameVisible == 1);                // shared surface unchanged
        CHECK(s.companionVisible == 0);           // decoupled value persisted
    }

    // --- Clear reverts to mirror --------------------------------------------
    host.stClearCompanion();
    host.stSetVisible(true);
    CHECK(host.stCompanionState().configured == 0);
    CHECK(host.stCompanionState().companionVisible == 1);
    host.stSetVisible(false);
    CHECK(host.stCompanionState().companionVisible == 0);   // mirrors the game once more

    // --- Sparse-save: a mirroring HUD persists NO companion keys -------------
    // With the divergence dropped, saving must not leave per-HUD companion
    // instance keys on disk (so an upgraded/renamed HUD can't inherit stale
    // companion state). Distinct from the global [Display] companionWindow*
    // geometry, which is always written.
    host.stClearCompanion();
    host.save();
    const ini::Map D = ini::parse(ini::readFile(iniPath));
    REQUIRE_FALSE(D.empty());
    for (const auto& kv : D) {
        const std::string& key = kv.first.second;
        INFO("stray companion key after save: [" << kv.first.first << "] " << key);
        CHECK(key != "companionConfigured");
        CHECK(key != "companionVisible");
        CHECK(key != "companionX");
        CHECK(key != "companionY");
    }

    host.shutdown();
}

TEST_CASE("companion decouple: configured-but-equal persists NO keys (upgrade-safe)") {
    // Opening the companion window snapshots the game layout into EVERY HUD (configured)
    // without the user moving anything, so companion == game. That must NOT persist
    // companion keys: otherwise a HUD the user never touched on the companion would pin
    // its old position and miss a changed default on a later upgrade. Only a genuine
    // divergence (companion != game) persists. This guards the sparse-save/upgrade-safety
    // property for the common "opened the companion, didn't rearrange it" case.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\companion_decouple\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\companion_decouple\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.hasCompanionDecouple());

    host.stClearCompanion();
    host.stSetVisible(true);
    // Configure the companion to EQUAL the game — exactly what a snapshot-on-open does:
    // visibility matches and the position is snapshot-copied. Configured, but not diverged.
    host.stSetCompanionVisible(true);
    REQUIRE(host.stCompanionState().configured == 1);
    REQUIRE(host.stCompanionState().gameVisible == 1);
    REQUIRE(host.stCompanionState().companionVisible == 1);   // == game

    host.save();
    const ini::Map D = ini::parse(ini::readFile(iniPath));
    REQUIRE_FALSE(D.empty());
    for (const auto& kv : D) {
        const std::string& key = kv.first.second;
        INFO("stray companion key (configured-but-equal): [" << kv.first.first << "] " << key);
        CHECK(key != "companionConfigured");
        CHECK(key != "companionVisible");
        CHECK(key != "companionX");
        CHECK(key != "companionY");
    }

    // Sanity: a real divergence from here DOES persist (so the gate isn't just off).
    host.stSetCompanionVisible(false);            // now companion != game
    host.save();
    const ini::Map D2 = ini::parse(ini::readFile(iniPath));
    bool sawCompanionKey = false;
    for (const auto& kv : D2)
        if (kv.first.second == "companionVisible") sawCompanionKey = true;
    CHECK(sawCompanionKey);

    host.shutdown();
}

TEST_CASE("companion routing: game-frame suppression, companion filtering + offset, X-close fallback") {
    // The load-bearing new code is collectSurface + draw()'s per-target routing.
    // Drive real frames and read what draw() emits to the game vs the built game/
    // companion frames. DisplayTarget: 0=IN_GAME, 1=COMPANION, 2=BOTH.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\companion_routing\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE((host.hasSurfaceRouting() && host.hasCompanionDecouple()),
                    "surface-routing hooks not exported (test build?)");
    host.startup(saveWin);

    // Populate a race so StandingsHud renders rows — a robustly non-empty frame.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
    });
    host.stSetVisible(true);        // StandingsHud shown in-game
    host.stClearCompanion();        // companion mirrors the game
    host.showSettings(false);

    host.draw();
    REQUIRE(host.surfaceFrameStats().gameQuads > 0);   // the frame really is built

    // --- Game-frame routing by display target -------------------------------
    // Not suppressed => draw() emits the whole built frame; suppressed => emits 0
    // while the frame is still built (routing decision, not an empty frame).
    host.setDisplayTarget(0 /*IN_GAME*/);
    host.draw();
    CHECK(host.lastGameQuads() == host.surfaceFrameStats().gameQuads);

    host.setDisplayTarget(2 /*BOTH*/);
    host.draw();
    CHECK(host.lastGameQuads() == host.surfaceFrameStats().gameQuads);

    host.setDisplayTarget(1 /*COMPANION*/);
    host.draw();
    CHECK(host.lastGameQuads() == 0);                  // in-game HUD suppressed...
    CHECK(host.surfaceFrameStats().gameQuads > 0);     // ...but the frame is still built

    // Settings-open escape hatch: the menu always shows in-game so the user can
    // switch back, even in COMPANION mode.
    host.showSettings(true);
    host.draw();
    CHECK(host.lastGameQuads() > 0);
    host.showSettings(false);

    // --- X-close fallback: closing the window reverts the target to In-game --
    host.setDisplayTarget(1 /*COMPANION*/);
    host.draw();
    REQUIRE(host.lastGameQuads() == 0);                // suppressed right before the close
    host.companionSimulateUserClose();
    host.draw();
    CHECK(host.displayTarget() == 0 /*IN_GAME*/);      // fell back so the HUD isn't lost
    CHECK(host.lastGameQuads() == host.surfaceFrameStats().gameQuads);  // HUD reappears

    // --- Companion frame: per-surface visibility filtering + offset delta ----
    host.setDisplayTarget(2 /*BOTH*/);
    host.companionWindow(true);     // enable the companion so its frame gets built
    host.stSetVisible(true);
    host.stClearCompanion();        // Standings mirrors the game on the companion
    host.draw();
    {
        auto s = host.surfaceFrameStats();
        CHECK(s.companionQuads == s.gameQuads);                 // mirror: identical frames
        CHECK(s.companionSumX == doctest::Approx(s.gameSumX));
    }

    host.stSetCompanionVisible(false);   // hide Standings on the companion only
    host.draw();
    {
        auto s = host.surfaceFrameStats();
        CHECK(s.companionQuads < s.gameQuads);                  // Standings dropped from companion
        CHECK(s.companionQuads > 0);                            // other HUDs still present
        CHECK(host.stCompanionState().gameVisible == 1);        // in-game unaffected
    }

    host.stSetCompanionVisible(true);          // show it again...
    host.stSetCompanionOffset(0.9f, 0.0f);     // ...far right of its (tiny) game offset
    host.draw();
    {
        auto s = host.surfaceFrameStats();
        CHECK(s.companionQuads == s.gameQuads);                 // visible on both again
        CHECK(s.companionSumX > s.gameSumX);                    // offset-delta translation applied
    }

    host.companionWindow(false);
    host.shutdown();
}

TEST_CASE("companion decouple: a HUD hidden in-game but shown on the companion still updates") {
    // A HUD's update() skips the expensive rebuild when it's not visible (to save
    // work on disabled HUDs). That gate must consider the COMPANION surface too —
    // otherwise a HUD hidden in-game but shown on the companion never rebuilds and
    // renders stale. StandingsHud is shown ONLY on the companion here, so its rows
    // contribute to the companion frame but not the game frame; the (companion -
    // game) quad difference is therefore Standings' own size, and it must GROW as
    // more riders arrive while the HUD is in-game-hidden.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\companion_offscreen\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasSurfaceRouting());
    host.startup(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");
    host.addEntry(3,  "Dave");
    host.addEntry(99, "Eve");

    host.companionWindow(true);
    host.stSetVisible(false);          // StandingsHud hidden in-game...
    host.stSetCompanionVisible(true);  // ...shown on the companion

    auto standingsOnlyOnCompanion = [&]() {
        auto s = host.surfaceFrameStats();
        return s.companionQuads - s.gameQuads;   // Standings is companion-only here
    };

    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
    });
    host.draw();
    int diffTwo = standingsOnlyOnCompanion();
    CHECK(diffTwo > 0);                            // Standings rendered on the companion at all

    // More riders arrive while the HUD stays in-game-hidden. It must rebuild (from
    // the companion surface) so the tower grows; without the fix it stays stale.
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 6, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 6, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 6, .gap = 3200 },
        { .num = 3,  .best = 93100, .laps = 6, .gap = 4800 },
        { .num = 99, .best = 94000, .laps = 6, .gap = 6100 },
    });
    host.draw();
    int diffFive = standingsOnlyOnCompanion();
    CHECK(diffFive > diffTwo);                     // grew with the new data while in-game-hidden

    host.companionWindow(false);
    host.shutdown();
}
