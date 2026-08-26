// ============================================================================
// tests/unit/test_icon_resolve.cpp
// Icon sprite resolution in both directions (core/icon_resolve.h).
//
// WHY IT MATTERS. A theme may restyle an icon, and its override sprite is registered
// PAST the contiguous base block. Forward resolution (name or shape -> sprite) is
// what the override is for; BACKWARD resolution (sprite -> shape) is how the marker
// paths ask whether a glyph is directional and how settings turn a saved name into a
// picker position. Ten call sites open-coded the backward one as
// `sprite - first + 1`, which silently returns a number off the end of the vocabulary
// for an override sprite -- markers stop rotating, saved markers resolve to nothing.
//
// The property under test is that the two directions are each other's inverse for
// BOTH kinds of sprite, which is exactly what that subtraction could not promise.
//
// Unit-level rather than in the integration suite on purpose: the harness stages no
// icons/ directory, and staging one would renumber every sprite index the parity
// goldens hash. The disk half (reading themes/<name>/icons/) stays manual.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/core/icon_resolve.h"

namespace {

// A vocabulary of four icons at sprites 100..103, and a theme overriding two of them
// at indices well past the block -- where a real theme's overrides sit, after its
// nine-slice sprites.
constexpr int FIRST = 100;
constexpr int COUNT = 4;
const std::string NAMES[COUNT] = {"circle", "crown", "hud-map", "flag"};

std::map<std::string, int> overrides() {
    return {{"crown", 900}, {"hud-map", 901}};
}
std::map<int, int> overrideShapes() {
    return {{900, 2}, {901, 3}};   // shape indices are 1-based positions in NAMES
}

}  // namespace

TEST_CASE("icon resolve: with no theme, sprites are the base block") {
    for (int shape = 1; shape <= COUNT; ++shape) {
        CHECK(IconResolve::spriteForShape(shape, FIRST, COUNT, NAMES[shape - 1], nullptr)
              == FIRST + shape - 1);
        CHECK(IconResolve::shapeForSprite(FIRST + shape - 1, FIRST, COUNT, nullptr) == shape);
    }
}

TEST_CASE("icon resolve: an override replaces the sprite, per name") {
    const auto ov = overrides();
    // Overridden.
    CHECK(IconResolve::spriteForName("crown", FIRST + 1, &ov) == 900);
    CHECK(IconResolve::spriteForShape(2, FIRST, COUNT, "crown", &ov) == 900);
    // NOT overridden -- the sparse case, which is what lets a theme ship two icons
    // and inherit the rest instead of blanking them.
    CHECK(IconResolve::spriteForName("circle", FIRST, &ov) == FIRST);
    CHECK(IconResolve::spriteForShape(4, FIRST, COUNT, "flag", &ov) == FIRST + 3);
}

TEST_CASE("icon resolve: backward mapping survives an override sprite") {
    // THE BUG. `sprite - first + 1` on an override sprite gives 801 here, off the end
    // of a four-icon vocabulary; the marker code then reads an empty filename and the
    // glyph stops rotating.
    const auto ovs = overrideShapes();
    CHECK(IconResolve::shapeForSprite(900, FIRST, COUNT, &ovs) == 2);
    CHECK(IconResolve::shapeForSprite(901, FIRST, COUNT, &ovs) == 3);
    // Base sprites still answer the same way with a theme on -- the vocabulary is
    // untouched, which is the whole promise of "restyle, never extend".
    for (int shape = 1; shape <= COUNT; ++shape) {
        CHECK(IconResolve::shapeForSprite(FIRST + shape - 1, FIRST, COUNT, &ovs) == shape);
    }
}

TEST_CASE("icon resolve: the two directions are inverses, themed and not") {
    const auto ov = overrides();
    const auto ovs = overrideShapes();
    for (int shape = 1; shape <= COUNT; ++shape) {
        const int sprite = IconResolve::spriteForShape(shape, FIRST, COUNT, NAMES[shape - 1], &ov);
        CHECK(IconResolve::shapeForSprite(sprite, FIRST, COUNT, &ovs) == shape);
    }
}

TEST_CASE("icon resolve: a sprite that is not an icon has no shape") {
    // Theme SLICE sprites live past the icon block too, and they are not markers.
    const auto ovs = overrideShapes();
    CHECK(IconResolve::shapeForSprite(0, FIRST, COUNT, &ovs) == 0);
    CHECK(IconResolve::shapeForSprite(FIRST - 1, FIRST, COUNT, &ovs) == 0);
    CHECK(IconResolve::shapeForSprite(FIRST + COUNT, FIRST, COUNT, &ovs) == 0);
    CHECK(IconResolve::shapeForSprite(999, FIRST, COUNT, &ovs) == 0);   // not an override either
}

TEST_CASE("icon resolve: an out-of-range shape draws nothing") {
    // 0 is the picker's "Off" slot, and a stale saved index can exceed the set after
    // icons are removed from an install.
    const auto ov = overrides();
    CHECK(IconResolve::spriteForShape(0, FIRST, COUNT, "", &ov) == 0);
    CHECK(IconResolve::spriteForShape(-3, FIRST, COUNT, "", &ov) == 0);
    CHECK(IconResolve::spriteForShape(COUNT + 1, FIRST, COUNT, "", &ov) == 0);
    // Guards the empty-vocabulary install as well: no icons discovered, nothing drawn.
    CHECK(IconResolve::spriteForShape(1, FIRST, 0, "", &ov) == 0);
}
