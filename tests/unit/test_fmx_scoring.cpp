// ============================================================================
// tests/unit/test_fmx_scoring.cpp
// Pure-logic unit tests for the FMX scoring math (core/fmx_scoring.h), which was
// extracted out of FmxManager so it could be exercised headless. Pins the two
// scoring calculations against the behavior documented in the source:
//   - calculateTrickScore: rotation scaling (floored at 1x), the air vs ground
//     duration/distance bonus, and the clutch-scaled coaster bonus,
//   - calculateChainMultiplier: the free first trick, the +chainBonus per extra
//     trick, the diminishing repetition penalty, L/R variants counting as one
//     type, and the optional in-progress "extra" trick.
//
// Doctest impl lives in test_plugin_utils.cpp; this TU only adds cases.
// ============================================================================
#include "doctest.h"

#include "core/fmx_scoring.h"

using namespace Fmx;

// A TrickInstance carrying just the fields the scoring reads.
static TrickInstance makeTrick(TrickType type, int baseScore, float duration,
                               float distance, float peakPitch = 0.0f,
                               float clutchHeldTime = 0.0f) {
    TrickInstance t;
    t.type = type;
    t.baseScore = baseScore;
    t.duration = duration;
    t.distance = distance;
    t.peakPitch = peakPitch;
    t.clutchHeldTime = clutchHeldTime;
    return t;
}

TEST_CASE("fmx scoring: rotation scale floors at 1x and scales past a full turn") {
    FmxConfig cfg;  // defaults

    // BACKFLIP is an air trick on the PITCH axis. Zero duration/distance keeps
    // the air bonus at exactly 1.0 so only the rotation scale is under test.
    // 720 deg = 2.0x; base 100 -> 200.
    CHECK(calculateTrickScore(makeTrick(TrickType::BACKFLIP, 100, 0.0f, 0.0f, 720.0f), cfg) == 200);

    // Under a full rotation the scale is floored at 1.0x (max(1, deg/360)),
    // so 350 deg is NOT 0.97x — it is 1.0x. Pins the floor, not the comment.
    CHECK(calculateTrickScore(makeTrick(TrickType::BACKFLIP, 100, 0.0f, 0.0f, 350.0f), cfg) == 100);
}

TEST_CASE("fmx scoring: air tricks scale with duration and distance") {
    FmxConfig cfg;  // durationBonusRate 0.25, distanceBonusRate 0.01
    // AIR has no rotation axis (scale 1.0). airBonus = 1 + 2*0.25 + 50*0.01 = 2.0.
    // base 50 -> 100.
    CHECK(calculateTrickScore(makeTrick(TrickType::AIR, 50, 2.0f, 50.0f), cfg) == 100);
}

TEST_CASE("fmx scoring: ground tricks scale with duration (floored) and distance") {
    FmxConfig cfg;
    // WHEELIE full duration = BALANCE_TRICK_FULL_DURATION (2s). duration 6s -> 3.0,
    // plus distance 100m * 0.01 = 1.0 -> groundBonus 4.0. base 10 -> 40.
    CHECK(calculateTrickScore(makeTrick(TrickType::WHEELIE, 10, 6.0f, 100.0f), cfg) == 40);
}

TEST_CASE("fmx scoring: coaster bonus is clutch-scaled and doubles a plain wheelie") {
    FmxConfig cfg;
    // Plain 2s wheelie: base 10, groundBonus max(1, 2/2)=1.0 -> 10.
    int wheelie = calculateTrickScore(makeTrick(TrickType::WHEELIE, 10, 2.0f, 0.0f), cfg);
    CHECK(wheelie == 10);

    // Coaster wheelie, clutch held the whole 2s (ratio 1.0): +COASTER_SCORE_BONUS
    // before the (1.0x) ground bonus -> (10 + 10) = 20, i.e. ~2x the plain wheelie.
    int coaster = calculateTrickScore(
        makeTrick(TrickType::COASTER_WHEELIE, 10, 2.0f, 0.0f, /*pitch*/0.0f, /*clutch*/2.0f), cfg);
    CHECK(coaster == 20);
    CHECK(coaster == 2 * wheelie);

    // Half the clutch time -> half the bonus: 10 + 5 = 15.
    int halfClutch = calculateTrickScore(
        makeTrick(TrickType::COASTER_WHEELIE, 10, 2.0f, 0.0f, 0.0f, /*clutch*/1.0f), cfg);
    CHECK(halfClutch == 15);
}

TEST_CASE("fmx chain: first trick is free, each extra adds a diminishing bonus") {
    FmxConfig cfg;  // chainBonusPerTrick 0.5, repetitionPenalty 0.5

    // No tricks / a single trick: multiplier stays 1.0 (the first is free).
    CHECK(calculateChainMultiplier({}, TrickType::NONE, cfg) == doctest::Approx(1.0f));
    CHECK(calculateChainMultiplier({makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f)},
                                   TrickType::NONE, cfg) == doctest::Approx(1.0f));

    // Two DIFFERENT tricks -> full +0.5 -> 1.5.
    std::vector<TrickInstance> twoDifferent = {
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
        makeTrick(TrickType::BACKFLIP, 100, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(twoDifferent, TrickType::NONE, cfg) == doctest::Approx(1.5f));

    // Two of the SAME type -> repetition penalty halves the bonus -> 1.25.
    std::vector<TrickInstance> twoSame = {
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(twoSame, TrickType::NONE, cfg) == doctest::Approx(1.25f));
}

TEST_CASE("fmx chain: L/R variants are one type, and the extra active trick counts") {
    FmxConfig cfg;

    // DRIFT_LEFT + DRIFT_RIGHT share a base type -> counts as a repeat -> 1.25.
    std::vector<TrickInstance> lr = {
        makeTrick(TrickType::DRIFT_LEFT, 10, 1.0f, 0.0f),
        makeTrick(TrickType::DRIFT_RIGHT, 10, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(lr, TrickType::NONE, cfg) == doctest::Approx(1.25f));

    // One banked trick + a different in-progress (extra) trick -> +0.5 -> 1.5.
    std::vector<TrickInstance> one = {makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f)};
    CHECK(calculateChainMultiplier(one, TrickType::BACKFLIP, cfg) == doctest::Approx(1.5f));
}
