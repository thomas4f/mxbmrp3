// ============================================================================
// tests/unit/small_vec_test.cpp
// SmallVec<T, N> (core/small_vec.h): inline storage up to N, heap spill beyond.
//
// WHY THIS FILE EXISTS. SmallVec was written for one reason -- to keep
// BaseHud::PanelWant::sectionH from doing a heap round-trip on every HUD rebuild,
// which measured 1.57us inside the game process -- and every shipped HUD but one
// states four sections or fewer. So the SPILL PATH, the half that is actually
// tricky, is the half nothing in the tree exercises: it is reachable today only
// through SessionChartsHud with more than eight charts enabled. A container whose
// hard case is unreachable from the product is a container whose hard case is
// untested, and the failure mode is silent -- a dropped element is a section the
// panel reserves no height for, which reads as a layout bug nowhere near here.
//
// The transition at exactly N is what these cases sit on: crossing it has to
// carry the inline elements over in order and leave size, iteration, indexing and
// equality indistinguishable from a container that never spilled.
// ============================================================================
#include "doctest.h"

#include "core/small_vec.h"

#include <vector>
#include <utility>

namespace {
// The elements a SmallVec holds, in order, however it is storing them -- every
// case below compares against this rather than against data(), so a case cannot
// accidentally pass by reading the buffer the value does not live in.
std::vector<float> drain(const SmallVec<float, 4>& v) {
    std::vector<float> out;
    for (float f : v) out.push_back(f);
    return out;
}
}  // namespace

TEST_CASE("SmallVec: empty by default") {
    SmallVec<float, 4> v;
    CHECK(v.size() == 0);
    CHECK(v.empty());
    CHECK(v.begin() == v.end());
}

TEST_CASE("SmallVec: initializer_list assignment, the shape every HUD uses") {
    SmallVec<float, 4> v = { 1.0f };
    CHECK(v.size() == 1);
    CHECK(v[0] == 1.0f);
    CHECK(v.back() == 1.0f);

    // Reassignment REPLACES rather than appends -- HUDs rebuild into a fresh
    // PanelWant every frame, but planStandardPanel assigns into one twice.
    v = { 2.0f, 3.0f };
    CHECK(v.size() == 2);
    CHECK(drain(v) == std::vector<float>{ 2.0f, 3.0f });
}

TEST_CASE("SmallVec: push_back up to capacity stays inline") {
    SmallVec<float, 4> v;
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<float>(i));
    CHECK(v.size() == 4);
    CHECK(drain(v) == std::vector<float>{ 0.0f, 1.0f, 2.0f, 3.0f });
    CHECK(v.back() == 3.0f);
}

TEST_CASE("SmallVec: spilling past capacity keeps every element, in order") {
    // THE CASE THE PRODUCT CANNOT REACH TODAY. One past N is the transition; the
    // inline elements have to be carried into the spill before the new one lands,
    // or the first N silently become garbage.
    SmallVec<float, 4> v;
    for (int i = 0; i < 9; ++i) v.push_back(static_cast<float>(i * 10));
    CHECK(v.size() == 9);
    CHECK(drain(v) == std::vector<float>{ 0.0f, 10.0f, 20.0f, 30.0f, 40.0f,
                                          50.0f, 60.0f, 70.0f, 80.0f });
    CHECK(v[0] == 0.0f);      // the first inline element survived the move
    CHECK(v[3] == 30.0f);     // the last inline one did too
    CHECK(v[4] == 40.0f);     // and the one that triggered the spill
    CHECK(v.back() == 80.0f);
}

TEST_CASE("SmallVec: clear returns a spilled vector to the inline path") {
    SmallVec<float, 4> v;
    for (int i = 0; i < 9; ++i) v.push_back(static_cast<float>(i));
    v.clear();
    CHECK(v.empty());
    CHECK(v.size() == 0);
    // Spilled-ness is tracked by the spill buffer being non-empty, so a clear that
    // emptied only the count would leave the next push_back appending to stale
    // elements. Refill and check we are back to plain inline behaviour.
    v.push_back(7.0f);
    CHECK(v.size() == 1);
    CHECK(drain(v) == std::vector<float>{ 7.0f });
}

TEST_CASE("SmallVec: copies are independent, inline and spilled alike") {
    // PanelWant is copied into the plan memo (m_planCacheWant) and compared
    // against on the next rebuild, so a copy sharing storage would make the memo
    // always hit.
    SmallVec<float, 4> a = { 1.0f, 2.0f };
    SmallVec<float, 4> b = a;
    b.push_back(3.0f);
    CHECK(a.size() == 2);
    CHECK(b.size() == 3);

    SmallVec<float, 4> big;
    for (int i = 0; i < 7; ++i) big.push_back(static_cast<float>(i));
    SmallVec<float, 4> bigCopy = big;
    bigCopy.push_back(99.0f);
    CHECK(big.size() == 7);
    CHECK(bigCopy.size() == 8);
    CHECK(big.back() == 6.0f);
    CHECK(bigCopy.back() == 99.0f);
}

TEST_CASE("SmallVec: equality is by elements, not by where they are stored") {
    // THE MEMO KEY. sameWant() compares two SmallVecs to decide whether a cached
    // PanelPlan still applies; if equality consulted storage rather than contents,
    // a panel that grew past N and shrank back would miss forever -- or worse,
    // two different asks would compare equal and serve the wrong plan.
    SmallVec<float, 4> inlineOnly = { 1.0f, 2.0f, 3.0f };
    SmallVec<float, 4> alsoInline;
    for (float f : { 1.0f, 2.0f, 3.0f }) alsoInline.push_back(f);
    CHECK(inlineOnly == alsoInline);
    CHECK_FALSE(inlineOnly != alsoInline);

    SmallVec<float, 4> spilled;
    for (int i = 0; i < 6; ++i) spilled.push_back(static_cast<float>(i));
    SmallVec<float, 4> spilledSame;
    for (int i = 0; i < 6; ++i) spilledSame.push_back(static_cast<float>(i));
    CHECK(spilled == spilledSame);

    // Same length, one element apart.
    SmallVec<float, 4> spilledDiff = spilledSame;
    spilledDiff[5] = -1.0f;
    CHECK(spilled != spilledDiff);

    // Different lengths never compare equal, across the capacity boundary too.
    CHECK(inlineOnly != spilled);
    SmallVec<float, 4> shorter = { 1.0f, 2.0f };
    CHECK(inlineOnly != shorter);
}

TEST_CASE("SmallVec: mutating through operator[] works on both paths") {
    SmallVec<float, 4> v = { 1.0f, 2.0f };
    v[1] = 9.0f;
    CHECK(drain(v) == std::vector<float>{ 1.0f, 9.0f });

    SmallVec<float, 4> spilled;
    for (int i = 0; i < 6; ++i) spilled.push_back(0.0f);
    spilled[0] = 5.0f;
    spilled[5] = 6.0f;
    CHECK(spilled[0] == 5.0f);
    CHECK(spilled[5] == 6.0f);
}

TEST_CASE("SmallVec: a moved-from vector is empty, spilled or not") {
    // THE BUG THIS PINS. The implicit move pair broke the "m_spill non-empty
    // IS the spilled flag" invariant on the SOURCE: it moved m_spill (leaving
    // it empty) while COPYING m_size, so a spilled moved-from SmallVec
    // reported size() > N with data() back on the inline buffer — and
    // iterating it read past the inline array. No caller reads a moved-from
    // SmallVec today, which is exactly why it needs a pin: the first one that
    // does would fault only when the list had spilled.
    SmallVec<float, 4> spilled;
    for (int i = 0; i < 6; ++i) spilled.push_back(static_cast<float>(i));

    SmallVec<float, 4> movedTo(std::move(spilled));
    CHECK(spilled.empty());
    CHECK(spilled.size() == 0);
    CHECK(drain(movedTo) == std::vector<float>{ 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f });

    // Move-assignment, both directions across the capacity boundary.
    SmallVec<float, 4> inl = { 1.0f, 2.0f };
    movedTo = std::move(inl);
    CHECK(inl.empty());
    CHECK(drain(movedTo) == std::vector<float>{ 1.0f, 2.0f });

    SmallVec<float, 4> spill2;
    for (int i = 0; i < 6; ++i) spill2.push_back(static_cast<float>(i + 10));
    movedTo = std::move(spill2);
    CHECK(spill2.empty());
    CHECK(movedTo.size() == 6);
    CHECK(movedTo.back() == 15.0f);

    // A moved-from vector is REUSABLE, not just inert: push_back must start
    // over from the inline buffer.
    spill2.push_back(7.0f);
    CHECK(drain(spill2) == std::vector<float>{ 7.0f });
}
