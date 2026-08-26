// ============================================================================
// tests/unit/test_marker_label.cpp
// Rider marker label formatting/colors (hud/marker_label.h).
//
// Before the extraction, MapHud, RadarHud and GapBarHud each carried their own
// copy of this logic, and the copies had already diverged in shape (GapBar
// early-returned where the others wrote an empty string) while agreeing in
// behavior only by review. This pins the now-shared behavior:
//   - the exact "P%d [#%d]" text for every mode, and the no-position
//     fallbacks (POSITION renders nothing, BOTH drops to "#%d")
//   - podium gold/silver/bronze applied ONLY when the mode shows positions —
//     RACE_NUM mode must NOT color P1's number gold
//   - the false return for empty labels, which is what lets callers skip
//     outline + main-text rendering entirely
// ============================================================================
#include "doctest.h"

#include "hud/marker_label.h"

#include <string>

using MarkerLabel::Mode;

TEST_CASE("marker label text per mode") {
    char buf[20];

    SUBCASE("POSITION shows P-prefixed position") {
        CHECK(MarkerLabel::format(Mode::POSITION, 3, 42, buf, sizeof(buf)));
        CHECK(std::string(buf) == "P3");
    }
    SUBCASE("POSITION with no position renders nothing") {
        CHECK_FALSE(MarkerLabel::format(Mode::POSITION, 0, 42, buf, sizeof(buf)));
        CHECK(buf[0] == '\0');
        CHECK_FALSE(MarkerLabel::format(Mode::POSITION, -1, 42, buf, sizeof(buf)));
    }
    SUBCASE("RACE_NUM shows bare race number (no prefix)") {
        CHECK(MarkerLabel::format(Mode::RACE_NUM, 3, 42, buf, sizeof(buf)));
        CHECK(std::string(buf) == "42");
    }
    SUBCASE("BOTH shows position and hash-prefixed race number") {
        CHECK(MarkerLabel::format(Mode::BOTH, 3, 42, buf, sizeof(buf)));
        CHECK(std::string(buf) == "P3 #42");
    }
    SUBCASE("BOTH with no position falls back to race number only") {
        CHECK(MarkerLabel::format(Mode::BOTH, 0, 42, buf, sizeof(buf)));
        CHECK(std::string(buf) == "#42");
    }
    SUBCASE("NONE renders nothing") {
        CHECK_FALSE(MarkerLabel::format(Mode::NONE, 3, 42, buf, sizeof(buf)));
        CHECK(buf[0] == '\0');
    }
    SUBCASE("three-digit values fit the callers' 20-byte buffers") {
        CHECK(MarkerLabel::format(Mode::BOTH, 100, 999, buf, sizeof(buf)));
        CHECK(std::string(buf) == "P100 #999");
    }
}

TEST_CASE("marker label podium colors") {
    using namespace PluginConstants;
    constexpr unsigned long DEFAULT_COLOR = 0xFFABCDEFul;

    SUBCASE("P1/P2/P3 get gold/silver/bronze in position-showing modes") {
        for (Mode m : {Mode::POSITION, Mode::BOTH}) {
            CHECK(MarkerLabel::color(m, 1, DEFAULT_COLOR) == PodiumColors::GOLD);
            CHECK(MarkerLabel::color(m, 2, DEFAULT_COLOR) == PodiumColors::SILVER);
            CHECK(MarkerLabel::color(m, 3, DEFAULT_COLOR) == PodiumColors::BRONZE);
            CHECK(MarkerLabel::color(m, 4, DEFAULT_COLOR) == DEFAULT_COLOR);
            CHECK(MarkerLabel::color(m, 0, DEFAULT_COLOR) == DEFAULT_COLOR);
        }
    }
    SUBCASE("RACE_NUM and NONE never apply podium colors") {
        for (Mode m : {Mode::RACE_NUM, Mode::NONE}) {
            for (int pos : {1, 2, 3, 4}) {
                CHECK(MarkerLabel::color(m, pos, DEFAULT_COLOR) == DEFAULT_COLOR);
            }
        }
    }
}

TEST_CASE("marker label mode values are the INI on-disk representation") {
    // GapBar serializes the mode as int — renumbering would silently remap
    // every user's saved setting.
    CHECK(static_cast<int>(Mode::NONE) == 0);
    CHECK(static_cast<int>(Mode::POSITION) == 1);
    CHECK(static_cast<int>(Mode::RACE_NUM) == 2);
    CHECK(static_cast<int>(Mode::BOTH) == 3);
}

// ============================================================================
// PLACEMENT, which this header did NOT own until the three HUDs were reconciled.
//
// Its comment used to say geometry "stays per-HUD", and that was a fair reading
// of three HUDs doing it three ways -- but the three ways were one design plus
// drift: two gap formulas (one of the icon, one of the font, and the font one
// silently ignored HUD scale), a four-way black outline on two of them against
// the shared drop shadow on the third, and an anchor switch only the map had.
// What survived reconciliation differs in two PARAMETERS, so place() takes them.
//
// The properties below are the ones a HUD would otherwise re-derive and get
// subtly wrong -- which is exactly how the drift happened.
// ============================================================================
namespace {
constexpr float kAspect = PluginConstants::UI_ASPECT_RATIO;
}

TEST_CASE("marker label: BELOW clears the icon by the gap, and stays centred") {
    const float half = 0.010f, font = 0.014f;
    const auto p = MarkerLabel::place(MarkerLabel::Anchor::BELOW, 0.5f, 0.4f, half, font);
    CHECK(p.x == doctest::Approx(0.5f));                       // no sideways shift
    CHECK(p.justify == PluginConstants::Justify::CENTER);
    // Clears the icon's bottom edge, and by the gap exactly -- not by "some" gap.
    CHECK(p.y == doctest::Approx(0.4f + half + half * MarkerLabel::GAP_RATIO));
    CHECK(p.y > 0.4f + half);
}

TEST_CASE("marker label: ABOVE leaves room for the text's own height") {
    const float half = 0.010f, font = 0.014f;
    const auto p = MarkerLabel::place(MarkerLabel::Anchor::ABOVE, 0.5f, 0.4f, half, font);
    // The string's y is its TOP, so an above-anchored label has to back off by its
    // own height as well as the gap or it overlaps the icon it labels.
    CHECK(p.y + font == doctest::Approx(0.4f - half - half * MarkerLabel::GAP_RATIO));
    CHECK(p.justify == PluginConstants::Justify::CENTER);
}

TEST_CASE("marker label: the side anchors use the icon's WIDTH, not its height") {
    const float half = 0.010f, font = 0.014f;
    const auto l = MarkerLabel::place(MarkerLabel::Anchor::LEFT,  0.5f, 0.4f, half, font);
    const auto r = MarkerLabel::place(MarkerLabel::Anchor::RIGHT, 0.5f, 0.4f, half, font);

    // Icons are aspect-corrected, so the horizontal half-size is the vertical one
    // divided by the aspect. Reusing iconHalfSize here would push the label 16/9 too
    // far out -- the mistake this function exists to make once instead of three times.
    const float halfW = half / kAspect;
    CHECK(l.x == doctest::Approx(0.5f - halfW - half * MarkerLabel::GAP_RATIO));
    CHECK(r.x == doctest::Approx(0.5f + halfW + half * MarkerLabel::GAP_RATIO));
    CHECK(l.x < 0.5f - halfW);
    CHECK(r.x > 0.5f + halfW);

    // ...and they justify AWAY from the icon, so the text grows outward.
    CHECK(l.justify == PluginConstants::Justify::RIGHT);
    CHECK(r.justify == PluginConstants::Justify::LEFT);

    // Both centre on the icon's line rather than sitting under it, and symmetrically.
    CHECK(l.y == doctest::Approx(r.y));
    CHECK(l.y == doctest::Approx(0.4f - font * MarkerLabel::SIDE_CENTER_RATIO));
}

TEST_CASE("marker label: every term scales with the icon, so the boost carries") {
    const float font = 0.014f;
    const auto plain = MarkerLabel::place(MarkerLabel::Anchor::BELOW, 0.5f, 0.4f,
                                          0.010f, font);
    const auto boosted = MarkerLabel::place(MarkerLabel::Anchor::BELOW, 0.5f, 0.4f,
                                            0.010f * MarkerLabel::PLAYER_BOOST,
                                            font * MarkerLabel::PLAYER_BOOST);
    // The local player's marker draws larger, and its label has to move out with it
    // rather than staying where the pack's labels sit.
    CHECK(boosted.y > plain.y);
    CHECK(boosted.y - 0.4f == doctest::Approx((plain.y - 0.4f) * MarkerLabel::PLAYER_BOOST));
    CHECK(MarkerLabel::boost(true) == doctest::Approx(MarkerLabel::PLAYER_BOOST));
    CHECK(MarkerLabel::boost(false) == doctest::Approx(1.0f));
}

// A LABEL MUST NOT HANG OUT OF A TIGHT BOX. The gap bar's content row is one text
// row tall, so an icon centred in it with a label BELOW put the label half outside
// the panel it belongs to. Centring the pair instead costs nothing where there is
// room (the map, the radar) and is the whole fix where there is not.
TEST_CASE("marker label: the icon shifts so icon+label centre together") {
    const float half = 0.010f, font = 0.014f;
    const float gap = half * MarkerLabel::GAP_RATIO;

    // A side label already shares the icon's line, so the pair is centred exactly
    // when the icon is -- nothing to move.
    CHECK(MarkerLabel::blockCenterShift(MarkerLabel::Anchor::LEFT,  half, font) == 0.0f);
    CHECK(MarkerLabel::blockCenterShift(MarkerLabel::Anchor::RIGHT, half, font) == 0.0f);

    // A stacked label moves the icon the other way, by half of what it adds.
    const float below = MarkerLabel::blockCenterShift(MarkerLabel::Anchor::BELOW, half, font);
    const float above = MarkerLabel::blockCenterShift(MarkerLabel::Anchor::ABOVE, half, font);
    CHECK(below < 0.0f);            // y grows downward: BELOW label -> icon rides UP
    CHECK(above == doctest::Approx(-below));
    CHECK(below == doctest::Approx(-(gap + font) * 0.5f));

    // THE PROPERTY, stated end to end: shift the icon by this and the pair straddles
    // the box centre evenly. Block runs from the icon's top to the label's bottom.
    const float boxMid = 0.5f;
    const float iconY = boxMid + below;
    const auto lp = MarkerLabel::place(MarkerLabel::Anchor::BELOW, 0.0f, iconY, half, font);
    const float blockTop = iconY - half;
    const float blockBot = lp.y + font;
    CHECK((blockTop + blockBot) * 0.5f == doctest::Approx(boxMid));
    // ...and without the shift it does not, which is the bug.
    const auto lp0 = MarkerLabel::place(MarkerLabel::Anchor::BELOW, 0.0f, boxMid, half, font);
    CHECK(((boxMid - half) + (lp0.y + font)) * 0.5f > boxMid);
}
