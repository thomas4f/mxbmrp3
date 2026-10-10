// ============================================================================
// tests/integration/tests/settings_malformed_test.cpp
// One malformed INI value must fail ALONE: the keys after it still apply.
//
// THE BUG THIS PINS. Every per-HUD applier in settings_hud_registry.cpp used to
// parse its keys with bare `std::stoi(settings.at(key))` inside ONE try/catch
// around the whole HUD. Hand-editing the INI is a supported workflow (CLAUDE.md),
// and a single `rotateToPlayer=yes` threw out of the middle of the Map applier,
// so every Map key the applier had not reached yet -- zoom, markers, anchor --
// was silently left at its default. One log line said "Failed to parse
// settings"; nothing said which key, and nothing said that the rest of the
// panel had been reset along with it.
//
// The appliers now read each key through the readers in settings_serde.h
// (readInt / readBool / readFloat / readStr), which log the offending key and
// value and return nothing for it, so ONLY that setting is left as it was --
// the factory default on a fresh load like this one; on a profile switch, the
// previous profile's value (the three Map floats that always stated an explicit
// default for a bad value keep doing so, via readFloat's fallback overload).
// Deliberately NOT "reset the whole HUD, so the user notices": with auto_save
// on the next save would persist those collateral defaults over the user's
// file, turning a one-character typo into the loss of the whole panel's setup.
//
// The probe: write a profile section whose FIRST Map key (in applier order) is
// garbage and whose LATER key turns zoom on, load it, and read the zoom flag
// back through MXBMRP3_Test_MapZoomEnabled. Zoom defaults off, so it can only
// read on if the applier survived the bad key. The reader semantics themselves
// (what counts as malformed) are the unit half in tests/unit/test_settings_serde.cpp.
//
// The second case is the same probe through settings_hud_registry_widgets.cpp,
// which kept its 30 bare std::sto* for a release after the full HUDs moved to
// the readers -- so the guarantee held for the Map and not for the Gap Bar
// next to it. It reads the applied value back the way the click tests do:
// save, then the saved INI, since widgets expose no typed getter hooks.
//
// The third case is the neighbour of that applier's readers: its legacy
// showMarkers migration is an `else` off the markerMode read, and a key
// inserted between the two (reference) once re-parented it, so a file
// carrying markerMode next to a stale showMarkers, and no reference key yet
// -- a hand-edit of a pre-Reference file, since a save rewrites the section
// -- had its mode overwritten with GHOST.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

TEST_CASE("a malformed INI value defaults that key alone; later keys still apply") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();                                   // default INI on disk: activeProfile=0 (Practice)

    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini at " << iniPath);
    REQUIRE(host.mapZoomEnabled());                // the probe key's factory default is on (Follow)

    // Applier order in app_MapHud: rotateToPlayer is read before zoomEnabled, so
    // a throw on the first used to skip the second.
    ini::writeFile(iniPath, defText + "\n"
                   "[MapHud:Practice]\n"
                   "rotateToPlayer=yes\n"
                   "zoomEnabled=0\n");
    host.loadSettings(saveWin);

    CHECK_MESSAGE(!host.mapZoomEnabled(),
                  "zoomEnabled=0 was not applied: a malformed key earlier in the "
                  "Map section abandoned the rest of the applier");

    // The control: the same section with the bad line removed must give the same
    // answer, so the assertion above is about surviving the bad key and not about
    // profile sections being applied at all.
    host.resetAll();
    REQUIRE(host.mapZoomEnabled());
    ini::writeFile(iniPath, defText + "\n[MapHud:Practice]\nzoomEnabled=0\n");
    host.loadSettings(saveWin);
    CHECK_FALSE(host.mapZoomEnabled());
}

TEST_CASE("a malformed widget INI value defaults that key alone; later keys still apply") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.save());
    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini at " << iniPath);
    // The probe value is not the default, so it can only read back if applied.
    const ini::Map defaults = ini::parse(defText);
    const auto def = defaults.find({"GapBarHud", "barWidth"});
    REQUIRE(def != defaults.end());
    REQUIRE(def->second != "200");

    // Applier order in app_GapBarHud: freezeDuration is read before barWidth, so
    // a throw on the first used to skip the second.
    ini::writeFile(iniPath, defText + "\n"
                   "[GapBarHud:Practice]\n"
                   "freezeDuration=yes\n"
                   "barWidth=200\n");
    host.loadSettings(saveWin);
    REQUIRE(host.save());
    const ini::Map saved = ini::parse(ini::readFile(iniPath));
    const auto width = saved.find({"GapBarHud:Practice", "barWidth"});
    REQUIRE_MESSAGE(width != saved.end(),
                    "barWidth=200 was not applied: a malformed key earlier in the "
                    "Gap Bar section abandoned the rest of the applier");
    CHECK(width->second == "200");
}

TEST_CASE("a legacy showMarkers key next to markerMode does not override it") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_malformed\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.save());
    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini at " << iniPath);

    // An INI from before the Gap Bar had a Reference setting: no `reference` key
    // anywhere, which is what let the re-parented else fire. The per-profile
    // cache is seeded with the base section's keys, so the key has to go from
    // the base section, not just be left out of the profile's.
    std::string preReference = defText;
    const size_t sec = preReference.find("[GapBarHud]");
    REQUIRE(sec != std::string::npos);
    const size_t ref = preReference.find("reference=", sec);
    REQUIRE(ref != std::string::npos);
    preReference.erase(ref, preReference.find('\n', ref) + 1 - ref);
    // markerMode 3 is Off, not the default; the legacy migration writes GHOST
    // (0), which IS the default and so would not be written back at all.
    ini::writeFile(iniPath, preReference + "\n"
                   "[GapBarHud:Practice]\n"
                   "markerMode=3\n"
                   "showMarkers=1\n");
    host.loadSettings(saveWin);
    REQUIRE(host.save());
    const ini::Map saved = ini::parse(ini::readFile(iniPath));
    const auto mode = saved.find({"GapBarHud:Practice", "markerMode"});
    const std::string got = (mode == saved.end()) ? "absent, i.e. the default" : mode->second;
    CHECK_MESSAGE(got == "3",
                  "markerMode=3 was overwritten by the legacy showMarkers migration (got " << got << ")");

    // The migration's own outcome (GHOST) is the factory default, so it is not
    // observable through a save; this case pins the regression only.
}
