// ============================================================================
// core/blue_flag_detect.h
// Blue-flag / lapping proximity detection — the pure pairwise core.
//
// WHAT THIS IS. Given a flat array of riders (race number, lap count, track
// position, and two role flags) it answers three questions at once:
//   - which riders are being caught by someone a lap or more ahead (blue flag)
//   - which lapper is catching which backmarker (the director follows this to
//     frame a front-runner working through traffic)
//   - whether the display rider is itself the lapper closing on a backmarker
//
// WHY IT LIVES HERE AND NOT IN PluginData. The inputs are five numbers per
// rider; everything else PluginData knows is irrelevant. Pulling the loop out
// makes it compile with a plain g++ and no game, so it is exercised by
// tests/unit/test_blue_flag_detect.cpp in ~1s instead of only through the DLL
// under Wine. The behaviour is also pinned end-to-end by
// tests/integration/tests/blueflag_test.cpp, which drives the real callbacks —
// the two layers check different things and both are worth having.
//
// PERFORMANCE CONTRACT (this code is on a ~30Hz path with up to 50 riders):
//   - the caller owns the arrays and reuses them across rebuilds, so a steady
//     state allocates nothing; nothing here allocates except the output
//     containers growing on first use
//   - the pairwise loop reads a CONTIGUOUS array on purpose. It used to index
//     two hash maps per inner iteration, which is what made the O(n^2) actually
//     expensive; flattening first is the optimization. Don't reintroduce map
//     lookups inside the loop.
//   - the caller is expected to skip the call entirely when every rider is on
//     the same lap (no lapping is possible), which is the common case.
//
// ORDERING CONTRACT: the outer loop takes the FIRST qualifying lapper it finds
// and stops, and lapperToLapped is last-writer-wins. Both depend on the
// caller's array order, so build the array in a stable order (PluginData builds
// it in m_standings order) or results will churn frame to frame.
// ============================================================================
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlueFlag {

// One rider's contribution. The role flags are deliberately separate because
// the roles have DIFFERENT eligibility, and conflating them changes behaviour:
// a BACKMARKER (the one shown a blue flag) must not be excluded or finished; a
// LAPPER needs a fresh position sample and must not be FINISHED — but MAY be
// otherwise excluded (pit lane), since a rider on pit exit can still be the
// reason someone ahead of them on track is being lapped.
//
// WHY A FINISHED LAPPER DOESN'T COUNT. `laps` is the LIVE count, and it stops
// meaning "race progress" the moment the leader takes the flag: a finished
// rider keeps completing cool-down laps, while everyone still racing is one
// crossing behind by definition. Feed that in raw and P2, racing to the flag
// on the lead lap, reads as "being lapped" by the winner cruising to the pits
// — and the phantom deficit grows every cool-down lap. Nobody needs to yield
// to a rider whose race is over, so the finished are no lappers. (A finished
// BACKMARKER was always excluded; this is the same rule on the other role.)
// Before the leader finishes nobody is finished, so this is provably inert
// during the race itself. Pinned by the "finished riders lap nobody" cases in
// test_blue_flag_detect.cpp.
struct Rider {
    int raceNum = 0;
    int laps = 0;
    float trackPos = 0.0f;          // 0..1 along the centerline
    bool active = false;            // has a fresh track-position sample this batch
    bool eligibleBackmarker = false;  // not excluded from detection, not finished
    bool finished = false;          // has taken the checkered — bars the LAPPER role
};

// The display rider, for the mirror case ("am I the one doing the lapping?").
// active=false disables that half without affecting anything else. `finished`
// bars the mirror the same way it bars the lapper role: a player cruising
// after the flag is not "lapping" the backmarkers they roll past.
struct Player {
    int raceNum = -1;
    int laps = -1;
    float trackPos = 0.0f;
    bool active = false;
    bool finished = false;
};

// Track-position distance from `behind` forward to `ahead`, wrapping through
// start/finish. Both are 0..1; the result is 0..1.
//
// The comparison is `<=`, not `<`, and that matters at exactly one input:
// equal positions. With a strict `<`, equality fell through to the wrap branch
// and returned 1.0 — a lapper sitting exactly on top of a backmarker read as a
// FULL LAP away and was skipped as a blue-flag candidate. Zero is the right
// answer for every consumer here: co-located means adjacent, not distant.
//
// (Reachability was always slim — it needs two riders on different laps at
// bitwise-identical float positions, the scan continues so another lapper in
// range still flags, and the next 30Hz sample separates them — so this was a
// latent wrong answer rather than a reported bug. Fixed on its own rather than
// inside the refactor that surfaced it, so the behaviour change is visible in
// history instead of buried in a "no functional change" commit.)
inline float distanceBehind(float behind, float ahead) {
    return (behind <= ahead) ? (ahead - behind) : ((1.0f - behind) + ahead);
}

// Fills the three outputs. They are CLEARED first, so the caller can hand in
// containers it reuses. `maxLaps` is the highest lap count in the field (a
// rider at it can't be lapped); `awarenessThreshold` is the proximity window as
// a fraction of a lap.
inline void detect(const std::vector<Rider>& riders,
                   int maxLaps,
                   float awarenessThreshold,
                   const Player& player,
                   std::unordered_set<int>& blueFlagged,
                   std::unordered_map<int, int>& lapperToLapped,
                   bool& playerLapping) {
    blueFlagged.clear();
    lapperToLapped.clear();
    playerLapping = false;

    for (const auto& rider : riders) {
        if (!rider.eligibleBackmarker) continue;
        if (rider.laps >= maxLaps) continue;   // on the leader's lap — can't be lapped

        // Mirror case: is the display rider the lapper closing on this
        // backmarker from behind? Folded into the same pass so the common
        // "player is lapping someone" question costs no extra traversal.
        if (player.active && !player.finished && !playerLapping
            && player.raceNum != rider.raceNum && player.laps > rider.laps) {
            if (distanceBehind(player.trackPos, rider.trackPos) <= awarenessThreshold) {
                playerLapping = true;
            }
        }

        for (const auto& other : riders) {
            if (other.raceNum == rider.raceNum) continue;
            if (other.laps < rider.laps + 1) continue;
            // A stale position may be from a previous lap, which would read as
            // false proximity — so a lapper must have a fresh sample. And a
            // finished rider laps nobody (see the struct comment).
            if (!other.active || other.finished) continue;

            if (distanceBehind(other.trackPos, rider.trackPos) <= awarenessThreshold) {
                blueFlagged.insert(rider.raceNum);
                lapperToLapped[other.raceNum] = rider.raceNum;
                break;   // first qualifying lapper wins (see ORDERING CONTRACT)
            }
        }
    }
}

}  // namespace BlueFlag
