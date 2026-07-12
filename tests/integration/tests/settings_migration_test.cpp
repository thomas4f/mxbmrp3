// ============================================================================
// tests/integration/tests/settings_migration_test.cpp
// Settings MIGRATION / forward-compat: a user's customised settings.ini must
// survive being loaded by a plugin build whose SETTINGS_VERSION doesn't exactly
// match the file's — the "keep settings from previous versions" contract.
//
// The bug this pins (settings_manager.cpp): the load dispatch gated HUD sections
// on `loadedVersion >= SETTINGS_VERSION`, i.e. an EXACT match. So:
//   * a file with a missing/unparseable version line (loadedVersion == 0) — e.g.
//     hand-edited, a supported workflow — matched neither the v4+ nor the v3
//     branch, and EVERY [HudName] section was silently skipped → all per-HUD
//     settings reverted to defaults on the next save. (Reproduced here; red
//     before the fix, green after.)
//   * the same wipe would hit EVERY user's v4 file the instant SETTINGS_VERSION
//     was bumped to 5 (their file then matched neither branch). Not reproducible
//     while the current version is 4, so the version=4 and version=99 phases
//     below stand as forward/backward-compat guards for that future bump.
//
// One process (singletons persist across TEST_CASEs, so one case): startup writes
// a default INI; we perturb known HUD anchors to stand in for user edits, mangle
// the version header, load via the hook, re-save, and assert the edits survived.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

namespace {

// Load `iniText` through the plugin and return the re-saved file, parsed. The
// round-trip (load → capture live state → save) is what discards wiped sections:
// if load skips a [HudName] section, the save rewrites that HUD at its default.
ini::Map roundTrip(PluginHost& host, const char* saveWin, const std::string& iniPath,
                   const std::string& iniText) {
    ini::writeFile(iniPath, iniText);
    host.loadSettings(saveWin);
    host.save();
    return ini::parse(ini::readFile(iniPath));
}

}  // namespace

TEST_CASE("settings migration: HUD settings survive a version-mismatched INI") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_migration\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_migration\\mxbmrp3\\mxbmrp3_settings.ini";

    // Per-HUD anchors that default to 0/1 (so perturb() flips them) — these are the
    // sections the version gate wrongly skipped. Reused from reset_test's set.
    const std::vector<ini::Anchor> hudAnchors = {
        { "StandingsHud", "classicLayout" },
        { "StandingsHud", "playerRowHighlightBrand" },
        { "MapHud",       "showTitle" },
    };

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();                                    // default INI (stamped version=4) on disk

    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini written at " << iniPath);
    const ini::Map D = ini::parse(defText);

    // "User customised their HUD": flip the anchors. This is our stand-in for a
    // settings.ini carried over from a previous install.
    const std::string userText = ini::perturb(defText, hudAnchors);
    const ini::Map P = ini::parse(userText);
    for (const auto& a : hudAnchors) {              // guard against a vacuous test
        ini::Key k{ a.section, a.key };
        REQUIRE(D.count(k)); REQUIRE(P.count(k));
        REQUIRE(D.at(k) != P.at(k));
    }

    auto surviving = [&](const ini::Map& R, const char* phase) {
        for (const auto& a : hudAnchors) {
            ini::Key k{ a.section, a.key };
            INFO(phase << ": [" << a.section << "] " << a.key);
            REQUIRE(R.count(k));
            CHECK(R.at(k) == P.at(k));              // the user's value, not the default
        }
    };

    // --- Phase 1: NO version line (loadedVersion == 0) -----------------------
    // The real red→green case: before the fix the loader skipped every HUD
    // section and the re-save wiped the user's edits back to default.
    surviving(roundTrip(host, saveWin, iniPath, ini::stripVersionLine(userText)), "missing version");

    // --- Phase 2: explicit version=4 (the current on-disk format) ------------
    // Passes today; stands as the guard that a v4 file still loads after a future
    // SETTINGS_VERSION bump (without the >= FIRST_BASE_SECTION_VERSION fix, a bump
    // to 5 would make this fail exactly as phase 1 did).
    surviving(roundTrip(host, saveWin, iniPath, ini::setVersionLine(userText, 4)), "version=4");

    // --- Phase 3: a FUTURE version (forward-compat) --------------------------
    // A newer file opened by this older build must still load its HUD sections
    // rather than discard them.
    surviving(roundTrip(host, saveWin, iniPath, ini::setVersionLine(userText, 99)), "version=99");

    host.shutdown();
}
