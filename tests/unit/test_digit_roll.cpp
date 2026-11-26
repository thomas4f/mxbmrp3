// ============================================================================
// tests/unit/test_digit_roll.cpp
// Motion's digit roll (mxbmrp3/hud/digit_roll.h):
//   - counting up the old text rises and the new comes from below, like an
//     odometer drum; counting down the reverse; N ranks between 1 and 2
//   - the old text starts in place and fully visible, the new one ends there
//   - the drum (cutAt) travels its box, fading near its edge (the game's
//     copy is the shared edge rule's: hud/clip_marks.h, test_clip_marks.cpp)
//   - settled, and when a change switches, the one text it always drew
//   - a second change before the first has shown keeps the old text rolling
//     out (5, 4, 3 in quick shifts never draws the 4, nor the 3 opaque at once)
//   - the text rolling out keeps the colour it had; the new one takes the
//     colour it is given now (a gear shifted on the red stays red as it leaves)
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/hud/digit_roll.h"

#include <string>
#include <vector>

namespace {
constexpr long long T0 = 1000000;
constexpr long long MID = T0 + DigitRoll::DURATION_US / 2;
constexpr long long END = T0 + DigitRoll::DURATION_US;
constexpr unsigned long WHITE = 0xFFFFFFFFul;
constexpr unsigned long RED = 0xFF0000FFul;
// A box a font size and a bit tall.
constexpr float TRAVEL = 1.6f;

struct Drawn { std::string text; float dy, alpha; unsigned long color; };

std::vector<Drawn> draw(const DigitRoll::Roller& r, long long now) {
    DigitRoll::Piece p[DigitRoll::MAX_PIECES];
    const int n = r.pieces(now, p, TRAVEL);
    std::vector<Drawn> out;
    for (int i = 0; i < n; ++i) out.push_back({ p[i].text, p[i].dy, p[i].alpha, p[i].color });
    return out;
}
}  // namespace

TEST_CASE("digit roll: gear direction follows the shift order, N between 1 and 2") {
    CHECK(DigitRoll::gearRank("1") < DigitRoll::gearRank("N"));
    CHECK(DigitRoll::gearRank("N") < DigitRoll::gearRank("2"));
    CHECK(DigitRoll::gearRank("5") < DigitRoll::gearRank("12"));
    CHECK(DigitRoll::gearRank("D") == -1);
    CHECK(DigitRoll::gearRank("-") == -1);
    CHECK(DigitRoll::gearRank("") == -1);
}

TEST_CASE("digit roll: counting up the old rises and the new comes from below, down the reverse") {
    const DigitRoll::Frame up = DigitRoll::cutAt(0.3f, 1, TRAVEL);
    CHECK(up.outDy < 0.0f);   // up the screen
    CHECK(up.inDy > 0.0f);    // below
    const DigitRoll::Frame down = DigitRoll::cutAt(0.3f, -1, TRAVEL);
    CHECK(down.outDy > 0.0f);
    CHECK(down.inDy < 0.0f);
}

TEST_CASE("digit roll: settled, and on a switch, the one text it always drew") {
    DigitRoll::Roller r;
    r.update("0009", 1, T0, WHITE);   // first text: nothing to roll from
    std::vector<Drawn> d = draw(r, T0);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "0009");
    CHECK(d[0].dy == 0.0f);
    CHECK(d[0].alpha == 1.0f);

    r.update("1234", 0, T0, WHITE);   // dir 0: switch
    d = draw(r, T0 + 1);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "1234");
    CHECK_FALSE(r.active(T0 + 1));
}

TEST_CASE("digit roll: a change rolls the old text out and the new in, then settles") {
    DigitRoll::Roller r;
    r.update("0009", 0, T0, WHITE);
    r.update("0010", 1, T0, WHITE);
    CHECK(r.active(MID));
    std::vector<Drawn> d = draw(r, MID);
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "0009");    // rising out
    CHECK(d[0].dy < 0.0f);
    CHECK(d[0].alpha < 1.0f);
    CHECK(d[1].text == "0010");    // coming up from below
    CHECK(d[1].dy > 0.0f);
    CHECK(d[1].alpha < 1.0f);

    d = draw(r, END);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "0010");
    CHECK(d[0].alpha == 1.0f);

    // An unchanged text starts nothing; reset() makes the next change switch.
    r.update("0010", 1, END, WHITE);
    CHECK_FALSE(r.active(END));
    r.reset();
    r.update("0011", 1, END, WHITE);
    CHECK_FALSE(r.active(END));
    CHECK(std::string(r.shown()) == "0011");
}

TEST_CASE("digit roll: a change before the last one has shown rolls on from the old text") {
    DigitRoll::Roller r;
    r.update("5", 0, T0, WHITE);
    r.update("4", 1, T0, WHITE);
    r.update("3", 1, T0 + DigitRoll::SWAP_US - 1, WHITE);   // the 4 is faint at most
    std::vector<Drawn> d = draw(r, MID);
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "5");         // still rolling out, from where it was
    CHECK(d[0].dy < 0.0f);
    CHECK(d[1].text == "3");         // comes in faded, never opaque at once
    CHECK(d[1].alpha < 1.0f);
    CHECK(std::string(r.shown()) == "3");
    d = draw(r, END);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "3");

    // Once the new text is showing, a further change starts a fresh roll from it.
    r.update("4", 1, END, WHITE);
    r.update("5", 1, END + DigitRoll::SWAP_US + 1, WHITE);
    d = draw(r, END + DigitRoll::SWAP_US + 2);
    REQUIRE(d.size() >= 1);
    CHECK(d[0].text == "4");
    CHECK(d[0].alpha == doctest::Approx(1.0f).epsilon(0.01));

    // Back to the text still rolling out: the roll ends on it.
    const long long t = END + 1000000;
    r.update("6", 1, t, WHITE);
    r.update("5", -1, t + 1, WHITE);
    CHECK_FALSE(r.active(t + 2));
    CHECK(std::string(r.shown()) == "5");
}

TEST_CASE("digit roll: the old text keeps its colour as it rolls out") {
    DigitRoll::Roller r;
    r.update("3", 0, T0, WHITE);
    r.update("3", 1, T0 + 1, RED);        // the shift point: same gear, now red
    std::vector<Drawn> d = draw(r, T0 + 1);
    REQUIRE(d.size() == 1);
    CHECK(d[0].color == RED);

    r.update("4", -1, T0 + 2, WHITE);     // shifted: the RPM drops, the gear is white again
    d = draw(r, T0 + 2 + DigitRoll::DURATION_US / 2);
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "3");
    CHECK(d[0].color == RED);             // leaves in the colour it had
    CHECK(d[1].text == "4");
    CHECK(d[1].color == WHITE);
    d = draw(r, T0 + 2 + DigitRoll::DURATION_US);
    REQUIRE(d.size() == 1);
    CHECK(d[0].color == WHITE);
}

TEST_CASE("digit roll: the drum travels its box's height, fading near its edge, the old out as the new settles") {
    // The travel is the box, so the old text has left it at the end and the
    // new one starts wholly outside it.
    const float travel = TRAVEL;
    const DigitRoll::Frame start = DigitRoll::cutAt(0.0f, 1, travel);
    CHECK(start.outDy == doctest::Approx(0.0f));
    CHECK(start.inDy == doctest::Approx(travel));      // counting up: the new comes from below
    CHECK(start.outAlpha == 1.0f);   // in place: opaque
    CHECK(start.inAlpha == 0.0f);    // at the edge: gone
    const DigitRoll::Frame mid = DigitRoll::cutAt(0.5f, 1, travel);
    CHECK(mid.outAlpha == doctest::Approx(mid.inAlpha));   // halfway out each
    CHECK(mid.outAlpha > 0.0f);
    CHECK(mid.outAlpha < 1.0f);
    // Opaque near its place, fading only toward the edge.
    CHECK(DigitRoll::cutAt(0.1f, 1, travel).outAlpha == 1.0f);
    CHECK(DigitRoll::cutAt(0.9f, 1, travel).inAlpha == 1.0f);
    CHECK(mid.outDy == doctest::Approx(-travel * 0.5f));
    CHECK(mid.inDy - mid.outDy == doctest::Approx(travel));   // one box apart, always
    const DigitRoll::Frame end = DigitRoll::cutAt(1.0f, -1, travel);
    CHECK(end.outDy == doctest::Approx(travel));
    CHECK(end.inDy == doctest::Approx(0.0f));

    // The roller hands out the drum.
    DigitRoll::Roller r;
    r.update("3", 0, 1000, 0xFFFFFFFFul);
    r.update("4", 1, 1000, 0xFFFFFFFFul);
    DigitRoll::Piece p[DigitRoll::MAX_PIECES];
    const long long half = 1000 + DigitRoll::DURATION_US / 2;
    REQUIRE(r.pieces(half, p, travel) == 2);
    CHECK(p[0].alpha == doctest::Approx(DigitRoll::cutAt(0.5f, 1, travel).outAlpha));
    CHECK(p[1].alpha == doctest::Approx(DigitRoll::cutAt(0.5f, 1, travel).inAlpha));
    CHECK(p[1].dy - p[0].dy == doctest::Approx(travel));
}
