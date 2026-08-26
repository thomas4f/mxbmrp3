// ============================================================================
// core/live_gap_engine.h
// Real-time leader-relative gaps — the pure timing core.
//
// WHAT THIS IS. Given the field flattened in classification order (leader
// first), it stamps the leader's session time at 1%-of-lap resolution and, for
// every other rider, answers "how long after the leader did this rider reach
// the same spot on the same lap" — the live gap the standings/gap-bar/overlay
// show between official classification updates. It owns the per-lap
// timing-point store, including its pruning.
//
// WHY IT LIVES HERE AND NOT IN PluginData. The inputs are six values per rider;
// everything else PluginData knows is irrelevant. Pulling the math out makes it
// compile with a plain g++ and no game, so it is exercised by
// tests/unit/test_live_gap_engine.cpp in ~1s instead of only through the DLL
// under Wine. The end-to-end wiring (staleness gating via the active set, the
// Standings notification coalescing, INI tuning) stays in
// plugin_data_livegaps.cpp and remains pinned by
// tests/integration/tests/livegaps_test.cpp — the two layers check different
// things and both are worth having.
//
// FREEZE SEMANTICS (load-bearing, don't "fix"): a rider whose gap can't be
// computed this batch — not in the position batch (stale data), finished
// (result final), no leader timing for their lap yet, or a non-positive delta
// (quantization artifact) — keeps their LAST shown gap rather than getting 0
// or garbage. GapResult::FREEZE encodes that; only SET carries a new value.
// The leader is always SET 0 (prevents a stale gap surviving a lead change),
// and a lapped rider is SET 0 because a same-lap delta is meaningless for them
// (display falls back to the official lapped-gap fields).
//
// PERFORMANCE CONTRACT (this runs on every RaceTrackPosition batch, ~30Hz):
// the caller owns and reuses the rider/result arrays, so a steady state
// allocates nothing; here only a new lap's timing array allocates, bounded by
// MAX_LAPS_TO_KEEP.
// ============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace LiveGap {

    // 1% of a lap per timing point. Finer costs memory per lap in flight;
    // coarser makes gaps step visibly (the quantization already dominates the
    // Standings notification rate — see m_gapNotifyIntervalMs in plugin_data.h).
    constexpr size_t NUM_TIMING_POINTS = 100;
    constexpr size_t MAX_LAPS_TO_KEEP = 20;

    struct Rider {
        int raceNum = 0;
        float trackPos = 0.0f;  // [0.0, 1.0] along the centerline
        int sessionTime = 0;    // ms; counts DOWN in time-based sessions, UP in lap-based
        int numLaps = 0;
        bool active = false;    // present in the current position batch (data is fresh)
        bool lapped = false;    // a lap or more behind the leader (official gapLaps > 0)
        bool finished = false;  // has taken the checkered flag
    };

    enum class GapAction : uint8_t {
        FREEZE,  // keep the previously shown gap
        SET,     // replace it with `gap`
    };

    struct GapResult {
        GapAction action = GapAction::FREEZE;
        int gap = 0;  // ms behind the leader; only meaningful for SET
    };

    struct TimingPoint {
        int sessionTime = 0;
        int lapNum = -1;  // -1 = never stamped
    };

    class Engine {
    public:
        // riders[0] must be the leader (classification order). Fills `results`
        // (resized to riders.size()) with one entry per rider, index-parallel.
        // `previousGaps` must be index-parallel too and is only read for the
        // threshold test; a size mismatch fails safe (all FREEZE, false).
        // Returns true when any SET gap moved by >= thresholdMs relative to
        // its previous value — the caller's cue that consumers may need a
        // rebuild. If the leader is not active, nothing can be stamped or
        // computed this batch: every rider is FREEZE and the return is false.
        bool update(const std::vector<Rider>& riders, bool timeBasedSession,
                    const std::vector<int>& previousGaps, int thresholdMs,
                    std::vector<GapResult>& results) {
            const size_t count = riders.size();
            results.assign(count, GapResult{});
            if (count == 0 || previousGaps.size() != count) return false;

            const Rider& leader = riders[0];
            if (!leader.active) return false;

            // Stamp when THE leader passed this 1% slot on this lap — always
            // overwrite, regardless of who held the lead when it was last stamped.
            int leaderIndex = positionIndex(leader.trackPos);
            m_laps[leader.numLaps][leaderIndex] =
                TimingPoint{leader.sessionTime, leader.numLaps};

            // Leader's own gap is definitionally 0; SET (not FREEZE) so a stale
            // value can't survive a lead change.
            results[0] = {GapAction::SET, 0};

            bool anyPastThreshold = false;
            int minLapNeeded = leader.numLaps;

            for (size_t i = 1; i < count; i++) {
                const Rider& rider = riders[i];
                if (!rider.active) continue;                      // stale -> FREEZE
                if (rider.lapped) { results[i] = {GapAction::SET, 0}; continue; }
                if (rider.finished) continue;                     // final -> FREEZE

                minLapNeeded = std::min(minLapNeeded, rider.numLaps);

                auto lapIt = m_laps.find(rider.numLaps);
                if (lapIt == m_laps.end()) continue;              // no data yet -> FREEZE

                const TimingPoint& point = lapIt->second[positionIndex(rider.trackPos)];
                if (point.lapNum < 0) continue;                   // slot never stamped -> FREEZE

                // Session clocks run in opposite directions per format; either
                // way "positive = behind the leader".
                int newGap = timeBasedSession ? point.sessionTime - rider.sessionTime
                                              : rider.sessionTime - point.sessionTime;
                if (newGap <= 0) continue;  // quantization artifact -> FREEZE

                results[i] = {GapAction::SET, newGap};
                int oldGap = previousGaps[i];
                int change = (newGap > oldGap) ? (newGap - oldGap) : (oldGap - newGap);
                if (change >= thresholdMs) anyPastThreshold = true;
            }

            prune(minLapNeeded - 1);  // keep one lap of slack behind the slowest rider
            return anyPastThreshold;
        }

        void clear() { m_laps.clear(); }

        // Maps a game-supplied trackPos float onto [0, NUM_TIMING_POINTS-1].
        // Public because tests/asan/memory_safety_fuzz.cpp fuzzes THIS function
        // over adversarial floats (NaN/Inf/huge/negative) and uses the result to
        // index a real timing array under ASan — the whole safety of the
        // m_laps[lap][idx] write rests on this clamp holding for every float.
        static int positionIndex(float trackPos) {
            // Clamp handles trackPos == 1.0 exactly (at the line before the lap increments)
            int index = static_cast<int>(trackPos * static_cast<float>(NUM_TIMING_POINTS));
            return std::max(0, std::min(index, static_cast<int>(NUM_TIMING_POINTS - 1)));
        }

    private:
        void prune(int oldestLapToKeep) {
            for (auto it = m_laps.begin(); it != m_laps.end();) {
                it = (it->first < oldestLapToKeep) ? m_laps.erase(it) : std::next(it);
            }
            while (m_laps.size() > MAX_LAPS_TO_KEEP) {
                m_laps.erase(m_laps.begin());  // std::map: begin() is the oldest lap
            }
        }

        std::map<int, std::array<TimingPoint, NUM_TIMING_POINTS>> m_laps;
    };

}  // namespace LiveGap
