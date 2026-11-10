// ============================================================================
// core/ghost_ride.h
// Ghost Ride: the rider is off, and the bike rides on without them. The one
// pure rule behind the achievement, kept header-only so the unit suite pins it
// against numbers taken from a real one (tests/unit/test_ghost_ride.cpp).
//
// WHAT THE GAME GIVES. The crashed flag means "rider is detached from bike"
// (mxb_api.h), and the bike's own telemetry keeps coming the whole time it is
// set: speed, roll, pitch, which wheels touch the ground. There is no rider
// position to measure a gap from, and there does not need to be - the flag
// already says the two are apart. The game ends the crash by putting the
// rider back, which in practice is as soon as the bike has all but stopped.
//
// WHY UPRIGHT AS WELL AS BOTH WHEELS DOWN. A bike lying on its side can report
// both tyres in contact while it slides, and every ordinary crash ends there:
// 46 of 47 crashes recorded on purpose (10 Nov 2026) spent at most 0.22 s
// upright, moving and on both wheels, all of it bounces. The one real ghost
// ride in that tape rolled upright for about 1.5 s - so the three seconds
// the row asks for, Endo's bar, is a rare one by design.
//
// A RUN, with a short grace: a bike rolling over rough ground skips a wheel
// off a bump without having stopped doing the thing, so a break of up to
// GRACE_SEC holds the run (the break itself is not counted); a longer one,
// or the rider being put back, ends it.
// ============================================================================
#pragma once

#include <cmath>

namespace GhostRide {

constexpr float MIN_SPEED_MS = 3.0f;    // ~11 km/h: still going, not settling
constexpr float MAX_ROLL_DEG = 35.0f;   // upright; a bike on its side reads ~70-140
constexpr float MAX_PITCH_DEG = 45.0f;  // not mid-loop-out
constexpr double GRACE_SEC = 0.25;

// Is the bike, this frame, riding on without its rider.
inline bool rolling(bool crashed, bool frontContact, bool rearContact,
                    float rollDeg, float pitchDeg, float speedMs) {
    return crashed && frontContact && rearContact &&
           std::fabs(rollDeg) < MAX_ROLL_DEG && std::fabs(pitchDeg) < MAX_PITCH_DEG &&
           speedMs > MIN_SPEED_MS;
}

// The unbroken run, in seconds of ghost riding. step() takes the time since
// the previous frame; the caller drops the run itself across a stall or a
// menu (a gap far longer than any frame), as Digging a Hole does.
class Run {
public:
    double step(bool crashed, bool isRolling, double dt) {
        if (!crashed) { reset(); return 0.0; }
        if (isRolling) {
            m_sec += dt;
            m_breakSec = 0.0;
        } else {
            m_breakSec += dt;
            if (m_breakSec > GRACE_SEC) m_sec = 0.0;
        }
        return m_sec;
    }
    void reset() { m_sec = 0.0; m_breakSec = 0.0; }
    double seconds() const { return m_sec; }

private:
    double m_sec = 0.0;
    double m_breakSec = 0.0;
};

}  // namespace GhostRide
