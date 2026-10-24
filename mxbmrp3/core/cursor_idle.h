// ============================================================================
// core/cursor_idle.h
// When the idle mouse cursor auto-hides: after HIDE_MS without movement or a
// click, unless something holds it (it rests on the Toggles strip, which shows
// only with the cursor). One decision for InputManager's real mouse and its
// test-build injected one, so the integration test of the hold exercises the
// same rule the game runs.
//
// In time, not frames: the 120 frames it used to count were 2 s at 60fps but a
// quarter second at 480.
//
// Pinned by tests/unit/test_cursor_idle.cpp.
// ============================================================================
#pragma once

namespace CursorIdle {

// Inactivity before the cursor hides.
constexpr long long HIDE_MS = 3000;

// True when a cursor idle for idleMs should hide; held keeps it up.
inline bool hides(long long idleMs, bool held) {
    return !held && idleMs >= HIDE_MS;
}

}  // namespace CursorIdle
