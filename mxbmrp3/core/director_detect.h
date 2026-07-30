// ============================================================================
// core/director_detect.h
// The auto-director's edge detectors: "who just passed someone" and "who is
// tumbling down the order". Pure functions over a position-sorted rider array
// plus a previous-position snapshot — no PluginData, no clock, no member state.
//
// WHY THESE ARE PURE AND WHY THAT MATTERS. Both detectors are comparisons
// between two samples of the field, and both are subtle in ways that reward
// tests over re-reading:
//
//   OVERTAKE is detected from RELATIVE ORDER, not from absolute positions. An
//   adjacent same-lap pair whose order flipped since the last pass is a
//   completed move. That construction is what makes it robust: a third rider
//   pitting or retiring shifts BOTH riders' positions but does not flip their
//   order, so it cannot manufacture a phantom pass. (The detector this replaced
//   keyed on live-gap sign flips, which depended on the frame-to-frame-volatile
//   active-track-position batch and so almost never fired.)
//
//   DROP is measured against a rolling baseline the caller re-seeds on its own
//   window, and is gated on where the slide STARTED, not where it ended — a
//   front-runner sliding to P12 is the story precisely because they started at
//   the front. Gating on the current position would discard exactly the case
//   worth showing.
//
// Both take the caller's already-built rider array (sorted by position, racing
// and on-track only) so a decision pass builds it once. Neither allocates.
//
// Pinned by tests/unit/test_director_detect.cpp — including the pit/retire
// immunity and the started-at-the-front gate, which are the two properties most
// likely to be "simplified" away by someone reading only the code.
// ============================================================================
#pragma once

#include "director_scoring.h"   // Rider

#include <climits>
#include <unordered_map>
#include <vector>

namespace director_detail {

// Previous-pass position snapshot, keyed by race number.
using PositionSnapshot = std::unordered_map<int, int>;

struct OvertakeResult {
    int overtaker = -1;   // race number that completed the pass (-1 = none)
    int overtaken = -1;   // the rider it got by (framed behind in the shot)
    int gained = 0;       // riders cleared by this move, always >= 1 when valid
    bool valid() const { return overtaker >= 0; }
};

// Front-most completed pass within `battleMaxPos` (<= 0 means no cutoff).
//
// `riders` must be sorted by position ascending. Riders with no entry in `prev`
// are skipped, which is what defers the opening-lap scramble: the caller only
// snapshots riders past lap 1, so no pair touching a lap-1 rider can fire.
inline OvertakeResult detectOvertake(const std::vector<Rider>& riders,
                                     const PositionSnapshot& prev,
                                     int battleMaxPos) {
    OvertakeResult out;
    int overtakerPos = INT_MAX;
    int overtakerPrev = 0;

    for (size_t i = 0; i + 1 < riders.size(); ++i) {
        const Rider& a = riders[i];      // now ahead (lower position)
        const Rider& b = riders[i + 1];  // now directly behind
        if (a.gapLaps != b.gapLaps) continue;   // same lap only
        // A finished rider's slow-down lap shuffles positions without an on-track
        // pass, so exclude pairs touching one (mirrors getBattleGroups).
        if (a.finished || b.finished) continue;
        const auto pa = prev.find(a.raceNum);
        const auto pb = prev.find(b.raceNum);
        if (pa == prev.end() || pb == prev.end()) continue;
        if (pa->second <= pb->second) continue;  // order unchanged -> no pass

        const bool within = (battleMaxPos <= 0 || a.position <= battleMaxPos);
        if (within && a.position < overtakerPos) {
            out.overtaker = a.raceNum;
            out.overtaken = b.raceNum;
            overtakerPos = a.position;
            overtakerPrev = pa->second;
        }
    }
    if (!out.valid()) return out;

    // How many racing riders the overtaker actually got by: those ahead of it
    // last pass and behind it now. Only the current on-track field is scanned, so
    // places inherited from a rider ahead pitting or retiring don't inflate it.
    int passed = 0;
    for (const Rider& r : riders) {
        if (r.raceNum == out.overtaker || r.finished) continue;
        const auto pr = prev.find(r.raceNum);
        if (pr != prev.end() && pr->second < overtakerPrev && r.position > overtakerPos) passed++;
    }
    out.gained = (passed < 1) ? 1 : passed;   // at least the immediate rider
    return out;
}

struct DropResult {
    int raceNum = -1;   // the worst tumbler (-1 = none)
    int lost = 0;       // places lost since the baseline
    bool valid() const { return raceNum >= 0; }
};

// Worst tumbler against `base` (the rolling-window baseline snapshot), requiring
// at least `threshold` places lost.
//
// The cutoff is applied to the BASELINE position, not the current one: the story
// is a rider who *was* near the front, so gating on where they ended up would
// filter out the very case worth showing.
inline DropResult detectDrop(const std::vector<Rider>& riders,
                             const PositionSnapshot& base,
                             int battleMaxPos,
                             int threshold) {
    DropResult out;
    for (const Rider& r : riders) {
        // numLaps < 1 keeps the opening-lap shuffle — where everyone's position
        // swings wildly — from reading as a tumble.
        if (r.finished || r.numLaps < 1) continue;
        const auto it = base.find(r.raceNum);
        if (it == base.end()) continue;
        const int lost = r.position - it->second;   // positive = fell back
        const bool within = (battleMaxPos <= 0 || it->second <= battleMaxPos);
        if (within && lost >= threshold && lost > out.lost) {
            out.raceNum = r.raceNum;
            out.lost = lost;
        }
    }
    return out;
}

}  // namespace director_detail
