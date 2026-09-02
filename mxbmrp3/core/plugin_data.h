// ============================================================================
// core/plugin_data.h
// Central data store for all game state received from the game API.
//
// The VALUE TYPES this store holds (SessionData, StandingsData, telemetry
// buffers, ...) live in plugin_data_types.h, which this header includes — so
// existing includers see no change. Include that header directly when you only
// need the data and not the singleton.
// ============================================================================
#pragma once

#include "plugin_data_types.h"
#include "plugin_utils.h"
#include "live_gap_engine.h"
#include "lap_timer.h"

class PluginData {
public:
    static PluginData& getInstance();

    // SessionData field setters (called by event/session handlers)
    void setRiderName(const char* riderName);
    void setBikeName(const char* bikeName);
    void setCategory(const char* category);
    void setTrackId(const char* trackId);
    void setTrackName(const char* trackName);
    void setTrackLength(float trackLength);
    void setEventType(int eventType);
    void setServerType(int serverType);
    void setServerName(const char* serverName);
    const char* getServerName() const { return m_sessionData.serverName; }
    void setShiftRPM(int shiftRPM);
    void setLimiterRPM(int limiterRPM);
    void setSteerLock(float steerLock);
    void setEngineTemperatureThresholds(float optTemp, float alarmLow, float alarmHigh);
    void setMaxFuel(float maxFuel);
    void setNumberOfGears(int numberOfGears);
    void setSession(int session);
    void setSessionSeries(int sessionSeries) { m_sessionData.sessionSeries = sessionSeries; }  // KRP heat index (0 elsewhere)
    void incrementSessionGeneration();  // Called on every new session (RaceSession callback)
    void setSessionState(int sessionState);
    void setSessionLength(int sessionLength);
    void setSessionNumLaps(int sessionNumLaps);
    void setConditions(int conditions);
    void setAirTemperature(float airTemperature);
    void setTrackTemperature(float trackTemperature);
    void setSetupFileName(const char* setupFileName);

    // Race entry management
    void addRaceEntry(int raceNum, const char* name, const char* bikeName);
    void removeRaceEntry(int raceNum);  // Also cleans up all per-rider data for this race number
    const std::unordered_map<int, RaceEntryData>& getRaceEntries() const { return m_raceEntries; }  // Collection (never null)
    const RaceEntryData* getRaceEntry(int raceNum) const;  // Per-rider (nullable)

    // Player race number with lazy evaluation (const-correct)
    int getPlayerRaceNum() const;
    void setPlayerRaceNum(int raceNum);  // Directly set player's race number (avoids name-based lookup)

    // Player entry detection (first RaceAddEntry with unactive=0 after EventInit is the player)
    void setWaitingForPlayerEntry(bool waiting) { m_bWaitingForPlayerEntry = waiting; }
    bool isWaitingForPlayerEntry() const { return m_bWaitingForPlayerEntry; }

    // Pending player entry (for spectate-first case where RaceAddEntry arrives before EventInit)
    void setPendingPlayerRaceNum(int raceNum) { m_iPendingPlayerRaceNum = raceNum; }
    int getPendingPlayerRaceNum() const { return m_iPendingPlayerRaceNum; }
    void clearPendingPlayerRaceNum() { m_iPendingPlayerRaceNum = -1; }

    // Spectate mode tracking
    void setDrawState(int state);  // Set current draw state (ON_TRACK/SPECTATE/REPLAY)
    void setSpectatedRaceNum(int raceNum);  // Set which rider is being spectated
    int getDrawState() const { return m_drawState; }  // Get current draw state
    int getSpectatedRaceNum() const { return m_spectatedRaceNum; }  // Rider being spectated (-1 if none)
    int getDisplayRaceNum() const;  // Get race number to display (player when on track, spectated rider otherwise)

    // ========================================================================
    // Per-Rider Data Management (Ideal Lap, Lap Logs, Current Lap)
    // ========================================================================
    // API Design Pattern:
    //   - Per-rider getters return POINTERS (nullable, returns nullptr if no data for that rider)
    //   - Collection getters return REFERENCES (never null, but may be empty collections)
    //   - This allows callers to distinguish "no data" from "empty data"
    // ========================================================================

    // Current lap and ideal lap management (per-rider)
    void updateCurrentLapSplit(int raceNum, int lapNum, int splitIndex, int accumulatedTime);
    void setCurrentLapNumber(int raceNum, int lapNum);  // Initialize lap number for next lap
    void updateIdealLap(int raceNum, int completedLapNum, int lapTime, int sector1, int sector2, int sector3, int sector4, bool isValid = true);
    void clearIdealLap(int raceNum);
    void clearAllIdealLap();  // Clear all riders' ideal lap data
    const CurrentLapData* getCurrentLapData(int raceNum) const;  // Returns nullptr if no data
    const IdealLapData* getIdealLapData(int raceNum) const;  // Returns nullptr if no data

    // Lap log management (per-rider, stores completed and in-progress laps)
    void updateLapLog(int raceNum, const LapLogEntry& entry);
    void clearLapLog(int raceNum);
    void clearAllLapLog();  // Clear all riders' lap log
    const std::deque<LapLogEntry>* getLapLog(int raceNum) const;  // Returns nullptr if no data

    // Best lap entry storage (per-rider, separate from lap log for easy access)
    void setBestLapEntry(int raceNum, const LapLogEntry& entry);
    const LapLogEntry* getBestLapEntry(int raceNum) const;  // Returns nullptr if no data

    // Overall best lap (fastest lap by any rider, with splits for gap comparison)
    void setOverallBestLap(const LapLogEntry& entry);
    const LapLogEntry* getOverallBestLap() const;
    const LapLogEntry* getPreviousOverallBestLap() const;
    void clearOverallBestLap() { m_overallBestLap.lapNum = -1; m_previousOverallBestLap.lapNum = -1; }

    // Convenience methods for display race number (uses getDisplayRaceNum internally)
    const CurrentLapData* getCurrentLapData() const { return getCurrentLapData(getDisplayRaceNum()); }
    const IdealLapData* getIdealLapData() const { return getIdealLapData(getDisplayRaceNum()); }
    const std::deque<LapLogEntry>* getLapLog() const { return getLapLog(getDisplayRaceNum()); }
    const LapLogEntry* getBestLapEntry() const { return getBestLapEntry(getDisplayRaceNum()); }

    // Check if display rider has finished the race (convenience helper)
    bool isDisplayRiderFinished() const;

    // ========================================================================
    // Centralized Lap Timer Management (display rider only)
    // Provides real-time elapsed lap and sector timing for HUDs
    // Tracks only the currently displayed rider (like GapBarHud pattern)
    // ========================================================================

    // Update lap timer with track position for S/F crossing detection
    // Returns true if S/F crossing was detected (anchor was set)
    bool updateLapTimerTrackPosition(int raceNum, float trackPos, int lapNum);

    // Set timer anchor when official split/lap event occurs
    // Called by handlers when splits are received
    void setLapTimerAnchor(int raceNum, int accumulatedTime, int lapNum, int sectorIndex);

    // Anchor the display rider's lap timer at the green flag of a standing (grid) start, so the
    // live time counts from the race start THROUGH the grid->S/F run and matches the official
    // splits (which accumulate from the start). Without this the timer would only anchor at the
    // first S/F crossing and then jump forward by the grid->S/F time when the first official
    // split arrives. Sets the grid-start grace so the first S/F crossing doesn't reset it.
    // No-op for a non-display rider. Called on the PRE_START->IN_PROGRESS transition (races and
    // grid qualifying); pit-start sessions never reach that transition.
    void startLapTimerAtRaceStart(int raceNum);

    // Reset timer on new lap (called when lap completes)
    void resetLapTimerForNewLap(int raceNum, int lapNum);

    // Reset timer completely (for session change, spectate target change, pit entry)
    void resetLapTimer(int raceNum);
    void resetAllLapTimers();

    // Gate-drop detection for the standing (grid) start lap-timer anchor, driven by the
    // RaceClassification's own sessionState. The PRE_START->IN_PROGRESS RaceSessionState flip is
    // NOT the race start: through the (variable) gate hold that follows, the CLASSIFICATION stream
    // reports state=Complete(0x20); the moment the gate actually DROPS it flips to IN_PROGRESS
    // (0x10). That discrete flip is the true race start (it coincides with the race clock starting)
    // and, unlike a clock-magnitude test, works for pure-lap races too.
    //
    // armGateDropDetect() is called on the RaceSessionState PRE_START->IN_PROGRESS transition (only
    // grid starts reach it; pit starts arrive already IN_PROGRESS via handleRaceSession).
    // detectGateDrop(clsState) is called per classification and returns true ONCE, on the first
    // classification that reports IN_PROGRESS *after* the gate hold was observed (a non-racing
    // classification state). If racing is reported immediately with no hold (no grid gate), it
    // never fires and the timer falls back to anchoring at the first S/F crossing, as before.
    void armGateDropDetect() { m_awaitingGateDrop = true; m_gateDropSawHold = false; }
    bool detectGateDrop(int classificationState) {
        if (!m_awaitingGateDrop) return false;
        const bool nowRacing = (classificationState & PluginConstants::SessionState::IN_PROGRESS) != 0;
        if (!nowRacing) { m_gateDropSawHold = true; return false; }  // gate hold in progress
        if (m_gateDropSawHold) { m_awaitingGateDrop = false; return true; }  // held -> racing = gate drop
        // Racing with no observed hold: not a distinguishable grid drop. Disarm so the watch
        // (and the grid-start grace that keys on it) can't stick for the whole session.
        m_awaitingGateDrop = false;
        return false;
    }

    // Invalidate the anchor (live time -> placeholder until next S/F) without losing track
    // monitoring. Called on the display rider's pit exit. No-op if raceNum isn't the tracked rider.
    void invalidateLapTimerAnchor(int raceNum);

    // Get elapsed times (returns -1 if no valid anchor or different rider)
    int getElapsedLapTime(int raceNum) const;
    int getElapsedSectorTime(int raceNum, int sectorIndex) const;  // sectorIndex: 0=S1, 1=S2, 2=S3

    // Check if timer has valid anchor
    bool isLapTimerValid(int raceNum) const;

    // Test-only: whether the display rider's timer is in the grid-start grace window (anchored
    // at the green flag, first lap not yet complete). Exposed for the grid-start regression.
    bool isLapTimerAnchoredFromRaceStart() const { return m_displayLapTimer.anchoredFromRaceStart; }

    // Get current lap number being timed
    int getLapTimerCurrentLap(int raceNum) const;

    // Get current sector being timed (0=before S1, 1=before S2, 2=before S3)
    int getLapTimerCurrentSector(int raceNum) const;

    // Convenience methods for display race number
    int getElapsedLapTime() const { return getElapsedLapTime(getDisplayRaceNum()); }
    int getElapsedSectorTime(int sectorIndex) const { return getElapsedSectorTime(getDisplayRaceNum(), sectorIndex); }
    bool isLapTimerValid() const { return isLapTimerValid(getDisplayRaceNum()); }
    int getLapTimerCurrentLap() const { return getLapTimerCurrentLap(getDisplayRaceNum()); }
    int getLapTimerCurrentSector() const { return getLapTimerCurrentSector(getDisplayRaceNum()); }

    // Standings management
    void updateStandings(int raceNum, int state, int bestLap, int bestLapNum,
        int numLaps, int gap, int gapLaps, int penalty, int pit, bool notify);
    void batchUpdateStandings(Unified::RaceClassificationEntry* entries, int numEntries);
    const std::unordered_map<int, StandingsData>& getStandings() const { return m_standings; }  // Collection (never null)
    // The session's best lap by ANYONE, -1 when nobody has set one. Lives here
    // rather than on a HUD because it is a question about the STANDINGS with
    // three consumers: TimingHud's "Overall" row, the pit board's lap
    // reference, and the spotter's {overall_best} — and a core singleton must
    // not reach into a HUD.
    int getOverallBestLapTime() const {
        int best = -1;
        for (const auto& [raceNum, standing] : m_standings) {
            (void)raceNum;
            if (standing.bestLap > 0 && (best < 0 || standing.bestLap < best)) {
                best = standing.bestLap;
            }
        }
        return best;
    }
    const StandingsData* getStanding(int raceNum) const;  // Per-rider (nullable)

    // Classification order (preserves the game's official race position order)
    void setClassificationOrder(const std::vector<int>& order);
    const std::vector<int>& getClassificationOrder() const { return m_classificationOrder; }

    // Position lookup - efficiently find a rider's position by race number (1-based, or -1 if not found)
    // Uses cached map that's only rebuilt when classification changes
    int getPositionForRaceNum(int raceNum) const;

    // Battle groups: the single battle definition shared by the auto-director (which
    // rider to follow) and the web overlay (which "Battle for Nth" cards to show), so
    // the detection logic lives in one place. Racing, on-track riders ordered by
    // position; adjacent same-lap riders whose interval (from the official split gap -
    // stable, unlike realTimeGap which flickers with the active-track-pos batch) is
    // > 0 and <= gapThresholdMs are greedily chained into a group (front rider first).
    // maxLeaderPos > 0 drops groups
    // whose front rider is beyond that position (0 = no limit). Returns groups of race
    // numbers; only groups of 2+ riders are returned.
    std::vector<std::vector<int>> getBattleGroups(int gapThresholdMs, int maxLeaderPos) const;

    // Display classification: official order with optional DNS filtering
    const std::vector<int>& getDisplayClassificationOrder() const;
    int getDisplayPositionForRaceNum(int raceNum) const;

    // Race-start grid snapshot (positions gained/lost since the race went green).
    // Captured on the PRE_START -> IN_PROGRESS transition in handleRaceSessionState.
    // Mid-race joins arrive already IN_PROGRESS via handleRaceSession, so that transition
    // self-skips and no snapshot is taken (the column shows its placeholder). A completed-lap
    // check inside the function is a secondary guard. Cleared on every new session; empty
    // outside of race sessions, so getRaceStartPosition() returns -1 and consumers render nothing.
    void snapshotRaceStartPositions();
    void clearRaceStartPositions() { m_raceStartPositions.clear(); }
    // Official starting position for a rider (1-based), or -1 if no snapshot exists.
    int getRaceStartPosition(int raceNum) const {
        auto it = m_raceStartPositions.find(raceNum);
        return (it != m_raceStartPositions.end()) ? it->second : -1;
    }

    // Rolling positions-gained references (for the standings "Since S/F" / "Since split"
    // modes). Captured per rider as they cross the start/finish line (recordSfReference)
    // or any split (recordSplitReference). The S/F line is itself a split boundary, so a
    // lap crossing advances both references. Unlike the race-start snapshot these need no
    // grid order and self-heal on mid-race joins: each rider gains a reference on their
    // next crossing, so the column populates within a lap of joining.
    void recordSfReference(int raceNum) {
        int pos = getPositionForRaceNum(raceNum);
        if (pos > 0) {
            m_lastSfPositions[raceNum] = pos;
            m_lastSplitPositions[raceNum] = pos;
        }
    }
    void recordSplitReference(int raceNum) {
        int pos = getPositionForRaceNum(raceNum);
        if (pos > 0) m_lastSplitPositions[raceNum] = pos;
    }
    int getSfReferencePosition(int raceNum) const {
        auto it = m_lastSfPositions.find(raceNum);
        return (it != m_lastSfPositions.end()) ? it->second : -1;
    }
    int getSplitReferencePosition(int raceNum) const {
        auto it = m_lastSplitPositions.find(raceNum);
        return (it != m_lastSplitPositions.end()) ? it->second : -1;
    }
    void clearPositionReferences() {
        m_lastSfPositions.clear();
        m_lastSplitPositions.clear();
    }

    void setShortTimeFormat(bool enabled) { m_shortTimeFormat = enabled; }
    bool isShortTimeFormat() const { return m_shortTimeFormat; }

    // DNS rider filtering: hide Did Not Start riders from display
    // When enabled, getDisplayClassificationOrder() and getDisplayPositionForRaceNum()
    // exclude riders with state == DNS. Official accessors are unaffected.
    void setFilterDnsRiders(bool enabled);
    bool isFilterDnsRiders() const { return m_filterDnsRiders; }

    // Real-time track position management (for time-based gap calculation)
    // Notifies SessionData on whole-second boundaries (drives 1Hz HUD/SSE refresh).
    void setSessionTime(int sessionTime);
    int getSessionTime() const { return m_currentSessionTime; }
    // ELAPSED ms since the session clock started — monotonic ascending in
    // BOTH session kinds, unlike getSessionTime(), which is the raw game
    // clock and counts DOWN (to zero, then negative in overtime) in timed
    // sessions. Anything doing cooldown/interval arithmetic (the spotter's
    // detectors) or stamping an ordered timeline (finish times, cue log)
    // must use this one; the raw clock silently breaks it in every timed
    // race. Same formula batchUpdateStandings uses for finishTime.
    int getSessionElapsedTime() const {
        // NOTHING has elapsed before the green flag, and the raw clock says
        // otherwise: through PRE_START and the sighting lap the game sends a
        // NEGATIVE countdown (race_classification_handler documents it), so
        // `length - time` comes out ABOVE the session length and then drops
        // to ~0 at the green. Unclamped, that is the one place this is not
        // the monotonic value it promises to be, and it is not a quiet one:
        // cues do fire on the grid (spectate target, session state,
        // proximity), so a whole race's transcript would open with timestamps
        // from beyond its own end. Overtime's negative clock is deliberately
        // NOT clamped — bonus laps take real time, so elapsed passing the
        // length is true.
        if (m_sessionData.sessionState &
            (PluginConstants::SessionState::PRE_START |
             PluginConstants::SessionState::SIGHTING_LAP)) {
            return 0;
        }
        if (m_sessionData.sessionLength > 0) {
            return m_sessionData.sessionLength - m_currentSessionTime;
        }
        return m_currentSessionTime > 0 ? m_currentSessionTime : 0;
    }

    // Leader's laps-to-go once a time+lap race enters overtime (the timed clock
    // has expired and the field is running the bonus laps). Drives the session
    // clock's "N TO GO / FINAL LAP / CHECKERED" label (see PluginUtils::
    // formatSessionClock), leader-relative like a real white-flag board.
    //   -1 = not in overtime, or still finishing the lap that was in progress when
    //        the clock expired (bonus laps not started yet) — show the normal clock,
    //        which reads 00:00 once the timer is at/below zero
    //    0 = leader has finished (checkered)
    //    1 = leader is on the final lap
    //   >1 = full laps remaining until the leader's final lap
    int getLeaderLapsToGo() const;
    void updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime);
    void updateActiveTrackPosRiders(int numVehicles, const Unified::TrackPositionData* positions);
    bool hasActiveTrackPos(int raceNum) const { return m_activeTrackPosRiders.count(raceNum) > 0; }
    void updateRealTimeGaps();  // Calculate gaps using time deltas
    void clearLiveGapTimingPoints();  // Clear timing points for new session

    // Per-rider session crash counter (rising-edge count from the crashed flag).
    // Returns 0 for unknown riders, riders we haven't observed yet, or games
    // that don't report crash state (WRS, KRP). Resets on new session.
    //
    // NOTE: For the local player, prefer StatsManager::getSessionCrashes() —
    // this per-rider counter shadows it (independent rising-edge detector fed
    // from RaceTrackPosition instead of RunTelemetry) and exists so spectated
    // riders can show a session crash count. Reading both for the player
    // would surface two sibling counters tracking the same underlying flag.
    int getRiderSessionCrashCount(int raceNum) const;

    // Wrong-way detection (based on track position changes)
    bool isPlayerGoingWrongWay() const;  // Check if display rider is going wrong way
    const RiderTrackState* getPlayerTrackPosition() const;  // Get display rider's track position data for debugging

    // Standing (grid) start grace: true from the green-flag state flip, through the (variable)
    // gate hold and the launch, until the DISPLAY rider crosses the first split. Reused to
    // suppress the launch-shuffle false positives that a grid start produces - the player's
    // wrong-way notice and the "hazard ahead" grid crowd - uniformly for races AND grid
    // qualifying, with no fixed grace duration. It spans two phases of the gate-drop machinery:
    // awaiting the gate drop (the grid hold), then the lap timer sitting on its race-start anchor
    // while still in sector 0 (before S1). False on pit starts (never armed, never anchored from
    // race start) and from S1 onward, so their behaviour is unchanged.
    bool isInGridStartGrace() const {
        return m_awaitingGateDrop ||
               (m_displayLapTimer.anchoredFromRaceStart && m_displayLapTimer.currentSector == 0);
    }

    // Blue flag detection (riders 1+ laps ahead approaching from behind)
    bool isPlayerBlueFlagged() const;  // True if display rider should yield to a lapper
    bool isRiderBlueFlagged(int raceNum) const;  // True if rider is being lapped and lapper is nearby
    bool isPlayerLapping() const;      // True if display rider is closing on a backmarker ahead (mirror of blue flag)
    // The lapper side of the same detection: is this rider (1+ laps up) closing on a
    // backmarker just ahead, and if so which one (-1 = not lapping). Lets the director
    // follow a front-runner carving through traffic.
    bool isRiderLapping(int raceNum) const;
    int getRiderLappingTarget(int raceNum) const;
    // ...and the other direction: WHO is closing to lap this rider (-1 = nobody).
    // The blue flag tells you one is coming; this is which bike to expect, which
    // is the only part of it you can act on.
    //
    // A scan of the same cache rather than a second map beside it: it is the
    // grid at worst, and it is asked once per blue-flag CUE (cooldown-gated,
    // seconds apart), never per frame. A reverse map would be a derived cache
    // whose eviction has to stay in step with the forward one, to save nothing
    // measurable.
    int getRiderLappedBy(int raceNum) const;

    // Blue flag tuning (INI-only advanced setting)
    // Blue-flag + hazard tuning (INI-only advanced settings) live in one struct;
    // see core/proximity_tuning.h for the clamp ranges.
    const ProximityTuning& proximityTuning() const { return m_proximity; }
    ProximityTuning& proximityTuning() { return m_proximity; }

    // Shared exclusion check for hazard/blue flag detection
    bool isRiderExcludedFromDetection(const StandingsData& standing) const;

    // Whether requestSpectateRider(raceNum) would actually land on this rider: they must be
    // an active participant currently on track. DNS/retired/DSQ/unknown riders (and anyone
    // sitting in the pits) are not in the game's spectate vehicle list, so the request is
    // consumed and silently dropped.
    //
    // ONE RULE, EVERY SURFACE. Standings, Map, Event Log and Session Charts all offer
    // click-to-spectate, and each must gate BOTH its hover highlight and its click region on
    // this — a row that highlights and then does nothing when clicked reads as a broken HUD.
    bool isRiderSpectatable(int raceNum) const;

    // Hazard detection (stationary or wrong-way riders ahead on track)
    HazardType getRiderHazardType(int raceNum) const;
    bool isHazardAhead() const;
    const std::vector<int>& getHazardRaceNums() const;


    // Live-gap HUD refresh coalescing (INI-only setting: [Advanced]
    // gapNotifyIntervalMs). 0 = notify on every change (table HUDs may
    // rebuild every frame during close racing).
    void setGapNotifyIntervalMs(int ms) { m_gapNotifyIntervalMs = std::max(0, std::min(ms, 1000)); }
    int getGapNotifyIntervalMs() const { return m_gapNotifyIntervalMs; }

    // Overtime tracking for time+laps races
    void setOvertimeStarted(bool started) { m_sessionData.overtimeStarted = started; }
    void setFinishLap(int lap) { m_sessionData.finishLap = lap; }
    void setLastSessionTime(int time) { m_sessionData.lastSessionTime = time; }
    void setLeaderFinishTime(int time) { m_sessionData.leaderFinishTime = time; }
    int getLeaderFinishTime() const { return m_sessionData.leaderFinishTime; }

    // Leader-change ("takes the lead") detection state. Must be reset per session:
    // m_lastLeaderRaceNum survives across sessions (only cleared in clear()), so without
    // this the first classification of a new session compares the new grid leader against
    // the previous session's leader and emits a spurious "takes the lead" event.
    void resetLeaderChangeTracking() { m_lastLeaderRaceNum = -1; }

    // Non-race session expiry tracking
    void setSessionTimeExpired(bool expired) { m_sessionData.sessionTimeExpired = expired; }
    void setRiderSessionFinished(int raceNum);
    // Resets ALL per-rider finish state (sessionFinished, finishTime, numLapsAtLeaderFinish)
    // for a new session. m_standings survives across sessions, so these must be cleared
    // explicitly or finish detection no-ops in later sessions (see definition).
    void resetStandingsFinishState();

    // Player running state (set by RunStart, cleared by RunStop/RunDeinit)
    void setPlayerRunning(bool running) {
        m_bPlayerIsRunning = running;
        // Pause/resume lap timer to account for game pause time
        if (running) {
            m_displayLapTimer.resume();
        } else {
            m_displayLapTimer.pause();
        }
    }
    bool isPlayerRunning() const { return m_bPlayerIsRunning; }

    // Session type checks
    bool isRaceSession() const;     // Returns true for RACE_1, RACE_2, SR sessions
    bool isQualifySession() const;  // Returns true for PRE_QUALIFY, QUALIFY_PRACTICE, QUALIFY
    // "Waiting" is the gap BETWEEN sessions (online lobbies, and a Testing event
    // before it goes green), not a session with a clock of its own: the game stops
    // sending classifications, so the last session's time just sits in
    // m_currentSessionTime.
    //
    // NOT a project-wide rule about that value -- the Time widget, the TimingHud
    // readout and the web header all keep drawing the raw clock here, deliberately:
    // a bare MM:SS claims nothing about which session it belongs to. This exists
    // for the callers that LABEL it, where "<session>: <clock>" would attribute the
    // previous session's time to this one.
    bool isWaitingSession() const;

    // Data accessors for HUD components
    const SessionData& getSessionData() const { return m_sessionData; }
    const DebugMetrics& getDebugMetrics() const { return m_debugMetrics; }
    BenchmarkMetrics& getBenchmarkMetrics() { return m_benchmarkMetrics; }
    const BenchmarkMetrics& getBenchmarkMetrics() const { return m_benchmarkMetrics; }
    const BikeTelemetryData& getBikeTelemetry() const { return m_bikeTelemetry; }
    const InputTelemetryData& getInputTelemetry() const { return m_inputTelemetry; }
    const HistoryBuffers& getHistoryBuffers() const { return m_historyBuffers; }
    void clearHistoryBuffers() { m_historyBuffers.clear(); }

    // Debug metrics update
    void updateDebugMetrics(float fps, float pluginTimeMs, float pluginPercent);

    // Bike telemetry update
    void updateSpeedometer(float speedometer, int gear, int rpm, float fuel);
    void updateRoll(float roll);
    void updatePitch(float pitch);
    void updateAcceleration(float accelX, float accelY, float accelZ);
    void updateTemperatures(float engineTemp, float waterTemp);
    void updateTreadTemperatures(const float temps[2][3]);
    void updateEcuData(int ecuMode, const char* engineMapping, int tractionControl, int engineBraking, int antiWheeling, int ecuState);
    void invalidateSpeedometer();

    // Suspension update
    void updateSuspensionMaxTravel(float frontMaxTravel, float rearMaxTravel);
    void updateSuspensionLength(float frontLength, float rearLength);

    // Input telemetry update
    void updateInputTelemetry(float steer, float throttle, float frontBrake, float rearBrake, float clutch);
    void updateXInputData(const XInputData& xinputData);

    // Limited telemetry update for spectate/replay (only updates data available in SPluginsRaceVehicleData_t)
    void updateRaceVehicleTelemetry(float speedometer, int gear, int rpm, float throttle, float frontBrake, float lean);

    // Clear telemetry data (when spectate target becomes invalid)
    void clearTelemetryData();

    // Clear all data (useful for reset scenarios)
    void clear();

    // Direct notification to HudManager (no observer pattern overhead)
    // Made public for batch update optimization (call once after multiple updates)
    void notifyHudManager(DataChangeType changeType);

    // ========================================================================
    // XInputReader Access (provides single access point for controller data)
    // ========================================================================
    // Returns const reference to XInputReader singleton
    // HUDs should use this instead of accessing XInputReader::getInstance() directly
    const XInputReader& getXInputReader() const;

    // ========================================================================
    // TrackedRiders Notification
    // ========================================================================
    // Called by TrackedRidersManager when tracked riders list/settings change
    // Triggers DataChangeType::TrackedRiders notification to HUDs
    void notifyTrackedRidersChanged();

    // ========================================================================
    // Live Gap (published by GapBarHud for use by LapLogHud and other HUDs)
    // ========================================================================
    // Positive = behind PB, Negative = ahead of PB
    void setLiveGap(int gapMs, bool valid) { m_liveGapMs = gapMs; m_liveGapValid = valid; }
    int getLiveGap() const { return m_liveGapMs; }
    bool hasValidLiveGap() const { return m_liveGapValid; }

    // ========================================================================
    // Timed Notice Flags (set by RaceLapHandler, consumed by NoticesHud)
    // ========================================================================
    void notifySessionPB()  { m_newSessionPB = true; m_sessionPBTime = std::chrono::steady_clock::now(); }
    void notifyFastestLap()  { m_newFastestLap = true; m_fastestLapTime = std::chrono::steady_clock::now(); }
    void notifyAllTimePB()  { m_newAllTimePB = true; m_allTimePBTime = std::chrono::steady_clock::now(); }

    bool hasNewSessionPB() const  { return m_newSessionPB; }
    bool hasNewFastestLap() const  { return m_newFastestLap; }
    bool hasNewAllTimePB() const  { return m_newAllTimePB; }

    std::chrono::steady_clock::time_point getSessionPBTime() const  { return m_sessionPBTime; }
    std::chrono::steady_clock::time_point getFastestLapTime() const  { return m_fastestLapTime; }
    std::chrono::steady_clock::time_point getAllTimePBTime() const   { return m_allTimePBTime; }

    void clearSessionPB()  { m_newSessionPB = false; }
    void clearFastestLap()  { m_newFastestLap = false; }
    void clearAllTimePB()  { m_newAllTimePB = false; }

    // Default setup warning (set by RunHandler when entering track with default setup)
    void notifyDefaultSetup()  { m_newDefaultSetup = true; m_defaultSetupTime = std::chrono::steady_clock::now(); }
    bool hasDefaultSetupNotice() const  { return m_newDefaultSetup; }
    std::chrono::steady_clock::time_point getDefaultSetupTime() const  { return m_defaultSetupTime; }
    void clearDefaultSetupNotice()  { m_newDefaultSetup = false; }

    // ========================================================================
    // Segment Timer (training tool: time custom track sections). The player drops
    // a chain of boundary points (Add hotkey); segments are the open arcs between
    // consecutive points (N points = N-1 segments, no auto-close), each timed live
    // as you drive through it. Times AGGREGATE like the official split timer: the
    // displayed time/delta are running totals from the chain's first point (see
    // `cum` / segment_cumulative.h), so points on the official splits read like the
    // regular HUD. Player-only, fed from RunTelemetry each tick. In-memory only -
    // never persisted.
    // ========================================================================
    enum class SegmentNoticeKind : uint8_t { None, Added, Removed };

    struct SegmentTimerData {
        // Upper bound on boundary points (a training aid - far more than anyone
        // needs; keeps the per-tick crossing scan and the "Segment N" labels sane).
        static constexpr int MAX_POINTS = 20;

        // Boundary points in the order added (trackPos 0-1). A SINGLE point is a special
        // case: a full-lap LOOP (segment 0 spans that point all the way around back to
        // itself), so one marker already times laps from it. With N>=2 points the points
        // are dividers and segments are the open arcs between consecutive points: N-1
        // segments, segment i spanning points[i] -> points[i+1] (the chain is NOT
        // auto-closed - to time the stretch back to start, drop a point on the S/F line).
        std::vector<float> points;

        // Per-segment session best (parallel, size = segmentCount()).
        std::vector<float> bests;     // best time per segment (seconds)
        std::vector<char>  hasBest;   // 1 if bests[i] is valid (char, not vector<bool>)

        // Live run state. Timed off a real wall clock (steady_clock), not the
        // telemetry on-track time which stops advancing when stationary.
        int runningSeg = -1;          // index of the segment currently being timed (-1 = none)
        std::chrono::steady_clock::time_point runStart;  // interpolated wall time of the run start

        // Index of the last completed segment: drives the split-style freeze display and
        // which segment the panel shows. The displayed time/delta come from the cumulative
        // aggregation in `cum` below.
        int lastSeg = -1;
        unsigned int completionCounter = 0;  // bumped on each completion (drives the display freeze)

        // Cumulative ("aggregate like the official splits") view of the contiguous
        // run through the chain: the shown time/delta are running totals from the
        // chain's first point, so points dropped on the official split positions
        // read like the regular timing HUD. Fed by updateSegmentTimer, read by the
        // TimingHud segment override. See segment_cumulative.h.
        SegmentCumulative cum;

        // Number of timed segments. A single point is a full-lap loop (1 segment); N>=2
        // open dividers (no wrap) make N-1 serial segments (segment i: points[i] ->
        // points[i+1]); 0 points, none.
        int segmentCount() const {
            const size_t n = points.size();
            if (n == 1) return 1;                                     // single point = full-lap loop
            return n >= 2 ? static_cast<int>(n) - 1 : 0;
        }

        // Circular distance (0-1 lap) at/under which a closing point counts as "back
        // on the first point". A closing add within this of points[0] is snapped
        // exactly onto it so the loop tiles with no sliver (see addSegmentPoint).
        static constexpr float CLOSE_EPS = 0.02f;  // ~2% of the lap

        // True once the chain is closed: the last point has come back onto the first
        // (needs >=3 points). A closed loop already tiles the lap, so adding more
        // points is blocked (a further point would overlap the closing segment).
        bool isClosed() const {
            if (points.size() < 3) return false;
            // Back on the first point, circular distance on the 0-1 lap.
            return trackSeparation(points.front(), points.back()) <=
                   CLOSE_EPS;
        }
    };

    // Official split positions (centerline 0-1), set from the track centerline handler.
    // Used to snap newly-placed segment boundaries onto a nearby real split.
    void setSplitPositions(const std::vector<float>& positions) { m_splitPositions = positions; }

    // Hotkeys: drop a boundary point at the player's current position / remove the last point.
    void addSegmentPoint();
    void removeSegmentPoint();
    // Feed the player's centerline position (0-1) each telemetry tick.
    void updateSegmentTimer(float trackPos);
    const SegmentTimerData& getSegmentTimer() const { return m_segment; }
    void resetSegmentTimer();

    // Segment notice (transient, consumed by NoticesHud like the other timed notices)
    bool hasSegmentNotice() const  { return m_segmentNotice != SegmentNoticeKind::None; }
    SegmentNoticeKind getSegmentNoticeKind() const  { return m_segmentNotice; }
    // 1-based ordinal of the POINT the notice refers to (the one just added/removed), so
    // the notice counts up as points are placed and down as they're removed.
    int getSegmentNoticeNumber() const  { return m_segmentNoticeNumber; }
    std::chrono::steady_clock::time_point getSegmentNoticeTime() const  { return m_segmentNoticeTime; }
    void clearSegmentNotice()  { m_segmentNotice = SegmentNoticeKind::None; }

    // ========================================================================
    // Event Log (ring buffer of notable race events)
    // ========================================================================
    // raceNum: the rider the event is about, or -1 for a session-level event. Only used to
    // offer click-to-spectate on the row — see EventLogEntry::raceNum.
    // nums: the event's NUMBERS, for consumers that need to compute with them
    // rather than render them (the spotter speaks the amounts). Defaulted —
    // most events have none, and the three that do are the three whose detail
    // column the spotter would otherwise have to parse back. See EventNumbers.
    void addEventLogEntry(EventLogType type, const char* message, const char* detail = nullptr,
                          int iconColorSlot = -1, int raceNum = -1,
                          const EventNumbers& nums = {});
    const std::deque<EventLogEntry>& getEventLog() const { return m_eventLog; }

private:
    // Order matches the member declaration order below (see BaseHud's note).
    PluginData() : m_bPositionCacheDirty(true),
                   m_currentSessionTime(0), m_playerRaceNum(-1), m_bPlayerRaceNumValid(false),
                   m_bPlayerNotFoundWarned(false), m_bWaitingForPlayerEntry(false),
                   m_iPendingPlayerRaceNum(-1), m_bPlayerIsRunning(false),
                   m_drawState(0), m_spectatedRaceNum(-1) {}
    ~PluginData() {}
    PluginData(const PluginData&) = delete;
    PluginData& operator=(const PluginData&) = delete;

    // Rebuild blue flag caches (player flag + per-rider set)
    void rebuildBlueFlagCaches() const;

    // Rebuild per-rider hazard type cache (lazy, called on first access when dirty)
    void rebuildHazardTypeCaches() const;

    // Compute hazard type for a single rider (uncached, used during cache rebuild)
    HazardType computeRiderHazardType(int raceNum, std::chrono::steady_clock::time_point now) const;

    // Initialize per-rider hazard state after a pit 1→0 transition
    // (grace period + movement-tracking flag). No-op if the rider has no
    // RiderTrackState entry yet.
    void startPitExitGrace(int raceNum);

    // Update cached player race number by searching race entries
    void updatePlayerRaceNum() const;

    // Template helper for setting char array values with change detection
    bool setStringValue(char* field, size_t fieldSize, const char* newValue);

    // Template helper for setting numeric values with change detection
    template<typename T>
    bool setValue(T& field, const T& newValue) {
        if (field != newValue) {
            field = newValue;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Per-rider container registry.
    //
    // Any container keyed by raceNum that holds AUTHORITATIVE per-rider state
    // must be evicted in removeRaceEntry() and reset in clear() — otherwise a
    // new rider joining mid-event with a departed rider's number inherits stale
    // standings / gap / position-reference / lap-history state. PerRider<>
    // makes that hold by construction rather than by a hand-maintained erase
    // list: declaring the member registers erase + clear callbacks with the
    // owning PluginData,
    // and removeRaceEntry() / clear() iterate the registry. The declaration IS
    // the registration — there is no second list to update.
    //
    // NOT registered, on purpose:
    //  - m_raceEntries: the primary entity map removeRaceEntry() itself drives
    //    (it needs the entry alive for logging/player checks, then erases it).
    //  - Derived caches rebuilt wholesale from other state when their dirty
    //    flag is set (m_positionCache, m_filteredPositionCache,
    //    m_cachedBlueFlaggedSet, m_cachedLapperToLapped, m_cachedHazardTypes):
    //    the dirty flag is their eviction mechanism — removeRaceEntry() sets it.
    struct PerRiderHook {
        void (*eraseFn)(void* container, int raceNum);
        void (*clearFn)(void* container);
        void* container;
    };
    std::vector<PerRiderHook> m_perRiderHooks;  // declared before every PerRider<> member (init order)

    template <typename C>
    class PerRider : public C {
    public:
        explicit PerRider(PluginData& owner) {
            owner.m_perRiderHooks.push_back({
                [](void* c, int raceNum) { static_cast<C*>(c)->erase(raceNum); },
                [](void* c) { static_cast<C*>(c)->clear(); },
                static_cast<C*>(this) });
        }
        // No copy: a copy would dodge registration (or double-register), and the
        // singleton owner never needs one.
        PerRider(const PerRider&) = delete;
        PerRider& operator=(const PerRider&) = delete;
    };

    SessionData m_sessionData;
    DebugMetrics m_debugMetrics;
    BenchmarkMetrics m_benchmarkMetrics;
    BikeTelemetryData m_bikeTelemetry;
    InputTelemetryData m_inputTelemetry;
    HistoryBuffers m_historyBuffers;
    std::unordered_map<int, RaceEntryData> m_raceEntries;
    PerRider<std::unordered_map<int, StandingsData>> m_standings{*this};
    PerRider<std::unordered_map<int, int>> m_lastValidOfficialGap{*this};  // Cache of last valid official gap per rider (prevents flicker)
    std::vector<int> m_classificationOrder;  // Official race position order from game
    int m_lastLeaderRaceNum = -1;  // Previous race leader (for leader change detection, race sessions only)
    mutable std::unordered_map<int, int> m_positionCache;  // Cached position lookup (race number -> position), rebuilt when classification changes
    mutable bool m_bPositionCacheDirty;  // Flag to rebuild position cache
    PerRider<std::unordered_map<int, int>> m_raceStartPositions{*this};  // raceNum -> official starting position (1-based), snapshotted at race green flag
    PerRider<std::unordered_map<int, int>> m_lastSfPositions{*this};     // raceNum -> position at last start/finish crossing (rolling, "Since S/F" mode)
    PerRider<std::unordered_map<int, int>> m_lastSplitPositions{*this};  // raceNum -> position at last split crossing (rolling, "Since split" mode)

    // Display filters (global toggles saved in [General])
    bool m_shortTimeFormat = true;               // Compact time format: drop leading 0: for sub-minute times (keeps ms precision)
    bool m_filterDnsRiders = false;              // Hide DNS riders from display

    // DNS-filtered cache (derived from official classification order, rebuilt when dirty)
    mutable std::vector<int> m_filteredClassificationOrder;
    mutable std::unordered_map<int, int> m_filteredPositionCache;
    mutable bool m_bFilteredOrderDirty = true;

    PerRider<std::unordered_map<int, RiderTrackState>> m_trackPositions{*this};  // Real-time track positions
    // Riders in the most recent API track position batch. Feeds liveGapValid;
    // no batches arrive while the player sits in menus, so a departed rider's
    // stale "active" bit would never refresh out on its own (hence PerRider).
    PerRider<std::unordered_set<int>> m_activeTrackPosRiders{*this};
    mutable bool m_cachedPlayerBlueFlagged = false;        // Cached: is the display rider blue-flagged?
    mutable bool m_cachedPlayerLapping = false;            // Cached: is the display rider lapping a backmarker ahead?
    mutable std::unordered_set<int> m_cachedBlueFlaggedSet;  // Cached per-rider blue flag lookup (recomputed when dirty)
    mutable std::unordered_map<int, int> m_cachedLapperToLapped;  // Cached: lapper raceNum -> the backmarker it's catching
    // Reused scratch for rebuildBlueFlagCaches: one flat pass collects every rider
    // that has a track position (with laps/pos + the two role flags), so the pairwise
    // proximity loop reads this array instead of doing 2 hash lookups per inner
    // iteration (the O(n^2) map-lookup cost). Reused across rebuilds (clear keeps cap).
    // The loop that consumes it is pure logic in core/blue_flag_detect.h.
    mutable std::vector<BlueFlag::Rider> m_blueFlagScratch;
    mutable bool m_blueFlagsDirty = true;                // Invalidated when track positions change

    // Hazard detection state and configuration
    mutable std::vector<int> m_cachedHazardRaceNums;     // Cached hazard result (recomputed when dirty)
    mutable std::unordered_map<int, HazardType> m_cachedHazardTypes;  // Cached per-rider hazard type (recomputed when dirty)
    mutable bool m_hazardsDirty = true;                  // Invalidated when track positions change
    mutable bool m_hazardTypesDirty = true;              // Invalidated alongside m_hazardsDirty
    ProximityTuning m_proximity;   // INI-only blue-flag + hazard tuning
    PerRider<std::unordered_map<int, CurrentLapData>> m_riderCurrentLap{*this};  // Current lap split data per rider
    PerRider<std::unordered_map<int, IdealLapData>> m_riderIdealLap{*this};  // Ideal lap sectors per rider
    PerRider<std::unordered_map<int, std::deque<LapLogEntry>>> m_riderLapLog{*this};  // Lap log per rider (newest first, deque for O(1) front insert)
    PerRider<std::unordered_map<int, LapLogEntry>> m_riderBestLap{*this};  // Best lap entry per rider (for easy access)
    LapLogEntry m_overallBestLap;          // Overall best lap (any rider) with splits for gap comparison
    LapLogEntry m_previousOverallBestLap;  // Previous overall best (for showing improvement)

    // Single centralized lap timer for display rider only (follows GapBarHud pattern)
    // Resets when spectate target changes - no need to track all riders
    LapTimer m_displayLapTimer;
    int m_displayLapTimerRaceNum = -1;  // Which rider the timer is currently tracking

    // Gate-drop detection (see armGateDropDetect / detectGateDrop). Watches the classification
    // sessionState flip from the gate hold (non-racing) to IN_PROGRESS on a grid start.
    bool m_awaitingGateDrop = false;
    bool m_gateDropSawHold = false;

    // Live leader-relative gaps: the timing-point store and gap math live in
    // the pure LiveGap::Engine (live_gap_engine.h, unit-tested without the
    // game); this class owns the flattening, the staleness gating and the
    // notification coalescing (plugin_data_livegaps.cpp).
    static constexpr int GAP_UPDATE_THRESHOLD_MS = 100;  // Minimum gap change (in ms) to trigger cache update (prevents flicker from small oscillations)
    // Time-coalescing for the Standings notification out of updateRealTimeGaps.
    // The per-rider threshold alone doesn't throttle on full grids: leader
    // timing is quantized to 1% of a lap, so gaps step by ~lapTime/100 when a
    // rider crosses a quantization boundary, and with 30+ riders that happens
    // on nearly every RaceTrackPosition callback. m_gapNotifyPending carries a
    // skipped notify so the last change always flushes on a later call.
    // Interval is INI-tunable via setGapNotifyIntervalMs (default 100ms).
    static constexpr int DEFAULT_GAP_NOTIFY_INTERVAL_MS = 100;
    int m_gapNotifyIntervalMs = DEFAULT_GAP_NOTIFY_INTERVAL_MS;
    std::chrono::steady_clock::time_point m_lastGapNotify{};
    bool m_gapNotifyPending = false;
    LiveGap::Engine m_liveGapEngine;
    // Scratch buffers reused across updateRealTimeGaps calls (steady state
    // allocates nothing on the ~30Hz path).
    std::vector<LiveGap::Rider> m_liveGapRiders;
    std::vector<StandingsData*> m_liveGapStandings;  // index-parallel with m_liveGapRiders
    std::vector<int> m_liveGapPrev;
    std::vector<LiveGap::GapResult> m_liveGapResults;
    int m_currentSessionTime;  // Most recent session time in milliseconds

    // Thread safety: These mutable cache members are NOT thread-safe
    // The plugin runs single-threaded - all API callbacks occur on the main game thread
    // If multi-threading is added in the future, these will need synchronization
    mutable int m_playerRaceNum;           // Cached player race number for performance
    mutable bool m_bPlayerRaceNumValid;     // Is the cached player race number still valid?
    mutable bool m_bPlayerNotFoundWarned;   // Have we already warned about player not found?
    mutable bool m_bWaitingForPlayerEntry;  // True after EventInit, cleared when player entry is identified
    int m_iPendingPlayerRaceNum;            // Stores raceNum from RaceAddEntry before EventInit (spectate-first case)

    bool m_bPlayerIsRunning;                // Set by RunStart, cleared by RunStop/RunDeinit

    // Spectate mode tracking
    int m_drawState;                       // Current draw state (ON_TRACK=0, SPECTATE=1, REPLAY=2)
    int m_spectatedRaceNum;                // Race number of rider being spectated (-1 if none)

    // Live gap tracking (published by GapBarHud)
    int m_liveGapMs = 0;                   // Current gap in milliseconds (positive = behind PB, negative = ahead)
    bool m_liveGapValid = false;           // Is the live gap valid?

    // Timed notice flags (set by RaceLapHandler, consumed by NoticesHud)
    // Uses steady_clock timestamps so NoticesHud can show timed notices.
    // Invariant: the bool flag and time_point are always set together in notify*().
    // The time_points default to epoch, but that's safe because the bool flag gates
    // access — isTimedNoticeActive() is only called when the flag is true.
    bool m_newSessionPB = false;
    bool m_newFastestLap = false;
    bool m_newAllTimePB = false;
    bool m_newDefaultSetup = false;
    std::chrono::steady_clock::time_point m_sessionPBTime;
    std::chrono::steady_clock::time_point m_fastestLapTime;
    std::chrono::steady_clock::time_point m_allTimePBTime;
    std::chrono::steady_clock::time_point m_defaultSetupTime;

    // Segment timer (training tool - see public API above)
    SegmentTimerData m_segment;
    bool m_segmentHasPrev = false;          // Have a previous telemetry sample for crossing detection
    float m_segmentPrevPos = 0.0f;          // Previous tick trackPos (0-1)
    std::chrono::steady_clock::time_point m_segmentPrevWall;  // Wall time of previous tick (for interpolation)
    SegmentNoticeKind m_segmentNotice = SegmentNoticeKind::None;
    int m_segmentNoticeNumber = 0;  // 1-based point ordinal the notice refers to (0 = none)
    std::chrono::steady_clock::time_point m_segmentNoticeTime;
    std::vector<float> m_splitPositions;    // official split positions (0-1) for snap-to-split

    // Event log ring buffer
    std::deque<EventLogEntry> m_eventLog;
};
