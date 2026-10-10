// ============================================================================
// core/pb_gap_tracker.h
// The display rider's live gap to their personal-best lap -- the pure engine.
//
// WHAT THIS IS. A lap is sampled as "elapsed time at each of 1000 track
// positions". When a lap completes, its samples become a REFERENCE; on every
// later lap, the gap at the rider's current position is (elapsed now) -
// (elapsed the reference lap had at this position), interpolated between the
// two nearest samples. The same table answers the inverse question -- "where on
// the track WAS the reference lap at this elapsed time" -- which places the
// Gap Bar's ghost marker.
//
// THREE REFERENCES, one sampling. Every committed lap becomes LAST_LAP; a
// session PB becomes SESSION_PB; a lap the caller says beat the all-time PB
// becomes ALLTIME_PB. The Gap Bar's Reference setting picks which one is read.
// ALLTIME_PB is the one that outlives the session: PluginData plants it from
// core/pb_trace_store.h (the persisted table of the rider's own PB on this
// track and bike) whenever the tracker is re-bound to the player, and hands
// the table back to the store when a lap beats it. Session change and spectate
// change reset() all three; forgetBestLap() (lap logs cleared) drops only the
// two the session owns.
//
// WHY IT LIVES IN core/ AND NOT IN THE GAP BAR. It used to be GapBarHud's:
// its own wall-clock anchor (a copy of LapTimer's, minus the grid-start grace),
// its own S/F detection, and a setLiveGap() call that pushed the result INTO
// PluginData so LapLogHud could read it -- a HUD writing the central data store,
// against the one-way flow every other HUD follows, and the reason the Gap Bar
// had to keep updating while hidden. PluginData now owns one PbGapTracker and
// drives it from the SAME transitions that drive the central lap timer
// (plugin_data_lap_timer.cpp), so the clock the gap is measured against is the
// clock the Timing panel shows, and both HUDs are readers. The engine reads
// nothing of PluginData: tests/unit/test_pb_gap_tracker.cpp drives it with a
// plain g++, feeding elapsed times directly.
//
// THE LAP FENCE. A lap ends twice, in either order: the track position WRAPS
// (0.98 -> 0.02) and the RaceLap callback COMPLETES it. Samples that land
// between the two belong to the new lap, and whichever table the completion
// commits must be the OLD lap's, intact. So the wrap is the fence:
//   - wrap before completion: the in-progress table is set aside as PENDING
//     and a fresh one starts; the completion then commits the pending table.
//   - completion before wrap: the completion commits and clears; the few
//     samples the position still takes at 0.99x before it wraps are strays of
//     the lap just ended, and the wrap discards them.
// Until the new lap has its own clock -- the timer re-anchored at the wrap,
// or the completion re-anchored it -- samples are not recorded at all: taken
// against the old lap's clock they would read ~60 s at slot 20. The old HUD
// only sampled from its per-frame update AFTER its own completion check, so it
// met these orders by luck of the frame; the fence meets them by construction.
// A pending table goes STALE if its completion never comes (nothing guarantees
// a RaceLap for every wrap; the one crossing that was in doubt, the out-lap
// after a re-entry, does get one, as a 0 ms lap -- run_handler.cpp): once the
// new lap has progressed past STALE_PENDING_SLOTS, a completion is the new
// lap's own, and the pending one is dropped rather than committed a lap late.
//
// THE OBSERVED-START GATE. A lap is committed as the reference only if its
// start was seen -- a wrap or a completion. Joining a session mid-lap, or
// spectating a rider mid-lap, produces a partial table whose missing head
// would read as "PB was here at 0 ms"; without the gate that partial lap
// becomes the reference the moment it happens to be a PB.
//
// THE GRID-START OPENING LAP IS NEVER THE REFERENCE. On a standing start the
// timer anchors at the green flag and keeps that anchor through the first S/F
// crossing (LapTimer's grace), so lap 1 is measured from the gate, grid run
// included, while every later lap is measured from the S/F line. Committing it
// would offset the whole of lap 2 by the grid run. Lap 1 of a grid start is
// therefore sampled for nothing and the reference arrives with the first PB
// from lap 2 on -- the same moment it did for the official split deltas.
// Nor is anything READ during it: a planted all-time reference is line-
// measured, and lap 1's gate-measured clock would show the grid run as a gap.
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>

class PbGapTracker {
public:
    static constexpr int NUM_POINTS = 1000;   // 0.1% track-position resolution
    static constexpr float WRAP_THRESHOLD = 0.5f;   // the same rule LapTimer uses for S/F
    // A completion this far into the new lap (5% of the track) cannot be the
    // pending lap's: a RaceLap arrives at the line, within a frame or two of the
    // wrap. Stated in track distance so it holds at any callback rate.
    static constexpr int STALE_PENDING_SLOTS = 50;

    struct Point {
        int elapsedMs = 0;
        bool valid = false;
    };
    using Table = std::array<Point, NUM_POINTS>;

    // A lap's gap to a reference at each slot (see lastLapGaps).
    struct GapPoint {
        int gapMs = 0;
        bool valid = false;
    };
    using GapTable = std::array<GapPoint, NUM_POINTS>;

    // Which reference a read compares against. Saved by value in the Gap Bar's
    // INI section, so new entries are appended.
    enum class Ref : uint8_t {
        SESSION_PB = 0,   // the session's best lap
        ALLTIME_PB = 1,   // the all-time best on this track and bike (persisted)
        LAST_LAP = 2      // the lap just completed
    };
    static constexpr int REF_COUNT = 3;

    // Everything: session change, spectate target change. The all-time
    // reference goes too: it belongs to one rider on one track and bike, and
    // PluginData re-plants it from the store once it knows which.
    void reset() {
        for (auto& ref : m_refs) ref = Reference{};
        m_current.fill(Point{});
        m_currentObserved = false;
        m_pending.fill(Point{});
        m_pendingObserved = false;
        m_hasPending = false;
        m_completedSinceWrap = false;
        m_awaitingClock = false;
        m_gridStartLap = false;
        m_maxSlotSinceWrap = 0;
        m_lineOpen = false;
        m_trackPos = 0.0f;
        m_lastTrackPos = 0.0f;
        m_haveLastTrackPos = false;
        clearLastLapGaps();
    }

    // The session's laps are gone (lap logs cleared) but the in-progress lap is
    // still being sampled and may become the next reference. The all-time
    // reference is not the session's to drop.
    void forgetBestLap() {
        m_refs[at(Ref::SESSION_PB)] = Reference{};
        m_refs[at(Ref::LAST_LAP)] = Reference{};
        clearLastLapGaps();
    }

    // Plant a reference from outside -- the persisted all-time table.
    void setReference(Ref ref, const Table& table, int lapTimeMs) {
        Reference& r = m_refs[at(ref)];
        r.table = table;
        r.lapTimeMs = lapTimeMs;
        r.has = lapTimeMs > 0;
    }
    const Table& referenceTable(Ref ref) const { return m_refs[at(ref)].table; }

    // The in-progress lap died (pit exit): drop its samples and anything set
    // aside for it, keep the reference. The next wrap starts a fresh lap, and
    // only that wrap makes it an observed one: the run from the pits to the
    // line is a partial, and the observed-start gate has to say so.
    void clearCurrentLap() {
        m_current.fill(Point{});
        m_currentObserved = false;
        m_pending.fill(Point{});
        m_hasPending = false;
        m_awaitingClock = false;
        m_gridStartLap = false;
        m_lineOpen = false;
    }

    // The rider re-entered the track somewhere else (LapTimer::forgetTrackPosition):
    // the next sample must not be read as a wrap from where they left, else the
    // run from the pit box is fenced as an observed lap.
    void forgetTrackPosition() {
        m_haveLastTrackPos = false;
    }

    // The gate dropped on a standing start: lap 1 is under way with its start
    // observed, measured from the gate -- see the header on why it is sampled
    // for nothing. LapTimer suppresses the re-anchor at its first S/F crossing,
    // so that wrap must not fence a new lap either.
    void onGridStart() {
        m_current.fill(Point{});
        m_currentObserved = true;
        m_gridStartLap = true;
        m_awaitingClock = false;
        m_lineOpen = false;
    }

    // A track-position sample. elapsedMs is the lap timer's elapsed time now
    // (-1 unanchored); timerReanchoredHere says the timer re-anchored at 0 on
    // THIS sample (LapTimer::onTrackPosition returned true), which is what makes
    // the new lap's clock available immediately after a wrap.
    void onTrackPosition(float trackPos, int elapsedMs, bool timerReanchoredHere) {
        if (!std::isfinite(trackPos)) return;   // the game's input; a NaN here is UB in the slot math
        trackPos = std::clamp(trackPos, 0.0f, 1.0f);
        m_trackPos = trackPos;

        if (m_haveLastTrackPos && (trackPos - m_lastTrackPos) < -WRAP_THRESHOLD) {
            onWrap(timerReanchoredHere);
        }
        m_lastTrackPos = trackPos;
        m_haveLastTrackPos = true;

        const int slot = indexFor(trackPos);
        m_maxSlotSinceWrap = std::max(m_maxSlotSinceWrap, slot);
        // A wrap-first line window whose completion never comes (nothing
        // guarantees a RaceLap for every wrap; see the header) closes by the
        // same staleness rule that drops its pending table, else the whole
        // lap reads blank. The completion-first window (m_completedSinceWrap) is
        // still waiting for its wrap at 0.99x and is not stale. Nor is a window
        // still waiting for its clock: the reads it guards would be the old lap's
        // elapsed against the reference's start, a lap ahead, so it stays open
        // until something brings the new lap's clock.
        if (m_lineOpen && !m_completedSinceWrap && !m_awaitingClock && m_maxSlotSinceWrap >= STALE_PENDING_SLOTS) {
            m_lineOpen = false;
        }
        if (elapsedMs >= 0 && !m_awaitingClock) {
            m_current[slot] = Point{ elapsedMs, true };
        }
    }

    // The lap completed with lapTimeMs. The lap that ended is the pending table
    // when a wrap already fenced it, else the in-progress one. A lap whose start
    // was observed is committed -- as LAST_LAP always, as SESSION_PB when it is
    // the session's best, as ALLTIME_PB when the caller says it beat the all-time
    // best; a grid-start opening lap never is. Returns whether it was committed,
    // so the caller can hand referenceTable(Ref::LAST_LAP) to the store.
    // The completion re-anchors the timer, so the new lap has its clock.
    bool onLapCompleted(int lapTimeMs, bool isPersonalBest, bool isAllTimeBest = false) {
        const bool wrapWasThisLine = m_maxSlotSinceWrap < STALE_PENDING_SLOTS;
        const bool usePending = m_hasPending && wrapWasThisLine;
        const Table& ended = usePending ? m_pending : m_current;
        const bool endedObserved = usePending ? m_pendingObserved : m_currentObserved;
        const bool committed = endedObserved && !m_gridStartLap && lapTimeMs > 0;
        if (committed) {
            recordLastLapGaps(ended);   // before the lap replaces the references it was ridden against
            setReference(Ref::LAST_LAP, ended, lapTimeMs);
            if (isPersonalBest) setReference(Ref::SESSION_PB, ended, lapTimeMs);
            if (isAllTimeBest) setReference(Ref::ALLTIME_PB, ended, lapTimeMs);
        }
        if (!usePending) {
            m_current.fill(Point{});     // completion-first: the new lap starts now
        }
        m_hasPending = false;
        m_currentObserved = true;        // the completion IS a lap start
        // Only a completion-first ending leaves strays for the next wrap to shed.
        // After a wrap-first one that wrap is the NEXT lap's fence: told it had
        // strays, it wiped that whole lap and its completion committed the empty
        // table (the consecutive wrap-first cases in test_pb_gap_tracker.cpp).
        m_completedSinceWrap = !wrapWasThisLine;
        m_awaitingClock = false;
        m_gridStartLap = false;
        // Wrap-first: this completion closes the line window. Completion-first:
        // it opens it, until the wrap.
        m_lineOpen = !wrapWasThisLine;
        return committed;
    }

    bool hasBestLap(Ref ref = Ref::SESSION_PB) const { return m_refs[at(ref)].has; }
    int bestLapTimeMs(Ref ref = Ref::SESSION_PB) const { return m_refs[at(ref)].lapTimeMs; }
    float trackPos() const { return m_trackPos; }
    // The lap in progress as sampled so far (the lap delta profile reads it next
    // to referenceTable()). Slots the lap has not reached are invalid.
    const Table& currentTable() const { return m_current; }
    // The lap last committed, as its gap to each reference AS IT STOOD while
    // that lap was ridden - taken at the commit, before the lap replaces any of
    // them, so a PB lap reads against the PB it beat rather than as a flat line
    // against itself. What the lap delta displays keep showing of the previous
    // lap while the next one draws over it. An uncommitted lap end (the run
    // from the pits, a grid-start lap) leaves it alone: it is still the last
    // lap ridden whole.
    const GapTable& lastLapGaps(Ref ref) const { return m_lastLapGaps[at(ref)].table; }
    bool hasLastLapGaps(Ref ref) const { return m_lastLapGaps[at(ref)].has; }
    // Changes whenever lastLapGaps() does: a reader's cache key.
    unsigned lastLapStamp() const { return m_lastLapStamp; }
    // Whether the lap now in progress can become the reference when it ends.
    bool currentLapObserved() const { return m_currentObserved && !m_gridStartLap; }
    // THE LINE WINDOW: the frames between the two ends of a lap, in either order.
    // Wrapped but the timer still runs the OLD clock (no re-anchor; the RaceLap
    // has not arrived): a read compares a whole lap of elapsed time against the
    // reference's first slots, +42 s in red. Completed but not yet wrapped: the
    // timer already restarted while the position still reads 0.99, a whole lap
    // AHEAD. The samples are already discarded in both windows; the reads must
    // be too. The real capture in pb_gap_golden_test hits both at its lines.
    bool atLine() const { return m_lineOpen; }

    // Gap at `trackPos` with `elapsedMs` on the clock: positive = behind the PB,
    // negative = ahead. Interpolates between the two enclosing PB samples; with
    // one missing, uses the other; with both missing, walks back up to 9 slots
    // for the nearest earlier sample. Returns 0 and *ok=false when there is no
    // reference, no sample near enough to compare against, or a non-finite
    // position -- a caller must not show that 0 as a gap.
    int gapAt(float trackPos, int elapsedMs, bool* ok = nullptr, Ref ref = Ref::SESSION_PB) const {
        if (ok) *ok = false;
        const Reference& r = m_refs[at(ref)];
        // No read during the grid-start opening lap: its clock runs from the
        // gate, so against ANY line-measured reference it reads the grid run as
        // a gap. The session reference could not exist yet; a planted all-time
        // one can, and was read that way until this guard. The flag clears on
        // the lap's completion, so a RaceLap the game never reports blanks one
        // extra lap -- the safe direction, and the same bound the pending-table
        // staleness already accepts.
        if (!r.has || m_gridStartLap || elapsedMs < 0 || !std::isfinite(trackPos)) return 0;
        const Table& best = r.table;

        const float exact = std::clamp(trackPos, 0.0f, 1.0f) * static_cast<float>(NUM_POINTS);
        const int lower = std::clamp(static_cast<int>(exact), 0, NUM_POINTS - 1);
        const int upper = std::clamp(lower + 1, 0, NUM_POINTS - 1);
        const float fraction = exact - static_cast<float>(static_cast<int>(exact));

        // 64-bit throughout: the samples are ints the game's clock produced, but a
        // difference of two of them is not guaranteed to fit one (UBSan, fed the
        // extremes by tests/asan/memory_safety_fuzz.cpp, says so).
        const Point& lo = best[lower];
        const Point& hi = best[upper];
        std::int64_t reference = 0;
        if (lo.valid && hi.valid) {
            const std::int64_t span = static_cast<std::int64_t>(hi.elapsedMs) - lo.elapsedMs;
            reference = lo.elapsedMs + static_cast<std::int64_t>(fraction * static_cast<float>(span));
        } else if (lo.valid) {
            reference = lo.elapsedMs;
        } else if (hi.valid) {
            reference = hi.elapsedMs;
        } else {
            bool found = false;
            for (int offset = 1; offset < 10 && !found; ++offset) {
                const int idx = lower - offset;
                if (idx >= 0 && best[idx].valid) {
                    reference = best[idx].elapsedMs;
                    found = true;
                }
            }
            if (!found) return 0;
        }
        if (ok) *ok = true;
        const std::int64_t gap = static_cast<std::int64_t>(elapsedMs) - reference;
        return static_cast<int>(std::clamp<std::int64_t>(gap, INT_MIN, INT_MAX));
    }

    // Where the PB lap was at `elapsedMs` into the lap, as a track position
    // 0..1 -- the ghost marker. Interpolates between the enclosing samples;
    // clamps to 1.0 once the PB lap would already have finished. -1 when there
    // is no reference.
    float bestLapProgressAt(int elapsedMs, Ref ref = Ref::SESSION_PB) const {
        const Reference& r = m_refs[at(ref)];
        if (!r.has || m_gridStartLap || r.lapTimeMs <= 0 || elapsedMs < 0) return -1.0f;
        const Table& best = r.table;
        for (int i = 0; i < NUM_POINTS; ++i) {
            if (!best[i].valid || best[i].elapsedMs < elapsedMs) continue;
            if (i > 0 && best[i - 1].valid) {
                const std::int64_t prev = best[i - 1].elapsedMs;
                const std::int64_t here = best[i].elapsedMs;
                if (here > prev) {
                    const float fraction = static_cast<float>(elapsedMs - prev) /
                                           static_cast<float>(here - prev);
                    return (static_cast<float>(i - 1) + fraction) / static_cast<float>(NUM_POINTS);
                }
            }
            return static_cast<float>(i) / static_cast<float>(NUM_POINTS);
        }
        return 1.0f;
    }

private:
    static int indexFor(float trackPos) {
        return std::clamp(static_cast<int>(trackPos * static_cast<float>(NUM_POINTS)), 0, NUM_POINTS - 1);
    }

    // The position wrapped past S/F. See THE LAP FENCE in the header.
    void onWrap(bool timerReanchoredHere) {
        if (m_gridStartLap) {
            // A crossing while the grace holds. The first is the grid run reaching
            // the line: the timer keeps its gate anchor and the lap keeps being
            // (pointlessly) sampled; nothing ends here. The lap's END can be a
            // wrap too, before the RaceLap that ends the grace, and that RaceLap
            // is its whole fence: its fill discards the samples taken here on the
            // old clock, and the count restarts so it reads as at-the-line rather
            // than completion-first -- which opened the line window for all of
            // lap 2 and had lap 2's wrap shed that lap as strays. No reference can
            // be read meanwhile: the session start that raised the grace reset it.
            m_maxSlotSinceWrap = 0;
            return;
        }
        if (m_completedSinceWrap) {
            // The completion already ended the lap; what the table holds are the
            // strays taken at 0.99x since. The clock is already the new lap's, and
            // the position now agrees with it: the line window closes.
            m_current.fill(Point{});
            m_awaitingClock = false;
            m_lineOpen = false;
        } else {
            // The lap ends here; its completion is still to come. Set the table
            // aside for it and start the new lap's, whose clock exists only if
            // the timer re-anchored on this very sample.
            m_pending = m_current;
            m_pendingObserved = m_currentObserved;
            m_hasPending = true;
            m_current.fill(Point{});
            m_awaitingClock = !timerReanchoredHere;
            m_lineOpen = true;               // until the completion lands
        }
        m_currentObserved = true;
        m_completedSinceWrap = false;
        m_maxSlotSinceWrap = 0;
    }

    static constexpr size_t at(Ref ref) { return static_cast<size_t>(ref); }

    // Once per committed lap: NUM_POINTS gapAt() reads per reference.
    void recordLastLapGaps(const Table& ended) {
        for (int r = 0; r < REF_COUNT; ++r) {
            LapGaps& out = m_lastLapGaps[static_cast<size_t>(r)];
            out.table.fill(GapPoint{});
            out.has = false;
            for (int i = 0; i < NUM_POINTS; ++i) {
                if (!ended[i].valid) continue;
                bool ok = false;
                const int gap = gapAt(static_cast<float>(i) / static_cast<float>(NUM_POINTS),
                                      ended[i].elapsedMs, &ok, static_cast<Ref>(r));
                if (!ok) continue;
                out.table[i] = GapPoint{ gap, true };
                out.has = true;
            }
        }
        ++m_lastLapStamp;
    }

    void clearLastLapGaps() {
        bool any = false;
        for (auto& g : m_lastLapGaps) {
            if (!g.has) continue;
            g.table.fill(GapPoint{});
            g.has = false;
            any = true;
        }
        if (any) ++m_lastLapStamp;
    }

    struct Reference {
        Table table{};
        int lapTimeMs = 0;
        bool has = false;
    };
    std::array<Reference, REF_COUNT> m_refs{};

    struct LapGaps {
        GapTable table{};
        bool has = false;
    };
    std::array<LapGaps, REF_COUNT> m_lastLapGaps{};   // see lastLapGaps()
    unsigned m_lastLapStamp = 0;

    Table m_current{};                 // the lap in progress
    bool m_currentObserved = false;

    Table m_pending{};                 // a lap that wrapped but has not completed yet
    bool m_pendingObserved = false;
    bool m_hasPending = false;

    bool m_completedSinceWrap = false; // completion arrived; the next wrap only sheds strays
    bool m_awaitingClock = false;      // wrapped, but the timer still runs the old lap's clock
    bool m_gridStartLap = false;       // lap 1 of a standing start: measured from the gate
    int m_maxSlotSinceWrap = 0;        // how far the lap since the last wrap has got (staleness)
    bool m_lineOpen = false;           // between the two ends of a lap, either order (see atLine)

    float m_trackPos = 0.0f;
    float m_lastTrackPos = 0.0f;
    bool m_haveLastTrackPos = false;
};
