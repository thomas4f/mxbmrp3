// ============================================================================
// core/feather_touch.h
// Feather Touch: a 25 m jump landed under 10 g, which is the whole rule and
// exactly what the row says ("Land 10 jumps of 25m under 10G"). Distance is
// take-off to touchdown; the g is the hardest taken from touchdown through
// the settle window FmxManager already waits out before a flight counts as
// ridden away from (so a soft landing you crash out of is not one).
//
// NO HEIGHT GATE. It had one (3 m above take-off), but the row's sentence has
// no room to say so, and a rule the sentence hides is the Steady Hands
// mistake. On the recording below it cost one soft landing in four: a 25 m
// flight under 3 m is a long, low table, still a jump.
//
// CUT FROM A RECORDING (a practice session, 22 Oct 2026, 109 landed
// flights): the highest any flight went was 4.6 m, so Sent It's 5 m Bronze
// would have made a height bar unreachable on that track. 28 flights were
// 25 m+; their landings peaked at 8.6-16.9 g (gravity included, a bike at
// rest reads ~1 g), median 12.5, and four of the 28 stayed under 10 g. So
// a session of jumping earns a handful, and the ladder is a career.
// Pinned by tests/unit/test_achievement_feeds.cpp.
// ============================================================================
#pragma once

namespace FeatherTouch {

constexpr float MIN_DISTANCE_M = 25.0f;
constexpr float MAX_G = 10.0f;

inline bool soft(float distanceM, float peakG) {
    return distanceM >= MIN_DISTANCE_M && peakG > 0.0f && peakG < MAX_G;
}

}  // namespace FeatherTouch
