// ============================================================================
// tests/unit/test_clip_marks.cpp
// hud/clip_marks.h -- the one edge rule every moving HUD hands its content to.
//
// What it pins: a piece inside its box is left alone and marked nothing; one
// crossing it is cut for our renderers and, for the game, faded by how far it
// crosses (Edge: start, ramp, hidePart), with our renderers' copy kept as it
// was laid out; a card is clamped to the box for the game, a slice wholly past
// it dropped, and what it draws returned in layout coordinates; cuts stay in
// range order whatever order they are made in; the one-axis boxes (rows,
// columns) and a range's shift; and the ramp itself (Motion::fadePast).
// ============================================================================
#include "doctest.h"
#include "hud/clip_marks.h"

#include <vector>

namespace {

SPluginQuad_t quadOver(float l, float t, float r, float b, unsigned long color) {
    SPluginQuad_t q{};
    q.m_aafPos[0][0] = l; q.m_aafPos[0][1] = t;
    q.m_aafPos[1][0] = l; q.m_aafPos[1][1] = b;
    q.m_aafPos[2][0] = r; q.m_aafPos[2][1] = b;
    q.m_aafPos[3][0] = r; q.m_aafPos[3][1] = t;
    q.m_ulColor = color;
    return q;
}

SPluginString_t stringOf(unsigned long color) {
    SPluginString_t s{};
    s.m_ulColor = color;
    return s;
}

const ClipMarks::Rect kBox{ 0.0f, 0.0f, 1.0f, 1.0f };

}  // namespace

TEST_CASE("edge: a piece inside its box is left alone") {
    std::vector<SPluginQuad_t> quads = { quadOver(0.1f, 0.1f, 0.9f, 0.9f, 0xFF112233ul) };
    std::vector<SPluginString_t> strings = { stringOf(0xFFFFFFFFul) };
    ClipMarks m;
    CHECK(m.edge(quads, strings, 0, 1, 0, 1, ClipMarks::Rect{ 0.0f, 0.2f, 1.0f, 1.0f }, kBox, Edge{ 0.0f, 0.1f, true }) ==
          1.0f);
    CHECK(m.empty());
    CHECK(quads[0].m_ulColor == 0xFF112233ul);
}

TEST_CASE("edge: crossing, ours is cut whole and the game's faded by how far it crosses") {
    std::vector<SPluginQuad_t> quads = { quadOver(0.0f, 0.95f, 1.0f, 1.05f, 0xFF112233ul) };
    std::vector<SPluginString_t> strings = { stringOf(0xFFFFFFFFul) };
    ClipMarks m;
    // 0.05 past, over a ramp of 0.1: half.
    const float a = m.edge(quads, strings, 0, 1, 0, 1, ClipMarks::Rect{ 0.0f, 0.95f, 1.0f, 1.05f }, kBox,
                           Edge{ 0.0f, 0.1f, false });
    CHECK(a == doctest::Approx(0.5f));
    REQUIRE(m.marks.size() == 2);
    CHECK(m.marks[0].kind == ClipMarks::Mark::CUT);
    CHECK(m.marks[0].b == doctest::Approx(1.0f));
    CHECK(m.marks[1].kind == ClipMarks::Mark::OURS);
    CHECK(m.quads[0].m_ulColor == 0xFF112233ul);
    CHECK(m.colors[0] == 0xFFFFFFFFul);
    CHECK(quads[0].m_ulColor == Motion::scaleAlpha(0xFF112233ul, 0.5f));
    CHECK(strings[0].m_ulColor == Motion::scaleAlpha(0xFFFFFFFFul, 0.5f));
}

TEST_CASE("edge: the rule's start, ramp and hidePart") {
    // A settings row: a third of a line, a line of 0.03.
    const Edge row{ 0.0f, 0.01f, false };
    CHECK(ClipMarks::gameAlpha(-0.5f, row) == 1.0f);
    CHECK(ClipMarks::gameAlpha(0.005f, row) == doctest::Approx(0.5f));
    CHECK(ClipMarks::gameAlpha(0.01f, row) == 0.0f);
    // The same row at rest: any part past is not drawn at all; inside is whole.
    const Edge rest{ 0.0f, 0.01f, true };
    CHECK(ClipMarks::gameAlpha(0.001f, rest) == 0.0f);
    CHECK(ClipMarks::gameAlpha(0.0f, rest) == 1.0f);
    // A negative start: gone by the time it reaches the edge.
    const Edge early{ -0.2f, 0.2f, false };
    CHECK(ClipMarks::gameAlpha(-0.3f, early) == 1.0f);
    CHECK(ClipMarks::gameAlpha(-0.1f, early) == doctest::Approx(0.5f));
    CHECK(ClipMarks::gameAlpha(0.0f, early) == 0.0f);
    // Overhang is the worst side, negative inside.
    CHECK(ClipMarks::overhang(ClipMarks::Rect{ 0.1f, 0.2f, 0.9f, 0.7f }, kBox) == doctest::Approx(-0.1f));
    CHECK(ClipMarks::overhang(ClipMarks::Rect{ -0.3f, 0.2f, 0.9f, 1.1f }, kBox) == doctest::Approx(0.3f));
}

TEST_CASE("edge: faded inside the box (a negative start) is not cut, still kept as ours") {
    std::vector<SPluginQuad_t> quads = { quadOver(0.85f, 0.4f, 0.95f, 0.6f, 0xFF000000ul) };
    std::vector<SPluginString_t> strings;
    ClipMarks m;
    const float a = m.edge(quads, strings, 0, 1, 0, 0, ClipMarks::Rect{ 0.85f, 0.4f, 0.95f, 0.6f }, kBox,
                           Edge{ -0.1f, 0.1f, false });
    CHECK(a == doctest::Approx(0.5f));
    REQUIRE(m.marks.size() == 1);
    CHECK(m.marks[0].kind == ClipMarks::Mark::OURS);
    CHECK(m.quads[0].m_ulColor == 0xFF000000ul);
}

TEST_CASE("card: clamped to the box for the game, a slice wholly past it dropped") {
    // Three slices down a card that runs from 0.5 to 1.5, its box ending at 1.
    std::vector<SPluginQuad_t> quads = { quadOver(0.0f, 0.5f, 1.0f, 0.6f, 0xFFFFFFFFul),
                                         quadOver(0.0f, 0.6f, 1.0f, 1.4f, 0xFFFFFFFFul),
                                         quadOver(0.0f, 1.4f, 1.0f, 1.5f, 0xFFFFFFFFul) };
    ClipMarks m;
    const ClipMarks::Rect drawn = m.card(quads, 0, 3, 0, kBox, 0.0f, 0.0f);
    CHECK(drawn.t == doctest::Approx(0.5f));
    CHECK(drawn.b == doctest::Approx(1.0f));
    CHECK(quads[0].m_aafPos[1][1] == doctest::Approx(0.6f));   // inside: as laid out
    CHECK(quads[1].m_aafPos[1][1] == doctest::Approx(1.0f));   // the side just stops
    CHECK(quads[2].m_ulColor == 0u);                           // the cap past it: gone
    // Ours: the whole card, cut at the box.
    REQUIRE(m.quads.size() == 3);
    CHECK(m.quads[2].m_aafPos[1][1] == doctest::Approx(1.5f));
    CHECK(m.marks[0].kind == ClipMarks::Mark::CUT);
}

TEST_CASE("card: inside is untouched; offset slices answer in layout coordinates") {
    std::vector<SPluginQuad_t> quads = { quadOver(2.1f, 3.2f, 2.9f, 3.8f, 0xFFFFFFFFul) };
    ClipMarks m;
    const ClipMarks::Rect drawn = m.card(quads, 0, 1, 0, kBox, 2.0f, 3.0f);
    CHECK(m.empty());
    CHECK(drawn.l == doctest::Approx(0.1f));
    CHECK(drawn.b == doctest::Approx(0.8f));
}

TEST_CASE("cuts stay in range order: cards placed after their rows come first") {
    // Two groups, each a card (nine slices) and a row crossing the box; the
    // cards are placed once every row is out, as a list does. The first
    // card's OURS mark lies between, and must not stop the second's cut
    // from finding its place: a cut out of order is dropped, its card uncut.
    std::vector<SPluginQuad_t> quads(20, quadOver(0.0f, 0.9f, 1.0f, 1.2f, 0xFFFFFFFFul));
    std::vector<SPluginString_t> strings(4, stringOf(0xFFFFFFFFul));
    ClipMarks m;
    const ClipMarks::Rect row{ 0.0f, 0.9f, 1.0f, 1.2f };
    m.edge(quads, strings, 9, 10, 0, 2, row, kBox, Edge{ 0.0f, 0.1f, false });
    m.edge(quads, strings, 19, 20, 2, 4, row, kBox, Edge{ 0.0f, 0.1f, false });
    m.card(quads, 0, 9, 0, kBox, 0.0f, 0.0f);
    m.card(quads, 10, 19, 2, kBox, 0.0f, 0.0f);
    int last = -1, lastS = -1, cuts = 0;
    for (const auto& mk : m.marks) {
        if (mk.kind != ClipMarks::Mark::CUT) continue;
        ++cuts;
        CHECK(mk.q0 > last);
        CHECK(mk.s0 >= lastS);
        last = mk.q0;
        lastS = mk.s1;
    }
    CHECK(cuts == 4);
    m.clear();
    CHECK(m.empty());
    CHECK(m.quads.empty());
    CHECK(m.colors.empty());
}

TEST_CASE("edge fade: whole before its start, gone a ramp past it, linear between") {
    CHECK(Motion::fadePast(-1.0f, 2.0f) == 1.0f);
    CHECK(Motion::fadePast(0.0f, 2.0f) == 1.0f);
    CHECK(Motion::fadePast(1.0f, 2.0f) == doctest::Approx(0.5f));
    CHECK(Motion::fadePast(2.0f, 2.0f) == 0.0f);
    CHECK(Motion::fadePast(9.0f, 2.0f) == 0.0f);
}

TEST_CASE("rows and columns: a box that cuts on one axis only; shiftUp moves a range") {
    const ClipMarks::Rect r = ClipMarks::rows(0.2f, 0.4f);
    const ClipMarks::Rect wide{ -0.5f, 0.25f, 1.5f, 0.35f };   // past any HUD across, inside down
    CHECK(ClipMarks::overhang(wide, r) < 0.0f);
    CHECK(ClipMarks::overhang(ClipMarks::Rect{ 0.0f, 0.3f, 1.0f, 0.45f }, r) == doctest::Approx(0.05f));
    const ClipMarks::Rect c = ClipMarks::columns(0.2f, 0.4f);
    CHECK(ClipMarks::overhang(ClipMarks::Rect{ 0.25f, -0.5f, 0.35f, 1.5f }, c) < 0.0f);

    std::vector<SPluginQuad_t> quads{ quadOver(0.0f, 0.5f, 1.0f, 0.6f, 0xFF000000ul),
                                      quadOver(0.0f, 0.5f, 1.0f, 0.6f, 0xFF000000ul) };
    std::vector<SPluginString_t> strings{ stringOf(0xFF000000ul) };
    strings[0].m_afPos[1] = 0.5f;
    ClipMarks::shiftUp(quads, strings, 1, 2, 0, 1, 0.1f);
    CHECK(quads[0].m_aafPos[0][1] == doctest::Approx(0.5f));   // outside the range
    CHECK(quads[1].m_aafPos[0][1] == doctest::Approx(0.4f));
    CHECK(quads[1].m_aafPos[2][1] == doctest::Approx(0.5f));
    CHECK(strings[0].m_afPos[1] == doctest::Approx(0.4f));
}

TEST_CASE("outside: wholly past a side, eps short of touching it") {
    CHECK(ClipMarks::outside(ClipMarks::Rect{ 0.2f, 1.0f, 0.4f, 1.2f }, kBox));   // touching the foot
    CHECK_FALSE(ClipMarks::outside(ClipMarks::Rect{ 0.2f, 0.9f, 0.4f, 1.2f }, kBox));
    CHECK(ClipMarks::outside(ClipMarks::Rect{ 0.2f, 0.9995f, 0.4f, 1.2f }, kBox, 1e-3f));
    CHECK(ClipMarks::outside(ClipMarks::Rect{ -0.5f, 0.2f, 0.0f, 0.4f }, kBox));   // left of it
}

