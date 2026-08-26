// ============================================================================
// tests/integration/tests/theme_palette_test.cpp
// THE THREE-STEP PRECEDENCE for colours and fonts: built-in default -> THEME ->
// user override. And what "overridden" means, which is the part that broke.
//
// WHY THIS EXISTS. This surface had no test of any kind. The Appearance tab's
// colour and font rows build their click regions BY HAND rather than through
// SettingsLayoutContext, so MXBMRP3_Test_SettingsClickCycle cannot reach them, and
// ColorConfig/FontConfig do not link into the unit suite (they drag in the asset
// stack). So the only check was someone looking at the menu.
//
// THE BUG IT PINS, which shipped: when themes gained a palette, cycleColor() still
// wrote the slot array directly instead of going through setColor(). That skipped
// the override flag, so isOverridden() stayed false, so getColor() kept preferring
// the THEME's value -- and cycling a colour in Appearance did visibly nothing. The
// font path had the same shape. A user found it, not a test.
//
// The theme here is synthetic (no .tga files); see theme_geometry_test's header.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {
// ColorSlot::ACCENT / FontCategory::TITLE by index. Deliberately literals: the enums
// live in headers this harness does not include, and pinning the INDEX is also a
// (weak) guard that nobody reorders the slots without noticing.
constexpr int SLOT_ACCENT = 9;
constexpr int SLOT_PRIMARY = 0;
constexpr int CAT_TITLE   = 0;

// An ABGR value chosen not to collide with any palette entry, so "the theme's
// value" is unmistakable in a failure message.
constexpr unsigned long THEME_ACCENT = 0xFF123456u;
}  // namespace

TEST_CASE("colour precedence: built-in -> theme -> user") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasPaletteHooks(),
                    "MXBMRP3_Test_*Color hooks not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_palette\\");

    // --- Step 1: no theme, no override -> the built-in default ---------------
    host.clearTheme();
    host.clearColorOverride(SLOT_ACCENT);
    const unsigned long builtIn = host.effectiveColor(SLOT_ACCENT);
    CHECK(host.colorOverridden(SLOT_ACCENT) == 0);
    CHECK(host.themeOrDefaultColor(SLOT_ACCENT) == builtIn);

    // --- Step 2: a theme with an opinion wins over the built-in --------------
    // Insets in CELLS. They were 0.020/0.010 -- normalized-Y fractions from before the
    // unit changed, i.e. two hundredths of a cell. Inert here (this file asks about
    // colours, not geometry) but a copy-paste source for a case where it would not be.
    host.installTheme("pal", 3.0f, 1.0f, 1, 1);
    host.setThemeColor(SLOT_ACCENT, THEME_ACCENT);
    CHECK(host.themeOrDefaultColor(SLOT_ACCENT) == THEME_ACCENT);
    CHECK(host.effectiveColor(SLOT_ACCENT) == THEME_ACCENT);
    CHECK(host.colorOverridden(SLOT_ACCENT) == 0);   // inherited, not overridden

    // --- Step 3: the USER beats the theme ------------------------------------
    // THE REGRESSION. cycleColor() has to go through setColor() so the override
    // flag is set; writing the slot array directly leaves isOverridden() false and
    // getColor() keeps returning the theme's value -- cycling does nothing on
    // screen. Asserted as BOTH halves, because either alone still passes with the
    // bug present: the flag alone says nothing about what is drawn, and comparing
    // against the theme value alone would pass if cycling happened to land on it.
    host.cycleColor(SLOT_ACCENT, true);
    CHECK(host.colorOverridden(SLOT_ACCENT) == 1);
    CHECK(host.effectiveColor(SLOT_ACCENT) != THEME_ACCENT);
    const unsigned long userPick = host.effectiveColor(SLOT_ACCENT);

    // Cycling again moves again -- i.e. it steps through the palette from the
    // EFFECTIVE colour rather than restarting from a stale slot value.
    host.cycleColor(SLOT_ACCENT, true);
    CHECK(host.effectiveColor(SLOT_ACCENT) != userPick);

    // --- CYCLING BACK REACHES THE THEME'S COLOUR AGAIN -----------------------
    // THE SECOND BUG, reported from the game: "cycle away from a theme's custom
    // colour and there is no way back to it". The ring was the palette and nothing
    // else, and a theme's colour is almost never a palette entry -- so one press of
    // an arrow pinned the slot with no index to cycle back TO, and only the reset
    // that clears all ten could undo it.
    //
    // The ring now carries one more stop than the palette: Default, entered by
    // wrapping off either end, which un-pins the slot. Asserted by walking backwards
    // from a known position rather than by counting presses, so the palette can grow
    // without this needing a new number.
    host.clearColorOverride(SLOT_ACCENT);
    REQUIRE(host.effectiveColor(SLOT_ACCENT) == THEME_ACCENT);
    host.cycleColor(SLOT_ACCENT, true);          // Default -> first palette entry
    REQUIRE(host.colorOverridden(SLOT_ACCENT) == 1);
    host.cycleColor(SLOT_ACCENT, false);         // ...and straight back off the end
    CHECK(host.colorOverridden(SLOT_ACCENT) == 0);
    CHECK(host.effectiveColor(SLOT_ACCENT) == THEME_ACCENT);

    // The same stop is reachable going FORWARD, which is the direction a user who
    // has cycled past the colour they wanted will keep pressing.
    int guard = 0;
    do { host.cycleColor(SLOT_ACCENT, true); } while (host.colorOverridden(SLOT_ACCENT) && ++guard < 64);
    CHECK(guard < 64);
    CHECK(host.effectiveColor(SLOT_ACCENT) == THEME_ACCENT);

    // --- Clearing the override falls back to the theme, not the built-in -----
    // The direction that proves precedence is a chain and not a two-way switch.
    host.clearColorOverride(SLOT_ACCENT);
    CHECK(host.colorOverridden(SLOT_ACCENT) == 0);
    CHECK(host.effectiveColor(SLOT_ACCENT) == THEME_ACCENT);

    // --- ...and with the theme gone, back to the built-in --------------------
    host.clearTheme();
    CHECK(host.effectiveColor(SLOT_ACCENT) == builtIn);
}

TEST_CASE("font precedence behaves the same way") {
    // NOT via cycleFont(). That early-returns when AssetManager has discovered no
    // fonts, and the integration suite stages no .fnt files -- so cycling is a no-op
    // here and an assertion on it would be testing the empty environment, not the
    // code. (Found the honest way: this case first asserted the override flag after a
    // cycle and failed 0 == 1.)
    //
    // What DID regress is the precedence machinery underneath -- an effective value
    // that ignores the user because the override flag never got set -- and setFont()
    // reaches that without any assets. The stepping itself stays covered only by the
    // in-game menu; noted rather than faked.
    //
    // THE GAP GREW: cycleFont() now carries a "Default" stop (the ring has one more
    // position than the font list, entered by wrapping off either end, and it calls
    // clearOverride) so a pinned category can be un-pinned without the Appearance
    // reset -- matching ColorConfig::cycleColor, whose ring IS covered above. None
    // of that stepping is asserted anywhere, for the reason in the paragraph above.
    // Closing it means staging a couple of .fnt files into the discovery dir before
    // startup(); the fontCount() message below is what tells you whether that has
    // happened yet.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasPaletteHooks(), "font hooks not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_palette_font\\");

    host.clearTheme();
    host.clearFontOverride(CAT_TITLE);
    const std::string builtIn = host.effectiveFont(CAT_TITLE);
    CHECK(host.fontOverridden(CAT_TITLE) == 0);
    CHECK_FALSE(builtIn.empty());

    // A user pick must WIN and must register as an override -- the two halves that
    // came apart in the colour path.
    host.setFont(CAT_TITLE, "TestFace-Regular");
    CHECK(host.fontOverridden(CAT_TITLE) == 1);
    CHECK(host.effectiveFont(CAT_TITLE) == "TestFace-Regular");

    // ...and clearing it falls back.
    host.clearFontOverride(CAT_TITLE);
    CHECK(host.fontOverridden(CAT_TITLE) == 0);
    CHECK(host.effectiveFont(CAT_TITLE) == builtIn);

    // Documents the environment the case above works around, so a future reader can
    // see at a glance whether staging fonts would let cycleFont() be covered here.
    MESSAGE("fonts discovered in this environment: " << host.fontCount());
}

// ============================================================================
// ABSENCE IS AUTHORITATIVE: a slot the settings file does not mention must come
// back UNPINNED after a load, so it follows the theme again.
//
// [Colors] and [Fonts] are written SPARSELY -- only the slots the user pinned are
// emitted, and an absent key means "follow the theme". The load side did not match:
// applyGlobalLine() pins every key it sees and nothing released the ones it does
// not, so absence could never un-pin. The only clearOverride() calls in the load
// path lived inside the v4 -> v5 migration, which a current file skips entirely.
//
// The two user-visible failures, both supported workflows:
//   - change a colour with auto-save off, then press Reload Config to discard it:
//     the value stayed pinned and was written back out on the next save.
//   - delete `accent=...` from [Colors] by hand: the line reappeared.
//
// The theme half is what makes this more than tidiness -- a pinned slot can never
// follow a theme's palette again, which is the branch's headline feature dead for
// anyone who has ever touched that colour.
//
// Drives the REAL loadSettings() against a file written by hand, because the shape
// under test is a key's ABSENCE and no capture path can produce that on demand.
TEST_CASE("palette: a colour absent from the file is released, not left pinned") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasPaletteHooks(), "palette hooks not exported (test build?)");
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\palette_absence\\";
    host.startup(save);

    host.clearColorOverride(SLOT_ACCENT);
    REQUIRE(host.colorOverridden(SLOT_ACCENT) == 0);

    // Pin it the way the settings menu does.
    host.cycleColor(SLOT_ACCENT, true);
    REQUIRE(host.colorOverridden(SLOT_ACCENT) == 1);

    // A CURRENT-version file that mentions one colour and not accent -- exactly what
    // the sparse writer emits for an unpinned slot, and what a hand edit leaves behind.
    host.writeSettingsFile(save,
        "[Settings]\nversion=6\n\n[Colors]\nprimary=0xFFFFFFFF\n");
    host.loadSettings(save);

    // The pin is gone, so the slot is free to follow a theme again.
    CHECK(host.colorOverridden(SLOT_ACCENT) == 0);
    // ...and the one colour the file DID state is still applied.
    CHECK(host.colorOverridden(SLOT_PRIMARY) == 1);
}
