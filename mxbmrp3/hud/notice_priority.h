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
// stepTimer() runs the display window only while the notice is on screen: it
// starts on the first unmasked frame (no higher-priority status notice
// showing), stops while masked -- held, never consumed -- and resumes where it
// stopped, so a notice shows for its duration IN TOTAL however often a mask
// comes and goes. (Restarting the window on each unmask let a flickering
// lapper or hazard banner keep a PB up for most of a lap.) A fresh re-trigger
// restarts the window, also one that arrives while masked.
//
// Header-only and dependency-free so a fast unit test (tests/) can exercise it
// against the real logic the HUD runs. All times are milliseconds; 0 is the
// "no anchor yet" sentinel (steady_clock since-epoch is never 0 in practice).
// ============================================================================
#pragma once

namespace NoticePriority {

// The persisted state of one consumable notice's window.
//  - startMs    : when the window (as shifted past masked stretches) started; 0 = not yet
//  - maskedAtMs : when the current mask began, while one holds a started window; 0 = none
struct Timer { long long startMs = 0; long long maskedAtMs = 0; };

// Inputs for one consumable notice this frame:
//  - pending    : the PluginData flag is set (an event is queued/showing)
//  - masked     : a higher-priority status notice is on screen this frame
//  - triggerMs  : when the event fired (PluginData trigger time)
//  - timer      : the state stepTimer() returned last frame
struct TimerIn { bool pending; bool masked; long long triggerMs; Timer timer; };

// Outputs:
//  - timer   : the state to persist back
//  - show    : the notice is within its display window this frame
//  - consume : the display window has elapsed: clear the PluginData flag now
struct TimerOut { Timer timer; bool show; bool consume; };

inline TimerOut stepTimer(const TimerIn& in, long long nowMs, long long durationMs) {
    if (!in.pending) return { {}, false, false };            // gone: drop the state
    Timer t = in.timer;
    if (in.masked) {                                          // held: don't show, don't consume
        if (t.startMs && !t.maskedAtMs) t.maskedAtMs = nowMs; // a started window stops here
        return { t, false, false };
    }
    if (!t.startMs || (t.maskedAtMs && in.triggerMs > t.maskedAtMs)) {
        t.startMs = nowMs;                                    // first shown, or re-triggered while held
    } else if (t.maskedAtMs) {
        t.startMs += nowMs - t.maskedAtMs;                    // resume: the masked stretch does not count
    }
    t.maskedAtMs = 0;
    if (in.triggerMs > t.startMs) t.startMs = in.triggerMs;  // a fresh re-trigger restarts it
    if (nowMs - t.startMs >= durationMs) return { t, false, true };   // elapsed: consume
    return { t, true, false };
}

} // namespace NoticePriority
