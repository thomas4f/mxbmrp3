// ============================================================================
// tests/unit/test_asset_packs.cpp
// The shipped ASSET PACKS' ini files still describe the things they replaced.
// Two pack types share one format and one set of rules: gamepad pads
// (gamepads/<name>/) and pit boards (pitboards/<name>/).
//
// WHAT THIS PINS. The two pads' geometry used to be ~60 hardcoded assignments in
// GamepadWidget::initDefaultLayouts() — variant 1 was the Xbox pad, variant 2 the
// DualShock. Moving those numbers into gamepads/<name>/<name>.ini is a pure data
// migration, and a pure data migration has exactly one failure mode worth
// guarding: a number that silently changed on the way across. A typo in an offset
// does not fail to build, does not fail to parse, and does not fail any other
// test — it just puts a button slightly off its socket, which is the sort of
// thing nobody notices until a screenshot.
//
// So the expectations below are the ORIGINAL hardcoded values, transcribed from
// the deleted initDefaultLayouts(), and they are deliberately written out rather
// than read from the same file under test.
//
// It also covers the second reason this subsystem exists at all: the two pads
// must NOT agree. If a future edit made the packs identical, the per-pad premise
// (and half the point of pack-per-pad) would be quietly gone, so that is asserted
// too.
//
// The parse path here is the real one — layoutForEachIniPairRaw feeding
// GamepadLayout::applyPadGeometryIni / PitboardLayout::applyBoardGeometryIni —
// which is why those tables live in headers rather than inside AssetManager's
// Win32-only discovery walk.
//
// The pit board cases are at the bottom; see the banner there for what its pack
// fixes that the gamepad's did not.
// ============================================================================
#include "doctest.h"

#include "hud/gamepad_geometry.h"
#include "hud/pitboard_geometry.h"
#include "core/layout_config.h"

#include <limits>
#include <string>

using namespace GamepadLayout;

#ifndef PITBOARDS_DIR
#error "PITBOARDS_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

#ifndef GAMEPADS_DIR
#error "GAMEPADS_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

namespace {

// Read one shipped pack ini through the production walk + mapping.
PadGeometry loadPack(const char* name) {
    PadGeometry g;
    const std::string path = std::string(GAMEPADS_DIR) + "/" + name + "/" + name + ".ini";
    const bool opened = layoutForEachIniPairRaw(path,
        [](const char* key, float value, const char* /*raw*/, bool numeric, void* ctx) -> bool {
            // [pad] name is the one non-numeric key and is not geometry.
            if (numeric) applyPadGeometryIni(*static_cast<PadGeometry*>(ctx), key, value);
            return true;
        }, &g);
    REQUIRE_MESSAGE(opened, "could not open " << path);
    return g;
}

}  // namespace

TEST_CASE("shipped xbox pack matches the geometry it replaced") {
    const PadGeometry g = loadPack("xbox");

    CHECK(g.backgroundWidth == doctest::Approx(750.0f));
    CHECK(g.backgroundHeight == doctest::Approx(630.0f));

    CHECK(g.triggerWidth == doctest::Approx(89.0f));
    CHECK(g.triggerHeight == doctest::Approx(61.0f));
    CHECK(g.bumperWidth == doctest::Approx(171.0f));
    CHECK(g.bumperHeight == doctest::Approx(63.0f));
    CHECK(g.dpadWidth == doctest::Approx(32.0f));
    CHECK(g.dpadHeight == doctest::Approx(53.0f));
    CHECK(g.faceButtonSize == doctest::Approx(47.0f));
    CHECK(g.menuButtonWidth == doctest::Approx(33.0f));
    CHECK(g.menuButtonHeight == doctest::Approx(33.0f));
    CHECK(g.stickSize == doctest::Approx(83.0f));

    CHECK(g.leftTriggerX == doctest::Approx(0.041f));
    CHECK(g.leftTriggerY == doctest::Approx(0.0143f));
    CHECK(g.rightTriggerX == doctest::Approx(-0.041f));
    CHECK(g.rightTriggerY == doctest::Approx(0.0143f));
    CHECK(g.leftBumperX == doctest::Approx(-0.01f));
    CHECK(g.leftBumperY == doctest::Approx(0.0573f));
    CHECK(g.rightBumperX == doctest::Approx(0.01f));
    CHECK(g.rightBumperY == doctest::Approx(0.0573f));
    CHECK(g.leftStickX == doctest::Approx(0.015f));
    CHECK(g.leftStickY == doctest::Approx(0.0563f));
    CHECK(g.rightStickX == doctest::Approx(-0.049f));
    CHECK(g.rightStickY == doctest::Approx(0.1263f));
    CHECK(g.dpadX == doctest::Approx(0.0473f));
    CHECK(g.dpadY == doctest::Approx(0.0408f));
    CHECK(g.faceButtonsX == doctest::Approx(-0.0162f));
    CHECK(g.faceButtonsY == doctest::Approx(-0.0343f));
    CHECK(g.menuButtonsX == doctest::Approx(0.0004f));
    CHECK(g.menuButtonsY == doctest::Approx(-0.0393f));

    CHECK(g.dpadSpacing == doctest::Approx(0.95f));
    CHECK(g.faceButtonSpacing == doctest::Approx(1.07f));
    CHECK(g.menuButtonSpacing == doctest::Approx(1.14f));
}

TEST_CASE("shipped ds4 pack matches the geometry it replaced") {
    const PadGeometry g = loadPack("ds4");

    CHECK(g.backgroundWidth == doctest::Approx(806.0f));
    CHECK(g.backgroundHeight == doctest::Approx(599.0f));

    CHECK(g.triggerWidth == doctest::Approx(99.0f));
    CHECK(g.triggerHeight == doctest::Approx(91.0f));
    CHECK(g.bumperWidth == doctest::Approx(99.0f));
    CHECK(g.bumperHeight == doctest::Approx(22.0f));
    CHECK(g.dpadWidth == doctest::Approx(32.0f));
    CHECK(g.dpadHeight == doctest::Approx(45.0f));
    CHECK(g.faceButtonSize == doctest::Approx(50.0f));
    CHECK(g.menuButtonWidth == doctest::Approx(27.0f));
    CHECK(g.menuButtonHeight == doctest::Approx(45.0f));
    CHECK(g.stickSize == doctest::Approx(94.0f));

    CHECK(g.leftTriggerX == doctest::Approx(0.0238f));
    CHECK(g.leftTriggerY == doctest::Approx(-0.0221f));
    CHECK(g.rightTriggerX == doctest::Approx(-0.0238f));
    CHECK(g.rightTriggerY == doctest::Approx(-0.0221f));
    CHECK(g.leftBumperX == doctest::Approx(-0.0133f));
    CHECK(g.leftBumperY == doctest::Approx(0.012f));
    CHECK(g.rightBumperX == doctest::Approx(0.0133f));
    CHECK(g.rightBumperY == doctest::Approx(0.012f));
    CHECK(g.leftStickX == doctest::Approx(0.0398f));
    CHECK(g.leftStickY == doctest::Approx(0.0873f));
    CHECK(g.rightStickX == doctest::Approx(-0.041f));
    CHECK(g.rightStickY == doctest::Approx(0.0873f));
    CHECK(g.dpadX == doctest::Approx(0.001f));
    CHECK(g.dpadY == doctest::Approx(-0.066f));
    CHECK(g.faceButtonsX == doctest::Approx(-0.0023f));
    CHECK(g.faceButtonsY == doctest::Approx(-0.066f));
    CHECK(g.menuButtonsX == doctest::Approx(0.0001f));
    CHECK(g.menuButtonsY == doctest::Approx(-0.1195f));

    CHECK(g.dpadSpacing == doctest::Approx(1.55f));
    CHECK(g.faceButtonSpacing == doctest::Approx(1.15f));
    CHECK(g.menuButtonSpacing == doctest::Approx(5.51f));
}

TEST_CASE("the two shipped pads are genuinely different geometry") {
    // The premise of the whole pack format: art and placement travel together
    // because placement is per-pad. If these ever coincide, either a pack was
    // copied over the other or the format has lost its reason to exist.
    const PadGeometry xbox = loadPack("xbox");
    const PadGeometry ds4 = loadPack("ds4");

    CHECK(xbox.backgroundWidth != ds4.backgroundWidth);
    CHECK(xbox.backgroundHeight != ds4.backgroundHeight);
    CHECK(xbox.menuButtonSpacing != ds4.menuButtonSpacing);
    CHECK(xbox.dpadSpacing != ds4.dpadSpacing);
    CHECK(xbox.leftStickY != ds4.leftStickY);
}

TEST_CASE("pad ini mapping rejects what it cannot use") {
    PadGeometry g;
    const float widthBefore = g.backgroundWidth;

    // An unknown key is REPORTED (false), so discovery can tell the author their
    // key is a typo rather than silently dropping an offset.
    CHECK_FALSE(applyPadGeometryIni(g, "art.wdith", 1.0f));
    CHECK_FALSE(applyPadGeometryIni(g, "size.trigger", 1.0f));
    CHECK_FALSE(applyPadGeometryIni(g, "", 1.0f));

    // A recognised key is consumed (true) even when the VALUE is rejected -- the
    // key is not a typo, so warning about it would send the author hunting for a
    // spelling mistake that isn't there.
    // From <limits>, not a literal: `1e39f` is itself a constraint violation that
    // -Werror=overflow rejects before the test can run.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(applyPadGeometryIni(g, "art.width", nan));
    CHECK(g.backgroundWidth == doctest::Approx(widthBefore));
    CHECK(applyPadGeometryIni(g, "art.width", inf));
    CHECK(g.backgroundWidth == doctest::Approx(widthBefore));
    CHECK(applyPadGeometryIni(g, "art.width", -inf));
    CHECK(g.backgroundWidth == doctest::Approx(widthBefore));

    // A NON-POSITIVE size is rejected the same way, and this one is not cosmetic:
    // art.width, size.trigger-w, size.bumper-w, size.dpad-w and size.menu-button-w are
    // DIVISORS in gamepad_widget.cpp. Zero makes the derived height inf, fitPanelToGrid
    // returns inf/NaN, and NaN vertices reach the game's DrawQuad for the whole widget.
    // The ini is hand-edited, so a typo'd or blank value is the ordinary case.
    for (const char* key : { "art.width", "art.height", "size.trigger-w",
                             "size.bumper-w", "size.dpad-w", "size.menu-button-w" }) {
        PadGeometry probe;
        const PadGeometry pristine;
        CHECK(applyPadGeometryIni(probe, key, 0.0f));
        CHECK(applyPadGeometryIni(probe, key, -8.0f));
        // Nothing moved: every field still reads its shipped default.
        for (const PadGeometryIniEntry& e : kPadGeometryIni)
            CHECK(probe.*(e.field) == doctest::Approx(pristine.*(e.field)));
    }

    // An OFFSET may legitimately be negative -- that is how art is placed left of or
    // above its anchor -- so the positivity rule is per-row, not global.
    CHECK(applyPadGeometryIni(g, "offset.dpad-x", -12.0f));
    CHECK(g.dpadX == doctest::Approx(-12.0f));

    // ...and a finite positive one lands.
    CHECK(applyPadGeometryIni(g, "art.width", 1024.0f));
    CHECK(g.backgroundWidth == doctest::Approx(1024.0f));
}

TEST_CASE("every pad ini key names a distinct field") {
    // Two rows pointing at the same member means one key silently shadows the
    // other; two rows with the same key means the second is dead. Both are the
    // kind of thing a copy-pasted table row produces.
    for (size_t i = 0; i < sizeof(kPadGeometryIni) / sizeof(kPadGeometryIni[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(kPadGeometryIni) / sizeof(kPadGeometryIni[0]); ++j) {
            CHECK(std::string(kPadGeometryIni[i].key) != std::string(kPadGeometryIni[j].key));
            CHECK(kPadGeometryIni[i].field != kPadGeometryIni[j].field);
        }
    }
}

// ============================================================================
// PIT BOARD PACKS. Same format, same rules; what differs is what the pack fixes.
// The board's ASPECT used to be PitboardHud::TEXTURE_ASPECT_RATIO, a compiled
// 1920/1080 -- so a board drawn at any other shape was stretched and no offset
// could correct it, because offsets move text inside the panel rather than
// reshaping it. Reading the aspect from the pack is what makes a custom board
// portable, so these cases pin the arithmetic as well as the parse.
// ============================================================================

namespace {

PitboardLayout::BoardGeometry loadBoard(const char* name) {
    PitboardLayout::BoardGeometry g;
    const std::string path = std::string(PITBOARDS_DIR) + "/" + name + "/" + name + ".ini";
    const bool opened = layoutForEachIniPairRaw(path,
        [](const char* key, float value, const char* /*raw*/, bool numeric, void* ctx) -> bool {
            if (numeric) PitboardLayout::applyBoardGeometryIni(
                *static_cast<PitboardLayout::BoardGeometry*>(ctx), key, value);
            return true;
        }, &g);
    REQUIRE_MESSAGE(opened, "could not open " << path);
    return g;
}

}  // namespace

TEST_CASE("shipped classic board matches the geometry it replaced") {
    const PitboardLayout::BoardGeometry g = loadBoard("classic");

    // The compiled constant this replaced was exactly 1920/1080, and the shipped
    // background.tga really is that size -- so the panel must come out identical.
    CHECK(g.artWidth == doctest::Approx(1920.0f));
    CHECK(g.artHeight == doctest::Approx(1080.0f));

    // Every offset zero: the artwork was drawn around the coded row positions.
    CHECK(g.riderIdX == doctest::Approx(0.0f));
    CHECK(g.riderIdY == doctest::Approx(0.0f));
    CHECK(g.sessionX == doctest::Approx(0.0f));
    CHECK(g.sessionY == doctest::Approx(0.0f));
    CHECK(g.positionX == doctest::Approx(0.0f));
    CHECK(g.positionY == doctest::Approx(0.0f));
    CHECK(g.timeX == doctest::Approx(0.0f));
    CHECK(g.timeY == doctest::Approx(0.0f));
    CHECK(g.lapX == doctest::Approx(0.0f));
    CHECK(g.lapY == doctest::Approx(0.0f));
    CHECK(g.lastLapX == doctest::Approx(0.0f));
    CHECK(g.lastLapY == doctest::Approx(0.0f));
    CHECK(g.gapX == doctest::Approx(0.0f));
    CHECK(g.gapY == doctest::Approx(0.0f));
}

TEST_CASE("board width reproduces the constant it replaced") {
    // The old expression, verbatim: (height * 1920/1080) / uiAspect.
    const float uiAspect = 16.0f / 9.0f;
    const PitboardLayout::BoardGeometry shipped = loadBoard("classic");

    for (float height : {0.1f, 0.25f, 0.4f}) {
        const float expected = (height * (1920.0f / 1080.0f)) / uiAspect;
        CHECK(shipped.widthForHeight(height, uiAspect) == doctest::Approx(expected));
    }

    // ...and a board at ANOTHER aspect gets its own shape rather than 16:9's.
    // This is the case the compiled constant could not express at all.
    PitboardLayout::BoardGeometry square;
    square.artWidth = 1000.0f;
    square.artHeight = 1000.0f;
    const float h = 0.3f;
    CHECK(square.widthForHeight(h, uiAspect) == doctest::Approx(h / uiAspect));
    CHECK(square.widthForHeight(h, uiAspect) < shipped.widthForHeight(h, uiAspect));
}

TEST_CASE("board width refuses to produce a degenerate panel") {
    // Hand-edited ini: a zero or negative art dimension must not yield a zero-width
    // or mirrored panel. Falling back to the height is arbitrary but bounded and
    // visible -- unlike a 0-wide board, which renders as nothing at all.
    const float uiAspect = 16.0f / 9.0f;
    PitboardLayout::BoardGeometry g;

    g.artHeight = 0.0f;
    CHECK(g.widthForHeight(0.3f, uiAspect) == doctest::Approx(0.3f));
    g.artHeight = -1080.0f;
    CHECK(g.widthForHeight(0.3f, uiAspect) == doctest::Approx(0.3f));
    g = PitboardLayout::BoardGeometry{};
    g.artWidth = 0.0f;
    CHECK(g.widthForHeight(0.3f, uiAspect) == doctest::Approx(0.3f));
    g = PitboardLayout::BoardGeometry{};
    CHECK(g.widthForHeight(0.3f, 0.0f) == doctest::Approx(0.3f));
}

TEST_CASE("board ini mapping rejects what it cannot use") {
    PitboardLayout::BoardGeometry g;
    const float widthBefore = g.artWidth;

    CHECK_FALSE(PitboardLayout::applyBoardGeometryIni(g, "art.wdith", 1.0f));
    CHECK_FALSE(PitboardLayout::applyBoardGeometryIni(g, "offset.rider-id", 1.0f));
    // A gamepad key must NOT resolve here: the two tables are separate vocabularies.
    CHECK_FALSE(PitboardLayout::applyBoardGeometryIni(g, "size.stick", 1.0f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(PitboardLayout::applyBoardGeometryIni(g, "art.width", nan));
    CHECK(g.artWidth == doctest::Approx(widthBefore));
    CHECK(PitboardLayout::applyBoardGeometryIni(g, "art.width", inf));
    CHECK(g.artWidth == doctest::Approx(widthBefore));

    CHECK(PitboardLayout::applyBoardGeometryIni(g, "art.width", 800.0f));
    CHECK(g.artWidth == doctest::Approx(800.0f));
}

TEST_CASE("every board ini key names a distinct field") {
    using PitboardLayout::kBoardGeometryIni;
    constexpr size_t n = sizeof(kBoardGeometryIni) / sizeof(kBoardGeometryIni[0]);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            CHECK(std::string(kBoardGeometryIni[i].key) != std::string(kBoardGeometryIni[j].key));
            CHECK(kBoardGeometryIni[i].field != kBoardGeometryIni[j].field);
        }
    }
}
