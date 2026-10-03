// ============================================================================
// core/plugin_data_lap_timer.cpp
// Centralized lap-timer management (display rider), and the live gap to PB that
// rides on its transitions (core/pb_gap_tracker.h).
// ============================================================================

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "hud_manager.h"  // Direct include for notification
#include "stats_manager.h"
#include "pb_trace_store.h"
#if GAME_HAS_DISCORD
#include "discord_manager.h"  // Direct include for Discord presence updates
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"  // Steam friends rich-presence integration
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"  // Direct include for web overlay updates
#endif
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// Centralized Lap Timer Management
// Single timer for display rider only. The PB gap tracker (m_pbGap) rides along:
// every transition that anchors, resets or invalidates the timer is also the
// transition the tracker needs, so the two cannot disagree about which lap is
// being measured or against which clock. See core/pb_gap_tracker.h.
// ============================================================================

bool PluginData::updateLapTimerTrackPosition(int raceNum, float trackPos, int lapNum) {
    // Only track the display rider (like GapBarHud)
    int displayRaceNum = getDisplayRaceNum();
    if (raceNum != displayRaceNum) {
        return false;
    }

    // Reset timer if spectate target changed
    if (m_displayLapTimerRaceNum != displayRaceNum) {
        DEBUG_INFO_F("LapTimer: Display rider changed %d -> %d, resetting timer",
                     m_displayLapTimerRaceNum, displayRaceNum);
        m_displayLapTimer.reset();
        m_pbGap.reset();
        m_displayLapTimerRaceNum = displayRaceNum;
        plantAllTimeGapReference();
    }

    bool sfCrossingDetected = m_displayLapTimer.onTrackPosition(trackPos, lapNum);
    if (sfCrossingDetected) {
        DEBUG_INFO_F("LapTimer: S/F crossing detected via track position, lap=%d", lapNum);
    }

    // Sample the lap in progress against the timer's clock (-1 unanchored: pit
    // exit, mid-lap join). The tracker fences laps on the position wrap itself
    // and needs to know whether THIS sample re-anchored the timer, so the new
    // lap's samples are taken against the new lap's clock.
    m_pbGap.onTrackPosition(trackPos, m_displayLapTimer.getElapsedLapTime(), sfCrossingDetected);

    return sfCrossingDetected;
}

void PluginData::setLapTimerAnchor(int raceNum, int accumulatedTime, int lapNum, int sectorIndex) {
    // Only update if this is the display rider
    if (raceNum != getDisplayRaceNum() || raceNum != m_displayLapTimerRaceNum) {
        return;
    }

    // Note: sectorIndex == 2 (lap complete) is handled by resetLapTimerForNewLap
    m_displayLapTimer.onOfficialSplit(accumulatedTime, lapNum, sectorIndex);

    DEBUG_INFO_F("LapTimer: Anchor set, time=%d ms, lap=%d, sector=%d",
                 accumulatedTime, lapNum, sectorIndex);
}

void PluginData::resetLapTimerForNewLap(int raceNum, int lapNum, bool lapValid,
                                        bool bikePbStored, bool beatsAllTimePb) {
    // Only update if this is the display rider
    if (raceNum != getDisplayRaceNum() || raceNum != m_displayLapTimerRaceNum) {
        return;
    }

    m_displayLapTimer.onLapComplete(lapNum);
    DEBUG_INFO_F("LapTimer: Reset for new lap, lap=%d", lapNum);

    // The lap that just ended: a PB whose start was observed becomes the gap
    // reference. race_lap_handler commits the lap log, ideal lap and best-lap
    // entry BEFORE calling this, so both reads describe the lap that just ended.
    // An invalid lap keeps its time in races (the ideal lap records it), but a
    // cut lap is faster than the rider can go and a pit lap slower: neither is
    // a pace to read the next lap against, so it ends without a time and the
    // tracker drops its table (pinned by pb_gap_test).
    const IdealLapData* idealLap = getIdealLapData(raceNum);
    const LapLogEntry* personalBest = getBestLapEntry(raceNum);
    const int lapTime = (lapValid && idealLap) ? idealLap->lastLapTime : 0;
    const bool isPersonalBest = personalBest && lapTime > 0 && lapTime == personalBest->lapTime;
    // Both stats verdicts imply a session best, so they are gated on it: a lap
    // the session does not rate as its best cannot be the all-time reference.
    const bool committed = m_pbGap.onLapCompleted(lapTime, isPersonalBest, isPersonalBest && beatsAllTimePb);
    if (isPersonalBest && m_pbGap.hasBestLap()) {
        DEBUG_INFO_F("LapTimer: session best lap %d ms is the new live-gap reference", lapTime);
    }
    // The lap's table outlives the session when the stats file stored it as
    // this track+bike's PB, replacing the stored one even if that was faster
    // (stats cleared or restored): the trace follows the PB.
    if (committed && isPersonalBest && bikePbStored && raceNum == getPlayerRaceNum()) {
        if (PbTraceStore::getInstance().store(m_sessionData.trackId, m_sessionData.bikeName,
                                              m_pbGap.referenceTable(PbGapTracker::Ref::LAST_LAP), lapTime)) {
            DEBUG_INFO_F("LapTimer: all-time PB lap %d ms saved as the gap trace", lapTime);
        }
    }
}

void PluginData::plantAllTimeGapReference() {
    if (m_displayLapTimerRaceNum < 0 || m_displayLapTimerRaceNum != getPlayerRaceNum()) return;
    // The bike the active PB scope resolves to (under the class scope, the
    // fastest bike in the class), so the trace matches the PB the player is shown.
    std::string bikeName;
    const StatsPersonalBestData* pb = StatsManager::getInstance().getPersonalBest(&bikeName);
    if (!pb) return;
    const int pbLapTimeMs = pb->lapTime;   // under class scope a scratch buffer: read at once
    int lapTimeMs = 0;
    const PbGapTracker::Table* table =
        PbTraceStore::getInstance().find(m_sessionData.trackId, bikeName, &lapTimeMs);
    if (!table) return;
    // The trace is the all-time reference only while it IS the PB lap. One that
    // lags the stats file (see pb_trace_store.h) is a slower lap, and read as
    // all-time it shows the rider ahead of a PB they are not beating; no
    // reference is the honest reading until the next PB brings its own trace.
    if (lapTimeMs != pbLapTimeMs) {
        DEBUG_WARN_F("LapTimer: all-time PB trace (%d ms, %s) is not the PB (%d ms), not planted",
                     lapTimeMs, bikeName.c_str(), pbLapTimeMs);
        return;
    }
    m_pbGap.setReference(PbGapTracker::Ref::ALLTIME_PB, *table, lapTimeMs);
    DEBUG_INFO_F("LapTimer: all-time PB trace (%d ms, %s) planted as the live-gap reference",
                 lapTimeMs, bikeName.c_str());
}

void PluginData::startLapTimerAtRaceStart(int raceNum) {
    // Only the display rider has a live timer.
    if (raceNum != getDisplayRaceNum()) {
        return;
    }

    // Bind the timer to this rider (a fresh session just reset it via resetAllLapTimers()).
    m_displayLapTimerRaceNum = raceNum;
    m_displayLapTimer.onRaceStart();
    m_pbGap.onGridStart();   // lap 1 runs from the gate: observed, but never the reference
    // A bind like any other: the all-time trace is planted here too, so a gate
    // drop that precedes the player's first position sample does not leave the
    // race without it (idempotent when a sample already bound and planted).
    plantAllTimeGapReference();

    DEBUG_INFO_F("LapTimer: Anchored at race start (gate drop) for raceNum=%d", raceNum);
}

void PluginData::resetLapTimer(int raceNum) {
    // Only reset if this is the rider we're tracking
    if (raceNum == m_displayLapTimerRaceNum) {
        m_displayLapTimer.reset();
        m_pbGap.reset();
        plantAllTimeGapReference();
        DEBUG_INFO_F("LapTimer: Reset for raceNum=%d", raceNum);
    }
}

void PluginData::resetAllLapTimers() {
    m_displayLapTimer.reset();
    m_pbGap.reset();
    m_displayLapTimerRaceNum = -1;
    m_lapViaPits.clear();        // a new session's laps owe nothing to the old one's pit visits
    m_awaitingGateDrop = false;  // drop any pending grid-start gate-drop watch
    DEBUG_INFO("LapTimer: Timer reset");
}

bool PluginData::hasLapTimerAnchor(int raceNum) const {
    return raceNum >= 0 && raceNum == m_displayLapTimerRaceNum && m_displayLapTimer.anchorValid;
}

void PluginData::markLapViaPits(int raceNum) {
    if (raceNum < 0) return;
    // Nothing else rebuilds the Lap Log here: the sim has stopped, so its live row
    // would sit frozen on the paused time until the line closed the lap.
    if (m_lapViaPits.insert(raceNum).second && raceNum == getDisplayRaceNum()) {
        notifyHudManager(DataChangeType::LapLog);
    }
}

bool PluginData::consumeLapViaPits(int raceNum) {
    return m_lapViaPits.erase(raceNum) > 0;
}

bool PluginData::isLapViaPits(int raceNum) const {
    return m_lapViaPits.count(raceNum) > 0;
}

void PluginData::invalidateLapTimerAnchor(int raceNum, bool rejoinedTrack) {
    // Only affect the rider we're tracking. Drops the anchor (live time -> placeholder) but
    // keeps track monitoring so the next S/F crossing re-anchors from 0 -- except on a
    // re-entry, where the next sample is a baseline: the rider is at the pit box now, not a
    // delta on from where they left the track (pinned by pb_gap_test).
    if (raceNum == m_displayLapTimerRaceNum) {
        m_displayLapTimer.invalidateAnchor();
        m_pbGap.clearCurrentLap();   // the dead lap's samples must not seed the next reference
        if (rejoinedTrack) {
            m_displayLapTimer.forgetTrackPosition();
            m_pbGap.forgetTrackPosition();
        }
        DEBUG_INFO_F("LapTimer: Anchor invalidated on pit exit for raceNum=%d", raceNum);
    }
}

// ============================================================================
// Live gap to PB (reads)
// ============================================================================

bool PluginData::hasValidLiveGap(GapRef ref) const {
    if (m_liveGapForced) return m_forcedLiveGapValid;
    // The tracker follows the timer's rider; between a spectate switch and the
    // next position sample the two can disagree, and the stale reference must
    // not be read for the new rider.
    if (m_displayLapTimerRaceNum != getDisplayRaceNum()) return false;
    // In the pits, or on a lap that went through them: the lap is void, so there
    // is no gap to show, the same as the Timing panel's (pinned by pb_gap_test).
    // The set covers pit entry up to the line; the pit flag covers a pit lane
    // that crosses the line, until pit exit drops the anchor.
    if (m_lapViaPits.count(m_displayLapTimerRaceNum)) return false;
    if (const StandingsData* s = getStanding(m_displayLapTimerRaceNum); s && s->pit) return false;
    const int elapsed = m_displayLapTimer.getElapsedLapTime();
    if (!m_pbGap.hasBestLap(ref) || elapsed < 0 || m_pbGap.atLine()) return false;
    // Not on a timed lap yet: the run out of the pits (or from a mid-lap join)
    // to the next line. An official split re-anchors the clock on the way, but
    // the lap it measures began before the pit visit, so there is no gap to
    // show until the line starts a lap (pinned by pb_gap_test).
    if (!m_pbGap.currentLapObserved()) return false;
    // A hole in the reference wider than the tracker's walk-back has no gap to
    // show; the readers must draw the placeholder there, not a "+0.000".
    bool ok = false;
    m_pbGap.gapAt(m_pbGap.trackPos(), elapsed, &ok, ref);
    return ok;
}

int PluginData::getLiveGap(GapRef ref) const {
    if (m_liveGapForced) return m_forcedLiveGapMs;
    if (!hasValidLiveGap(ref)) return 0;
    return m_pbGap.gapAt(m_pbGap.trackPos(), m_displayLapTimer.getElapsedLapTime(), nullptr, ref);
}

float PluginData::getPbGhostProgress(GapRef ref) const {
    if (m_liveGapForced || !hasValidLiveGap(ref)) return -1.0f;
    return m_pbGap.bestLapProgressAt(m_displayLapTimer.getElapsedLapTime(), ref);
}

float PluginData::getDisplayRiderTrackPos() const {
    return m_pbGap.trackPos();
}

void PluginData::testForceLiveGap(int gapMs, bool valid) {
    m_liveGapForced = true;
    m_forcedLiveGapMs = gapMs;
    m_forcedLiveGapValid = valid;
}

int PluginData::getElapsedLapTime(int raceNum) const {
    if (raceNum == m_displayLapTimerRaceNum) {
        return m_displayLapTimer.getElapsedLapTime();
    }
    return -1;
}

int PluginData::getElapsedSectorTime(int raceNum, int sectorIndex) const {
    if (raceNum == m_displayLapTimerRaceNum) {
        return m_displayLapTimer.getElapsedSectorTime(sectorIndex);
    }
    return -1;
}

bool PluginData::isLapTimerValid(int raceNum) const {
    // Timer is only valid if the anchor is set for this race
    // When on track, also check if simulation is paused (RunStop called)
    // Spectate/replay modes don't have pause concept - simulation always runs
    if (m_drawState == PluginConstants::ViewState::ON_TRACK && !m_bPlayerIsRunning) {
        return false;
    }
    if (raceNum == m_displayLapTimerRaceNum) {
        return m_displayLapTimer.anchorValid;
    }
    return false;
}

int PluginData::getLapTimerCurrentLap(int raceNum) const {
    if (raceNum == m_displayLapTimerRaceNum) {
        return m_displayLapTimer.currentLapNum;
    }
    return 0;
}

int PluginData::getLapTimerCurrentSector(int raceNum) const {
    if (raceNum == m_displayLapTimerRaceNum) {
        return m_displayLapTimer.currentSector;
    }
    return 0;
}
