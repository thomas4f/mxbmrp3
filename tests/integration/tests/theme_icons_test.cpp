// ============================================================================
// tests/integration/tests/theme_icons_test.cpp
// THEME ICON OVERRIDES survive a theme SWITCH.
//
// WHAT IS AND IS NOT HERE. The resolution arithmetic is pure and lives in
// tests/unit/test_icon_resolve.cpp -- it needs no plugin at all. What only the real
// plugin can answer is whether the resolution notices a theme CHANGE: AssetManager
// memoises the active theme against themeGeneration(), for the same measured reason
// BaseHud does (one HUD rebuild resolves it dozens of times), and a memoised pointer
// that outlives its theme is the classic way this goes wrong -- icons keep coming
// from a theme the user just switched off.
//
// The overrides are INJECTED rather than loaded: the harness stages no icons/
// directory, so there is no base vocabulary to key a real override to, and staging
// one would renumber every sprite index the parity goldens hash. Reading
// themes/<name>/icons/ off disk stays a manual check.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("theme icon overrides follow the selected theme") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasIconTheming(),
                    "MXBMRP3_Test_SetThemeIconOverride not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_icons\\");

    // Sprite indices far past anything an install registers, so a resolution that
    // quietly fell through to the base set is distinguishable from one that worked.
    constexpr int OVERRIDE_A = 900001;
    constexpr int OVERRIDE_B = 900002;
    constexpr int SHAPE = 7;

    const int unthemed = host.iconSpriteForName("hud-map");

    SUBCASE("the active theme's override is what resolves") {
        host.installTheme("ticons", 3.0f, 1.0f, /*titleBand=*/1, /*card=*/1);
        REQUIRE(host.setThemeIcon("hud-map", OVERRIDE_A, SHAPE));
        host.draw();

        CHECK(host.iconSpriteForName("hud-map") == OVERRIDE_A);
        // Backward too: the override sprite maps to the shape it stands for, which is
        // what keeps shouldRotate() and the shape pickers working under a theme.
        CHECK(host.shapeForIconSprite(OVERRIDE_A) == SHAPE);
        // A name the theme does not carry still falls through to the base set.
        CHECK(host.iconSpriteForName("hud-timing") != OVERRIDE_A);

        host.clearTheme();
    }

    SUBCASE("switching themes switches the icons with them") {
        // THE MEMOISATION CASE. Two themes, each with its own override for the same
        // name: the second must win the moment it is selected, and the first must
        // stop applying. A stale cached theme pointer passes every other assertion in
        // this file and fails only here.
        host.installTheme("tfirst", 3.0f, 1.0f, 1, 1);
        REQUIRE(host.setThemeIcon("hud-map", OVERRIDE_A, SHAPE));
        host.draw();
        REQUIRE(host.iconSpriteForName("hud-map") == OVERRIDE_A);

        host.installTheme("tsecond", 3.0f, 1.0f, 1, 1);
        REQUIRE(host.setThemeIcon("hud-map", OVERRIDE_B, SHAPE));
        host.draw();
        CHECK(host.iconSpriteForName("hud-map") == OVERRIDE_B);
        CHECK(host.shapeForIconSprite(OVERRIDE_B) == SHAPE);

        host.clearTheme();
    }

    SUBCASE("clearing the theme puts the base icon back") {
        host.installTheme("trevert", 3.0f, 1.0f, 1, 1);
        REQUIRE(host.setThemeIcon("hud-map", OVERRIDE_A, SHAPE));
        host.draw();
        REQUIRE(host.iconSpriteForName("hud-map") == OVERRIDE_A);

        host.clearTheme();
        host.draw();
        CHECK(host.iconSpriteForName("hud-map") == unthemed);
        // And the override sprite is nobody's shape once its theme is gone.
        CHECK(host.shapeForIconSprite(OVERRIDE_A) == 0);
    }
}
