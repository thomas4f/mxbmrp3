// ============================================================================
// core/plugin_data_lap_timer.cpp
// Centralized lap-timer management (display rider)
// (extracted verbatim from plugin_data.cpp; no behavior change)
// ============================================================================

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "hud_manager.h"  // Direct include for notification
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
// Single timer for display rider only (follows GapBarHud pattern)
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
        m_displayLapTimerRaceNum = displayRaceNum;
    }

    LapTimer& timer = m_displayLapTimer;

    if (!timer.trackMonitorInitialized) {
        timer.lastTrackPos = trackPos;
        timer.lastLapNum = lapNum;
        timer.trackMonitorInitialized = true;
        return false;
    }

    float delta = trackPos - timer.lastTrackPos;
    bool sfCrossingDetected = false;

    // Detect S/F crossing: large negative delta (0.95 → 0.05 gives delta ~ -0.9)
    if (delta < -LapTimer::WRAP_THRESHOLD) {
        if (timer.anchoredFromRaceStart) {
            // First lap of a standing (grid) start: the anchor was set at the green flag, so the
            // live time already spans the grid->S/F run and matches the official accumulated
            // splits. This intermediate S/F crossing must NOT reset it to 0 - that would drop the
            // grid->S/F time and reintroduce the jump when the first official split resyncs the
            // anchor. Keep the anchor; the grace ends when lap 1 completes (resetLapTimerForNewLap).
        } else if (!timer.anchorValid || lapNum != timer.lastLapNum) {
            // Crossed S/F line - set anchor if we don't have one or lap changed
            timer.setAnchor(0);  // Start timing from 0
            timer.currentLapNum = lapNum;
            timer.currentSector = 0;  // Reset to sector 0 (before S1)
            timer.lastSplit1Time = -1;
            timer.lastSplit2Time = -1;
            sfCrossingDetected = true;
            DEBUG_INFO_F("LapTimer: S/F crossing detected via track position, lap=%d", lapNum);
        }
    }

    timer.lastTrackPos = trackPos;
    timer.lastLapNum = lapNum;

    return sfCrossingDetected;
}

void PluginData::setLapTimerAnchor(int raceNum, int accumulatedTime, int lapNum, int sectorIndex) {
    // Only update if this is the display rider
    if (raceNum != getDisplayRaceNum() || raceNum != m_displayLapTimerRaceNum) {
        return;
    }

    LapTimer& timer = m_displayLapTimer;
    timer.setAnchor(accumulatedTime);
    timer.currentLapNum = lapNum;

    // Update sector tracking based on which split was crossed
    // sectorIndex: 0=S1, 1=S2, 2=S3 (lap complete)
    if (sectorIndex == 0) {
        timer.currentSector = 1;  // Now in sector 2 (between S1 and S2)
        timer.lastSplit1Time = accumulatedTime;
    } else if (sectorIndex == 1) {
        timer.currentSector = 2;  // Now in sector 3 (between S2 and S3/finish)
        timer.lastSplit2Time = accumulatedTime;
    }
    // Note: sectorIndex == 2 (lap complete) is handled by resetLapTimerForNewLap

    DEBUG_INFO_F("LapTimer: Anchor set, time=%d ms, lap=%d, sector=%d",
                 accumulatedTime, lapNum, sectorIndex);
}

void PluginData::resetLapTimerForNewLap(int raceNum, int lapNum) {
    // Only update if this is the display rider
    if (raceNum != getDisplayRaceNum() || raceNum != m_displayLapTimerRaceNum) {
        return;
    }

    LapTimer& timer = m_displayLapTimer;

    // Reset anchor for new lap (accumulated = 0)
    timer.setAnchor(0);
    timer.currentLapNum = lapNum;
    timer.currentSector = 0;  // Reset to sector 0 (before S1)
    timer.lastSplit1Time = -1;
    timer.lastSplit2Time = -1;
    // The first lap of a grid start has now completed, so the grid-start grace is over: lap 2
    // onward anchors normally at each S/F crossing.
    timer.anchoredFromRaceStart = false;
    // Keep track monitor initialized - we don't want to lose position tracking

    DEBUG_INFO_F("LapTimer: Reset for new lap, lap=%d", lapNum);
}

void PluginData::startLapTimerAtRaceStart(int raceNum) {
    // Only the display rider has a live timer.
    if (raceNum != getDisplayRaceNum()) {
        return;
    }

    // Bind the timer to this rider (a fresh session just reset it via resetAllLapTimers()).
    m_displayLapTimerRaceNum = raceNum;

    LapTimer& timer = m_displayLapTimer;
    // Anchor NOW at accumulated 0: the green flag is the race's t=0, so the live time counts
    // from here through the grid->S/F run. The first official split (accumulated from the start)
    // then resyncs to essentially the same value instead of jumping forward by the grid->S/F
    // time. Keep the track monitor as-is so the upcoming S/F crossing is still detected (and
    // suppressed) via the grace flag below.
    timer.setAnchor(0);
    timer.currentLapNum = 0;   // first lap
    timer.currentSector = 0;   // before S1
    timer.lastSplit1Time = -1;
    timer.lastSplit2Time = -1;
    timer.anchoredFromRaceStart = true;

    DEBUG_INFO_F("LapTimer: Anchored at race start (gate drop) for raceNum=%d", raceNum);
}

void PluginData::resetLapTimer(int raceNum) {
    // Only reset if this is the rider we're tracking
    if (raceNum == m_displayLapTimerRaceNum) {
        m_displayLapTimer.reset();
        DEBUG_INFO_F("LapTimer: Reset for raceNum=%d", raceNum);
    }
}

void PluginData::resetAllLapTimers() {
    m_displayLapTimer.reset();
    m_displayLapTimerRaceNum = -1;
    m_awaitingGateDrop = false;  // drop any pending grid-start gate-drop watch
    DEBUG_INFO("LapTimer: Timer reset");
}

void PluginData::invalidateLapTimerAnchor(int raceNum) {
    // Only affect the rider we're tracking. Drops the anchor (live time -> placeholder) but
    // keeps track monitoring so the next S/F crossing re-anchors from 0.
    if (raceNum == m_displayLapTimerRaceNum) {
        m_displayLapTimer.invalidateAnchor();
        DEBUG_INFO_F("LapTimer: Anchor invalidated on pit exit for raceNum=%d", raceNum);
    }
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
