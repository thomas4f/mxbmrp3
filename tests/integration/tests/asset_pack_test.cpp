// ============================================================================
// tests/integration/tests/asset_pack_test.cpp
// AN ASSET PACK IS SELECTED BY NAME, and an unknown name degrades without
// forgetting. Two pack types, one rule: gamepad pads and pit boards.
//
// WHY THIS EXISTS. The pad used to be chosen by TEXTURE VARIANT INDEX -- _1 was
// the Xbox art, _2 the DualShock -- which is the bug this whole subsystem was
// reorganised to remove: an index into discovery order silently reassigns every
// user's selection the moment a pack is added, removed or renamed. Themes and
// icon overrides already stored names; the gamepad was the outlier.
//
// The rule that replaced it has TWO halves, and only the first is obvious:
//
//   1. an unresolvable pack name must still RENDER something -- the shipped
//      default -- rather than leaving an empty panel;
//   2. it must NOT rewrite what is stored. A user who moves a pack folder out
//      and back must get their pad back, not silently inherit the default. A
//      "helpful" normalisation on load is the natural way to write this and it
//      destroys the user's choice on the first launch after they tidy a folder.
//
// The two hooks exist to tell those apart: gamepadPackStored() is what a save
// would write, gamepadPackActive() is what the widget draws. They are EQUAL in
// every ordinary case, which is precisely why a test that only checked one would
// pass while half the rule was missing.
//
// NO PACK FILES. discoverGamepads() wants a directory of 17 .tga; the selection
// logic wants a name. installGamepad() adds one directly, the same trick
// theme_geometry_test.cpp uses and for the same reason -- staging real assets
// into the build dir corrupts an unrelated golden.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("gamepad pack: a known name resolves to itself") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    // The AssetManager singleton outlives a case, so start from a known set
    // rather than inheriting whatever an earlier case installed.
    host.clearGamepads();
    host.installGamepad("xbox", 750.0f);
    host.installGamepad("ds4", 806.0f);

    host.setGamepadPack("ds4");
    CHECK(host.gamepadPackStored() == "ds4");
    CHECK(host.gamepadPackActive() == "ds4");

    host.setGamepadPack("xbox");
    CHECK(host.gamepadPackStored() == "xbox");
    CHECK(host.gamepadPackActive() == "xbox");
}

TEST_CASE("gamepad pack: an unknown name degrades to the default but is remembered") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    // The AssetManager singleton outlives a case, so start from a known set
    // rather than inheriting whatever an earlier case installed.
    host.clearGamepads();
    host.installGamepad("xbox", 750.0f);
    host.installGamepad("ds4", 806.0f);

    // The user picked a pack, then removed its folder.
    host.setGamepadPack("someone-elses-pad");

    // Half 1: it renders. "xbox" is AssetManager::DEFAULT_GAMEPAD.
    CHECK(host.gamepadPackActive() == "xbox");

    // Half 2: and their choice survives. This is the assertion a normalise-on-load
    // implementation fails -- it would report "xbox" here and the setting would be
    // gone for good on the next save.
    CHECK(host.gamepadPackStored() == "someone-elses-pad");

    // Putting the folder back restores the pad, with no re-picking.
    host.installGamepad("someone-elses-pad", 900.0f);
    CHECK(host.gamepadPackActive() == "someone-elses-pad");
    CHECK(host.gamepadPackStored() == "someone-elses-pad");
}

TEST_CASE("gamepad pack: the default survives the shipped pack being absent") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    // The AssetManager singleton outlives a case, so start from a known set
    // rather than inheriting whatever an earlier case installed.
    host.clearGamepads();
    // No "xbox" at all -- only a third-party pad is installed. getDefaultGamepad()
    // falls back to the first discovered pack rather than to nullptr, so the widget
    // still draws a controller instead of an empty frame.
    host.installGamepad("gamecube", 700.0f);

    host.setGamepadPack("xbox");
    CHECK(host.gamepadPackActive() == "gamecube");
    CHECK(host.gamepadPackStored() == "xbox");
}

TEST_CASE("gamepad pack: no packs installed resolves to nothing, and does not crash") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    // The AssetManager singleton outlives a case, so start from a known set
    // rather than inheriting whatever an earlier case installed.
    host.clearGamepads();

    // A stripped install. The widget must cope: activePack() is null, every sprite
    // lookup returns 0, and the draw helpers fall back to their solid-colour shapes.
    host.setGamepadPack("xbox");
    CHECK(host.gamepadPackActive().empty());
    CHECK(host.gamepadPackStored() == "xbox");

    // Rendering with no pack is the case that would dereference a null pack.
    host.fakeGamepad(true);
    host.draw();
    CHECK(host.gamepadPackActive().empty());
}

// ============================================================================
// PIT BOARDS. Identical selection rule, so these cases are the gamepad ones
// restated -- deliberately, because the rule is what must hold for BOTH, and a
// pack type that only half-implements it is exactly the regression worth
// catching.
//
// The board adds one thing the pad did not: the panel's SHAPE comes from the
// pack. Its aspect used to be a compiled 1920/1080, so a board drawn at any
// other shape was stretched with no way to fix it.
// ============================================================================

TEST_CASE("pitboard pack: a known name resolves to itself") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    host.clearPitboards();
    host.installPitboard("classic", 1920.0f, 1080.0f);
    host.installPitboard("wide", 2560.0f, 1080.0f);

    host.setPitboardPack("wide");
    CHECK(host.pitboardPackStored() == "wide");
    CHECK(host.pitboardPackActive() == "wide");

    host.setPitboardPack("classic");
    CHECK(host.pitboardPackStored() == "classic");
    CHECK(host.pitboardPackActive() == "classic");
}

TEST_CASE("pitboard pack: an unknown name degrades to the default but is remembered") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    host.clearPitboards();
    host.installPitboard("classic", 1920.0f, 1080.0f);

    host.setPitboardPack("someone-elses-board");

    // Renders as the shipped board ("classic" is AssetManager::DEFAULT_PITBOARD)...
    CHECK(host.pitboardPackActive() == "classic");
    // ...without discarding what the user chose.
    CHECK(host.pitboardPackStored() == "someone-elses-board");

    host.installPitboard("someone-elses-board", 1600.0f, 900.0f);
    CHECK(host.pitboardPackActive() == "someone-elses-board");
    CHECK(host.pitboardPackStored() == "someone-elses-board");
}

TEST_CASE("pitboard pack: no packs installed resolves to nothing, and does not crash") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");
    host.clearPitboards();

    host.setPitboardPack("classic");
    CHECK(host.pitboardPackActive().empty());
    CHECK(host.pitboardPackStored() == "classic");

    // Rendering with no pack is the case that would dereference a null pack --
    // and the pitboard dereferences it for its WIDTH, before any sprite lookup.
    host.draw();
    CHECK(host.pitboardPackActive().empty());
}

// ============================================================================
// THE PACK CYCLE MUST BE ABLE TO TURN THE ART BACK ON.
//
// THE BUG THIS PINS, reported in-game and reproduced from a screenshot. The pack
// cycle replaced each HUD's texture-variant cycle, and I dropped its "Off" entry
// on the reasoning that on/off belongs to showBackgroundTexture "like every other
// widget". That reasoning was backwards: for every other widget the texture cycle
// IS the control for that flag -- setTextureVariant(0) is literally what clears
// it, and nothing else in the plugin writes it from the UI. So dropping Off
// deleted the only way to switch the art back on, and any user whose flag was
// already false (e.g. Texture left on Off under the previous build, which
// persisted showBackgroundTexture=0) got a black panel with the buttons floating
// on it and no way back.
//
// WHY NOTHING CAUGHT IT. The widget state was never wrong -- set the flag and it
// renders correctly, which is exactly what the headless captures showed. The
// defect was UI REACHABILITY: no control could reach the state. A test that drives
// only the widget's own setters cannot see that, so these cases drive the SETTINGS
// CYCLE itself (MXBMRP3_Test_CyclePack) and assert what a user can get back to.
//
// The rule, for both pack types: Off is a position IN the cycle, so stepping is
// always able to leave it -- including when only one pack is installed, which is
// the case the first fix still got wrong by disabling the control below two packs.
// ============================================================================

// THE INVARIANT SURVIVED ITS MECHANISM. The stranding above was fixed by making Off
// a position IN the cycle, so stepping could always leave it. Off has since been
// removed outright (BaseHud::m_textureRequired -- the state it produced was the
// widget's contents floating on an empty panel), which satisfies the same rule more
// simply: there is no longer a state to be stranded in, and the setter refuses one
// left over in an existing INI.
//
// So these cases still guard "a user always ends up with visible artwork" -- they
// just assert it of a cycle that cannot reach Off, rather than of one that can leave
// it. The single-pack case is kept for its own reason, unchanged by any of this: with
// one pack there is nothing to cycle BETWEEN, which is what tempted the first fix
// into disabling the control.
static void checkPackCycleKeepsArtOn(PluginHost& host, bool pitboard, const char* what) {
    // Even asked directly, the art cannot be switched off.
    host.setPackShowBg(pitboard, false);
    CHECK_MESSAGE(host.packShowBg(pitboard), what << ": the art was switched off");

    // And a full lap in each direction never lands on an off state.
    for (int i = 0; i < 4; ++i) {
        host.cyclePack(pitboard, /*forward=*/true);
        CHECK_MESSAGE(host.packShowBg(pitboard), what << ": cycling forward turned the art off");
    }
    for (int i = 0; i < 4; ++i) {
        host.cyclePack(pitboard, /*forward=*/false);
        CHECK_MESSAGE(host.packShowBg(pitboard), what << ": cycling backward turned the art off");
    }
}

TEST_CASE("pack cycle: the art stays on, with several packs") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");

    host.clearGamepads();
    host.installGamepad("xbox", 750.0f);
    host.installGamepad("ds4", 806.0f);
    checkPackCycleKeepsArtOn(host, /*pitboard=*/false, "gamepad");

    host.clearPitboards();
    host.installPitboard("classic", 1920.0f, 1080.0f);
    host.installPitboard("wide", 2560.0f, 1080.0f);
    checkPackCycleKeepsArtOn(host, /*pitboard=*/true, "pitboard");
}

TEST_CASE("pack cycle: a SINGLE installed pack still cycles") {
    // The case the first fix still got wrong. With one pack there is nothing to
    // cycle BETWEEN, so it is tempting to disable the control -- but the cycle is
    // [Off, pack], so disabling it is precisely what strands a user whose art is
    // off. One pack is also the shipping state for pitboards.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");

    host.clearGamepads();
    host.installGamepad("xbox", 750.0f);
    checkPackCycleKeepsArtOn(host, /*pitboard=*/false, "gamepad (one pack)");

    host.clearPitboards();
    host.installPitboard("classic", 1920.0f, 1080.0f);
    checkPackCycleKeepsArtOn(host, /*pitboard=*/true, "pitboard (one pack)");
}

TEST_CASE("pack cycle: the chosen pack survives a redundant show-art call") {
    // This case was "switching the art off does not forget which pack was chosen",
    // from when Off was a cycle position and had to be a display state rather than an
    // erasure. Off is gone (BaseHud::m_textureRequired), so the half that could lose
    // the name is gone with it -- what remains worth pinning is that the stored name
    // is independent of the show-art flag at all, which is what let Off be harmless
    // in the first place and what keeps a refused off-request harmless now.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");

    host.clearGamepads();
    host.installGamepad("xbox", 750.0f);
    host.installGamepad("ds4", 806.0f);

    host.setGamepadPack("ds4");
    host.setPackShowBg(false, false);
    CHECK(host.gamepadPackStored() == "ds4");

    host.setPackShowBg(false, true);
    CHECK(host.gamepadPackActive() == "ds4");
}

// ============================================================================
// RELOAD_CONFIG REACHES EVERY PACK TYPE.
//
// The hotkey re-reads each theme's and each pack's ini without re-discovering
// sprites, so authoring a pad or a board is an edit-and-look loop rather than an
// edit-and-restart one. README documents exactly that for both pack types.
//
// It reached pads and not boards. The two loops were written out by hand, side by
// side, and the board's was simply never written -- so RELOAD_CONFIG copied a
// board's ini into the plugin folder (the sync above it always ran) and then did
// not read it. Nothing failed: no test drove the reload path at all, and the one
// signal that should have caught it was a log line passing m_pitboards.size() to a
// format string with two conversions, where the extra argument went nowhere.
//
// HOW THIS SEES IT WITHOUT STAGING FILES. A synthetic pack (installPitboard /
// installGamepad) has a name and a geometry but no ini on disk. The reload resets
// each pack's geometry to the built-in default BEFORE re-reading -- so that a key
// deleted from a real ini goes back to the built-in value instead of persisting
// forever -- and with no file to read, the default is where it stays. So "the
// geometry moved back to its default" IS "the loop ran", observable with no
// filesystem work. Both types are asserted together: the pad is the one that
// always worked, and pairing them is what makes a future one-sided edit visible.
TEST_CASE("reload config: re-reads every pack type, not just gamepads") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asset_pack\\");

    // Art widths deliberately unlike the built-in defaults (1920 for a board,
    // 750 for a pad), so "reset to default" is unambiguous.
    host.clearPitboards();
    host.clearGamepads();
    host.installPitboard("reloadboard", 640.0f, 480.0f);
    host.installGamepad("reloadpad", 123.0f);
    host.setPitboardPack("reloadboard");
    host.setGamepadPack("reloadpad");

    REQUIRE(host.pitboardPackActive() == "reloadboard");
    REQUIRE(host.gamepadPackActive() == "reloadpad");
    // Precondition: the installed geometry is what the pack carries.
    CHECK(host.pitboardArtWidth() == doctest::Approx(640.0f));
    CHECK(host.gamepadArtWidth() == doctest::Approx(123.0f));

    host.reloadAssetLayouts();

    // The pad: this always passed, and is here as the control.
    CHECK(host.gamepadArtWidth() != doctest::Approx(123.0f));
    // The board: this is the regression. Before the fix the reload never touched
    // m_pitboards, so the art width stayed at the installed 640.
    CHECK(host.pitboardArtWidth() != doctest::Approx(640.0f));
    CHECK(host.pitboardArtWidth() == doctest::Approx(1920.0f));

    host.clearPitboards();
    host.clearGamepads();
}
