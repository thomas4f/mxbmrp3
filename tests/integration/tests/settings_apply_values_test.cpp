// ============================================================================
// tests/integration/tests/settings_apply_values_test.cpp
// Apply-path coverage for NON-DEFAULT enum / float / int values.
//
// The idempotency test round-trips DEFAULT values only, so an app_* branch that
// ignored a loaded value but left the (identical) live default would still pass
// it. This test closes that blind spot: it loads profile-OVERRIDE values that
// differ from the base defaults and asserts they survive a load -> save round
// trip. That round trip only preserves an override if applyProfile() actually
// applied the loaded value to the live HUD (the active profile's overrides are
// re-captured FROM the live HUD on save — a sparse diff vs base). If app_<Hud>
// mis-parsed the enum, clamped/dropped the float/int, or ignored the key, the
// live HUD keeps the base default, capture writes no override, and the section
// vanishes here.
//
// Uses the same proven mechanism as reset_profile_test (base at factory + a
// [Hud:Practice] override that differs), but with enum/float/int anchors instead
// of booleans — the value types the registry's app_* parse paths (stringToX /
// validateX / std::stoi) that no other test exercises with non-default data.
// Practice (index 0) is the default active profile. Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <string>
#include <vector>

namespace {
struct Anchor { const char* section; const char* key; const char* alt; };

// Non-default, in-range alternates. Enums must be valid stringToX outputs; the
// numerics sit inside their validateX clamp ranges; floats use the exact
// std::to_string formatting the capture side re-emits, so they compare equal.
const std::vector<Anchor> kAnchors = {
    { "StandingsHud", "gapMode",          "PLAYER"    },  // enum, base ALL
    { "StandingsHud", "gapReferenceMode", "LEADER"    },  // enum, base PLAYER
    { "StandingsHud", "animationMode",    "COLORED"   },  // enum, base BASIC
    { "StandingsHud", "displayRowCount",  "12"        },  // int,  base 10   (range 1..100)
    { "MapHud",       "riderColorMode",   "UNIFORM"   },  // enum, base RELATIVE_POS
    { "MapHud",       "labelMode",        "NONE"      },  // enum, base RACE_NUM
    { "MapHud",       "trackWidthScale",  "2.000000"  },  // float,base 1.0  (range 0.5..3.0)
    { "LapLogHud",    "maxDisplayLaps",   "8"         },  // int,  base 5    (range 1..30)
};
} // namespace

TEST_CASE("settings apply: non-default enum/float/int values round-trip via applyProfile") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_apply_values\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_apply_values\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();  // default INI: base sections at factory, activeProfile=0 (Practice)

    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini written at " << iniPath);
    const ini::Map D = ini::parse(defText);

    // Guard against a vacuous test: every anchor must exist in the base and its
    // alternate must actually differ from the base default.
    for (const Anchor& a : kAnchors) {
        const ini::Key base{ a.section, a.key };
        REQUIRE_MESSAGE(D.count(base), "base key missing: [" << a.section << "] " << a.key);
        REQUIRE_MESSAGE(D.at(base) != a.alt,
                        "alt equals base (vacuous) for [" << a.section << "] " << a.key
                        << " = " << a.alt);
    }

    // Append [Section:Practice] overrides carrying the alternates (grouped per
    // section so each header appears once), then load -> save -> re-read.
    std::string text = defText + "\n";
    std::string curSection;
    for (const Anchor& a : kAnchors) {
        if (a.section != curSection) {
            text += "[" + std::string(a.section) + ":Practice]\n";
            curSection = a.section;
        }
        text += std::string(a.key) + "=" + a.alt + "\n";
    }
    ini::writeFile(iniPath, text);
    host.loadSettings(saveWin);   // applyProfile(Practice) applies the overrides live
    host.save();                  // captures live Practice -> writes the sparse diff

    const ini::Map R = ini::parse(ini::readFile(iniPath));
    for (const Anchor& a : kAnchors) {
        const ini::Key ovr{ std::string(a.section) + ":Practice", a.key };
        const ini::Key base{ a.section, a.key };
        // Precompute the identity as a std::string so it prints correctly in the
        // failure message (streaming a bare const char* here mis-renders).
        const std::string id = "[" + std::string(a.section) + ":Practice] " + a.key;
        // Survived => app_<Hud> parsed the loaded value, applied it to the live HUD,
        // and capture read it back as a diff-vs-base override. Use CHECK (not REQUIRE)
        // so a failure still reaches host.shutdown() and the process exits cleanly
        // instead of hanging on the still-running HTTP-server thread; guard R.at()
        // on the missing-key path so it can't throw past the assertion.
        CHECK_MESSAGE(R.count(ovr) == 1,
                      "override lost for " << id << " — applyProfile did not apply the "
                      "loaded value (app_" << std::string(a.section) << " mis-parsed/ignored it)");
        if (R.count(ovr)) CHECK_MESSAGE(R.at(ovr) == a.alt, "wrong value for " << id);
        CHECK(R.at(base) == D.at(base));   // base default untouched
    }

    host.shutdown();
}
