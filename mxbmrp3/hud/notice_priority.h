// ============================================================================
// hud/notice_priority.h
// Pure display-timer logic for the *consumable* notices in NoticesHud (the ones
// whose expiry clears a shared PluginData flag: all-time PB, fastest lap, session
// PB, default-setup, segment-timer feedback).
//
// The bug this fixes: those notices used to run their display countdown from the
// *event* time, independent of whether they were ever actually on screen. The
// NoticesHud render is a single-winner priority ladder (WRONG WAY > HAZARD > BLUE
// FLAG > LAPPER > OVERTIME > PB* > SEGMENT > ...), so a session PB set on a lap
// where the player also briefly went the wrong way would count down *behind* the
// WRONG WAY banner and get cleared having never been displayed.
//
// stepTimer() measures the display window from the later of (a) the event trigger
// and (b) the moment the notice became *unmasked* (no higher-priority status
// notice showing). While masked it is held — never consumed — so it shows for its
// full duration once the mask clears. Taking the *later* of the two also
// preserves the "a fresh re-trigger restarts the window" behaviour (a new trigger
// time is newer than the unmask anchor).
//
// Header-only and dependency-free so a fast unit test (tests/) can exercise it
// against the real logic the HUD runs. All times are milliseconds; 0 is the
// "no anchor yet" sentinel (steady_clock since-epoch is never 0 in practice).
// ============================================================================
#pragma once

namespace NoticePriority {

// Inputs for one consumable notice this frame:
//  - pending    : the PluginData flag is set (an event is queued/showing)
//  - masked     : a higher-priority status notice is on screen this frame
//  - triggerMs  : when the event fired (PluginData trigger time)
//  - unmaskAtMs : persisted anchor — when the notice last became unmasked (0 = none)
struct TimerIn { bool pending; bool masked; long long triggerMs; long long unmaskAtMs; };

// Outputs:
//  - unmaskAtMs : new anchor to persist back (0 clears it)
//  - show       : the notice is within its display window this frame
//  - consume    : the display window has elapsed — clear the PluginData flag now
struct TimerOut { long long unmaskAtMs; bool show; bool consume; };

inline TimerOut stepTimer(const TimerIn& in, long long nowMs, long long durationMs) {
    if (!in.pending) return { 0, false, false };            // gone: drop the anchor
    if (in.masked)   return { 0, false, false };            // held: don't start, don't consume
    // First unmasked frame anchors the window; the window starts at the later of the
    // trigger and that anchor (so a fresh re-trigger, newer than the anchor, restarts it).
    long long unmaskAt = in.unmaskAtMs ? in.unmaskAtMs : nowMs;
    long long start = in.triggerMs > unmaskAt ? in.triggerMs : unmaskAt;
    if (nowMs - start >= durationMs) return { unmaskAt, false, true };   // elapsed: consume
    return { unmaskAt, true, false };
}

} // namespace NoticePriority
