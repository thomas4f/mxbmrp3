// ============================================================================
// tests/unit/test_severity_ramp.cpp
// The shared POSITIVE -> NEUTRAL -> NEGATIVE gauge ramp (hud/severity_ramp.h).
//
// WHY THIS IS WORTH A TEST. The ramp had been written out three times before it
// was a function, and each copy was a chance for the two halves to disagree about
// where the middle is. What matters is not any one colour but that the SHAPE holds:
// the endpoints are exact, the midpoint is exactly NEUTRAL, and both halves are
// linear -- because that is what lets two gauges at the same fraction of their own
// full scale read as the same severity.
//
// The alpha case is the one that bit in the original: interpolating RGB only forces
// the output opaque, which silently ignores a palette slot a user made translucent.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/hud/severity_ramp.h"

namespace {

// The engine's colour word is 0xAABBGGRR.
constexpr unsigned long rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}
constexpr unsigned chan(unsigned long v, int shift) { return (v >> shift) & 0xFF; }

constexpr unsigned long LO  = rgba(0, 255, 0);      // POSITIVE
constexpr unsigned long MID = rgba(255, 255, 0);    // NEUTRAL
constexpr unsigned long HI  = rgba(255, 0, 0);      // NEGATIVE

}  // namespace

TEST_CASE("severity ramp: the three anchors are exact") {
    CHECK(SeverityRamp::at(0.0f, LO, MID, HI) == LO);
    CHECK(SeverityRamp::at(0.5f, LO, MID, HI) == MID);
    CHECK(SeverityRamp::at(1.0f, LO, MID, HI) == HI);
}

TEST_CASE("severity ramp: out-of-range readings clamp, never wrap") {
    // A gauge feeds this `reading / fullScale`, and a reading past full scale is
    // normal (a big landing, full lock). Clamping is what keeps that pinned at
    // NEGATIVE instead of walking back down the ramp.
    CHECK(SeverityRamp::at(-3.0f, LO, MID, HI) == LO);
    CHECK(SeverityRamp::at(9.0f, LO, MID, HI) == HI);
}

TEST_CASE("severity ramp: each half is linear between its anchors") {
    // Quarter points: halfway from LO to MID, and halfway from MID to HI.
    const unsigned long q1 = SeverityRamp::at(0.25f, LO, MID, HI);
    CHECK(chan(q1, 0) == 128);    // R: 0 -> 255, halfway (rounded)
    CHECK(chan(q1, 8) == 255);    // G: 255 throughout
    CHECK(chan(q1, 16) == 0);     // B: 0 throughout

    const unsigned long q3 = SeverityRamp::at(0.75f, LO, MID, HI);
    CHECK(chan(q3, 0) == 255);    // R: 255 throughout
    CHECK(chan(q3, 8) == 128);    // G: 255 -> 0, halfway (rounded)
}

TEST_CASE("severity ramp: alpha travels with the colour") {
    // The bug this pins: lerping RGB alone and rebuilding the word with a hardcoded
    // 0xFF, so a user's translucent palette slot came out fully opaque.
    const unsigned long lo = rgba(0, 255, 0, 0);
    const unsigned long mid = rgba(255, 255, 0, 128);
    const unsigned long hi = rgba(255, 0, 0, 255);

    CHECK(chan(SeverityRamp::at(0.0f, lo, mid, hi), 24) == 0);
    CHECK(chan(SeverityRamp::at(0.5f, lo, mid, hi), 24) == 128);
    CHECK(chan(SeverityRamp::at(1.0f, lo, mid, hi), 24) == 255);
    CHECK(chan(SeverityRamp::at(0.25f, lo, mid, hi), 24) == 64);
}

TEST_CASE("severity ramp: a monochrome palette stays monochrome") {
    // Not a curiosity: a user may set all three slots the same, and every reading
    // must then produce that exact colour rather than a rounding-drifted neighbour.
    const unsigned long grey = rgba(90, 90, 90, 200);
    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        CHECK(SeverityRamp::at(t, grey, grey, grey) == grey);
    }
}
