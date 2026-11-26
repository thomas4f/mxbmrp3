// ============================================================================
// core/warp_detect.h
// The display rider was put somewhere else on the track - the pure detector
// behind blanking the live gap after a reset (PluginData::updateLapTimerTrackPosition).
//
// WHAT IT CATCHES. A hotlapper who falls resets to a marker near the end of the
// track to start the next lap sooner. The game voids that lap only at the line,
// so until then the live gap compared ~18 s on the clock against the ~66 s the
// reference had at the marker: -48 s on every gap display. Two signals together
// mark the case, and neither does alone:
//   - the WORLD position jumped faster than any bike moves (a teleport). The
//     along-track position alone cannot say it: it is the nearest centerline
//     point, so riding far off track or a crashed bike flying makes it leap
//     hundreds of metres while the bike moves smoothly;
//   - the rider landed well AHEAD along the lap of where they last rode
//     uncrashed, or across the line either way. The game's own crash recovery
//     also teleports, but puts the rider back near where they fell, or behind:
//     that lap goes on and its gap stays honest.
// The along-track distance is measured from the last UNCRASHED sample, because
// a crashed bike's centerline position is meaningless (a tumbling bike read
// 0.62 on a lap it was riding at 0.05).
//
// THE NUMBERS come from Thomas's three tapes with 140 teleports (two crash
// reels on one 1669 m track, one online race). Crash recoveries landed at most
// 157 m ahead; the online one 405 m ahead was a real skip. Teleports moved the
// bike 15-183 m within a single 3-8 ms batch; riding at 50 m/s moves it 0.4 m.
// Some recoveries landed on a nearby section of track ~700 m further along:
// that lap is broken too, and blanking it is right.
//
// EVERY teleport is reported, not only the skips: a jump back across half a lap
// reads as an S/F wrap (0.63 -> 0.05), and the lap timer and gap tracker must not
// fence a lap on it (the caller re-baselines them).
//
// WHY IT LIVES HERE. Time is an input, so tests/unit/test_warp_detect.cpp drives
// it with plain numbers.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

struct WarpDetector {
    // A jump shorter than this is never a teleport: position noise, a hitch.
    static constexpr float MIN_TELEPORT_M = 15.0f;
    // Faster than any bike: 360 km/h. At the ~10 Hz the game reports spectated
    // riders a bike covers ~5 m per sample.
    static constexpr float MAX_RIDE_SPEED_MPS = 100.0f;
    // How far ahead of the last ridden spot a teleport must land to be a skip.
    static constexpr float MIN_SKIP_M = 200.0f;
    // The same, as a share of the lap, when the track length is unknown.
    static constexpr float MIN_SKIP_LAP = 0.12f;
    // Further BACK than this along the lap is a reset across the line, forward
    // (0.95 -> 0.05): the clock still runs from the last line.
    static constexpr float ACROSS_LINE_LAP = 0.5f;

    enum class Result : uint8_t {
        NONE,        // ridden, or the first sample
        TELEPORT,    // put down elsewhere, not far ahead: the gap still holds
        SKIP         // put down well ahead: the lap's gap is meaningless
    };

    // One sample of the rider: world position (m), centerline position (0..1),
    // the crashed flag, the sample's time (us) and the track length (m, <= 0
    // unknown).
    Result update(float x, float y, float z, float trackPos, bool crashed,
                  int64_t nowUs, float trackLengthM) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            !std::isfinite(trackPos)) {
            return Result::NONE;
        }
        Result result = Result::NONE;
        if (m_have) {
            const float dx = x - m_x, dy = y - m_y, dz = z - m_z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double dtSec = static_cast<double>(nowUs - m_us) / 1e6;
            const bool teleport = dist > MIN_TELEPORT_M &&
                                  static_cast<double>(dist) > MAX_RIDE_SPEED_MPS * (dtSec > 0.0 ? dtSec : 0.0);
            if (teleport) {
                // Along the lap, NOT around it: the gap reads the position against
                // a clock that only the line restarts, so a reset across the line
                // either way breaks it as surely as a skip ahead does.
                const float from = m_haveRidden ? m_riddenTrackPos : m_trackPos;
                const float ahead = trackPos - from;
                auto skipsFar = [&](float lap) {
                    return trackLengthM > 0.0f ? lap * trackLengthM >= MIN_SKIP_M : lap >= MIN_SKIP_LAP;
                };
                if (ahead <= -ACROSS_LINE_LAP && !skipsFar(ahead + 1.0f)) {
                    // Across the line and barely on: the line crossing it looks
                    // like, which is what it is to the clock. (A world that is
                    // not a closed loop jumps here every lap - the synthetic perf
                    // tape's does.)
                    result = Result::NONE;
                } else {
                    result = (ahead <= -ACROSS_LINE_LAP || skipsFar(ahead)) ? Result::SKIP : Result::TELEPORT;
                }
            }
        }
        m_x = x; m_y = y; m_z = z;
        m_trackPos = trackPos;
        m_us = nowUs;
        m_have = true;
        if (!crashed) {
            m_riddenTrackPos = trackPos;
            m_haveRidden = true;
        }
        return result;
    }

    void reset() { *this = WarpDetector{}; }

private:
    float m_x = 0.0f, m_y = 0.0f, m_z = 0.0f;
    float m_trackPos = 0.0f;
    int64_t m_us = 0;
    bool m_have = false;
    float m_riddenTrackPos = 0.0f;   // the last uncrashed sample's centerline position
    bool m_haveRidden = false;
};
