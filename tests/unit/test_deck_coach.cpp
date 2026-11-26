// ============================================================================
// tests/unit/test_deck_coach.cpp
// Unit tests for hud/deck_coach.h -- the riding decks' game rules: the
// Wheelie deck's hold timer, learned balance zone and cue, the riding
// gauges' Spell timer and the Slide deck's warning bands.
//
// The Wheelie's NEW BEST stamp reads bestSeq, so a case pins that the FIRST
// hold of a session sets a best WITHOUT bumping it: there was nothing to
// beat, and a stamp on every session's first wheelie is noise.
// ============================================================================
#include "doctest.h"

#include "hud/deck_coach.h"
#include "core/fmx_types.h"

#include <cmath>
#include <limits>

using namespace DeckCoach;

namespace {
// One frame on the back wheel at `angle` (the raw pitch reads it nose-up)
void up(Wheelie& w, float angle, float dt) { w.step(angle, false, -angle, dt); }
// One frame with the front wheel down again
void down(Wheelie& w, float dt) { w.step(0.0f, true, 0.0f, dt); }
// Hold a wheelie at `angle` for `seconds` in 10 ms steps, then drop it.
void holdWheelie(Wheelie& w, float angle, float seconds) {
    for (float t = 0.0f; t < seconds - 1e-4f; t += 0.01f) up(w, angle, 0.01f);
    down(w, 0.01f);
}
}  // namespace

TEST_CASE("a wheelie hold counts while the nose is up and keeps its length after") {
    Wheelie w;
    up(w, 10.0f, 0.5f);   // under FROM_DEG: not a wheelie
    CHECK_FALSE(w.active);
    CHECK(w.hold == doctest::Approx(0.0f));
    for (int i = 0; i < 150; ++i) up(w, 30.0f, 0.01f);
    CHECK(w.active);
    CHECK(w.hold == doctest::Approx(1.5f).epsilon(0.01));
    down(w, 0.01f);
    CHECK_FALSE(w.active);
    CHECK(w.hold == doctest::Approx(1.5f).epsilon(0.01));   // shown until the next one
}

TEST_CASE("the first hold sets the best quietly; a longer one stamps") {
    Wheelie w;
    holdWheelie(w, 30.0f, 1.0f);
    CHECK(w.best == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(w.bestSeq == 0);
    holdWheelie(w, 30.0f, 0.5f);   // shorter: nothing
    CHECK(w.bestSeq == 0);
    holdWheelie(w, 30.0f, 2.0f);
    CHECK(w.bestSeq == 1);
    CHECK(w.best == doctest::Approx(2.0f).epsilon(0.02));
}

TEST_CASE("the balance zone is the default until three long holds teach it") {
    Wheelie w;
    CHECK(w.zoneLo() == doctest::Approx(Wheelie::DEFAULT_LO));
    CHECK(w.zoneHi() == doctest::Approx(Wheelie::DEFAULT_HI));
    holdWheelie(w, 50.0f, 1.0f);   // too short to teach anything
    holdWheelie(w, 50.0f, 2.5f);
    holdWheelie(w, 52.0f, 2.5f);
    CHECK_FALSE(w.learned());
    holdWheelie(w, 54.0f, 2.5f);
    REQUIRE(w.learned());
    CHECK(w.centre() == doctest::Approx(52.0f).epsilon(0.01));
    CHECK(w.zoneLo() == doctest::Approx(52.0f - Wheelie::ZONE_HALF).epsilon(0.01));
    CHECK(w.zoneHi() == doctest::Approx(52.0f + Wheelie::ZONE_HALF).epsilon(0.01));
}

// FMX's Wheelie trick and the deck read one rule (core/wheelie_angle.h): in
// at FROM_DEG of the contact-gated angle; out when the front wheel touches or
// the RAW pitch comes back over half of it. The end reads the raw pitch, as
// FMX always has, so a wheelie folded past vertical (pitch back near level,
// roll flipped) ends there although the angle still reads it high.
TEST_CASE("a wheelie hold is one as FMX counts it: in at FROM_DEG, out on the front wheel or the raw pitch") {
    CHECK(WheelieAngle::FROM_DEG == Fmx::FmxConfig{}.wheelieAngleThreshold);
    Wheelie w;
    // Literal angles, so a rule without hysteresis fails
    up(w, 14.0f, 0.1f);
    CHECK_FALSE(w.active);                       // under 15: not yet
    up(w, 15.0f, 0.1f);
    REQUIRE(w.active);
    up(w, 8.0f, 0.1f);                           // the nose bobbing down is the same wheelie
    CHECK(w.active);
    CHECK(w.hold == doctest::Approx(0.2f));
    up(w, 7.0f, 0.1f);                           // raw pitch over -7.5: over
    CHECK_FALSE(w.active);
    // The front wheel touching ends it at any angle
    up(w, 40.0f, 0.1f);
    REQUIRE(w.active);
    w.step(0.0f, true, -40.0f, 0.1f);
    CHECK_FALSE(w.active);
    // Off the ground with the nose up (a jump mid-wheelie) goes on, as FMX's does
    up(w, 40.0f, 0.1f);
    REQUIRE(w.active);
    w.step(0.0f, false, -40.0f, 0.1f);
    CHECK(w.active);
    // Folded past vertical (174 up reads pitch -6): ended, as FMX ends it
    w.step(174.0f, false, -6.0f, 0.1f);
    CHECK_FALSE(w.active);
    CHECK_FALSE(WheelieAngle::starts(8.0f));     // 8 never starts one
    CHECK_FALSE(WheelieAngle::starts(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(WheelieAngle::ends(false, std::numeric_limits<float>::quiet_NaN()));   // FMX lets it pass
    CHECK(WheelieAngle::ends(false, -9.0f, 20.0f));   // a threshold of 20 ends under 10
}

TEST_CASE("a spell times what is going on now, keeps the last, and its max rises with it") {
    Spell s;
    for (int i = 0; i < 100; ++i) s.step(true, 0.01f);
    CHECK(s.active);
    CHECK(s.t == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(s.max == doctest::Approx(1.0f).epsilon(0.01));
    s.step(false, 0.01f);
    CHECK_FALSE(s.active);
    CHECK(s.t == doctest::Approx(1.0f).epsilon(0.01));   // shown, dimmed, until the next
    for (int i = 0; i < 50; ++i) s.step(true, 0.01f);
    CHECK(s.t == doctest::Approx(0.5f).epsilon(0.02));    // a new spell starts from zero
    CHECK(s.max == doctest::Approx(1.0f).epsilon(0.01));  // a shorter one leaves the max
    for (int i = 0; i < 100; ++i) s.step(true, 0.01f);
    CHECK(s.max == doctest::Approx(s.t));                 // a longer one carries it up live
    s.step(true, 0.0f);
    s.step(true, std::numeric_limits<float>::quiet_NaN());
    CHECK(s.t == doctest::Approx(1.5f).epsilon(0.02));
}

TEST_CASE("the slide bands: grip, sliding, save, either way") {
    CHECK(slideBand(0.0f) == SlideBand::GRIP);
    CHECK(slideBand(-5.9f) == SlideBand::GRIP);
    CHECK(slideBand(GRIP_BELOW) == SlideBand::SLIDING);
    CHECK(slideBand(-19.0f) == SlideBand::SLIDING);
    CHECK(slideBand(25.0f) == SlideBand::SLIDING);
    CHECK(slideBand(-SAVE_FROM) == SlideBand::SLIDING);
    CHECK(slideBand(36.0f) == SlideBand::SAVE);
    CHECK(slideBand(std::numeric_limits<float>::infinity()) == SlideBand::GRIP);
}

TEST_CASE("a non-finite angle or step changes nothing") {
    Wheelie w;
    w.step(std::numeric_limits<float>::infinity(), false, -30.0f, 0.01f);
    up(w, 30.0f, 0.0f);
    up(w, 30.0f, std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(w.active);
}
