// ============================================================================
// tests/integration/tests/reset_profile_test.cpp
// Per-profile settings OPERATIONS — the paths reset_test.cpp can't reach. They
// all act on the PROFILE DIFF (the [HudName:Profile] overrides), NOT the base, so
// a base-section perturbation (what reset_test uses) doesn't exercise them (the
// "m_hudDefaults is not a clean factory snapshot" property). Each phase hand-
// authors an INI whose base [HudName] stays at factory and adds a
// [HudName:Profile] override that differs, then drives an operation and asserts
// the on-disk result:
//   1. ResetActiveProfile — clears the active (Practice) profile's diff.
//   2. ResetHud           — clears only the named HUD's diff.
//   3. negative control   — a non-active-profile (Race) diff survives a reset.
//   4. CopyProfileToAll   — the active profile's diff propagates to every profile.
//   5. SwitchProfile      — the active profile persists and diffs are preserved.
//
// Practice (index 0) is the default active profile. Single process, sequential
// phases; the re-authored INI is pulled in each phase via the LoadSettings hook
// (which clears and reloads the caches). Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

static std::string flip(const std::string& v) {
    return v == "0" ? "1" : (v == "1" ? "0" : v + "9");
}

TEST_CASE("profile ops: reset scope, copy-to-all, and switch (profile-diff based)") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\reset_profile\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\reset_profile\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();   // default INI: base sections at factory, [Profiles] activeProfile=0 (Practice)

    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini at " << iniPath);
    const ini::Map D = ini::parse(defText);

    // Per-profile HUD anchors (default 0/1, so flip() makes a differing override).
    const ini::Key sKey{ "StandingsHud", "classicLayout" };
    const ini::Key mKey{ "MapHud", "showTitle" };
    REQUIRE(D.count(sKey)); REQUIRE(D.count(mKey));
    const std::string sBase = D.at(sKey), mBase = D.at(mKey);
    const std::string sDiff = flip(sBase), mDiff = flip(mBase);

    // Section keys for the profile-diff overrides written to disk.
    const ini::Key sPractice{ "StandingsHud:Practice", "classicLayout" };
    const ini::Key mPractice{ "MapHud:Practice", "showTitle" };
    const ini::Key sRace{ "StandingsHud:Race", "classicLayout" };

    // An INI = default base + Practice overrides that differ from base.
    auto withPracticeDiff = [&]() {
        return defText + "\n"
            "[StandingsHud:Practice]\nclassicLayout=" + sDiff + "\n"
            "[MapHud:Practice]\nshowTitle=" + mDiff + "\n";
    };
    auto loadSaveParse = [&](const std::string& text) {
        ini::writeFile(iniPath, text);
        host.loadSettings(saveWin);
        host.save();
        return ini::parse(ini::readFile(iniPath));
    };

    // --- Phase 1: ResetActiveProfile clears the active (Practice) diff --------
    {
        ini::Map R = loadSaveParse(withPracticeDiff());
        // The override round-tripped (guards a vacuous test) and base is untouched.
        REQUIRE(R.count(sPractice));
        CHECK(R.at(sPractice) == sDiff);
        CHECK(R.at(mPractice) == mDiff);
        CHECK(R.at(sKey) == sBase);
    }
    host.resetActiveProfile();
    host.save();
    {
        ini::Map R = ini::parse(ini::readFile(iniPath));
        CHECK(R.count(sPractice) == 0);       // diff cleared → section gone
        CHECK(R.count(mPractice) == 0);
        CHECK(R.at(sKey) == sBase);           // base still factory
        CHECK(R.at(mKey) == mBase);
    }

    // --- Phase 2: ResetHud clears ONLY the named HUD's diff -------------------
    {
        ini::Map R = loadSaveParse(withPracticeDiff());
        REQUIRE(R.count(sPractice)); REQUIRE(R.count(mPractice));
    }
    host.resetHud("StandingsHud", /*keepVisibility=*/false);
    host.save();
    {
        ini::Map R = ini::parse(ini::readFile(iniPath));
        CHECK(R.count(sPractice) == 0);       // named HUD's diff cleared
        REQUIRE(R.count(mPractice));          // the other HUD's diff survives
        CHECK(R.at(mPractice) == mDiff);
    }

    // --- Phase 3: negative control — a NON-active profile's diff survives -----
    // A Race (profile 2) override while Practice is active must NOT be touched by
    // ResetActiveProfile — proving the reset is scoped to the active profile.
    {
        ini::Map R = loadSaveParse(
            defText + "\n[StandingsHud:Race]\nclassicLayout=" + sDiff + "\n");
        REQUIRE(R.count(sRace));
    }
    host.resetActiveProfile();   // Practice is active, not Race
    host.save();
    {
        ini::Map R = ini::parse(ini::readFile(iniPath));
        REQUIRE(R.count(sRace));              // survived the active-profile reset
        CHECK(R.at(sRace) == sDiff);
    }

    // --- Phase 4: CopyProfileToAll propagates the active diff to every profile -
    // The Practice diff exists only on Practice; after copy, every profile carries
    // it, so [StandingsHud:Qualify/Race/Spectate] all appear with the same value.
    {
        ini::writeFile(iniPath, withPracticeDiff());
        host.loadSettings(saveWin);
        host.copyProfileToAll();
        host.save();
        ini::Map R = ini::parse(ini::readFile(iniPath));
        for (const char* prof : { "Qualify", "Race", "Spectate" }) {
            ini::Key k{ std::string("StandingsHud:") + prof, "classicLayout" };
            INFO("copied to profile " << prof);
            REQUIRE(R.count(k));
            CHECK(R.at(k) == sDiff);
        }
    }

    // --- Phase 5: SwitchProfile persists the active profile, keeps the diffs ---
    // Practice holds a StandingsHud diff, Race a MapHud diff. Switch to Race (2):
    // the persisted activeProfile becomes 2 and BOTH profiles' diffs survive.
    {
        ini::writeFile(iniPath, defText + "\n"
            "[StandingsHud:Practice]\nclassicLayout=" + sDiff + "\n"
            "[MapHud:Race]\nshowTitle=" + mDiff + "\n");
        host.loadSettings(saveWin);
        host.switchProfile(2);   // 0=Practice, 1=Qualify, 2=Race, 3=Spectate
        host.save();
        ini::Map R = ini::parse(ini::readFile(iniPath));
        CHECK(R.at(ini::Key{ "Profiles", "activeProfile" }) == "2");   // switch persisted
        const ini::Key mRace{ "MapHud:Race", "showTitle" };
        REQUIRE(R.count(sPractice)); CHECK(R.at(sPractice) == sDiff);  // Practice diff kept
        REQUIRE(R.count(mRace));     CHECK(R.at(mRace) == mDiff);      // Race diff kept
    }

    host.shutdown();
}
