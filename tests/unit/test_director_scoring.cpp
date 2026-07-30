// ============================================================================
// tests/unit/test_director_scoring.cpp
// The auto-director's story-scoring formulas (core/director_scoring.h).
//
// WHAT THESE PIN, AND WHY IT IS THE ORDERING RATHER THAN THE NUMBERS. The
// multipliers are editorial taste and are meant to be tunable; what must NOT
// change by accident is the resulting RANKING of story types, because that is
// what makes the director cut to the right thing. A retune that inverts two
// story types is a silently worse broadcast, so the ranking assertions below are
// the real contract and the exact-value checks are only there to catch a typo.
// ============================================================================
#include "doctest.h"

#include "core/director_scoring.h"

using namespace director_detail;

TEST_CASE("posWeight rewards the front and flattens by P11") {
    CHECK(posWeight(1) == doctest::Approx(1.8));
    CHECK(posWeight(11) == doctest::Approx(1.0));
    // Clamped, not negative: a 40-rider field must not score backmarkers below
    // the floor (a negative weight would flip the sign of every product).
    CHECK(posWeight(12) == doctest::Approx(1.0));
    CHECK(posWeight(50) == doctest::Approx(1.0));

    // Monotonic non-increasing. Several call sites exploit this by comparing
    // positions directly instead of weights (see the incident-preemption gate in
    // evaluate()), so it is a real contract and not just a nicety.
    for (int p = 1; p < 40; ++p) CHECK(posWeight(p) >= posWeight(p + 1));
}

TEST_CASE("battle score scales with closeness and group size") {
    // Nose-to-tail scores near the full multiplier; at the gap threshold, ~zero.
    const double close = battleScore(/*interval*/ 100, /*gap*/ 2000, /*pos*/ 1, /*size*/ 2);
    const double far = battleScore(/*interval*/ 1900, /*gap*/ 2000, /*pos*/ 1, /*size*/ 2);
    CHECK(close > far);
    CHECK(far > 0.0);
    CHECK(battleScore(2000, 2000, 1, 2) == doctest::Approx(0.0));

    // A bigger train beats a lone pair at identical closeness and position.
    CHECK(battleScore(500, 2000, 3, 3) > battleScore(500, 2000, 3, 2));
    CHECK(battleScore(500, 2000, 3, 4) > battleScore(500, 2000, 3, 3));

    // Same fight, further back = lower score.
    CHECK(battleScore(500, 2000, 2, 2) > battleScore(500, 2000, 8, 2));

    // Divide-by-zero guard: a battleGapMs of 0 must not produce inf/NaN.
    CHECK(battleScore(500, 0, 1, 2) == doctest::Approx(0.0));
}

TEST_CASE("size and pass boosts are capped") {
    CHECK(battleSizeBoost(2) == doctest::Approx(1.0));
    CHECK(battleSizeBoost(3) == doctest::Approx(1.25));
    CHECK(battleSizeBoost(6) == doctest::Approx(2.0));
    CHECK(battleSizeBoost(50) == doctest::Approx(2.0));   // cap holds on a huge train

    CHECK(overtakePassBoost(1) == doctest::Approx(1.0));
    CHECK(overtakePassBoost(2) == doctest::Approx(1.5));
    CHECK(overtakePassBoost(4) == doctest::Approx(2.5));
    CHECK(overtakePassBoost(30) == doctest::Approx(2.5));  // a lapped-traffic pile can't dominate
}

TEST_CASE("drop boost grows from the detection threshold") {
    CHECK(dropBoost(3, 3) == doctest::Approx(1.0));   // exactly at threshold
    CHECK(dropBoost(5, 3) == doctest::Approx(1.5));
    CHECK(dropBoost(20, 3) == doctest::Approx(2.0));  // capped
}

TEST_CASE("story ranking: overtake > battle > drop > lapper > leader baseline") {
    // Hold position constant so only the story-type multiplier differs — this is
    // the ordering the director's feel depends on.
    const int pos = 4;
    const double baseline = leaderBaselineScore();
    const double lapper = lapperScore(pos);
    const double drop = dropScore(pos, /*lost*/ 3, /*threshold*/ 3);   // minimum tumble
    const double battle = battleScore(/*interval*/ 1, /*gap*/ 2000, pos, /*size*/ 2);  // near-perfect closeness
    const double overtake = overtakeScore(pos, /*gained*/ 1);          // single pass

    CHECK(overtake > battle);
    CHECK(battle > drop);
    CHECK(drop > lapper);
    CHECK(lapper > baseline);

    // The battle/lapper ordering is NOT unconditional, and the crossover is worth
    // knowing rather than assuming. Battle scales with closeness (x2.0 at the
    // limit) while a lapper is a flat x1.2, so they cross at 60% closeness — an
    // interval of 40% of the battle-gap budget:
    //
    //   closer than that -> the fight wins (a real scrap beats traffic)
    //   wider than that  -> the lapper wins (a pair barely inside the gap
    //                       threshold is not yet a scrap; traffic is the better
    //                       shot until they actually close up)
    //
    // That is deliberate, but it means widening the battle-gap setting makes
    // marginal battles lose to lappers. Pin the crossover so a retune has to
    // face it.
    const int gap = 2000;
    CHECK(battleScore(/*interval*/ 700, gap, pos, 2) > lapper);   // 65% close -> fight
    CHECK(battleScore(/*interval*/ 900, gap, pos, 2) < lapper);   // 55% close -> traffic
    CHECK(battleScore(/*interval*/ 800, gap, pos, 2) == doctest::Approx(lapper));
}

TEST_CASE("a front-runner story outranks the same story further back") {
    CHECK(overtakeScore(1, 1) > overtakeScore(10, 1));
    CHECK(dropScore(2, 4, 3) > dropScore(9, 4, 3));
    CHECK(lapperScore(1) > lapperScore(9));
}

TEST_CASE("a multi-place move outranks a single pass at the same position") {
    CHECK(overtakeScore(5, 3) > overtakeScore(5, 1));
    // ...and a bigger tumble outranks a smaller one.
    CHECK(dropScore(5, 6, 3) > dropScore(5, 3, 3));
}

TEST_CASE("the leader baseline is a floor no real story falls below") {
    // Even the weakest qualifying story at the back of the cutoff beats dead air.
    CHECK(lapperScore(20) > leaderBaselineScore());
    CHECK(dropScore(20, 3, 3) > leaderBaselineScore());
}
