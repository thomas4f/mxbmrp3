// ============================================================================
// tests/unit/test_deck_score.cpp
// Unit tests for hud/deck_score.h -- the later decks' rules: the Ride
// deck's reaction grade (the REACT stamp) and the Spin deck's wheelspin.
//
// The edges are what reads wrong on screen: a reaction on a limit takes the
// slower grade (GREAT_BELOW is GOOD), a rear wheel off the ground is no spin
// rather than no reading, and the shown share eases toward the raw one but
// drops at once on a crash, so the gauge never coasts down through a fall.
//
// Also hud/deck_format.h's live delta: INT_MIN must not reach
// std::abs, which is undefined for it.
// ============================================================================
#include "doctest.h"

#include "hud/deck_score.h"
#include "hud/deck_format.h"

#include <climits>
#include <cmath>
#include <string>
#include <limits>

using namespace DeckScore;

TEST_CASE("deck score: a reaction is graded by how quickly the bike got away") {
    CHECK(reactionGrade(0.18f) == Reaction::GREAT);
    CHECK(reactionGrade(GREAT_BELOW) == Reaction::GOOD);
    CHECK(reactionGrade(0.49f) == Reaction::GOOD);
    CHECK(reactionGrade(GOOD_BELOW) == Reaction::LATE);
    CHECK(reactionGrade(-0.1f) == Reaction::NONE);
    CHECK(reactionGrade(std::numeric_limits<float>::quiet_NaN()) == Reaction::NONE);
}

TEST_CASE("wheel spin: one overrun ratio, 0 off the ground as for the rumble") {
    CHECK(WheelSpin::overrun(11.0f, 10.0f) == doctest::Approx(0.1f));
    CHECK(WheelSpin::overrun(9.0f, 10.0f) == 0.0f);                    // braking
    CHECK(WheelSpin::overrun(3.0f, 0.0f) == doctest::Approx(3.0f));    // a burnout: over FLOOR_MS, not clamped
    CHECK(WheelSpin::overrun(-1.0f, 10.0f) == 0.0f);                   // rear off the ground
    CHECK(WheelSpin::overrun(std::numeric_limits<float>::quiet_NaN(), 10.0f) == 0.0f);
    CHECK(WheelSpin::overrun(11.0f, std::numeric_limits<float>::infinity()) == 0.0f);
}

TEST_CASE("deck score: wheelspin is the rear's overrun as a share of the bike's speed") {
    CHECK(spinShare(11.0f, 10.0f) == doctest::Approx(0.1f));
    CHECK(spinShare(10.0f, 10.0f) == 0.0f);
    CHECK(spinShare(9.0f, 10.0f) == 0.0f);                 // braking: no spin
    CHECK(spinShare(0.5f, 0.0f) == doctest::Approx(0.5f)); // from a standstill: over FLOOR_MS
    CHECK(spinShare(40.0f, 2.0f) == 1.0f);                 // shown as 1 past it
    CHECK(spinShare(-1.0f, 10.0f) == 0.0f);                // rear off the ground: 0, never "no reading"
    CHECK(spinShare(std::numeric_limits<float>::quiet_NaN(), 10.0f) == 0.0f);
}

TEST_CASE("deck score: the shown spin eases toward the raw one, and drops at once on a crash") {
    CHECK(easeSpin(-1.0f, 0.3f, 0.01f) == doctest::Approx(0.3f));     // out of a crash: no ease up from nothing
    CHECK(easeSpin(0.1f, 0.3f, SPIN_SMOOTH_S * 0.5f) == doctest::Approx(0.2f));
    CHECK(easeSpin(0.1f, 0.3f, SPIN_SMOOTH_S * 3.0f) == doctest::Approx(0.3f));
    CHECK(easeSpin(0.1f, -1.0f, 0.01f) == -1.0f);
}

TEST_CASE("deck formats: a live delta is signed hundredths, INT_MIN included") {
    char buf[32];
    DeckFormat::formatDelta(buf, sizeof(buf), -50);
    CHECK(std::string(buf) == "-0.05");
    DeckFormat::formatDelta(buf, sizeof(buf), 210);
    CHECK(std::string(buf) == "+0.21");
    DeckFormat::formatDelta(buf, sizeof(buf), INT_MIN);
    CHECK(std::string(buf) == "-9:59.9");
    // Unsigned, for a rival's gap and its GAINED/LOST stamp alike: one rounding
    DeckFormat::formatHundredths(buf, sizeof(buf), 1239);
    CHECK(std::string(buf) == "1.23");
    DeckFormat::formatHundredths(buf, sizeof(buf), -1239);
    CHECK(std::string(buf) == "1.23");
    DeckFormat::formatHundredths(buf, sizeof(buf), 62400);
    CHECK(std::string(buf) == "1:02.4");
}

// The decks' clamp01 is Motion's (one name, one meaning): a NaN reads 0 rather
// than slipping both compares and reaching a meter.
TEST_CASE("deck formats: clamp01 is Motion's, NaN reading 0") {
    CHECK(DeckFormat::clamp01(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(Motion::clamp01(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(DeckFormat::clamp01(-0.5f) == 0.0f);
    CHECK(DeckFormat::clamp01(0.25f) == 0.25f);
    CHECK(DeckFormat::clamp01(std::numeric_limits<float>::infinity()) == 1.0f);
}
