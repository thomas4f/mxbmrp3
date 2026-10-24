// ============================================================================
// core/segment_cumulative.h
// Cumulative aggregation for the custom segment timer.
//
// The segment timer (plugin_data_segment_timer.cpp) times each user-defined arc
// in isolation. This helper makes it aggregate a *contiguous* run through the
// chain the SAME way the official split timer aggregates sectors: segment i's
// displayed time is the running total measured from the chain's first point
// (segment 0), and its "Best" delta is that running total minus the summed
// per-segment bests. So a chain whose points are dropped on the official split
// positions reads exactly like the regular timing HUD (S2 shows S1+S2 vs
// best S1 + best S2). Off a clean run it degrades to the isolated arc.
//
// Deliberately clock-free and free of PluginData so it is unit-testable in
// isolation (tests/unit/test_segment_cumulative.cpp) — the wall-clock timing
// stays in the caller; only the aggregation lives here.
// ============================================================================
#pragma once

// Pure accumulator for one run through the segment chain. All times are seconds.
//
// Usage from updateSegmentTimer():
//   - beginChain()          when segment 0 starts (anchor / re-anchor the run)
//   - breakChain()          when a non-zero segment starts out of sequence, or on
//                           a teleport/reset (the next completion will be isolated)
//   - completeSegment(...)  when a segment finishes (folds it into the run)
// The display then reads the last* snapshot.
struct SegmentCumulative {
    // --- Live accumulator for the run currently in progress --------------------
    bool  active = false;     // in a contiguous run that began at segment 0
    int   nextSeg = 0;        // the segment index expected to complete next
    float time = 0.0f;        // summed time of segments completed so far
    float best = 0.0f;        // summed PRIOR best of those same segments
    bool  bestValid = false;  // every completed segment in the run had a prior best

    // --- Snapshot at the last completion (what the freeze display reads) --------
    float lastTime = 0.0f;      // cumulative time through the last completed segment
    float lastBest = 0.0f;      // cumulative prior-best through it
    bool  lastValid = false;    // it was reached by a contiguous run from segment 0
    bool  lastHasDelta = false; // ...AND every segment in it had a prior best

    void reset() { *this = SegmentCumulative{}; }

    // Segment 0 has started a fresh run: (re)anchor the accumulator at the chain's
    // first point. Mirrors the official timer resetting at the start/finish line.
    void beginChain() {
        active = true;
        nextSeg = 0;
        time = 0.0f;
        best = 0.0f;
        bestValid = true;
    }

    // A non-zero segment started without continuing the active run (mid-chain
    // entry / broken sequence), or a teleport/reset happened: the next completion
    // is treated as an isolated arc rather than part of a cumulative run.
    void breakChain() { active = false; }

    // Fold a just-completed segment into the run.
    //   i        : the segment's index
    //   t        : its measured time (seconds)
    //   hasPrior : it had a session best BEFORE this completion
    //   prior    : that best (only meaningful when hasPrior)
    // A completion that continues the active run (i == nextSeg while active) grows
    // the running total; anything else re-anchors the accumulator on this single
    // arc so the display degrades to the isolated segment (the timer's original
    // behavior) until segment 0 is crossed again.
    void completeSegment(int i, float t, bool hasPrior, float prior) {
        const bool contiguous = active && (i == nextSeg);
        if (contiguous) {
            time += t;
            if (hasPrior && bestValid) best += prior;
            else bestValid = false;         // one missing best voids the cumulative ideal
            nextSeg = i + 1;
        } else {
            // Isolated arc: this segment stands alone (no clean run to add it to).
            active = false;
            time = t;
            best = prior;
            bestValid = hasPrior;
        }
        lastTime = time;
        lastBest = best;
        lastValid = contiguous;             // cumulative time is only meaningful on a clean run
        lastHasDelta = bestValid;           // ...and the delta only when every prior best existed
    }
};

// What a timing line shows in segment mode: the Timing HUD's time cell and "Best"
// row, and the Dash's Timing page, read it from this one place so both agree.
// The shown segment is a just-completed one held through the split-style freeze
// (`holding`), else the one being driven. Its time is the running total from the
// chain's first point on a clean run (the isolated arc off one); the delta is that
// total against the summed per-segment bests, only while held and only when every
// segment in the run had a best. Between deltas, `bestMs` is the passive target:
// the summed bests through the shown segment (its own best off a clean run), or,
// in a dead zone outside every segment, the whole chain's. -1 = nothing yet.
struct SegmentReadout {
    bool frozen = false;   // a completed segment held through the freeze
    int timeMs = -1;       // the shown time; -1 = none
    bool hasDelta = false;
    int deltaMs = 0;       // the cumulative delta to the summed bests
    int deltaRefMs = 0;    // ...and those summed bests
    int bestMs = -1;       // the passive target; -1 = none yet
};

// bests/hasBest: the per-segment session bests (seconds), `count` of them.
// liveArcSec: the wall time since the running segment started (the caller owns
// the clock; ignored with no segment running).
inline SegmentReadout readSegmentLine(const float* bests, const char* hasBest, int count,
                                      int runningSeg, int lastSeg, const SegmentCumulative& cum,
                                      bool holding, double liveArcSec) {
    auto toMs = [](double sec) { return static_cast<int>(sec * 1000.0 + (sec < 0.0 ? -0.5 : 0.5)); };
    // The summed bests through segment idx; -1 if any of them has none yet.
    auto summedBestMs = [&](int idx) {
        float sum = 0.0f;
        for (int k = 0; k <= idx; ++k) {
            if (!hasBest[k]) return -1;
            sum += bests[k];
        }
        return toMs(sum);
    };
    SegmentReadout r;
    int shown = -1;
    if (holding && lastSeg >= 0 && lastSeg < count) {
        shown = lastSeg;
        r.frozen = true;
        r.timeMs = toMs(cum.lastTime);
        if (cum.lastHasDelta) {
            r.hasDelta = true;
            r.deltaMs = toMs(static_cast<double>(cum.lastTime) - static_cast<double>(cum.lastBest));
            r.deltaRefMs = toMs(cum.lastBest);
        }
    } else if (runningSeg >= 0 && runningSeg < count) {
        shown = runningSeg;
        r.timeMs = toMs(cum.active ? static_cast<double>(cum.time) + liveArcSec : liveArcSec);
    }
    const bool cleanRun = r.frozen ? cum.lastValid : cum.active;
    if (shown >= 0) {
        if (cleanRun) r.bestMs = summedBestMs(shown);
        else if (hasBest[shown]) r.bestMs = toMs(bests[shown]);
    } else if (count > 0) {
        r.bestMs = summedBestMs(count - 1);
    }
    return r;
}
