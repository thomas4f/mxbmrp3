// ============================================================================
// tests/unit/test_notice_priority.cpp
// Unit tests for hud/notice_priority.h — the pure display-timer step for the
// consumable NoticesHud notices. This is the core of the "consumed while masked"
// fix: a PB (or setup/segment) notice masked behind a higher-priority status
// banner must be HELD, not counted down and cleared unseen. Header-only, no game
// engine — compiles with plain g++. See tests/unit/run_tests.sh.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "hud/notice_priority.h"

using NoticePriority::TimerIn;
using NoticePriority::TimerOut;
using NoticePriority::stepTimer;

static constexpr long long DUR = 5000;  // 5s display window

TEST_CASE("stepTimer: an unmasked notice shows, then consumes after the window") {
    // First unmasked frame at t=1000: anchors, shows, no consume yet.
    TimerOut a = stepTimer({ /*pending=*/true, /*masked=*/false, /*triggerMs=*/1000, /*unmaskAtMs=*/0 }, 1000, DUR);
    CHECK(a.show);
    CHECK_FALSE(a.consume);
    CHECK(a.unmaskAtMs == 1000);

    // Mid-window: still showing.
    TimerOut b = stepTimer({ true, false, 1000, a.unmaskAtMs }, 3000, DUR);
    CHECK(b.show);
    CHECK_FALSE(b.consume);

    // At/after the window edge: consume (clear the flag), stop showing.
    TimerOut c = stepTimer({ true, false, 1000, b.unmaskAtMs }, 6000, DUR);
    CHECK_FALSE(c.show);
    CHECK(c.consume);
}

TEST_CASE("stepTimer: a masked notice is HELD — never shown, never consumed") {
    // The bug: without this, the window would run from triggerMs=1000 and be gone
    // by t=6000+ even though the notice was masked the whole time.
    long long anchor = 0;
    // Masked for a long stretch well past the raw duration.
    for (long long t = 1000; t <= 20000; t += 1000) {
        TimerOut r = stepTimer({ true, /*masked=*/true, 1000, anchor }, t, DUR);
        anchor = r.unmaskAtMs;
        CHECK_FALSE(r.show);
        CHECK_FALSE(r.consume);   // must NOT be consumed while masked
        CHECK(anchor == 0);       // no anchor while masked
    }

    // Mask clears at t=20000: the full window starts NOW, not from the old trigger.
    TimerOut u = stepTimer({ true, false, 1000, anchor }, 20000, DUR);
    CHECK(u.show);
    CHECK_FALSE(u.consume);
    CHECK(u.unmaskAtMs == 20000);

    // Still showing a moment later; only consumes 5s after the unmask.
    CHECK(stepTimer({ true, false, 1000, u.unmaskAtMs }, 24000, DUR).show);
    CHECK(stepTimer({ true, false, 1000, u.unmaskAtMs }, 25000, DUR).consume);
}

TEST_CASE("stepTimer: mask that lands mid-display holds the remaining window") {
    // Shows briefly, then a status notice masks it before the window elapses.
    TimerOut a = stepTimer({ true, false, 1000, 0 }, 1000, DUR);
    REQUIRE(a.show);

    // Masked at t=2000 (2s in): held, anchor dropped, not consumed.
    TimerOut m = stepTimer({ true, true, 1000, a.unmaskAtMs }, 2000, DUR);
    CHECK_FALSE(m.show);
    CHECK_FALSE(m.consume);
    CHECK(m.unmaskAtMs == 0);

    // Unmasks again at t=10000: fresh full window (re-anchors), not instantly gone.
    TimerOut u = stepTimer({ true, false, 1000, m.unmaskAtMs }, 10000, DUR);
    CHECK(u.show);
    CHECK_FALSE(u.consume);
    CHECK(u.unmaskAtMs == 10000);
}

TEST_CASE("stepTimer: a fresh re-trigger restarts the window (later of trigger/anchor)") {
    // Anchored at t=1000; a new event at t=3000 (newer trigger) should keep it on
    // screen a full duration past the re-trigger, not expire at 6000.
    long long anchor = 1000;
    // Just before the original window would end, a re-trigger arrives.
    TimerOut r = stepTimer({ true, false, /*triggerMs=*/3000, anchor }, 4000, DUR);
    CHECK(r.show);
    // Original window (from 1000) would be done by 6000, but trigger=3000 dominates.
    CHECK(stepTimer({ true, false, 3000, r.unmaskAtMs }, 6000, DUR).show);
    CHECK_FALSE(stepTimer({ true, false, 3000, r.unmaskAtMs }, 6000, DUR).consume);
    // Consumes 5s after the re-trigger.
    CHECK(stepTimer({ true, false, 3000, r.unmaskAtMs }, 8000, DUR).consume);
}

TEST_CASE("stepTimer: not pending resets everything") {
    TimerOut r = stepTimer({ /*pending=*/false, false, 1000, 4321 }, 9000, DUR);
    CHECK_FALSE(r.show);
    CHECK_FALSE(r.consume);
    CHECK(r.unmaskAtMs == 0);
}
