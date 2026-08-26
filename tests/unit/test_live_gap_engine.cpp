// ============================================================================
// tests/unit/test_live_gap_engine.cpp
// The live leader-relative gap core (core/live_gap_engine.h), previously
// inline in PluginData::updateRealTimeGaps and reachable only through the DLL
// under Wine. Pins:
//   - gap direction per session format: time-based clocks count DOWN (leader
//     time - rider time), lap-based count UP (rider time - leader time) —
//     getting this backwards silently shows every gap negative-frozen
//   - the FREEZE semantics: inactive (stale batch), finished, no leader
//     timing for the rider's lap, and non-positive deltas all keep the last
//     shown value; only SET carries a new gap
//   - leader always SET 0 (a stale gap must not survive a lead change), and
//     an inactive LEADER freezes the whole field (no stale baseline)
//   - lapped riders SET 0 (they display the official lapped gap instead)
//   - the threshold return: sub-threshold changes still update the gap but
//     report false, which is what keeps full grids from rebuilding every HUD
//     on every 30Hz batch
//   - trackPos quantization incl. the 1.0 clamp, and lap pruning (a rider on
//     a pruned lap can only FREEZE)
// The end-to-end wiring (active-set gating, notification coalescing) stays
// pinned by tests/integration/tests/livegaps_test.cpp.
// ============================================================================
#include "doctest.h"

#include "core/live_gap_engine.h"

#include <vector>

using LiveGap::Engine;
using LiveGap::GapAction;
using LiveGap::GapResult;
using LiveGap::Rider;

namespace {

Rider rider(int raceNum, float trackPos, int sessionTime, int numLaps,
            bool active = true, bool lapped = false, bool finished = false) {
    Rider r;
    r.raceNum = raceNum;
    r.trackPos = trackPos;
    r.sessionTime = sessionTime;
    r.numLaps = numLaps;
    r.active = active;
    r.lapped = lapped;
    r.finished = finished;
    return r;
}

// Run one engine update with previous gaps defaulting to 0.
bool run(Engine& engine, const std::vector<Rider>& riders, bool timeBased,
         std::vector<GapResult>& results, std::vector<int> prev = {},
         int thresholdMs = 100) {
    prev.resize(riders.size(), 0);
    return engine.update(riders, timeBased, prev, thresholdMs, results);
}

}  // namespace

TEST_CASE("lap-based session: rider gap is rider time minus leader time") {
    Engine engine;
    std::vector<GapResult> results;

    // Leader stamps slot 50 (trackPos 0.50) at t=100000 on lap 3.
    run(engine, {rider(1, 0.50f, 100000, 3)}, false, results);
    // Rider reaches the same slot on the same lap 2.5s later.
    bool changed = run(engine, {rider(1, 0.60f, 103000, 3),
                                rider(2, 0.505f, 102500, 3)}, false, results);
    CHECK(changed);
    REQUIRE(results.size() == 2);
    CHECK(results[0].action == GapAction::SET);
    CHECK(results[0].gap == 0);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 2500);
}

TEST_CASE("time-based session: countdown clock flips the subtraction") {
    Engine engine;
    std::vector<GapResult> results;

    // Countdown: leader passes slot 50 with 300000 ms left; the rider passes
    // it later, so LESS time remains (297000). Gap = 300000 - 297000.
    run(engine, {rider(1, 0.50f, 300000, 3)}, true, results);
    run(engine, {rider(1, 0.60f, 298000, 3),
                 rider(2, 0.505f, 297000, 3)}, true, results);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 3000);
    // Overtime: sessionTime can go negative; the delta must still work.
    run(engine, {rider(1, 0.70f, -1000, 3)}, true, results);
    run(engine, {rider(1, 0.80f, -2000, 3),
                 rider(2, 0.705f, -4000, 3)}, true, results);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 3000);
}

TEST_CASE("freeze semantics") {
    Engine engine;
    std::vector<GapResult> results;
    run(engine, {rider(1, 0.50f, 100000, 3)}, false, results);

    SUBCASE("inactive rider freezes") {
        run(engine, {rider(1, 0.60f, 103000, 3),
                     rider(2, 0.505f, 102000, 3, /*active=*/false)}, false, results);
        CHECK(results[1].action == GapAction::FREEZE);
    }
    SUBCASE("finished rider freezes") {
        run(engine, {rider(1, 0.60f, 103000, 3),
                     rider(2, 0.505f, 102000, 3, true, false, /*finished=*/true)},
            false, results);
        CHECK(results[1].action == GapAction::FREEZE);
    }
    SUBCASE("no leader timing for the rider's lap freezes") {
        run(engine, {rider(1, 0.60f, 103000, 3),
                     rider(2, 0.505f, 102000, /*numLaps=*/1)}, false, results);
        CHECK(results[1].action == GapAction::FREEZE);
    }
    SUBCASE("slot the leader never stamped freezes") {
        run(engine, {rider(1, 0.60f, 103000, 3),
                     rider(2, 0.905f, 102000, 3)}, false, results);
        CHECK(results[1].action == GapAction::FREEZE);
    }
    SUBCASE("non-positive delta freezes instead of going negative") {
        // Rider reaches the slot EARLIER than the stamped leader time
        // (possible right after a lead change / quantization edge).
        run(engine, {rider(1, 0.60f, 103000, 3),
                     rider(2, 0.505f, 99000, 3)}, false, results);
        CHECK(results[1].action == GapAction::FREEZE);
    }
    SUBCASE("inactive leader freezes the entire field") {
        bool changed = run(engine, {rider(1, 0.60f, 103000, 3, /*active=*/false),
                                    rider(2, 0.505f, 102000, 3)}, false, results);
        CHECK_FALSE(changed);
        CHECK(results[0].action == GapAction::FREEZE);
        CHECK(results[1].action == GapAction::FREEZE);
    }
}

TEST_CASE("lapped rider is SET 0, not frozen") {
    Engine engine;
    std::vector<GapResult> results;
    run(engine, {rider(1, 0.50f, 100000, 3)}, false, results);
    run(engine, {rider(1, 0.60f, 103000, 3),
                 rider(2, 0.505f, 102000, 2, true, /*lapped=*/true)}, false, results);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 0);
}

TEST_CASE("threshold: sub-threshold change still SETs but returns false") {
    Engine engine;
    std::vector<GapResult> results;
    run(engine, {rider(1, 0.50f, 100000, 3)}, false, results);

    // Previous gap 2450, new gap 2500 -> 50ms change, below the 100ms threshold.
    bool changed = engine.update(
        {rider(1, 0.60f, 103000, 3), rider(2, 0.505f, 102500, 3)},
        false, {0, 2450}, 100, results);
    CHECK_FALSE(changed);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 2500);
}

TEST_CASE("trackPos 1.0 clamps into the last slot") {
    Engine engine;
    std::vector<GapResult> results;
    // Leader exactly on the line stamps slot 99, rider at 0.995 reads slot 99.
    run(engine, {rider(1, 1.0f, 100000, 3)}, false, results);
    run(engine, {rider(1, 0.05f, 103000, 3),
                 rider(2, 0.995f, 101200, 3)}, false, results);
    CHECK(results[1].action == GapAction::SET);
    CHECK(results[1].gap == 1200);
}

TEST_CASE("laps behind the slowest rider are pruned") {
    Engine engine;
    std::vector<GapResult> results;
    run(engine, {rider(1, 0.50f, 100000, 1)}, false, results);
    // Everyone advances to lap 4: minLapNeeded=4, keep >= 3, lap 1 is pruned.
    run(engine, {rider(1, 0.60f, 200000, 4),
                 rider(2, 0.55f, 201000, 4)}, false, results);
    // A rider reappearing on lap 1 can only freeze now.
    run(engine, {rider(1, 0.70f, 203000, 4),
                 rider(2, 0.505f, 202000, 1)}, false, results);
    CHECK(results[1].action == GapAction::FREEZE);
}

TEST_CASE("clear() drops all timing data") {
    Engine engine;
    std::vector<GapResult> results;
    run(engine, {rider(1, 0.50f, 100000, 3)}, false, results);
    engine.clear();
    run(engine, {rider(1, 0.60f, 103000, 3),
                 rider(2, 0.505f, 102000, 3)}, false, results);
    CHECK(results[1].action == GapAction::FREEZE);
}
