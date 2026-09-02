// ============================================================================
// core/spotter_milestones.h
// Session-progress milestone cues: "ten minutes to go", "five minutes left",
// "halfway there". Pure (all inputs injected) so the unit suite drives every
// edge (test_spotter_milestones.cpp); SpotterManager feeds it the elapsed
// clock each track-position tick (timed sessions) and the leader's completed
// laps at each of the leader's crossings (pure lap races).
//
// Design constraints:
//  - CROSSING-edged, not level-triggered: joining a session mid-way (or a
//    tape seek) must swallow already-passed thresholds silently, not machine-
//    gun three announcements. The first tick only arms the baseline.
//  - A threshold only exists when the session comfortably clears it: a ten-
//    minute call in a ten-minute race is the start, not a milestone.
//  - Milestones that would land on top of each other collapse: in a 12-min
//    race, halfway (6:00 elapsed) and "five minutes left" (7:00) both exist,
//    but in a 10-min race halfway IS "five left" — halfway yields.
//  - The clock rewinds on session restart: reset, same rule as the hazard
//    detector.
// ============================================================================
#pragma once

namespace SpotterMilestones {

class State {
public:
    // Timed sessions: call per clock tick with the session ELAPSED time and
    // the session length (ms). Returns the cue key that just crossed, or
    // nullptr. At most one per call; when several thresholds sit between two
    // ticks (huge jump), the if-chain's order picks the winner — ten-to-go
    // over five-to-go over halfway, which for any race longer than 20 minutes
    // is the LATEST-crossed threshold, i.e. the most current fact — and the
    // rest are swallowed as "already passed". A jump is a join/seek, not
    // racing. Pinned by the multi-threshold-jump case in
    // test_spotter_milestones.cpp.
    const char* updateTime(int elapsedMs, int lengthMs) {
        if (lengthMs <= 0) return nullptr;
        if (elapsedMs + 5000 < m_lastElapsedMs) reset();
        const int prev = m_armed ? m_lastElapsedMs : elapsedMs;
        m_lastElapsedMs = elapsedMs;
        if (!m_armed) {
            m_armed = true;
            return nullptr;
        }

        const char* cue = nullptr;
        auto crossed = [&](int thresholdElapsedMs) {
            return prev < thresholdElapsedMs && elapsedMs >= thresholdElapsedMs;
        };
        // Existence rules: a "N left" call needs length >= 1.5N; halfway
        // needs a minute of race and a minute of separation from the fixed
        // calls on BOTH sides (else it duplicates one of them).
        const int t10 = lengthMs - 10 * 60000;
        const int t5 = lengthMs - 5 * 60000;
        const int half = lengthMs / 2;
        const bool has10 = lengthMs >= 15 * 60000;
        const bool has5 = lengthMs >= 8 * 60000;
        const bool hasHalf =
            lengthMs >= 60000 && (!has10 || absDiff(half, t10) >= 60000) &&
            (!has5 || absDiff(half, t5) >= 60000);

        if (has10 && !m_fired10 && crossed(t10)) {
            m_fired10 = true;
            cue = "ten_minutes_remaining";
        } else if (has5 && !m_fired5 && crossed(t5)) {
            m_fired5 = true;
            cue = "five_minutes_remaining";
        } else if (hasHalf && !m_firedHalf && crossed(half)) {
            m_firedHalf = true;
            cue = "halfway_point";
        }
        // Anything else that was jumped over is spent silently.
        if (has10 && elapsedMs >= t10) m_fired10 = true;
        if (has5 && elapsedMs >= t5) m_fired5 = true;
        if (elapsedMs >= half) m_firedHalf = true;
        return cue;
    }

    // Pure lap races (no clock): call when the LEADER completes a lap.
    // Halfway = the leader completing the first half of the distance; short
    // sprints (< 4 laps) have no meaningful halfway.
    const char* updateLaps(int leaderCompletedLaps, int totalLaps) {
        if (totalLaps < 4) return nullptr;
        const bool passed = leaderCompletedLaps * 2 >= totalLaps;
        // The first call ARMS, exactly as updateTime's first tick does, and a
        // threshold already behind you at that point is spent SILENTLY. The
        // header promises crossings, and level-triggered this would announce
        // "Halfway there" on the spot when joining a ten-lap race with the
        // leader on lap eight.
        if (!m_lapArmed) {
            m_lapArmed = true;
            if (passed) m_firedHalf = true;
            return nullptr;
        }
        if (m_firedHalf) return nullptr;
        if (passed) {
            m_firedHalf = true;
            return "halfway_point";
        }
        return nullptr;
    }

    void reset() {
        m_fired10 = m_fired5 = m_firedHalf = false;
        m_armed = false;
        m_lapArmed = false;
        m_lastElapsedMs = 0;
    }

private:
    static int absDiff(int a, int b) { return a > b ? a - b : b - a; }

    bool m_fired10 = false, m_fired5 = false, m_firedHalf = false;
    bool m_armed = false;
    // updateLaps' own baseline flag: the two paths are mutually exclusive per
    // session (a race is timed or it is not), but sharing one would make each
    // depend on which had been called first.
    bool m_lapArmed = false;
    int m_lastElapsedMs = 0;
};

}  // namespace SpotterMilestones
