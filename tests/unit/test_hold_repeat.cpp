// ============================================================================
// tests/unit/test_hold_repeat.cpp
// Unit tests for core/hold_repeat.h -- the one hold-to-repeat curve behind the
// settings arrows and the channel fields' editing keys. The two used to be
// separate copies of the same constants; they now share this curve, and this
// pins its shape (the initial slow interval, a strictly faster ramp, then flat
// at the user's [Advanced] holdRepeatFastMs) so neither can drift from what a
// held arrow feels like. The key polling itself is not reachable headless.
// Header-only, no game engine.
// ============================================================================
#include "doctest.h"

#include "core/hold_repeat.h"

TEST_CASE("hold repeat: ramps from slow to the configured fast interval") {
    CHECK(HoldRepeat::intervalMs(0, 50) == HoldRepeat::SLOW_MS);
    // Strictly faster as repeats accumulate, until the ramp ends...
    long long prev = HoldRepeat::intervalMs(0, 50);
    for (int i = 1; i <= HoldRepeat::ACCEL_REPEATS; ++i) {
        const long long now = HoldRepeat::intervalMs(i, 50);
        CHECK(now < prev);
        prev = now;
    }
    // ...then flat at the fast interval.
    CHECK(HoldRepeat::intervalMs(HoldRepeat::ACCEL_REPEATS, 50) == 50);
    CHECK(HoldRepeat::intervalMs(1000, 50) == 50);
}

TEST_CASE("hold repeat: a fast setting slower than SLOW_MS ramps the other way") {
    // holdRepeatFastMs accepts up to 500; the ramp then slows down to it rather
    // than overshooting.
    CHECK(HoldRepeat::intervalMs(0, 500) == HoldRepeat::SLOW_MS);
    CHECK(HoldRepeat::intervalMs(HoldRepeat::ACCEL_REPEATS, 500) == 500);
}
