// ============================================================================
// tests/unit/test_cursor_idle.cpp
// Unit tests for core/cursor_idle.h -- when the idle cursor auto-hides. Pins the
// 3 s boundary (it used to be 120 frames, a quarter second at 480fps) and the
// hold: a cursor resting on the Toggles strip never hides, however long it
// idles. toggle_strip_test drives the same rule end to end through the injected
// mouse. Header-only, no game engine.
// ============================================================================
#include "doctest.h"

#include "core/cursor_idle.h"

TEST_CASE("cursor idle: hides at 3 s, not before") {
    CHECK(CursorIdle::HIDE_MS == 3000);
    CHECK_FALSE(CursorIdle::hides(0, false));
    CHECK_FALSE(CursorIdle::hides(2999, false));
    CHECK(CursorIdle::hides(3000, false));
    CHECK(CursorIdle::hides(60000, false));
}

TEST_CASE("cursor idle: a held cursor never hides") {
    CHECK_FALSE(CursorIdle::hides(3000, true));
    CHECK_FALSE(CursorIdle::hides(60000, true));
}
