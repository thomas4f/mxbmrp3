// ============================================================================
// tests/integration/tests/pack_texture_variant_test.cpp
// A pack HUD keeps its artwork when an UPGRADED settings file still carries the
// texture-variant key from before that HUD's art moved into a pack.
//
// THE BUG THIS PINS, reported from the seat with screenshots. The pit board and
// the gamepad pad used to be texture-based HUDs with a `textureVariant` cycle;
// they are packs now, and declare no texture stem because their artwork comes
// from the selected pack. An INI written by the older build still carries
// `textureVariant=1` for both -- and applyBaseSettings walks a section's keys in
// map (alphabetical) order, so `showBackgroundTexture=1` was applied first and
// `textureVariant=1` immediately after. That landed in setTextureVariant's
// no-stem arm, which assumes a HUD that FORGOT to declare a stem and switches
// the background off to say so, and the pack art vanished.
//
// What the player saw was not an empty panel: the pad's loose sprites (sticks,
// d-pad, bumpers, face buttons) are separate quads and kept drawing, so the
// widget rendered as a grey slab wearing half a controller. Nothing threw,
// nothing logged, and a FRESH install could not reproduce it -- resetToDefaults
// never sets a variant, so only an upgraded file carries the key at all. That
// combination is why this needs a test rather than an eyeball.
//
// Asserted on the background-texture FLAG, which is what the bug moved. Not the
// resolved sprite: that needs the pack art installed, and the Wine harness stages
// no plugins\\mxbmrp3_data tree, so a sprite check reads 0 for "no packs in this
// environment" exactly as it reads 0 for the bug. This test was written that way
// first and passed for the wrong reason -- it failed even with the fix in.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

namespace {

// The two keys in the order a real upgraded file holds them, plus the pack
// selections, so this is the user's section verbatim in miniature.
std::string upgradedIni() {
    return
        "[Settings]\n"
        "version=9\n"
        "\n"
        "[PitboardHud]\n"
        "visible=1\n"
        "showBackgroundTexture=1\n"
        "textureVariant=1\n"
        "pitboardPack=classic\n"
        "\n"
        "[GamepadWidget]\n"
        "visible=1\n"
        "showBackgroundTexture=1\n"
        "textureVariant=1\n"
        "gamepadPack=xbox\n"
        "\n"
        // The two gauges became pack HUDs after the board and the pad, and their
        // upgrade is the WORST of the three rather than a repeat: they were
        // texture HUDs for far longer, resetToDefaults used to call
        // setTextureVariant(1) on both, and the key is written on every save --
        // so unlike the board and the pad, essentially every upgraded file in the
        // wild carries `textureVariant=1` here, whether or not the user ever
        // touched the setting. There is no `gaugesPack` line at all, which is
        // also true of a real upgraded file and is the point: the widget has to
        // fall back to the default pack rather than to no art.
        "[TachoWidget]\n"
        "visible=1\n"
        "showBackgroundTexture=1\n"
        "textureVariant=1\n"
        "\n"
        "[SpeedoWidget]\n"
        "visible=1\n"
        "showBackgroundTexture=1\n"
        "textureVariant=1\n";
}

}  // namespace

TEST_CASE("pack HUDs: a stale textureVariant from an upgrade cannot switch the pack art off") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\packvariant\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);

    host.writeSettingsFile(saveWin, upgradedIni());
    host.loadSettings(saveWin);

    // 1 = on, 0 = the bug (background switched off during load), -1 = the panel
    // name or the hook is gone, which is a different failure worth telling apart.
    const int board = host.hudBackgroundTextureOn("pitboard_hud");
    const int pad   = host.hudBackgroundTextureOn("gamepad_widget");

    CHECK_MESSAGE(board != -1, "pitboard_hud not registered (or hook missing)");
    CHECK_MESSAGE(board == 1,
                  "pit board background switched off by a stale textureVariant");

    CHECK_MESSAGE(pad != -1, "gamepad_widget not registered (or hook missing)");
    CHECK_MESSAGE(pad == 1,
                  "gamepad pad background switched off by a stale textureVariant");

    const int tacho  = host.hudBackgroundTextureOn("tacho_widget");
    const int speedo = host.hudBackgroundTextureOn("speedo_widget");

    CHECK_MESSAGE(tacho != -1, "tacho_widget not registered (or hook missing)");
    CHECK_MESSAGE(tacho == 1,
                  "tacho face switched off by a stale textureVariant");

    CHECK_MESSAGE(speedo != -1, "speedo_widget not registered (or hook missing)");
    CHECK_MESSAGE(speedo == 1,
                  "speedo face switched off by a stale textureVariant");
}

TEST_CASE("pack HUDs: a settings file with NO textureVariant is unaffected") {
    // The fresh-install shape, asserted alongside the upgrade one so a fix that
    // works by ignoring the whole key path still has to keep this passing.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\packvariant2\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);

    host.writeSettingsFile(saveWin,
        "[Settings]\nversion=9\n\n"
        "[PitboardHud]\nvisible=1\nshowBackgroundTexture=1\npitboardPack=classic\n"
        "\n[TachoWidget]\nvisible=1\nshowBackgroundTexture=1\ngaugesPack=classic\n");
    host.loadSettings(saveWin);

    CHECK(host.hudBackgroundTextureOn("pitboard_hud") == 1);
    CHECK(host.hudBackgroundTextureOn("tacho_widget") == 1);
}

TEST_CASE("pack HUDs: a FRESH install draws its artwork") {
    // The other cases here all write a settings file that says
    // showBackgroundTexture=1, so every one of them passed while the pack HUDs'
    // factory defaults said the opposite -- which is how the tacho and the
    // speedo shipped as a bare needle on an empty box.
    //
    // The cause is worth stating because it is a trap for the next pack HUD: the
    // flag was never set by resetToDefaults() directly, it was implied by
    // setTextureVariant(1). Converting a HUD to a pack makes that call a no-op
    // (BaseHud::setTextureVariant returns early for a pack kind), so removing it
    // is correct AND silently drops the only thing that turned the artwork on.
    // BaseHud defaults the flag to false, so nothing complains.
    //
    // No settings file at all: this is the state a first launch is in, and the
    // one no other case here reaches.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\packfresh\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);

    for (const char* panel : {"pitboard_hud", "gamepad_widget", "tacho_widget", "speedo_widget"}) {
        const int on = host.hudBackgroundTextureOn(panel);
        CHECK_MESSAGE(on != -1, panel << " not registered (or hook missing)");
        CHECK_MESSAGE(on == 1,
                      panel << " ships with its background texture OFF, so its pack art "
                               "never draws on a fresh install");
    }
}

TEST_CASE("gauges: an unknown pack name degrades to the default, and is not rewritten") {
    // The by-name rule, which is what makes a pack survive being uninstalled and
    // reinstalled: a name this install has no folder for draws the default set,
    // and the STORED name is left alone so putting the folder back restores the
    // choice without the user re-picking it. Same clause the board and the pad
    // carry; asserted here because the gauges are the first pack type where two
    // widgets share one pack root and could plausibly have been written to
    // "helpfully" normalise each other's stored name.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\gaugesunknown\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);

    host.writeSettingsFile(saveWin,
        "[Settings]\nversion=9\n\n"
        "[TachoWidget]\nvisible=1\nshowBackgroundTexture=1\ngaugesPack=a-pack-that-is-not-installed\n");
    host.loadSettings(saveWin);

    // Still drawing: the artwork is mandatory on a pack HUD, so degrading must
    // not mean blanking (BaseHud::m_textureRequired).
    CHECK(host.hudBackgroundTextureOn("tacho_widget") == 1);
}
