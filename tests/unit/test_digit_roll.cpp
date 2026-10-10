// ============================================================================
// tests/unit/test_digit_roll.cpp
// Motion's digit roll (mxbmrp3/hud/digit_roll.h):
//   - counting up the old text rises and the new comes from below, like an
//     odometer drum; counting down the reverse; N ranks between 1 and 2
//   - the old text starts in place and fully visible, the new one ends there
//   - NO CLIPPING NEEDED: a text is invisible before it has travelled a third
//     of the font size, so it never leaves its row
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

struct Drawn { std::string text; float dy, alpha; unsigned long color; };

std::vector<Drawn> draw(const DigitRoll::Roller& r, long long now) {
    DigitRoll::Piece p[DigitRoll::MAX_PIECES];
    const int n = r.pieces(now, p);
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

TEST_CASE("digit roll: starts on the old text, ends on the new one") {
    const DigitRoll::Frame start = DigitRoll::at(0.0f, 1);
    CHECK(start.outDy == doctest::Approx(0.0f));
    CHECK(start.outAlpha == doctest::Approx(1.0f));
    CHECK(start.inAlpha == doctest::Approx(0.0f));
    const DigitRoll::Frame end = DigitRoll::at(1.0f, 1);
    CHECK(end.inDy == doctest::Approx(0.0f));
    CHECK(end.inAlpha == doctest::Approx(1.0f));
    CHECK(end.outAlpha == doctest::Approx(0.0f));
}

TEST_CASE("digit roll: counting up the old rises and the new comes from below, down the reverse") {
    const DigitRoll::Frame up = DigitRoll::at(0.3f, 1);
    CHECK(up.outDy < 0.0f);   // up the screen
    CHECK(up.inDy > 0.0f);    // below
    const DigitRoll::Frame down = DigitRoll::at(0.3f, -1);
    CHECK(down.outDy > 0.0f);
    CHECK(down.inDy < 0.0f);
}

TEST_CASE("digit roll: a text is gone before it has travelled a third of the font size") {
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int i = 0; i <= 100; ++i) {
            const float p = static_cast<float>(i) / 100.0f;
            const DigitRoll::Frame f = DigitRoll::at(p, dir);
            if (f.outAlpha > 0.0f) CHECK(f.outDy * f.outDy < 0.34f * 0.34f);
            if (f.inAlpha > 0.0f) CHECK(f.inDy * f.inDy < 0.34f * 0.34f);
        }
    }
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
