// ============================================================================
// core/wrong_way_detect.h
// Per-rider wrong-way confirmation - the pure timer behind the player's WRONG
// WAY notice and the "rider oncoming" hazard.
//
// WHAT IT DOES. Each RaceTrackPosition sample gives one along-track delta. A
// run of backward samples lasting `confirmMs` flags the rider; the flag drops
// only once the rider has stopped going backward for CLEAR_HOLD_MS. The run
// must also cover MIN_BACKWARD_TRACK, so a stopped rider's position wobble
// never confirms.
//
// WHY THE CLEAR IS HELD. It used to drop on the first sample that was not
// backward, and the game repeats a rider's position now and then: about 1 in
// 55 moving samples in a real 50 Hz capture is an exact repeat (delta 0). That
// is one repeat a second, so a rider riding the wrong way had the flag cleared
// about once a second - it rarely survived the 1.5 s confirmation and, when it
// did, vanished again within a second. One dropped sample says nothing about
// direction; stopping or turning round does, and that lasts far longer than a
// sample. Pinned by tests/unit/test_wrong_way_detect.cpp.
//
// WHY IT LIVES HERE AND NOT IN PluginData. Time is an input, so the unit suite
// drives it with a fake clock in ~1s instead of real sleeps under Wine.
// ============================================================================
#pragma once

#include <chrono>

struct WrongWayDetector {
    using Clock = std::chrono::steady_clock;

    // How long a rider must stop going backward (moving forward, standing still,
    // or a repeated sample) before the flag clears. Several samples at every rate
    // the game reports (50 Hz riding, ~10 Hz spectating) and short beside a rider
    // turning round.
    static constexpr long long CLEAR_HOLD_MS = 500;

    // How far back along the track (fraction of a lap, about 3 m on a typical
    // track) the run must have gone before it confirms. A rider standing still
    // reports positions that wobble by tiny amounts in both directions; without
    // this, a few backward wobbles a second kept the run alive and flagged a
    // stopped rider. Net distance, so forward wobbles count against it.
    static constexpr float MIN_BACKWARD_TRACK = 0.002f;

    Clock::time_point backwardSince;     // First backward sample of the run (epoch = none)
    Clock::time_point notBackwardSince;  // First non-backward sample since then (epoch = none)
    float backwardTrack = 0.0f;          // Net distance travelled backward since the run began
    bool confirmed = false;

    // Feeds one sample; returns whether the rider counts as going the wrong way.
    // Only a backward sample can confirm, so a run that had not confirmed when it
    // stopped going backward does not confirm during the hold.
    bool update(float alongTrackDelta, Clock::time_point now, long long confirmMs) {
        if (alongTrackDelta < 0.0f) {
            if (isUnset(backwardSince)) backwardSince = now;
            notBackwardSince = {};
            backwardTrack -= alongTrackDelta;
            if (!confirmed && msSince(backwardSince, now) >= confirmMs &&
                backwardTrack >= MIN_BACKWARD_TRACK) {
                confirmed = true;
            }
        } else if (!isUnset(backwardSince)) {
            backwardTrack -= alongTrackDelta;
            if (isUnset(notBackwardSince)) notBackwardSince = now;
            if (msSince(notBackwardSince, now) >= CLEAR_HOLD_MS) reset();
        }
        return confirmed;
    }

    void reset() {
        backwardSince = {};
        notBackwardSince = {};
        backwardTrack = 0.0f;
        confirmed = false;
    }

private:
    static bool isUnset(Clock::time_point t) { return t.time_since_epoch().count() == 0; }
    static long long msSince(Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    }
};
