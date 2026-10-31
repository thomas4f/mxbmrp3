// ============================================================================
// tests/unit/test_engine_cull.cpp
// EngineCull (core/engine_cull.h): which primitives the engine is spared, and
// that the cut leaves the rest exactly as they were, in order.
//
// The two failure modes worth a test: culling something VISIBLE (a vanished
// panel piece, which no counter notices), and reordering what is kept (the
// engine draws in array order, so a swap is a stacking change). And the copy
// must not happen on a frame with nothing to cut - that is the common case.
// ============================================================================
#include "doctest.h"

#include "core/engine_cull.h"

#include <vector>

namespace {
struct Quad { float m_aafPos[4][2]; int m_iSprite; unsigned long m_ulColor; };
struct Str { char m_szString[100]; float m_afPos[2]; int m_iFont; float m_fSize; int m_iJustify; unsigned long m_ulColor; };

Quad quad(float x, float y, float w, float h, unsigned long color, int sprite = 0) {
    Quad q{};
    q.m_aafPos[0][0] = x;     q.m_aafPos[0][1] = y;
    q.m_aafPos[1][0] = x;     q.m_aafPos[1][1] = y + h;
    q.m_aafPos[2][0] = x + w; q.m_aafPos[2][1] = y + h;
    q.m_aafPos[3][0] = x + w; q.m_aafPos[3][1] = y;
    q.m_iSprite = sprite;
    q.m_ulColor = color;
    return q;
}
Str str(const char* text, unsigned long color) {
    Str s{};
    for (int i = 0; text[i] && i < 99; ++i) s.m_szString[i] = text[i];
    s.m_ulColor = color;
    return s;
}
bool cutQ(const Quad& q) { return EngineCull::invisibleQuad(q); }
bool cutS(const Str& s) { return EngineCull::invisibleString(s); }
}  // namespace

TEST_CASE("a quad is cut only when it cannot put a pixel on screen") {
    CHECK(EngineCull::invisibleQuad(quad(0.1f, 0.1f, 0.2f, 0.2f, 0x00FFFFFFul)));   // alpha 0
    CHECK(EngineCull::invisibleQuad(quad(0.3f, 0.3f, 0.0f, 0.0f, 0xFFFFFFFFul)));   // one point
    CHECK_FALSE(EngineCull::invisibleQuad(quad(0.1f, 0.1f, 0.2f, 0.2f, 0x01000000ul)));  // faint, not gone
    // A hairline (zero width, real height) still rasterises in some engines: kept.
    CHECK_FALSE(EngineCull::invisibleQuad(quad(0.1f, 0.1f, 0.0f, 0.2f, 0xFF000000ul)));
    // Off screen is NOT invisible: the game shows elements outside [0,1] on wide displays.
    CHECK_FALSE(EngineCull::invisibleQuad(quad(-0.5f, 0.1f, 0.2f, 0.2f, 0xFF000000ul)));
}

TEST_CASE("a string is cut only when it has no glyph to draw") {
    CHECK(EngineCull::invisibleString(str("", 0xFFFFFFFFul)));
    CHECK(EngineCull::invisibleString(str("P1", 0x00FFFFFFul)));
    CHECK_FALSE(EngineCull::invisibleString(str(" ", 0xFFFFFFFFul)));   // a space is the font's business
    CHECK_FALSE(EngineCull::invisibleString(str("P1", 0x80FFFFFFul)));
}

TEST_CASE("nothing to cut hands the frame over as is, with no copy") {
    std::vector<Quad> in = { quad(0, 0, 0.1f, 0.1f, 0xFF0000FFul), quad(0.2f, 0, 0.1f, 0.1f, 0xFF00FF00ul) };
    std::vector<Quad> scratch;
    const std::vector<Quad>& out = EngineCull::visibleOnly(in, scratch, cutQ);
    CHECK(&out == &in);
    CHECK(scratch.empty());
}

TEST_CASE("the kept primitives keep their order, whatever is cut between them") {
    std::vector<Quad> in = {
        quad(0.0f, 0, 0.1f, 0.1f, 0x00000000ul),                 // cut (first)
        quad(0.1f, 0, 0.1f, 0.1f, 0xFF000001ul, 1),
        quad(0.2f, 0, 0.0f, 0.0f, 0xFF000002ul),                 // cut (middle)
        quad(0.3f, 0, 0.1f, 0.1f, 0xFF000003ul, 2),
        quad(0.4f, 0, 0.1f, 0.1f, 0xFF000004ul, 1),
        quad(0.5f, 0, 0.1f, 0.1f, 0x00000005ul),                 // cut (last)
    };
    std::vector<Quad> scratch;
    const std::vector<Quad>& out = EngineCull::visibleOnly(in, scratch, cutQ);
    REQUIRE(out.size() == 3);
    CHECK(out[0].m_ulColor == 0xFF000001ul);
    CHECK(out[1].m_ulColor == 0xFF000003ul);
    CHECK(out[2].m_ulColor == 0xFF000004ul);
    CHECK(in.size() == 6);                                       // the plugin's frame is untouched

    std::vector<Str> sin = { str("A", 0xFFFFFFFFul), str("", 0xFFFFFFFFul), str("B", 0xFFFFFFFFul) };
    std::vector<Str> sscratch;
    const std::vector<Str>& sout = EngineCull::visibleOnly(sin, sscratch, cutS);
    REQUIRE(sout.size() == 2);
    CHECK(sout[0].m_szString[0] == 'A');
    CHECK(sout[1].m_szString[0] == 'B');
}

// The copy goes a visible run at a time: every pattern of cut and kept,
// runs of each and either end included, against the one-at-a-time filter.
TEST_CASE("the run-wise copy keeps exactly what a one-at-a-time filter keeps") {
    std::vector<Quad> scratch;
    for (unsigned pattern = 0; pattern < (1u << 10); ++pattern) {
        std::vector<Quad> in;
        std::vector<unsigned long> want;
        for (int i = 0; i < 10; ++i) {
            const bool cut = (pattern >> i) & 1u;
            const unsigned long color = (cut ? 0x00000000ul : 0xFF000000ul) | static_cast<unsigned long>(i);
            in.push_back(quad(0.1f * i, 0, 0.1f, 0.1f, color));
            if (!cut) want.push_back(color);
        }
        const std::vector<Quad>& out = EngineCull::visibleOnly(in, scratch, cutQ);
        INFO("pattern=" << pattern);
        REQUIRE(out.size() == want.size());
        for (size_t k = 0; k < want.size(); ++k) CHECK(out[k].m_ulColor == want[k]);
    }
}

TEST_CASE("a refill of the same size reuses the scratch buffer") {
    std::vector<Quad> in(40, quad(0.1f, 0.1f, 0.1f, 0.1f, 0xFFFFFFFFul));
    in[7].m_ulColor = 0;
    std::vector<Quad> scratch;
    EngineCull::visibleOnly(in, scratch, cutQ);
    const Quad* buffer = scratch.data();
    EngineCull::visibleOnly(in, scratch, cutQ);
    CHECK(scratch.data() == buffer);
    CHECK(scratch.size() == 39);
}
