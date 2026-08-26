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
//     was bumped to 5 (their file then matched neither branch). THAT BUMP HAS NOW
//     HAPPENED (v5 = sparse [Colors]/[Fonts]), so the version=4 phase below is no
//     longer a placeholder for a hypothetical future -- it is the live
//     backward-compat case, and it passes because the dispatch gate is
//     `>= FIRST_BASE_SECTION_VERSION` (4) rather than an exact match.
//
// Phase 4 pins the v4 -> v5 MIGRATION itself: that upgrade releases colour and
// font slots a pre-v5 file pinned merely by listing them, without touching slots
// the user genuinely chose.
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
#include <cmath>
#include <string>
#include <vector>

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

    // --- Phase 2: explicit version=4 (the PREVIOUS on-disk format) -----------
    // The live backward-compat case now that the current version is 5: a v4 file
    // must still load every HUD section. Without the >= FIRST_BASE_SECTION_VERSION
    // gate this would fail exactly as phase 1 did.
    surviving(roundTrip(host, saveWin, iniPath, ini::setVersionLine(userText, 4)), "version=4");

    // --- Phase 3: a FUTURE version (forward-compat) --------------------------
    // A newer file opened by this older build must still load its HUD sections
    // rather than discard them.
    surviving(roundTrip(host, saveWin, iniPath, ini::setVersionLine(userText, 99)), "version=99");

    // --- Phase 3b: a version<7 file MISSING the centre-anchor keys -----------
    // The v7/v8 migrations also shift the file-carried base values in
    // m_hudDefaults (see the v7 block in settings_manager.cpp) -- and the rest
    // of that map is the factory snapshot, whose offsets are ALREADY
    // centre-anchored. A file that does not carry the key must leave the
    // written base block at the factory value: an unconditional defaults-shift
    // walks an untouched panel half a screen right on upgrade. Placed here
    // deliberately -- m_hudDefaults persists across loads, and phase 3's v99
    // load (no migration) re-folded userText's factory-valued offsets last, so
    // the map holds the factory number this phase compares against.
    {
        ini::Key noticesX{ "NoticesHud", "offsetX" };
        REQUIRE(D.count(noticesX));
        const double factoryNotices = std::stod(D.at(noticesX));
        const ini::Map bare =
            roundTrip(host, saveWin, iniPath, "[Settings]\nversion=6\n");
        bool baseAtFactory = false, baseShiftedWrongly = false;
        for (const auto& kv : bare) {
            if (kv.first.first != "NoticesHud" || kv.first.second != "offsetX") continue;
            const double v = std::stod(kv.second);
            if (std::abs(v - factoryNotices) < 1e-4) baseAtFactory = true;
            if (std::abs(v - (factoryNotices + 0.5)) < 1e-4) baseShiftedWrongly = true;
        }
        CHECK_MESSAGE(baseAtFactory,
                      "base [NoticesHud] offsetX moved off factory on a bare v6 load");
        CHECK_MESSAGE(!baseShiftedWrongly,
                      "the v7 migration shifted a factory-seeded default it had no file value for");
    }

    // --- Phase 4: the v4 -> v5 colour migration ------------------------------
    // A pre-v5 file listed EVERY colour, and the load path pins every key it reads,
    // so an upgrading user had all ten slots marked "mine" and no theme palette
    // could ever show through. The migration releases only the slots whose stored
    // value equals the built-in default -- the ones carrying no user intent,
    // present merely because the old writer wrote everything.
    //
    // WHY THIS TOOK TWO ATTEMPTS. The first one asserted the same two cases and saw
    // the CUSTOM slot come back released too. That was recorded as "the appended
    // [Colors] section never reached applyGlobalLine" -- a reasonable-sounding
    // inference, and wrong. The section is read fine; the VALUE was not.
    // PluginUtils::parseColorHex is std::stoul(value, nullptr, 0), which takes
    // "0xAABBGGRR" and throws on the "#rrggbb" a skinner writes in a THEME ini
    // (that notation is parseRgbHex, deliberately a different function -- see the
    // note at both). On a throw it returns its fallback, which is the slot's
    // CURRENT colour, i.e. the default -- so a #rrggbb value lands on the default
    // and the migration then correctly releases it. The test was measuring its own
    // typo, and the diagnosis blamed the code under test.
    //
    // Hence the explicit 0xAABBGGRR literals below, and hence asserting the
    // effective colour as well as the pin: if a value silently falls back again,
    // effectiveColor() says so instead of the pin quietly telling the wrong story.
    {
        constexpr int kPrimary = 0;    // ColorSlot::PRIMARY -- default WHITE
        constexpr int kAccent  = 9;    // ColorSlot::ACCENT
        constexpr unsigned long kWhite  = 0xFFFFFFFFul;   // the PRIMARY default
        constexpr unsigned long kCustom = 0xFF0088FFul;   // nothing's default

        // Start from a clean pin state so this phase measures the migration rather
        // than whatever phases 1-3 left in the singleton.
        host.clearColorOverride(kPrimary);
        host.clearColorOverride(kAccent);

        std::string v4 = ini::setVersionLine(userText, 4);
        v4 += "\n[Colors]\nprimary=0xFFFFFFFF\naccent=0xFF0088FF\n";
        ini::writeFile(iniPath, v4);
        host.loadSettings(saveWin);

        // Both values must actually have been parsed. Without this the two pin
        // assertions below are satisfiable by the fallback path, which is exactly
        // how the first attempt misread itself.
        CHECK(host.effectiveColor(kPrimary) == kWhite);
        CHECK(host.effectiveColor(kAccent) == kCustom);

        // Written at the built-in default -> no user intent -> released, so a theme
        // can colour it. This is the migration's whole point.
        CHECK(host.colorOverridden(kPrimary) == 0);
        // Genuinely chosen -> stays pinned, and keeps its value.
        CHECK(host.colorOverridden(kAccent) == 1);
        CHECK(host.effectiveColor(kAccent) == kCustom);
    }

    // ------------------------------------------------------------------------
    // The v5 FONT unpinning, same rule as the colours above -- and separately
    // pinned because it separately broke: the first implementation compared the
    // strings with ==, which on the two `const char*` operands compared POINTERS
    // (see the comment at the v5 block in settings_manager.cpp). Every font
    // stayed pinned while the migration looked correct, and only the colour half
    // was tested, so nothing said so.
    {
        constexpr int kNormal = 1;   // FontCategory::NORMAL -- default RobotoMono-Regular
        constexpr int kStrong = 2;   // FontCategory::STRONG -- default RobotoMono-Bold

        host.clearFontOverride(kNormal);
        host.clearFontOverride(kStrong);

        std::string v4 = ini::setVersionLine(userText, 4);
        v4 += "\n[Fonts]\nnormal=RobotoMono-Regular\nstrong=IBMPlexMono-Regular\n";
        ini::writeFile(iniPath, v4);
        host.loadSettings(saveWin);

        // Both values must actually have been parsed and resolved to real fonts,
        // for the same reason the colour phase asserts effective values first.
        CHECK(host.effectiveFont(kNormal) == "RobotoMono-Regular");
        CHECK(host.effectiveFont(kStrong) == "IBMPlexMono-Regular");

        // Written at the built-in default -> released; chosen -> stays pinned.
        CHECK(host.fontOverridden(kNormal) == 0);
        CHECK(host.fontOverridden(kStrong) == 1);

        // Leave no pinned font behind: the later phases assert geometry against
        // the default lattice, and this phase is about the pin, not the font.
        host.clearFontOverride(kStrong);
    }


    // ------------------------------------------------------------------------
    // v5 -> v6: triggerFillMode is RESCUED from the retired per-variant layout block.
    //
    // The pad's geometry moved into gamepads/<name>/, but triggerFillMode is a display
    // preference, so it became a widget-level key in [GamepadWidget] instead. The pack
    // migration maps only the texture variant onto a pack NAME, so without this the old
    // [GamepadWidget_Layout_N] section is parsed into a cache nothing reads, then pruned
    // from the rewritten file -- the user's choice gone, unrecoverably, on upgrade.
    //
    // The rescue prefers the variant the user was actually looking at, which is why the
    // file below states two blocks with DIFFERENT values and selects the second.
    {
        const std::string v5 =
            "[Settings]\nversion=5\n\n"
            "[GamepadWidget]\ntextureVariant=2\n\n"
            "[GamepadWidget_Layout_1]\ntriggerFillMode=0\n\n"
            "[GamepadWidget_Layout_2]\ntriggerFillMode=1\n";
        const ini::Map out = roundTrip(host, saveWin, iniPath, v5);

        // Asserted on LIVE state, not on a named section of the re-saved file: the BASE
        // [GamepadWidget] block is written from the factory-default snapshot, so a
        // migrated value only ever appears in whichever profile section diverges from it.
        // Which profile that is depends on the active one, and none of that is what this
        // case is about.
        //
        // Variant 2 was selected, so its value is the one that survives -- not variant 1's.
        CHECK(host.gamepadPackStored() == "ds4");
        // The rescued key reaches the file too. Matched on the WIDGET's own section --
        // "GamepadWidget" or "GamepadWidget:<profile>" -- and deliberately not on a
        // "GamepadWidget" prefix: the retired [GamepadWidget_Layout_N] blocks reach the
        // parse cache (any unknown [Name] is a base section) but the writer iterates
        // only hudSectionRegistry(), so they are PRUNED from the rewritten file --
        // which is why the rescue in settings_manager.cpp exists at all. The exact
        // match keeps this assertion honest either way, and documents the contract:
        // unknown KEYS in known sections survive a round-trip; unknown SECTIONS do not.
        bool sawFill = false;
        for (const auto& kv : out) {
            const std::string& sec = kv.first.first;
            const bool isWidgetSection =
                sec == "GamepadWidget" || sec.rfind("GamepadWidget:", 0) == 0;
            if (isWidgetSection && kv.first.second == "triggerFillMode" && kv.second == "1") {
                sawFill = true;   // ANY of them: see below
            }
        }
        // ANY widget section, not all of them. The BASE [GamepadWidget] block is written
        // from the factory-default snapshot and correctly still reads 0; the rescued value
        // lands in the profile section that diverges from it ([GamepadWidget:Practice]).
        // Asserting on every matching section would be asserting that the base/profile
        // diff does not work.
        CHECK(sawFill);
    }

    // ------------------------------------------------------------------------
    // v6 -> v7: Notices and Timing offsetX is RE-ANCHORED on the screen centre.
    //
    // Those two stored offsetX as a DELTA from their computed centre while the Gap Bar
    // and Version stored the CENTRE itself -- one key, two meanings across four panels
    // that sit in a column and are meant to line up. v7 unifies on the centre, so an
    // existing file's delta has to gain the 0.5 or every upgrading user's Notices and
    // Timing jump half a screen to the left.
    //
    // Asserted on the re-saved file rather than on live geometry: this is a question
    // about a STORED value, and reading it back through a rendered panel would also be
    // reading the layout change that motivated it.
    {
        auto storedOffsetX = [](const ini::Map& m, const char* hud) {
            // ANY of the HUD's sections, for the reason the gamepad phase spells out:
            // the base block is written from the factory-default snapshot, so a value
            // that differs from the default appears only in the profile section that
            // diverges from it. Prefixed match on "<hud>:" plus the bare name.
            std::vector<double> found;
            for (const auto& kv : m) {
                const std::string& sec = kv.first.first;
                if (sec != hud && sec.rfind(std::string(hud) + ":", 0) != 0) continue;
                if (kv.first.second != "offsetX") continue;
                found.push_back(std::stod(kv.second));
            }
            return found;
        };
        auto sawValue = [](const std::vector<double>& v, double want) {
            for (double d : v) if (std::abs(d - want) < 1e-4) return true;
            return false;
        };

        // A user who had dragged Notices a tenth of a screen LEFT of centre and left
        // Timing where it was. Under the old meaning those render at 0.4 and 0.5.
        const std::string v6 =
            "[Settings]\nversion=6\n\n"
            "[NoticesHud]\noffsetX=-0.100000\n\n"
            "[TimingHud]\noffsetX=0.000000\n";
        const ini::Map out = roundTrip(host, saveWin, iniPath, v6);

        const auto notices = storedOffsetX(out, "NoticesHud");
        const auto timing  = storedOffsetX(out, "TimingHud");
        REQUIRE_MESSAGE(!notices.empty(), "no [NoticesHud] offsetX in the re-saved file");
        REQUIRE_MESSAGE(!timing.empty(),  "no [TimingHud] offsetX in the re-saved file");
        INFO("notices offsetX values in the re-saved file: " << notices.size());
        CHECK(sawValue(notices, 0.4));   // the delta, re-anchored: the panel did not move
        CHECK(sawValue(timing,  0.5));   // dead centre, which is now what 0.5 MEANS

        // The BASE section specifically must carry the shifted value too. The file's
        // [NoticesHud] block is folded into m_hudDefaults during the parse, and
        // m_hudDefaults is what the writer emits as the base block -- with every
        // profile section diffed against it. A migration that shifts only the
        // profile caches leaves an OLD-SEMANTICS number in the base block forever
        // (a landmine for the hand-editor who deletes a profile override to
        // "revert") and pins an explicit offsetX override into every profile, so
        // no future default can ever reach an upgraded user. Red before the
        // m_hudDefaults pass was added to the v7 block; green after.
        {
            bool baseShifted = false;
            for (const auto& kv : out) {
                if (kv.first.first == "NoticesHud" && kv.first.second == "offsetX" &&
                    std::abs(std::stod(kv.second) - 0.4) < 1e-4) {
                    baseShifted = true;
                }
            }
            CHECK_MESSAGE(baseShifted,
                          "base [NoticesHud] offsetX still carries the pre-v7 delta");
        }

        // AND IT RUNS ONCE. The re-saved file is stamped v7, so loading it again must
        // leave the value alone -- a migration that re-applies walks the panel 0.5 per
        // launch, which is the failure mode a version gate exists to prevent and the
        // one an in-place value rewrite makes easy to get wrong.
        const ini::Map again = roundTrip(host, saveWin, iniPath, ini::readFile(iniPath));
        CHECK(sawValue(storedOffsetX(again, "NoticesHud"), 0.4));
        CHECK(sawValue(storedOffsetX(again, "TimingHud"),  0.5));
    }

    // ------------------------------------------------------------------------
    // v7 -> v8: the Radar's offsetX is RE-ANCHORED on its centre.
    //
    // Same destination as v7, different arithmetic, which is why it is its own version:
    // Notices and Timing stored a DELTA from a centre, so re-anchoring them was + 0.5
    // and needed no geometry. The Radar stored a LEFT EDGE, so the shift is half the
    // panel's width and depends on the scale in the same section.
    //
    // The shipped default was 0.43275f, and that number is 0.5 minus exactly half the
    // dial's content width -- so an untouched radar must land on 0.5 EXACTLY. Anything
    // else means the migration is not using the width the old default was built from,
    // and every upgrading user's radar moves.
    {
        auto storedOffsetX = [](const ini::Map& m, const char* hud) {
            std::vector<double> found;
            for (const auto& kv : m) {
                const std::string& sec = kv.first.first;
                if (sec != hud && sec.rfind(std::string(hud) + ":", 0) != 0) continue;
                if (kv.first.second != "offsetX") continue;
                found.push_back(std::stod(kv.second));
            }
            return found;
        };
        auto sawValue = [](const std::vector<double>& v, double want) {
            for (double d : v) if (std::abs(d - want) < 1e-4) return true;
            return false;
        };

        const std::string v7 =
            "[Settings]\nversion=7\n\n"
            "[RadarHud]\noffsetX=0.432750\nscale=1.000000\n";
        const ini::Map out = roundTrip(host, saveWin, iniPath, v7);
        const auto radar = storedOffsetX(out, "RadarHud");
        REQUIRE_MESSAGE(!radar.empty(), "no [RadarHud] offsetX in the re-saved file");
        CHECK(sawValue(radar, 0.5));

        // The base block too, for the reason the v7 phase spells out: it is written
        // from m_hudDefaults, which the file's [RadarHud] section was folded into.
        {
            bool baseShifted = false;
            for (const auto& kv : out) {
                if (kv.first.first == "RadarHud" && kv.first.second == "offsetX" &&
                    std::abs(std::stod(kv.second) - 0.5) < 1e-4) {
                    baseShifted = true;
                }
            }
            CHECK_MESSAGE(baseShifted,
                          "base [RadarHud] offsetX still carries the pre-v8 left edge");
        }

        // Runs once: the re-saved file is stamped v8, so loading it again must leave
        // the value alone rather than shift it half a panel further right each launch.
        const ini::Map again = roundTrip(host, saveWin, iniPath, ini::readFile(iniPath));
        CHECK(sawValue(storedOffsetX(again, "RadarHud"), 0.5));
    }

    // ------------------------------------------------------------------------
    // A per-HUD theme override can be CLEARED back to "follow Appearance".
    //
    // The theme key used to be captured only when non-empty, and an absent profile key
    // cannot override a present base one. GamepadWidget and RadarHud reset to THEME_NONE
    // (they opt out of theming), so their base section carries theme=none and choosing
    // "Default" in the menu never stuck -- the base value was re-applied on every load.
    // Reachable from the settings menu alone.
    {
        const std::string pinned =
            "[Settings]\nversion=6\n\n"
            "[RadarHud]\ntheme=none\n";
        roundTrip(host, saveWin, iniPath, pinned);
        CHECK(host.radarTheme() == "none");

        // Back to Default, the way the menu does it, then persist and reload.
        host.setRadarTheme("");
        host.save();
        host.loadSettings(saveWin);
        CHECK(host.radarTheme() == "");
    }

    // ------------------------------------------------------------------------
    // v8 -> v9: the row-pitch DEFAULT moves, conservatively.
    //
    // The writer emits uiLineHeight into every INI whether or not anyone chose it,
    // so without a migration nobody would ever see the new default -- the stored
    // 1.17335 would keep winning forever. The rule is the v5 one: move ONLY the
    // value that IS the old default (no user intent), leave anything chosen alone.
    // Asserted on the re-saved file; [Advanced] applies straight into the live
    // LayoutConfig, so the file is where the outcome is observable.
    //
    // LAST phase on purpose: the chosen-pitch case leaves the live ratio at 1.3,
    // and every earlier phase's geometry assertions assume the default lattice.
    {
        auto storedLineHeight = [](const ini::Map& m) -> double {
            for (const auto& kv : m) {
                if (kv.first.first == "Advanced" && kv.first.second == "uiLineHeight") {
                    return std::stod(kv.second);   // stops at the inline " ; " comment
                }
            }
            return -1.0;
        };

        // The old default carries no user intent: it follows the new default.
        const std::string v8def =
            "[Settings]\nversion=8\n\n[Advanced]\nuiLineHeight=1.17335\n";
        CHECK(std::abs(storedLineHeight(roundTrip(host, saveWin, iniPath, v8def)) - 1.1) < 1e-4);

        // A pitch the user chose is left alone.
        const std::string v8chosen =
            "[Settings]\nversion=8\n\n[Advanced]\nuiLineHeight=1.300000\n";
        CHECK(std::abs(storedLineHeight(roundTrip(host, saveWin, iniPath, v8chosen)) - 1.3) < 1e-4);

        // Runs once: the re-saved file is stamped v9, so loading it again must not
        // touch the chosen value.
        const ini::Map again = roundTrip(host, saveWin, iniPath, ini::readFile(iniPath));
        CHECK(std::abs(storedLineHeight(again) - 1.3) < 1e-4);
    }

    host.shutdown();
}
