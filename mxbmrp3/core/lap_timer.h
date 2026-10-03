// ============================================================================
// core/lap_timer.h
// The display rider's live lap timer — the pure state machine.
//
// WHAT THIS IS. A single wall-clock-anchored timer (session time can count UP
// in practice or DOWN in races, so wall clock is the only reliable base) plus
// the transition logic that keeps it honest: S/F-line detection from track
// position wraparound, re-anchoring on official splits, per-sector elapsed
// math, pause/resume, and the grid-start grace described below. Consumed by
// TimingHud, LapLogHud and IdealLapHud through PluginData's thin wrappers
// (plugin_data_lap_timer.cpp), which own the display-rider gating and logging.
//
// WHY IT LIVES HERE AND NOT IN plugin_data_types.h / PluginData. The
// transitions read nothing of PluginData beyond the timer itself, so keeping
// them with the state makes the machine a unit: tests/unit/test_lap_timer.cpp
// drives the transitions with a plain g++ rather than through the DLL under
// Wine. (Elapsed-time reads use the real steady_clock; the tests assert
// transition STATE, not wall-clock durations.)
//
// THE GRID-START GRACE (anchoredFromRaceStart — don't "simplify" it away): on
// a standing start the green flag is the race's t=0, so the timer anchors at
// the gate drop and the live time spans the grid->S/F run, matching the
// official splits (which accumulate from the start). The S/F crossing the
// rider then makes on the opening lap must NOT re-anchor to 0 — that would
// drop the grid->S/F time and make the display jump forward when the first
// official split resyncs. The grace ends when lap 1 completes (onLapComplete)
// or the in-progress lap dies (invalidateAnchor, e.g. pitting on lap 1).
// ============================================================================
#pragma once

#include <chrono>

struct LapTimer {
    // Wall clock anchor for elapsed time calculation
    std::chrono::steady_clock::time_point anchorTime;  // Real time when anchor was set
    int anchorAccumulatedTime;    // Known accumulated lap time at anchor (ms)
    bool anchorValid;             // Do we have a usable anchor?

    // Pause support
    std::chrono::steady_clock::time_point pausedAt;  // When pause started
    bool isPaused;                // Is timer currently paused?

    // Track position monitoring for S/F line detection
    float lastTrackPos;           // Previous track position (0.0-1.0)
    int lastLapNum;               // Previous lap number
    bool trackMonitorInitialized; // Have we received first position?

    // Current state
    int currentLapNum;            // Current lap being timed
    int currentSector;            // Current sector (0=before S1, 1=before S2, 2=before S3)
    int lastSplit1Time;           // Accumulated time at S1 (for sector 2 calculation)
    int lastSplit2Time;           // Accumulated time at S2 (for sector 3 calculation)

    // Grid (standing) start grace — see the header comment.
    bool anchoredFromRaceStart;

    // Threshold for S/F line detection (position jump > 0.5 = S/F crossing)
    static constexpr float WRAP_THRESHOLD = 0.5f;

    // The clock every anchor and read uses. TEST ONLY override
    // (MXBMRP3_Test_LapTimerSetNowUs): a replayed tape fires its events
    // back-to-back, but this timer is wall-clock anchored, so a burst replay
    // would read ~0 ms between every split. The harness feeds each recorded
    // event's timestamp here instead (see PluginHost::replayTapeClocked), the
    // same way DirectorManager::testSetNowMs paces the director. -1 = the real
    // clock. Compiled out of shipping builds.
    static std::chrono::steady_clock::time_point now() {
#if defined(MXBMRP3_TEST_BUILD)
        if (s_testNowUs >= 0) {
            return std::chrono::steady_clock::time_point(std::chrono::microseconds(s_testNowUs));
        }
#endif
        return std::chrono::steady_clock::now();
    }
#if defined(MXBMRP3_TEST_BUILD)
    static void testSetNowUs(long long us) { s_testNowUs = us; }
    inline static long long s_testNowUs = -1;
#endif

    LapTimer()
        : anchorAccumulatedTime(0), anchorValid(false), isPaused(false)
        , lastTrackPos(0.0f), lastLapNum(0), trackMonitorInitialized(false)
        , currentLapNum(0), currentSector(0)
        , lastSplit1Time(-1), lastSplit2Time(-1)
        , anchoredFromRaceStart(false) {}

    void reset() {
        anchorAccumulatedTime = 0;
        anchorValid = false;
        isPaused = false;
        lastTrackPos = 0.0f;
        lastLapNum = 0;
        trackMonitorInitialized = false;
        currentLapNum = 0;
        currentSector = 0;
        lastSplit1Time = -1;
        lastSplit2Time = -1;
        anchoredFromRaceStart = false;
    }

    void setAnchor(int accumulatedTime) {
        anchorTime = now();
        anchorAccumulatedTime = accumulatedTime;
        anchorValid = true;
        isPaused = false;  // Clear pause state when setting new anchor
    }

    // Drop the anchor without touching track monitoring, so getElapsedLapTime() returns the
    // placeholder (-1) until the next S/F crossing re-anchors it. Used on pit exit: the
    // in-progress lap is dead, so the live timer should read like a fresh track entry rather
    // than keep ticking. Keeping trackMonitorInitialized means the next S/F crossing is still
    // detected (onTrackPosition re-anchors on !anchorValid).
    void invalidateAnchor() {
        anchorValid = false;
        isPaused = false;
        // The grid-start anchor is abandoned once the lap is dropped (e.g. the rider pitted on
        // lap 1), so end the grace: the next S/F crossing must re-anchor normally rather than be
        // skipped (which would leave the timer stuck on the placeholder until the lap completes).
        anchoredFromRaceStart = false;
    }

    // The rider is somewhere else now: the next sample is a baseline, not a
    // delta from the last one. Used when the player re-enters the track from the
    // pits (RunInit) -- read as a delta from where they LEFT, a pit box more than
    // half a lap "behind" that point (left at 0.70, box at 0.02) is a wrap, and
    // with the anchor already dropped onTrackPosition would re-anchor the lap at
    // the pit box. The classification's pit-flag path does not need this: its
    // sample has already gone through by the time the flag arrives.
    void forgetTrackPosition() {
        trackMonitorInitialized = false;
    }

    // ------------------------------------------------------------------------
    // Transitions (the state machine)
    // ------------------------------------------------------------------------

    // Feed a track-position sample. Detects an S/F crossing via wraparound
    // (0.95 -> 0.05 gives delta ~ -0.9) and re-anchors from 0 unless the
    // grid-start grace suppresses it. Returns true when a crossing re-anchored
    // the timer.
    bool onTrackPosition(float trackPos, int lapNum) {
        if (!trackMonitorInitialized) {
            lastTrackPos = trackPos;
            lastLapNum = lapNum;
            trackMonitorInitialized = true;
            return false;
        }

        float delta = trackPos - lastTrackPos;
        bool sfCrossingDetected = false;

        if (delta < -WRAP_THRESHOLD) {
            if (anchoredFromRaceStart) {
                // Opening lap of a grid start: keep the green-flag anchor (see header).
            } else if (!anchorValid || lapNum != lastLapNum) {
                // Crossed S/F line - set anchor if we don't have one or lap changed
                setAnchor(0);  // Start timing from 0
                currentLapNum = lapNum;
                currentSector = 0;  // Reset to sector 0 (before S1)
                lastSplit1Time = -1;
                lastSplit2Time = -1;
                sfCrossingDetected = true;
            }
        }

        lastTrackPos = trackPos;
        lastLapNum = lapNum;
        return sfCrossingDetected;
    }

    // An official split arrived (accumulated from lap start): resync the anchor
    // and advance the sector. sectorIndex: 0=S1, 1=S2 (2=lap complete is
    // handled by onLapComplete instead).
    void onOfficialSplit(int accumulatedTime, int lapNum, int sectorIndex) {
        setAnchor(accumulatedTime);
        currentLapNum = lapNum;
        if (sectorIndex == 0) {
            currentSector = 1;  // Now in sector 2 (between S1 and S2)
            lastSplit1Time = accumulatedTime;
        } else if (sectorIndex == 1) {
            currentSector = 2;  // Now in sector 3 (between S2 and S3/finish)
            lastSplit2Time = accumulatedTime;
        }
    }

    // A lap completed: re-anchor at 0 for the new lap. Also ends the grid-start
    // grace — lap 2 onward anchors normally at each S/F crossing.
    void onLapComplete(int lapNum) {
        setAnchor(0);
        currentLapNum = lapNum;
        currentSector = 0;  // Reset to sector 0 (before S1)
        lastSplit1Time = -1;
        lastSplit2Time = -1;
        anchoredFromRaceStart = false;
        // Keep track monitor initialized - we don't want to lose position tracking
    }

    // The gate dropped on a standing start: anchor NOW at accumulated 0 (the
    // green flag is the race's t=0) and raise the grace so the upcoming S/F
    // crossing keeps this anchor instead of resetting it.
    void onRaceStart() {
        setAnchor(0);
        currentLapNum = 0;   // first lap
        currentSector = 0;   // before S1
        lastSplit1Time = -1;
        lastSplit2Time = -1;
        anchoredFromRaceStart = true;
    }

    // ------------------------------------------------------------------------
    // Reads
    // ------------------------------------------------------------------------

    // Pause/resume support - adjusts anchor to exclude pause duration
    void pause() {
        if (!isPaused && anchorValid) {
            pausedAt = now();
            isPaused = true;
        }
    }

    void resume() {
        if (isPaused && anchorValid) {
            // Adjust anchor forward by the pause duration so elapsed time is correct
            auto pauseDuration = now() - pausedAt;
            anchorTime += pauseDuration;
            isPaused = false;
        }
    }

    // Calculate elapsed lap time since anchor
    int getElapsedLapTime() const {
        if (!anchorValid) {
            return -1;  // No anchor - show placeholder
        }

        // Use pause time if paused, otherwise use now
        auto endTime = isPaused ? pausedAt : now();
        auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - anchorTime
        ).count();

        int elapsed = anchorAccumulatedTime + static_cast<int>(wallElapsed);

        // Sanity check - don't show negative time
        if (elapsed < 0) elapsed = 0;

        return elapsed;
    }

    // Calculate elapsed sector time
    // sectorIndex: 0=S1 (from lap start), 1=S2 (from S1), 2=S3 (from S2)
    int getElapsedSectorTime(int sectorIndex) const {
        int lapTime = getElapsedLapTime();
        if (lapTime < 0) {
            return -1;  // No valid elapsed time
        }

        switch (sectorIndex) {
            case 0:  // S1: time from lap start
                return lapTime;
            case 1:  // S2: time from S1
                if (lastSplit1Time > 0) {
                    return lapTime - lastSplit1Time;
                }
                return -1;  // S1 not crossed yet
            case 2:  // S3: time from S2
                if (lastSplit2Time > 0) {
                    return lapTime - lastSplit2Time;
                }
                return -1;  // S2 not crossed yet
            default:
                return -1;
        }
    }
};
