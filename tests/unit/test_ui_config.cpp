// ============================================================================
// tests/unit/test_ui_config.cpp
// Guards the INI-only grid-overlay settings on UiConfig: the factory defaults
// (off, every-10th, the two default colors) and the majorEvery clamp that keeps
// a hand-edited INI from feeding a zero/negative/huge stride into the grid
// drawer (a 0 stride would make every line "major"; the drawer does i % every).
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/ui_config.h"

TEST_CASE("UiConfig: grid overlay factory defaults") {
    UiConfig& ui = UiConfig::getInstance();
    ui.resetToDefaults();

    CHECK(ui.getGridOverlay() == false);          // off by default (debug aid)
    CHECK(ui.getGridOverlayMajorEvery() == 10);   // emphasize every 10th line
    CHECK(ui.getGridOverlayColor() == 0x22FFFFFFul);
    CHECK(ui.getGridOverlayMajorColor() == 0x9933CCFFul);
}

TEST_CASE("UiConfig: gridOverlayMajorEvery is clamped to [1, 1000]") {
    UiConfig& ui = UiConfig::getInstance();

    // A hand-edited 0 (or negative) must not reach the drawer's `i % every`.
    ui.setGridOverlayMajorEvery(0);
    CHECK(ui.getGridOverlayMajorEvery() == 1);
    ui.setGridOverlayMajorEvery(-5);
    CHECK(ui.getGridOverlayMajorEvery() == 1);

    // Absurdly large strides are capped (every line would be minor otherwise —
    // harmless, but the clamp keeps the stored value sane).
    ui.setGridOverlayMajorEvery(1000000);
    CHECK(ui.getGridOverlayMajorEvery() == 1000);

    // In-range values pass through untouched.
    ui.setGridOverlayMajorEvery(5);
    CHECK(ui.getGridOverlayMajorEvery() == 5);

    ui.resetToDefaults();  // leave the singleton at defaults for other TUs
}
