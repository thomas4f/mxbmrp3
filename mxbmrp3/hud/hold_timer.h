// ============================================================================
// hud/hold_timer.h
// "Show this for N ms, then stop": the hold behind every freeze -- the Gap
// Bar's and Lap Log's official gap (OfficialGapFreeze), the Timing panel's
// split/lap and segment holds, and the Pitboard's At Splits board. The
// duration is passed at each check rather than stored, so a setting changed
// mid-hold takes effect on the hold already running, as it always has.
// Pinned by tests/unit/test_split_crossing.cpp.
// ============================================================================
#pragma once

#include <chrono>

class HoldTimer {
public:
    using Clock = std::chrono::steady_clock;

    void start(Clock::time_point now = Clock::now()) {
        m_active = true;
        m_startedAt = now;
    }
    void stop() { m_active = false; }
    bool active() const { return m_active; }

    // Still inside the hold (a const read, for visibility checks)
    bool running(int durationMs, Clock::time_point now = Clock::now()) const {
        return m_active && elapsedMs(now) < durationMs;
    }

    // Ends the hold once durationMs has passed. True on the call that ended it,
    // so the caller marks itself dirty exactly once.
    bool expire(int durationMs, Clock::time_point now = Clock::now()) {
        if (!m_active || elapsedMs(now) < durationMs) return false;
        m_active = false;
        return true;
    }

private:
    long long elapsedMs(Clock::time_point now) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startedAt).count();
    }

    bool m_active = false;
    Clock::time_point m_startedAt;
};
