// ============================================================================
// core/director_manager_internal.h
// Shared internal tuning constants + helpers for the DirectorManager translation
// units (director_manager*.cpp). Extracted verbatim from director_manager.cpp's
// file-local anonymous namespace when evaluate() was split into
// director_manager_evaluate.cpp; the values and logic are unchanged. Header-
// inline (was a file-local unnamed namespace in the single TU) so both TUs share
// one set without ODR conflicts — mirrors hud/map_hud_internal.h.
// ============================================================================
#pragma once

#include "xinput_reader.h"  // XInputData (stickMoved / cameraFlying)

#include <cmath>            // std::fabs

namespace director_detail {
    // Tuning constants (ms). Shot lengths and the battle gap are live-tunable
    // members instead (see the Director tab).
    constexpr long long kDecisionIntervalMs = 300;   // coalesce: decide at most ~3x/sec
    constexpr long long kPendingMs          = 1500;  // grace for a requested cut to land
    constexpr long long kManualGraceMs      = 6000;  // yield after the user takes control
    // The incident-linger / incident-cap / fastest-linger / takeover-resume timings
    // are live-tunable members (see the Director tab), not constants.

    // How long the overtaker keeps a scoring boost after a detected pass.
    constexpr long long kOvertakeWindowMs = 4000;

    // Drop detection: a rider losing >= kDropThreshold places within a rolling
    // kDropWindowMs baseline is "tumbling"; the boost then lasts kOvertakeWindowMs.
    constexpr long long kDropWindowMs = 6000;
    constexpr int       kDropThreshold = 3;

    // After the leader takes the flag, hold on them this long (a winner celebration),
    // then move the finish lock to the battle for the next position - a parked winner
    // isn't a shot.
    constexpr long long kWinnerCelebrationMs = 6000;

    // Trap-guard for gamepad takeover: if the caster grabs Free-Roam while "Resume
    // after" is Off, auto-resume after this much idle anyway (they have no menu to
    // switch back from). Hardcoded - not a second user-facing resume setting.
    constexpr long long kTakeoverResumeFallbackMs = 3000;

    // Throttle for the per-frame pollManualControl(): ~30 Hz is plenty to catch a
    // stick gesture and time the resume, without polling XInput at the full frame rate.
    constexpr long long kManualPollIntervalMs = 33;

    // A deliberate stick push, the gesture that flies a free-roam camera. Used as the
    // gamepad-takeover trigger so a button press (or drift, via the 0.25 deadzone -
    // the stored stick values are raw/un-deadzoned) doesn't grab the camera.
    inline bool stickMoved(const XInputData& d) {
        if (!d.isConnected) return false;
        constexpr float kStick = 0.25f;
        return std::fabs(d.leftStickX) > kStick || std::fabs(d.leftStickY) > kStick ||
               std::fabs(d.rightStickX) > kStick || std::fabs(d.rightStickY) > kStick;
    }

    // "Still hand-flying the camera": sticks (pan/move) or triggers (height/speed) -
    // the inputs that actually drive a free-roam camera. Deliberately EXCLUDES
    // buttons/dpad/shoulders, so incidental button presses while flying don't keep
    // resetting the resume timer (which made auto-resume feel inconsistent). Used to
    // detect inactivity for resume, not to trigger takeover.
    inline bool cameraFlying(const XInputData& d) {
        if (!d.isConnected) return false;
        constexpr float kStick = 0.25f, kTrig = 0.1f;
        if (std::fabs(d.leftStickX) > kStick || std::fabs(d.leftStickY) > kStick ||
            std::fabs(d.rightStickX) > kStick || std::fabs(d.rightStickY) > kStick) return true;
        return d.leftTrigger > kTrig || d.rightTrigger > kTrig;
    }

    // Leaders are worth more than midfield; P1 ~1.8x, fading to 1.0 by ~P11.
    inline double posWeight(int position) {
        int boost = 11 - position;
        if (boost < 0) boost = 0;
        return 1.0 + boost * 0.08;
    }

    struct Rider {
        int position;
        int raceNum;
        int gapToLeaderMs;
        int gapLaps;
        int numLaps;      // completed laps (for final-lap detection)
        int bestLapMs;    // best lap so far in ms (-1 = none; for fastest-lap detection)
        bool finished;    // crossed the line for good (don't follow incidents/battles on them)
    };
}  // namespace director_detail
