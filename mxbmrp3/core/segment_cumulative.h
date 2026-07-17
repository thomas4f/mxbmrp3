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
