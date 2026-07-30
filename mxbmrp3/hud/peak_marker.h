// ============================================================================
// hud/peak_marker.h
// The "max marker" state machine shared by every gauge that shows a peak:
// hold the highest recent reading, reveal it once the live value falls away,
// then fade it out after a linger. Pure state transitions — no rendering, no
// PluginData, no time source (the caller ticks it once per rebuilt frame).
//
// Callers: LeanWidget (lean and steer, both sides), BarsWidget (one per bar),
// RumbleHud (one per motor).
//
// TWO RELATED CALL SITES ARE DELIBERATELY LEFT ALONE, because folding them in
// would change behaviour rather than share it:
//   - BarsWidget's crash snapshot tests `currentValue >= 0.01f` where
//     snapOnImpact() below tests `magnitude > deadzone`. The two differ at
//     exactly the threshold, and a parameter to select which comparison applies
//     costs more than the four lines it would save.
//   - GForceWidget keeps a single RADIAL marker (one magnitude, no opposed
//     sides) rather than the two-sided gauge this models. It shares the idea,
//     not the state.
//
// THE STATE IS TWO VALUES, and their relationship is the whole contract:
//   value           - the peak being held, as a MAGNITUDE (always >= 0)
//   framesRemaining - linger countdown; > 0 is exactly "this marker is on screen"
// Callers gate rendering on framesRemaining > 0, never on value != 0: a steady
// hold leaves a nonzero value with the linger unarmed, and drawing that would
// pin a marker under the live needle permanently.
//
// THE COUNTERINTUITIVE BRANCH, and the reason this is worth a unit test: a NEW
// PEAK HIDES THE MARKER (framesRemaining = 0) rather than showing it. While the
// rider is still leaning further there is nothing useful to mark — the marker
// would sit exactly under the live value. It only becomes information once the
// value retreats, which is what arms the linger. Getting this backwards produces
// a marker that is always visible and always redundant, which reads as "working"
// in a screenshot.
//
// THE THRESHOLD IS DEADBAND ON BOTH SIDES, not a single trip point: a reading
// must exceed value + threshold to re-peak, and drop below value - threshold to
// arm the linger. In between, nothing happens. Telemetry is noisy at the top of
// an arc, and a single trip point makes the marker flicker there.
//
// These take references to the caller's existing members rather than owning a
// struct, deliberately: the linger length is a PERSISTED per-HUD setting
// (`maxMarkerLingerFrames`) and several HUDs keep parallel arrays indexed by bar,
// so an owned type would churn the on-disk settings for no behavioural gain.
//
// Pinned by tests/unit/test_peak_marker.cpp (~1s, no game).
// ============================================================================
#pragma once

namespace PeakMarker {

namespace detail {
// Tick a live linger down one frame, clearing the held peak when it expires.
// Callers reach this through advanceActive/advanceIdle; both guard framesRemaining
// > 0 first, so this is only ever entered on a marker that is actually on screen.
inline void tickLinger(float& value, int& framesRemaining) {
    framesRemaining--;
    if (framesRemaining == 0) value = 0.0f;
}
}  // namespace detail

// Advance the marker for a gauge the live reading is currently ON.
//
// magnitude    - absolute value of the current reading (callers pass std::abs
//                for the negative side of a two-sided gauge)
// threshold    - jitter deadband, in the same units as magnitude
// lingerFrames - how long the marker stays up once armed
//
// The three branches are mutually exclusive and ordered; see the header comment
// for why a new peak hides rather than shows.
inline void advanceActive(float& value, int& framesRemaining,
                          float magnitude, float threshold, int lingerFrames) {
    if (magnitude > value + threshold) {
        // New peak: move the marker up to it and hide it — nothing to mark yet.
        value = magnitude;
        framesRemaining = 0;
    } else if (magnitude < value - threshold && framesRemaining == 0) {
        // The reading has retreated from the peak: now the marker means something.
        framesRemaining = lingerFrames;
    } else if (framesRemaining > 0) {
        detail::tickLinger(value, framesRemaining);
    }
}

// Advance a marker for a gauge the live reading is NOT on (the opposite side of
// a two-sided gauge, or a bar with no input this frame). Linger only — the peak
// is never re-armed from here.
inline void advanceIdle(float& value, int& framesRemaining) {
    if (framesRemaining > 0) {
        detail::tickLinger(value, framesRemaining);
    }
}

// Pin the current reading as the peak at the moment of impact, so the rider can
// read what was happening when they went down.
//
// Only fires when nothing is currently drawn on this marker: a still-visible
// marker is holding a HIGHER, earlier peak, which is the more informative
// reading and must not be overwritten by the (often collapsed) impact value.
// Gated on framesRemaining rather than value for the reason in the header.
inline void snapOnImpact(float& value, int& framesRemaining,
                         float magnitude, float deadzone, int lingerFrames) {
    if (magnitude > deadzone && framesRemaining == 0) {
        value = magnitude;
        framesRemaining = lingerFrames;
    }
}

// Collapse two opposed markers to the single most recent one, so a frozen crash
// display shows one peak rather than two. A higher remaining count means the
// linger was armed more recently. Ties keep side A, matching the original
// >= comparison.
inline void collapseToMostRecent(float& valueA, int& framesA,
                                 float& valueB, int& framesB) {
    if (framesA > 0 && framesB > 0) {
        if (framesA >= framesB) {
            valueB = 0.0f;
            framesB = 0;
        } else {
            valueA = 0.0f;
            framesA = 0;
        }
    }
}

inline void clear(float& value, int& framesRemaining) {
    value = 0.0f;
    framesRemaining = 0;
}

}  // namespace PeakMarker
