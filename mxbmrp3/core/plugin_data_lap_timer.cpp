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

    bool sfCrossingDetected = m_displayLapTimer.onTrackPosition(trackPos, lapNum);
    if (sfCrossingDetected) {
        DEBUG_INFO_F("LapTimer: S/F crossing detected via track position, lap=%d", lapNum);
    }
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

void PluginData::resetLapTimerForNewLap(int raceNum, int lapNum) {
    // Only update if this is the display rider
    if (raceNum != getDisplayRaceNum() || raceNum != m_displayLapTimerRaceNum) {
        return;
    }

    m_displayLapTimer.onLapComplete(lapNum);

    DEBUG_INFO_F("LapTimer: Reset for new lap, lap=%d", lapNum);
}

void PluginData::startLapTimerAtRaceStart(int raceNum) {
    // Only the display rider has a live timer.
    if (raceNum != getDisplayRaceNum()) {
        return;
    }

    // Bind the timer to this rider (a fresh session just reset it via resetAllLapTimers()).
    m_displayLapTimerRaceNum = raceNum;
    m_displayLapTimer.onRaceStart();

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
