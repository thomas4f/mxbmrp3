// ============================================================================
// tests/integration/tests/settings_sections_test.cpp
// Guards against a capture/serialize divergence in the settings layer.
//
// Historically, adding a HUD meant editing three parallel hardcoded lists —
// captureToCache(), applyProfile(), and serializeSettings()'s section-order list —
// and forgetting the last one silently dropped the HUD's settings on restart (the
// FriendsHud bug). Those three lists are now ONE: the per-HUD serializer registry
// (settings_hud_registry), iterated by all three. So this divergence is structurally
// prevented rather than merely caught.
//
// This test remains a belt-and-suspenders check on the machinery end to end: it asks
// the plugin which sections captureToCache() produces (MXBMRP3_Test_CapturedSections)
// and asserts every one appears as a base [Section] in the settings.ini the plugin
// actually wrote — catching e.g. a registered section whose buildHudSection() emits
// nothing, or a future refactor that reintroduces a second list.
//
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <set>
#include <string>

TEST_CASE("settings sections: every captured section is serialized to the INI") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_sections\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_sections\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();  // guarantee a default INI on disk

    const std::vector<std::string> captured = host.capturedSections();
    REQUIRE_MESSAGE(!captured.empty(),
                    "MXBMRP3_Test_CapturedSections returned nothing "
                    "(hook missing from this build?)");

    // Collect the base [Section] names actually written to disk. Profile overrides
    // are "[HudName:Profile]" — skip anything with a ':' so we compare against the
    // base sections serializeSettings' order list emits.
    const std::string text = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!text.empty(), "no settings.ini written at " << iniPath);
    const ini::Map parsed = ini::parse(text);
    std::set<std::string> baseSections;
    for (const auto& kv : parsed) {
        const std::string& section = kv.first.first;
        if (section.find(':') == std::string::npos) baseSections.insert(section);
    }

    // Every section captureToCache() produces must have been serialized. A HUD
    // captured/applied but missing from the serialize order list would fail here.
    for (const std::string& section : captured) {
        CHECK_MESSAGE(baseSections.count(section) == 1,
                      "captured section '" << section
                      << "' was NOT written to the INI — it is missing from "
                         "serializeSettings' section-order list (the third "
                         "hardcoded list; see CLAUDE.md 'Adding a New HUD' step 6)");
    }

    host.shutdown();
}
