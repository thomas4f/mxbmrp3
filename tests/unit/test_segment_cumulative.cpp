// ============================================================================
// tests/unit/test_segment_cumulative.cpp
// Unit tests for the segment-timer aggregation in core/segment_cumulative.h.
//
// This is the logic that makes the custom segment timer AGGREGATE a contiguous
// run through the chain the same way the official split timer aggregates
// sectors: the running total from the chain's first point, and a cumulative
// delta vs the summed per-segment bests. The header is clock-free and
// dependency-free (no PluginData, no steady_clock), so it runs here with no game
// engine. This TU does NOT define the doctest main (test_plugin_utils.cpp does).
// ============================================================================
#include "doctest.h"

#include "core/segment_cumulative.h"

// A clean 3-segment run accumulates times AND deltas from the chain start, just
// like the official timer's S1 / S1+S2 / S1+S2+S3 splits.
TEST_CASE("segment cumulative: contiguous run aggregates like official splits") {
    SegmentCumulative c;
    c.beginChain();  // segment 0 starts

    c.completeSegment(0, 30.0f, /*hasPrior*/ true, /*prior*/ 29.0f);
    CHECK(c.lastValid);
    CHECK(c.lastHasDelta);
    CHECK(c.lastTime == doctest::Approx(30.0f));
    CHECK(c.lastBest == doctest::Approx(29.0f));            // cumulative delta = +1.0

    c.completeSegment(1, 25.0f, true, 24.0f);
    CHECK(c.lastTime == doctest::Approx(55.0f));            // running total, not the arc
    CHECK(c.lastBest == doctest::Approx(53.0f));            // cumulative delta = +2.0
    CHECK(c.lastValid);

    c.completeSegment(2, 35.0f, true, 36.0f);               // faster than its own best
    CHECK(c.lastTime == doctest::Approx(90.0f));
    CHECK(c.lastBest == doctest::Approx(89.0f));            // cumulative delta = +1.0
    CHECK(c.lastHasDelta);
}

// The acceptance criterion: points dropped on the official split positions must
// read like the regular timer. That reduces to this identity — the aggregated
// time/best equal the summed per-segment values, so the cumulative delta equals
// the sum of the per-segment deltas.
TEST_CASE("segment cumulative: on-sectors identity (sum of arcs = split total)") {
    const float s1 = 31.2f, s2 = 28.7f, s3 = 33.9f;   // "sector" times
    const float b1 = 30.9f, b2 = 29.1f, b3 = 33.0f;   // "best sectors"

    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, s1, true, b1);
    c.completeSegment(1, s2, true, b2);
    c.completeSegment(2, s3, true, b3);

    CHECK(c.lastTime == doctest::Approx(s1 + s2 + s3));
    CHECK(c.lastBest == doctest::Approx(b1 + b2 + b3));
    // Cumulative delta == sum of per-segment deltas.
    CHECK((c.lastTime - c.lastBest) ==
          doctest::Approx((s1 - b1) + (s2 - b2) + (s3 - b3)));
}

// A completion that is not part of an active run (mid-chain entry) stands alone:
// the display degrades to the isolated arc, exactly the timer's original behavior.
TEST_CASE("segment cumulative: isolated arc when no clean run is active") {
    SegmentCumulative c;  // never beginChain()'d
    c.completeSegment(2, 40.0f, true, 41.0f);
    CHECK_FALSE(c.lastValid);                 // not a cumulative run
    CHECK(c.lastHasDelta);                    // but the single arc still has a delta
    CHECK(c.lastTime == doctest::Approx(40.0f));
    CHECK(c.lastBest == doctest::Approx(41.0f));
}

// Completing a segment out of sequence (skipping one) re-anchors on that arc,
// even without an explicit breakChain() from the caller.
TEST_CASE("segment cumulative: out-of-order completion re-anchors") {
    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, 30.0f, true, 29.0f);  // nextSeg -> 1
    c.completeSegment(2, 40.0f, true, 39.0f);  // i=2 != nextSeg=1 -> isolated
    CHECK_FALSE(c.lastValid);
    CHECK(c.lastTime == doctest::Approx(40.0f));
}

// One missing prior best voids the cumulative *delta* for the rest of the run
// (you can't form a full ideal), but the *time* keeps aggregating.
TEST_CASE("segment cumulative: missing best voids delta, time still accumulates") {
    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, 30.0f, true, 29.0f);
    CHECK(c.lastHasDelta);

    c.completeSegment(1, 25.0f, /*hasPrior*/ false, 0.0f);  // no best yet for seg 1
    CHECK(c.lastTime == doctest::Approx(55.0f));            // time still cumulative
    CHECK_FALSE(c.lastHasDelta);                            // ...but no clean delta

    c.completeSegment(2, 35.0f, true, 34.0f);
    CHECK(c.lastTime == doctest::Approx(90.0f));
    CHECK_FALSE(c.lastHasDelta);                            // stays void for the run
}

// A break mid-run (teleport / reset) makes the next completion isolated.
TEST_CASE("segment cumulative: breakChain stops aggregation") {
    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, 30.0f, true, 29.0f);
    c.breakChain();                             // e.g. forward teleport
    c.completeSegment(1, 25.0f, true, 24.0f);
    CHECK_FALSE(c.lastValid);
    CHECK(c.lastTime == doctest::Approx(25.0f));  // isolated, not 55
}

// A fresh beginChain() re-anchors: the next lap starts the running total over.
TEST_CASE("segment cumulative: beginChain re-anchors each lap") {
    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, 30.0f, true, 29.0f);
    c.completeSegment(1, 25.0f, true, 24.0f);
    CHECK(c.lastTime == doctest::Approx(55.0f));

    c.beginChain();                             // new lap crosses segment 0 again
    CHECK(c.active);
    CHECK(c.time == doctest::Approx(0.0f));
    c.completeSegment(0, 28.0f, true, 30.0f);
    CHECK(c.lastTime == doctest::Approx(28.0f));  // reset, not 83
}

// A single-point "loop" segment: each lap crosses the one point, which completes
// segment 0 (the whole-lap loop) and immediately re-anchors it (beginChain). Verify
// the loop reports each lap's time and a delta vs the loop's own best.
TEST_CASE("segment cumulative: single-point loop times each lap vs its best") {
    SegmentCumulative c;

    c.beginChain();                              // first cross of the point starts the loop
    c.completeSegment(0, 90.0f, false, 0.0f);    // lap 1 completes: no prior loop best yet
    CHECK(c.lastValid);
    CHECK_FALSE(c.lastHasDelta);
    CHECK(c.lastTime == doctest::Approx(90.0f));
    c.beginChain();                              // the same crossing re-anchors for lap 2

    c.completeSegment(0, 88.0f, true, 90.0f);    // lap 2 vs the 90.0 loop best
    CHECK(c.lastValid);
    CHECK(c.lastHasDelta);
    CHECK(c.lastTime == doctest::Approx(88.0f));
    CHECK((c.lastTime - c.lastBest) == doctest::Approx(-2.0f));  // 2s faster
    c.beginChain();                              // re-anchor for lap 3

    c.completeSegment(0, 91.0f, true, 88.0f);    // lap 3 vs the improved 88.0 best
    CHECK(c.lastTime == doctest::Approx(91.0f));
    CHECK((c.lastTime - c.lastBest) == doctest::Approx(3.0f));   // 3s slower
}

// reset() clears everything (used by resetSegmentTimer on session/track changes).
TEST_CASE("segment cumulative: reset clears state") {
    SegmentCumulative c;
    c.beginChain();
    c.completeSegment(0, 30.0f, true, 29.0f);
    c.reset();
    CHECK_FALSE(c.active);
    CHECK_FALSE(c.lastValid);
    CHECK(c.time == doctest::Approx(0.0f));
    CHECK(c.lastTime == doctest::Approx(0.0f));
}
