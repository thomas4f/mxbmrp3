// ============================================================================
// core/field_lap_traces.h
// Every rider's lap as a trace, and the server best lap kept as one -- the
// pure engine behind the "Server best" gap reference.
//
// WHAT THIS IS. The display rider's own laps are traced by PbGapTracker
// (elapsed time at each of 1000 track positions). This records the same table
// for EVERY rider in the game's track position batches, and when a lap the
// game flags as the session's overall best completes, keeps that rider's table
// as the server best trace. PluginData plants it into the tracker as
// Ref::SERVER_BEST, so the Gap Bar, Delta Trace, Map lap delta, Timing and Lap
// Log read a gap to the fastest lap on the server the same way they read one
// to the rider's own PB.
//
// WHY IT WORKS NOW. The position batch carries every rider on track, not only
// the ones near the camera (a 25-entry online tape: up to 21 riders per batch,
// every on-track rider in 75% of batches). Replayed through this rule, every
// lap ridden while batches flowed came out whole, and 50 of 51 matched the
// official lap time within 150 ms (median 13 ms).
//
// A LAP IS FENCED BY ITS POSITION WRAPS, NOT BY ITS RaceLap. The table is the
// time since the rider's position wrapped past the line, so the reference
// starts where the player's own lap clock is compared against it. The RaceLap
// only brings the official time and the overall-best flag, and it arrives
// within a frame or two of the wrap in either order: after it (the lap already
// closed: judged at once) or before it (held until the wrap closes the lap,
// within MATCH_WINDOW_MS).
//
// A TRACE IS KEPT ONLY WHEN IT IS THAT LAP. Every check below must hold, so a
// table that is not the flagged lap is dropped rather than shown as it:
//   - the lap's start was seen: a wrap while the rider was in the batches,
//     with no jump since (a reset to track, a pit exit) -- the first lap a
//     rider is seen on, and laps the game replays on join, have none;
//   - no stretch of the lap without samples longer than MAX_HOLE_POINTS (the
//     tracker's walk-back, so every point of the lap reads);
//   - wrap to wrap within MAX_TIME_ERROR_MS of the official lap time (which
//     also rejects a lap paired with the wrong RaceLap).
// The kept table is then stretched to the official time, so the reference
// ends exactly on the lap the timing screens show.
//
// A server best the engine could not trace leaves best() on the older, slower
// lap. That is not the server best any more; PluginData plants best() only
// while its time IS the session's overall best, so the reference reads nothing
// until a lap it can trace beats it (pinned by server_best_test).
//
// PER RIDER, REGISTERED. erase()/clear() make this a PerRider<> container in
// PluginData: a departed rider's lap in progress goes with them, a reused race
// number starts clean. The kept best survives erase(): the lap was set, as the
// game's overall best (which PluginData keeps) was.
//
// COST. One map lookup and one slot write per rider per batch, on the frame
// rate path at full grids; a rider's tables are allocated once, when first
// seen (8 KB each). Pure (no plugin state, time passed in):
// tests/unit/test_field_lap_traces.cpp drives it with a plain g++.
// ============================================================================
#pragma once

#include "pb_gap_tracker.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>

class FieldLapTraces {
public:
    static constexpr int NUM_POINTS = PbGapTracker::NUM_POINTS;
    // A lap with a longer stretch than this without a sample is not kept (0.9%
    // of the lap: what PbGapTracker::gapAt walks back over).
    static constexpr int MAX_HOLE_POINTS = 9;
    // Wrap-to-wrap time against the official lap time.
    static constexpr int MAX_TIME_ERROR_MS = 150;
    // How far apart a lap's RaceLap and its closing wrap may arrive.
    static constexpr int MATCH_WINDOW_MS = 3000;
    // A move further than this between two samples, other than across the
    // line, is no riding: a reset to track or a pit exit. The lap is broken.
    static constexpr float JUMP_TRACK_POS = 0.05f;

    struct Best {
        PbGapTracker::Table table{};
        int lapTimeMs = 0;   // 0 = none
        int raceNum = -1;
    };

    // A track position sample at nowMs. Returns whether best() changed.
    bool onSample(int raceNum, float trackPos, std::int64_t nowMs) {
        if (!std::isfinite(trackPos)) return false;   // the game's input; NaN would be UB in the slot math
        trackPos = trackPos < 0.0f ? 0.0f : (trackPos > 1.0f ? 1.0f : trackPos);
        Rider& r = m_riders[raceNum];
        bool changed = false;
        if (r.haveLast) {
            const float delta = trackPos - r.lastTrackPos;
            if (delta < -PbGapTracker::WRAP_THRESHOLD) {
                changed = onWrap(r, raceNum, nowMs);
            } else if (std::fabs(delta) > JUMP_TRACK_POS) {
                r.currentObserved = false;
            }
        }
        r.lastTrackPos = trackPos;
        r.haveLast = true;
        if (r.currentObserved) {
            r.current[slotFor(trackPos)] = static_cast<std::int32_t>(nowMs - r.lapStartMs);
        }
        return changed;
    }

    // A rider completed a lap. `candidate`: a valid lap the game flags as the
    // overall best; no other lap is of interest here. Returns whether best() changed.
    bool onLapCompleted(int raceNum, int lapTimeMs, bool candidate, std::int64_t nowMs) {
        if (!candidate || lapTimeMs <= 0) return false;
        auto it = m_riders.find(raceNum);
        if (it == m_riders.end()) return false;   // never seen: a lap replayed on join
        Rider& r = it->second;
        if (r.closedAtMs >= 0 && !r.closedUsed && nowMs - r.closedAtMs <= MATCH_WINDOW_MS) {
            r.closedUsed = true;
            return judge(r, raceNum, lapTimeMs);
        }
        // Before its wrap: the lap is still the one in progress.
        r.awaitingLapMs = lapTimeMs;
        r.awaitingSinceMs = nowMs;
        return false;
    }

    const Best& best() const { return m_best; }

    // PerRider<> container interface (see the header).
    void erase(int raceNum) { m_riders.erase(raceNum); }
    void clear() {
        m_riders.clear();
        m_best = Best{};
    }

private:
    struct Rider {
        std::array<std::int32_t, NUM_POINTS> current;   // ms since the lap's wrap, -1 = no sample
        std::array<std::int32_t, NUM_POINTS> closed;    // the lap the last wrap ended
        std::int64_t lapStartMs = 0;
        std::int64_t closedAtMs = -1;                   // when the last wrap closed `closed`
        std::int64_t awaitingSinceMs = 0;
        int closedDurationMs = 0;
        int awaitingLapMs = 0;                          // a flagged RaceLap waiting for its wrap
        float lastTrackPos = 0.0f;
        bool haveLast = false;
        bool currentObserved = false;                   // the lap in progress started at a seen wrap
        bool closedObserved = false;
        bool closedUsed = false;                        // already paired with a RaceLap
        Rider() {
            current.fill(-1);
            closed.fill(-1);
        }
    };

    static int slotFor(float trackPos) {
        const int slot = static_cast<int>(trackPos * static_cast<float>(NUM_POINTS));
        return slot < 0 ? 0 : (slot >= NUM_POINTS ? NUM_POINTS - 1 : slot);
    }

    // The position wrapped: the lap in progress is closed, a new one starts.
    bool onWrap(Rider& r, int raceNum, std::int64_t nowMs) {
        r.closed = r.current;
        r.closedObserved = r.currentObserved;
        r.closedDurationMs = static_cast<int>(nowMs - r.lapStartMs);
        r.closedAtMs = nowMs;
        r.closedUsed = false;
        r.current.fill(-1);
        r.lapStartMs = nowMs;
        r.currentObserved = true;
        if (r.awaitingLapMs > 0) {
            const int lapTimeMs = r.awaitingLapMs;
            r.awaitingLapMs = 0;
            if (nowMs - r.awaitingSinceMs <= MATCH_WINDOW_MS) {
                r.closedUsed = true;
                return judge(r, raceNum, lapTimeMs);
            }
        }
        return false;
    }

    // Keep the closed lap as the server best if it is the flagged lap whole.
    bool judge(const Rider& r, int raceNum, int lapTimeMs) {
        if (!r.closedObserved || r.closedDurationMs <= 0) return false;
        if (std::abs(r.closedDurationMs - lapTimeMs) > MAX_TIME_ERROR_MS) return false;
        int lastFilled = -1;
        for (int i = 0; i < NUM_POINTS; ++i) {
            if (r.closed[i] < 0) continue;
            if (i - lastFilled - 1 > MAX_HOLE_POINTS) return false;
            lastFilled = i;
        }
        if (NUM_POINTS - 1 - lastFilled > MAX_HOLE_POINTS) return false;
        if (m_best.lapTimeMs > 0 && lapTimeMs >= m_best.lapTimeMs) return false;   // a tie keeps the first

        const double scale = static_cast<double>(lapTimeMs) / r.closedDurationMs;
        for (int i = 0; i < NUM_POINTS; ++i) {
            m_best.table[i] = r.closed[i] < 0
                ? PbGapTracker::Point{}
                : PbGapTracker::Point{ static_cast<int>(std::lround(r.closed[i] * scale)), true };
        }
        m_best.lapTimeMs = lapTimeMs;
        m_best.raceNum = raceNum;
        return true;
    }

    std::unordered_map<int, Rider> m_riders;
    Best m_best;
};
