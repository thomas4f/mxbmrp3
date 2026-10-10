// ============================================================================
// hud/official_gap_freeze.h
// The freeze the Gap Bar, the Lap Log's gap row and the Timing panel's Gap section
// share: after each split and the line, hold the OFFICIAL gap (the game's own
// split/lap time against the reference lap's) on screen for a while before going
// back to the live reading. shownGap() is the one place that choice is made, so
// the three cannot print different numbers for the same moment.
//
// AGAINST THE HUD'S OWN REFERENCE. Each HUD reads its live gap against the lap
// its Reference setting names (PbGapTracker::Ref), so the held gap is measured
// against the same lap -- else the readout jumps to another number at every
// split and back. Until 1.31's pre-release it was always the session PB's:
//   - Session PB: the PB lap's accumulated sectors; on the crossing that SETS
//     the PB (gap 0) the previous PB's, so the gain is shown (as TimingHud).
//   - Last lap: the most recent VALID lap before this one (TimingHud's rule;
//     an invalid lap ends without a time, so it is never the live reference
//     either).
//   - All-time PB: the stats file's PB for the current track and bike (the PB
//     scope's, as PluginData plants it), cached BEFORE each lap completes --
//     by then the stats may already hold this lap as the PB. The local
//     player's only: there is none to compare a spectated rider against.
// WHAT IS NOT HELD. The freeze holds the official value of the gap the HUD
// shows, so where it shows none it holds none: an invalid lap (cut, or through
// the pits) starts over at the line, and the out-lap from the pits and a grid
// start's opening lap (no live reading, PluginData::hasValidLiveGap) hold
// nothing at their splits or line. Nor does a reference with no time for the
// crossing. The line ends any split still held; changing the reference drops
// a freeze taken against the old one.
//
// Detection runs whether or not the HUD is shown (a few integer compares): a
// split crossed while it was off must not read as new the moment it is shown.
// A new session or spectate target adopts the splits already there.
// Pinned by pb_gap_test.cpp (the "freeze" cases).
// ============================================================================
#pragma once

#include "../core/pb_gap_tracker.h"
#include "hold_timer.h"
#include "split_crossing.h"

class OfficialGapFreeze {
public:
    using Ref = PbGapTracker::Ref;

    // Once per HUD update. Returns true when what the HUD shows changed (a
    // freeze started, ended or was dropped), so the caller marks itself dirty.
    bool update(Ref ref, int durationMs);

    // Drop the freeze and re-adopt the current splits on the next update.
    void reset();

    bool isFrozen() const { return m_hold.active(); }

    // The gap a HUD shows against `ref` now: the held official one during a
    // freeze, else PluginData's live one, else none (false -> the placeholder).
    bool shownGap(Ref ref, int* gapMs) const;

private:
    static constexpr int NUM_SPLITS = SplitCrossingDetector::NUM_SPLITS;

    void cacheAllTimePB();
    // The reference time to the given crossing (split index, or -1 = the line),
    // -1 when the reference has none.
    int referenceTime(Ref ref, int splitIndex) const;

    SplitCrossingDetector m_crossings;
    bool m_liveSeenThisLap = false;         // the HUD had a live reading on the current lap
    int m_allTimeLap = -1;                  // the all-time PB before the current lap
    int m_allTimeSplits[NUM_SPLITS] = {};   // ...and its accumulated splits

    HoldTimer m_hold;
    int m_frozenGap = 0;
    Ref m_frozenRef = Ref::SESSION_PB;
};
