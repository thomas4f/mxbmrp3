// ============================================================================
// core/director_scoring.h
// The auto-director's story-scoring formulas — the pure arithmetic that decides
// which rider is the most interesting thing on track right now.
//
// WHAT THIS IS. DirectorManager::evaluate() gathers the field, detects edges
// (crashes, passes, tumbles), and hands each candidate a score; the highest wins
// the camera. Everything that turns "P3, 0.4s behind, in a group of 3" into a
// number lives here. Nothing in this file reads PluginData, the clock, or any
// member state — same input, same score, always.
//
// WHY IT LIVES HERE AND NOT IN evaluate(). These formulas ARE the director's
// editorial taste, and they were previously scattered as inline expressions
// through a 700-line function, each with its magic constants a screen apart from
// the comment explaining them. That made them impossible to test and easy to
// tweak by accident. Pulled out, they compile with a plain g++ and no game, so
// tests/unit/test_director_scoring.cpp exercises them in ~1s instead of only
// through the DLL under Wine.
//
// THE RANKING IS THE CONTRACT, not the individual magic numbers. The relative
// order of the story types is what makes the director feel right, and it is what
// the unit tests pin:
//
//     overtake  (x3.0, up to x2.5 more for a multi-place move)
//   > battle    (x2.0, scaled by closeness and group size)
//   > drop      (x1.6, up to x2.0 more for a bigger tumble)
//   > lapper    (x1.2 — filler, must never outrank a real position fight)
//   > leader baseline (x0.6 — floor, so there is never dead air)
//
// Retune a multiplier and you are changing what the director cuts to; the tests
// assert the ORDERING holds, so a change that inverts two story types fails
// loudly rather than shipping as a subtly worse show.
// ============================================================================
#pragma once

#include <algorithm>

namespace director_detail {

// One rider's contribution to a decision pass. Deliberately a flat POD of
// already-resolved values (no map lookups behind it) — evaluate() builds this
// array once per pass and every consumer here reads it directly.
struct Rider {
    int position;
    int raceNum;
    int gapToLeaderMs;
    int gapLaps;
    int numLaps;      // completed laps (for final-lap detection)
    int bestLapMs;    // best lap so far in ms (-1 = none; for fastest-lap detection)
    bool finished;    // crossed the line for good (don't follow incidents/battles on them)
};

// Leaders are worth more than midfield; P1 ~1.8x, fading to 1.0 by ~P11.
// Monotonic non-increasing in `position`, which several call sites rely on:
// comparing posWeight(a) >= posWeight(b) is the same as a <= b, so they compare
// positions directly instead.
inline double posWeight(int position) {
    int boost = 11 - position;
    if (boost < 0) boost = 0;
    return 1.0 + boost * 0.08;
}

// The score floor: the leader is always a candidate so the camera never has
// "nothing" to cut to. Every real story is built to beat this.
inline double leaderBaselineScore() {
    return posWeight(1) * 0.6;
}

// A 3+ rider train nose-to-tail is a better shot than a lone pair.
// 2 riders -> 1.0, 3 -> 1.25, 4 -> 1.5 ... capped at 2.0.
inline double battleSizeBoost(int groupSize) {
    return std::min(2.0, 1.0 + (groupSize - 2) * 0.25);
}

// Battle score. `intervalMs` is the official split gap between the front rider
// and the chaser; closeness is how much of the battle-gap budget they are
// inside, so a nose-to-tail pair scores near the full multiplier and a pair at
// the very edge of the threshold scores near zero.
inline double battleScore(int intervalMs, int battleGapMs, int frontPos, int groupSize) {
    if (battleGapMs <= 0) return 0.0;
    const double closeness = 1.0 - static_cast<double>(intervalMs) / battleGapMs;
    return closeness * posWeight(frontPos) * 2.0 * battleSizeBoost(groupSize);
}

// One rider clearing several at once is a bigger story than a single pass, and
// between two passes the director should prefer the one who gained more.
// 1 pass -> 1.0, 2 -> 1.5, 3 -> 2.0, capped at 2.5 so a pile of lapped-rider
// passes can't dominate the whole show.
inline double overtakePassBoost(int gained) {
    return std::min(2.5, 1.0 + (gained - 1) * 0.5);
}

// A freshly-completed pass outranks a routine battle for its reward window.
inline double overtakeScore(int pos, int gained) {
    return posWeight(pos) * 3.0 * overtakePassBoost(gained);
}

// A front-runner working through backmarkers: good filler, deliberately scored
// BELOW a real position battle (~x2.0) so traffic never steals a live fight.
inline double lapperScore(int pos) {
    return posWeight(pos) * 1.2;
}

// A bigger tumble is a bigger story: at the detection threshold -> 1.0, growing
// 0.25 per extra place lost, capped at 2.0.
inline double dropBoost(int lost, int threshold) {
    return std::min(2.0, 1.0 + (lost - threshold) * 0.25);
}

// A rider sliding down the order — a real story, but below a battle/overtake.
// Weighted by the rider's CURRENT (dropped) position.
inline double dropScore(int pos, int lost, int threshold) {
    return posWeight(pos) * 1.6 * dropBoost(lost, threshold);
}

}  // namespace director_detail
