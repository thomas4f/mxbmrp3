// ============================================================================
// core/plugin_data.cpp
// Central data store for all game state received from the game API
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

PluginData& PluginData::getInstance() {
    static PluginData instance;
    return instance;
}

bool PluginData::setStringValue(char* field, size_t fieldSize, const char* newValue) {
    if (strcmp(field, newValue) != 0) {
        strncpy_s(field, fieldSize, newValue, fieldSize - 1);
        field[fieldSize - 1] = '\0';
        return true;
    }
    return false;
}

void PluginData::setRiderName(const char* riderName) {
    if (setStringValue(m_sessionData.riderName, sizeof(m_sessionData.riderName), riderName)) {
        m_bPlayerRaceNumValid = false;
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setBikeName(const char* bikeName) {
    if (setStringValue(m_sessionData.bikeName, sizeof(m_sessionData.bikeName), bikeName)) {
        notifyHudManager(DataChangeType::SessionData);
        // Notify rumble profile manager of bike change
        RumbleProfileManager::getInstance().setCurrentBike(bikeName);
    }
}

void PluginData::setCategory(const char* category) {
    if (setStringValue(m_sessionData.category, sizeof(m_sessionData.category), category)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setTrackId(const char* trackId) {
    if (setStringValue(m_sessionData.trackId, sizeof(m_sessionData.trackId), trackId)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setTrackName(const char* trackName) {
    if (setStringValue(m_sessionData.trackName, sizeof(m_sessionData.trackName), trackName)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setTrackLength(float trackLength) {
    if (setValue(m_sessionData.trackLength, trackLength)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setEventType(int eventType) {
    if (setValue(m_sessionData.eventType, eventType)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setServerType(int serverType) {
    if (setValue(m_sessionData.serverType, serverType)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setServerName(const char* serverName) {
    if (setStringValue(m_sessionData.serverName, sizeof(m_sessionData.serverName), serverName)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setShiftRPM(int shiftRPM) {
    if (setValue(m_sessionData.shiftRPM, shiftRPM)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setLimiterRPM(int limiterRPM) {
    if (setValue(m_sessionData.limiterRPM, limiterRPM)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setSteerLock(float steerLock) {
    if (setValue(m_sessionData.steerLock, steerLock)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setEngineTemperatureThresholds(float optTemp, float alarmLow, float alarmHigh) {
    m_sessionData.engineOptTemperature = optTemp;
    m_sessionData.engineTempAlarmLow = alarmLow;
    m_sessionData.engineTempAlarmHigh = alarmHigh;
    // No notification needed - thresholds are set once during bike initialization
}

void PluginData::setMaxFuel(float maxFuel) {
    m_bikeTelemetry.maxFuel = maxFuel;
}

void PluginData::setNumberOfGears(int numberOfGears) {
    m_bikeTelemetry.numberOfGears = numberOfGears;
}

void PluginData::setSessionTime(int sessionTime) {
    // Notify on whole-second boundary so SSE clients (web overlay) get a
    // fresh snapshot once per second instead of having to interpolate.
    int prevSec = m_currentSessionTime / 1000;
    m_currentSessionTime = sessionTime;
    int curSec = m_currentSessionTime / 1000;
    if (prevSec != curSec) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setSession(int session) {
    if (setValue(m_sessionData.session, session)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::incrementSessionGeneration() {
    ++m_sessionData.sessionGeneration;
    notifyHudManager(DataChangeType::SessionData);
}

void PluginData::setSessionState(int sessionState) {
    if (setValue(m_sessionData.sessionState, sessionState)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setSessionLength(int sessionLength) {
    if (setValue(m_sessionData.sessionLength, sessionLength)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setSessionNumLaps(int sessionNumLaps) {
    if (setValue(m_sessionData.sessionNumLaps, sessionNumLaps)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setConditions(int conditions) {
    if (setValue(m_sessionData.conditions, conditions)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setAirTemperature(float airTemperature) {
    if (setValue(m_sessionData.airTemperature, airTemperature)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setTrackTemperature(float trackTemperature) {
    if (setValue(m_sessionData.trackTemperature, trackTemperature)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::setSetupFileName(const char* setupFileName) {
    if (setStringValue(m_sessionData.setupFileName, sizeof(m_sessionData.setupFileName), setupFileName)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::addRaceEntry(int raceNum, const char* name, const char* bikeName) {
    // Validate input strings (defensive check for public API)
    if (!name || !bikeName) return;

    // Compute bike abbreviation, brand name, and color once when entry is added
    const char* bikeAbbr = PluginUtils::getBikeAbbreviationPtr(bikeName);
    const char* brandName = PluginUtils::getBikeBrandName(bikeName);
    unsigned long bikeBrandColor = PluginUtils::getBikeBrandColor(bikeName);

    auto it = m_raceEntries.find(raceNum);

    if (it != m_raceEntries.end()) {
        // Entry already exists - check if data changed
        // PERFORMANCE: Cache comparison results to avoid redundant strcmp calls
        bool nameChanged = (strcmp(it->second.name, name) != 0);
        bool bikeChanged = (strcmp(it->second.bikeName, bikeName) != 0);

        if (nameChanged || bikeChanged) {
            // Update name
            strncpy_s(it->second.name, sizeof(it->second.name), name, sizeof(it->second.name) - 1);
            it->second.name[sizeof(it->second.name) - 1] = '\0';

            // Update bike name
            strncpy_s(it->second.bikeName, sizeof(it->second.bikeName), bikeName, sizeof(it->second.bikeName) - 1);
            it->second.bikeName[sizeof(it->second.bikeName) - 1] = '\0';

            // Update cached abbreviation and color
            it->second.bikeAbbr = bikeAbbr;
            it->second.brandName = brandName;
            it->second.bikeBrandColor = bikeBrandColor;

            // PERFORMANCE: Skip race number formatting - race number never changes for existing entries
            // (formattedRaceNum was already set during initial creation)

            // Update truncated name (optimized: avoid strlen, use direct copy with bounds check)
            size_t copyLen = 0;
            while (copyLen < 3 && name[copyLen] != '\0') {
                it->second.truncatedName[copyLen] = name[copyLen];
                copyLen++;
            }
            it->second.truncatedName[copyLen] = '\0';

            // Invalidate player race number if THIS rider is the player and name changed
            // (Only matters if this specific rider is the player)
            if (nameChanged && raceNum == m_playerRaceNum && m_bPlayerRaceNumValid) {
                m_bPlayerRaceNumValid = false;
                DEBUG_INFO("Player name changed - invalidating race number cache");
            }

            notifyHudManager(DataChangeType::RaceEntries);
        }
    }
    else {
        // New entry - pass pre-computed abbreviation and color
        m_raceEntries.emplace(raceNum, RaceEntryData(raceNum, name, bikeName, bikeAbbr, brandName, bikeBrandColor));

        // Player race number is cached directly in RaceAddEntry handler
        // No need to invalidate here - RaceAddEntry will call setPlayerRaceNum() if this is the player

        notifyHudManager(DataChangeType::RaceEntries);
    }
}

void PluginData::removeRaceEntry(int raceNum) {

    auto it = m_raceEntries.find(raceNum);

    if (it != m_raceEntries.end()) {
        DEBUG_INFO_F("Race entry %d removed: %s", raceNum, it->second.name);

        // Invalidate cache if we're removing the player's entry
        if (raceNum == m_playerRaceNum) {
            m_bPlayerRaceNumValid = false;
        }

        // Clean up ALL per-rider data structures. Beyond the memory (trivial),
        // this matters for raceNum reuse: a new rider joining mid-event with a
        // departed rider's number must not inherit stale standings, gap cache,
        // or position-gain reference points.
        m_riderCurrentLap.erase(raceNum);
        m_riderIdealLap.erase(raceNum);
        m_riderLapLog.erase(raceNum);
        m_riderBestLap.erase(raceNum);
        m_trackPositions.erase(raceNum);
        m_standings.erase(raceNum);
        m_lastValidOfficialGap.erase(raceNum);
        m_raceStartPositions.erase(raceNum);
        m_lastSfPositions.erase(raceNum);
        m_lastSplitPositions.erase(raceNum);
        m_cachedHazardTypes.erase(raceNum);
        // Also the live-gap "recently seen in a RaceTrackPosition batch" set: while
        // the player sits in menus no batches arrive to refresh it, so a departed
        // rider would otherwise stay "active" indefinitely and a rejoiner reusing
        // the number would inherit hasActiveTrackPos() == true (liveGapValid).
        m_activeTrackPosRiders.erase(raceNum);
        m_blueFlagsDirty = true;
        // The derived hazard-raceNum vector must be rebuilt too: erasing the type
        // cache above keeps IT consistent, but m_cachedHazardRaceNums still lists
        // the departed rider — and with no callbacks arriving in menus, the next
        // RaceTrackPosition batch that would refresh it may never come.
        m_hazardsDirty = true;
        m_hazardTypesDirty = true;
        // Same stale-in-menus class for the derived position caches: their source
        // (m_classificationOrder) legitimately lists the rider until the next
        // classification, but that classification may never arrive in menus —
        // dirty them so the next lookup rebuilds instead of serving the departed
        // rider's position indefinitely.
        m_bPositionCacheDirty = true;

        // Reset lap timer if we're removing the display rider
        if (raceNum == m_displayLapTimerRaceNum) {
            m_displayLapTimer.reset();
            m_displayLapTimerRaceNum = -1;
        }

        m_raceEntries.erase(it);
        notifyHudManager(DataChangeType::RaceEntries);
    }
    else {
        DEBUG_WARN_F("Attempted to remove non-existent race entry %d", raceNum);
    }
}

const RaceEntryData* PluginData::getRaceEntry(int raceNum) const {
    auto it = m_raceEntries.find(raceNum);
    return (it != m_raceEntries.end()) ? &it->second : nullptr;
}

int PluginData::getPlayerRaceNum() const {
    if (m_bPlayerRaceNumValid) {
        return m_playerRaceNum;
    }

    updatePlayerRaceNum();
    return m_playerRaceNum;
}

void PluginData::setPlayerRaceNum(int raceNum) {
    if (m_playerRaceNum != raceNum || !m_bPlayerRaceNumValid) {
        m_playerRaceNum = raceNum;
        m_bPlayerRaceNumValid = true;
    }
}

// ============================================================================
// Spectate Mode Tracking
// ============================================================================

void PluginData::setDrawState(int state) {
    if (m_drawState != state) {
        int previousState = m_drawState;
        m_drawState = state;
        const char* stateStr = (state == 0) ? "ON_TRACK" : (state == 1) ? "SPECTATE" : "REPLAY";
        DEBUG_INFO_F("Draw state changed: %s (%d)", stateStr, state);

        // Clear telemetry data when switching between view states
        // This prevents stale data from showing (e.g., spectated rider's data when back on track)
        bool wasSpectating = (previousState == PluginConstants::ViewState::SPECTATE ||
                              previousState == PluginConstants::ViewState::REPLAY);
        bool isSpectating = (state == PluginConstants::ViewState::SPECTATE ||
                             state == PluginConstants::ViewState::REPLAY);
        if (wasSpectating != isSpectating) {
            clearTelemetryData();
        }

        // Notify HudManager so profile auto-switch can detect spectate/replay mode changes
        notifyHudManager(DataChangeType::SpectateTarget);
    }
}

void PluginData::setSpectatedRaceNum(int raceNum) {
    if (m_spectatedRaceNum != raceNum) {
        int previousRaceNum = m_spectatedRaceNum;
        m_spectatedRaceNum = raceNum;
        DEBUG_INFO_F("Spectated race number: %d", raceNum);

        // Clear telemetry when spectate target becomes invalid or changes
        // This prevents stale data from showing when switching riders or stopping spectate
        if (raceNum <= 0 || (previousRaceNum > 0 && raceNum != previousRaceNum)) {
            clearTelemetryData();
        }

        // Notify HudManager to update HUDs that display rider-specific data
        notifyHudManager(DataChangeType::SpectateTarget);
    }
}

int PluginData::getDisplayRaceNum() const {
    // When spectating or in replay, show the spectated (camera) rider's data.
    if ((m_drawState == PluginConstants::ViewState::SPECTATE ||
         m_drawState == PluginConstants::ViewState::REPLAY) && m_spectatedRaceNum > 0) {
        return m_spectatedRaceNum;
    }
    // Otherwise fall back to the local player. NOTE: when spectating with no camera
    // target and no local player (joined mid-session purely to spectate), this is -1
    // on purpose — display-rider widgets (Lap, Position, gaps) then render "-", which
    // correctly reads as "no rider selected" rather than inventing a subject (e.g. the
    // leader). Don't add a leader fallback here.
    return getPlayerRaceNum();
}

bool PluginData::isDisplayRiderFinished() const {
    int displayRaceNum = getDisplayRaceNum();
    const StandingsData* standing = getStanding(displayRaceNum);
    return standing && m_sessionData.isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish);
}

// ============================================================================
// Current Lap and Ideal Lap Management
// ============================================================================

void PluginData::updateCurrentLapSplit(int raceNum, int lapNum, int splitIndex, int accumulatedTime) {
    // Validate accumulated time
    if (accumulatedTime <= 0) {
        DEBUG_WARN_F("Invalid split time: raceNum=%d, lapNum=%d, splitIndex=%d, time=%d",
                     raceNum, lapNum, splitIndex, accumulatedTime);
        return;
    }

    // Get or create current lap data for this rider
    CurrentLapData& currentLap = m_riderCurrentLap[raceNum];

    // Reset if this is a new lap
    if (currentLap.lapNum != lapNum) {
        currentLap.clear();
        currentLap.lapNum = lapNum;
    }

    // Update the appropriate split
    bool updated = false;
    if (splitIndex == 0 && currentLap.split1 != accumulatedTime) {
        currentLap.split1 = accumulatedTime;
        updated = true;
    } else if (splitIndex == 1 && currentLap.split2 != accumulatedTime) {
        // Validate that S2 > S1 if S1 is set
        if (currentLap.split1 > 0 && accumulatedTime <= currentLap.split1) {
            DEBUG_WARN_F("Invalid split progression: S2=%d <= S1=%d",
                         accumulatedTime, currentLap.split1);
            return;
        }
        currentLap.split2 = accumulatedTime;
        updated = true;
    } else if (splitIndex == 2 && currentLap.split3 != accumulatedTime) {
        // Validate that S3 > S2 if S2 is set
        if (currentLap.split2 > 0 && accumulatedTime <= currentLap.split2) {
            DEBUG_WARN_F("Invalid split progression: S3=%d <= S2=%d",
                         accumulatedTime, currentLap.split2);
            return;
        }
        currentLap.split3 = accumulatedTime;
        updated = true;
    }

    if (updated) {
        notifyHudManager(DataChangeType::IdealLap);
    }
}

void PluginData::setCurrentLapNumber(int raceNum, int lapNum) {
    // Initialize the lap number for the next lap (called after lap completion)
    // Clears splits but keeps the lap number so splits know what lap we're on
    CurrentLapData& currentLap = m_riderCurrentLap[raceNum];
    currentLap.clear();
    currentLap.lapNum = lapNum;
}

void PluginData::updateIdealLap(int raceNum, int completedLapNum, int lapTime, int sector1, int sector2, int sector3, int sector4, bool isValid) {
    // Get or create ideal lap data for this rider
    IdealLapData& idealLap = m_riderIdealLap[raceNum];

    bool updated = false;
    bool isFirstValidLap = isValid && (idealLap.bestSector1 < 0);

    // Always update lap completion info (for TimingHud detection)
    // This triggers even for invalid laps with no timing data
    if (idealLap.lastCompletedLapNum != completedLapNum) {
        idealLap.lastCompletedLapNum = completedLapNum;
        updated = true;
    }
    if (idealLap.lastLapTime != lapTime) {
        idealLap.lastLapTime = lapTime;
        updated = true;
    }
    if (idealLap.lastLapSector1 != sector1) {
        idealLap.lastLapSector1 = sector1;
        updated = true;
    }
    if (idealLap.lastLapSector2 != sector2) {
        idealLap.lastLapSector2 = sector2;
        updated = true;
    }
    if (idealLap.lastLapSector3 != sector3) {
        idealLap.lastLapSector3 = sector3;
        updated = true;
    }
    if (idealLap.lastLapSector4 != sector4) {
        idealLap.lastLapSector4 = sector4;
        updated = true;
    }

    // Only update best sectors for valid laps (invalid laps don't count as PBs)
    if (isValid) {
        if (sector1 > 0 && (isFirstValidLap || sector1 < idealLap.bestSector1)) {
            // Save previous best before updating
            if (idealLap.bestSector1 > 0) {
                idealLap.previousIdealSector1 = idealLap.bestSector1;
            }
            idealLap.bestSector1 = sector1;
            updated = true;
        }
        if (sector2 > 0 && (isFirstValidLap || sector2 < idealLap.bestSector2)) {
            // Save previous best before updating
            if (idealLap.bestSector2 > 0) {
                idealLap.previousIdealSector2 = idealLap.bestSector2;
            }
            idealLap.bestSector2 = sector2;
            updated = true;
        }
        if (sector3 > 0 && (isFirstValidLap || sector3 < idealLap.bestSector3)) {
            // Save previous best before updating
            if (idealLap.bestSector3 > 0) {
                idealLap.previousIdealSector3 = idealLap.bestSector3;
            }
            idealLap.bestSector3 = sector3;
            updated = true;
        }
        // Sector4 only valid for 4-sector games (GP Bikes) - sector4 > 0 means it's present
        if (sector4 > 0 && (isFirstValidLap || idealLap.bestSector4 < 0 || sector4 < idealLap.bestSector4)) {
            // Save previous best before updating
            if (idealLap.bestSector4 > 0) {
                idealLap.previousIdealSector4 = idealLap.bestSector4;
            }
            idealLap.bestSector4 = sector4;
            updated = true;
        }
    }

    if (updated) {
        notifyHudManager(DataChangeType::IdealLap);
    }
}

void PluginData::clearIdealLap(int raceNum) {
    auto itCurrent = m_riderCurrentLap.find(raceNum);
    if (itCurrent != m_riderCurrentLap.end()) {
        itCurrent->second.clear();
    }

    auto itBest = m_riderIdealLap.find(raceNum);
    if (itBest != m_riderIdealLap.end()) {
        itBest->second.clear();
    }

    DEBUG_INFO_F("Ideal lap data cleared for race #%d", raceNum);
    notifyHudManager(DataChangeType::IdealLap);
}

void PluginData::clearAllIdealLap() {
    m_riderCurrentLap.clear();
    m_riderIdealLap.clear();
    DEBUG_INFO("All riders' ideal lap data cleared");
    notifyHudManager(DataChangeType::IdealLap);
}

const CurrentLapData* PluginData::getCurrentLapData(int raceNum) const {
    auto it = m_riderCurrentLap.find(raceNum);
    if (it != m_riderCurrentLap.end() && it->second.lapNum >= 0) {
        return &it->second;
    }
    return nullptr;
}

const IdealLapData* PluginData::getIdealLapData(int raceNum) const {
    auto it = m_riderIdealLap.find(raceNum);
    if (it != m_riderIdealLap.end()) {
        const IdealLapData& data = it->second;
        // Return data if any meaningful info exists (PB sectors OR lap completion)
        if (data.bestSector1 > 0 || data.bestSector2 > 0 || data.bestSector3 > 0 ||
            data.lastCompletedLapNum >= 0) {
            return &data;
        }
    }
    return nullptr;
}

// ============================================================================
// Lap Log Management
// ============================================================================

void PluginData::updateLapLog(int raceNum, const LapLogEntry& entry) {
    // Get or create lap log for this rider
    std::deque<LapLogEntry>& lapLog = m_riderLapLog[raceNum];

    bool wasUpdated = false;

    // Check if this is an update to the most recent lap (same lap number)
    // This handles both incomplete lap updates and completing an incomplete lap
    if (!lapLog.empty() && entry.lapNum == lapLog[0].lapNum) {
        // CRITICAL: Never modify a lap that is already marked as complete
        // Once a lap is complete, its data is final and should never change
        if (lapLog[0].isComplete) {
            // Lap already complete - this should not happen in normal operation
            DEBUG_WARN_F("Attempted to update already-complete lap #%d for race #%d - ignoring",
                         entry.lapNum, raceNum);
            return;
        }

        // Update the current lap in place (only if not already complete)
        bool changed = (lapLog[0].sector1 != entry.sector1 ||
                        lapLog[0].sector2 != entry.sector2 ||
                        lapLog[0].sector3 != entry.sector3 ||
                        lapLog[0].sector4 != entry.sector4 ||
                        lapLog[0].lapTime != entry.lapTime ||
                        lapLog[0].isComplete != entry.isComplete);

        if (changed) {
            lapLog[0] = entry;
            wasUpdated = true;
            notifyHudManager(DataChangeType::LapLog);
        }
    } else {
        // New lap - add to front of log (O(1) with deque)
        lapLog.push_front(entry);

        // Keep a bounded history per rider (best lap is stored separately via
        // setBestLapEntry). Storage cap is larger than the Lap Log HUD's display
        // cap so the Session Charts HUD can plot a full race.
        if (lapLog.size() > static_cast<size_t>(PluginConstants::HudLimits::MAX_LAP_LOG_STORAGE)) {
            lapLog.resize(static_cast<size_t>(PluginConstants::HudLimits::MAX_LAP_LOG_STORAGE));
        }

        wasUpdated = true;
        notifyHudManager(DataChangeType::LapLog);
    }
}

void PluginData::clearLapLog(int raceNum) {
    auto itLog = m_riderLapLog.find(raceNum);
    if (itLog != m_riderLapLog.end()) {
        itLog->second.clear();
    }

    auto itBest = m_riderBestLap.find(raceNum);
    if (itBest != m_riderBestLap.end()) {
        itBest->second = LapLogEntry();  // Reset to default-constructed state
    }

    // Also clear previous PB data since we're clearing the current PB
    auto itSession = m_riderIdealLap.find(raceNum);
    if (itSession != m_riderIdealLap.end()) {
        itSession->second.previousBestLapTime = -1;
        itSession->second.previousBestSector1 = -1;
        itSession->second.previousBestSector2 = -1;
        itSession->second.previousBestSector3 = -1;
    }

    DEBUG_INFO_F("Lap log cleared for race #%d", raceNum);
    notifyHudManager(DataChangeType::LapLog);
}

void PluginData::clearAllLapLog() {
    m_riderLapLog.clear();
    m_riderBestLap.clear();
    clearOverallBestLap();

    // Also clear previous PB data for all riders since we're clearing all current PBs
    for (auto& pair : m_riderIdealLap) {
        pair.second.previousBestLapTime = -1;
        pair.second.previousBestSector1 = -1;
        pair.second.previousBestSector2 = -1;
        pair.second.previousBestSector3 = -1;
    }

    // Clear live gap so gap row doesn't show stale data
    setLiveGap(0, false);

    DEBUG_INFO("All riders' lap log cleared");
    notifyHudManager(DataChangeType::LapLog);
}

const std::deque<LapLogEntry>* PluginData::getLapLog(int raceNum) const {
    auto it = m_riderLapLog.find(raceNum);
    if (it != m_riderLapLog.end()) {
        return &it->second;
    }
    return nullptr;
}

void PluginData::setBestLapEntry(int raceNum, const LapLogEntry& entry) {
    // Before updating to new PB, save the current PB as "previous PB" for comparison
    auto it = m_riderBestLap.find(raceNum);
    if (it != m_riderBestLap.end() && it->second.lapNum >= 0) {
        // There's an existing PB - save it as previous best
        const LapLogEntry& currentBest = it->second;
        IdealLapData& idealLap = m_riderIdealLap[raceNum];
        idealLap.previousBestLapTime = currentBest.lapTime;
        idealLap.previousBestSector1 = currentBest.sector1;
        idealLap.previousBestSector2 = currentBest.sector2;
        idealLap.previousBestSector3 = currentBest.sector3;
        idealLap.previousBestSector4 = currentBest.sector4;
    }

    // Update to new PB
    m_riderBestLap[raceNum] = entry;
}

const LapLogEntry* PluginData::getBestLapEntry(int raceNum) const {
    auto it = m_riderBestLap.find(raceNum);
    if (it != m_riderBestLap.end() && it->second.lapNum >= 0) {
        return &it->second;
    }
    return nullptr;
}

void PluginData::setOverallBestLap(const LapLogEntry& entry) {
    // Save previous overall best before updating (for showing improvement)
    if (m_overallBestLap.lapNum >= 0 && m_overallBestLap.lapTime > 0) {
        m_previousOverallBestLap = m_overallBestLap;
    }
    m_overallBestLap = entry;
    DEBUG_INFO_F("Overall best lap updated: lapTime=%d, S1=%d, S2=%d",
                 entry.lapTime, entry.sector1, entry.sector2);
}

const LapLogEntry* PluginData::getOverallBestLap() const {
    if (m_overallBestLap.lapNum >= 0 && m_overallBestLap.lapTime > 0) {
        return &m_overallBestLap;
    }
    return nullptr;
}

const LapLogEntry* PluginData::getPreviousOverallBestLap() const {
    if (m_previousOverallBestLap.lapNum >= 0 && m_previousOverallBestLap.lapTime > 0) {
        return &m_previousOverallBestLap;
    }
    return nullptr;
}

void PluginData::clearTelemetryData() {
    // Preserve static bike configuration values set in EventInit
    // These don't change during a session and shouldn't be wiped on view state transitions
    float savedFrontSuspMaxTravel = m_bikeTelemetry.frontSuspMaxTravel;
    float savedRearSuspMaxTravel = m_bikeTelemetry.rearSuspMaxTravel;
    float savedMaxFuel = m_bikeTelemetry.maxFuel;
    int savedNumberOfGears = m_bikeTelemetry.numberOfGears;

    m_bikeTelemetry = BikeTelemetryData();

    // Restore preserved values
    m_bikeTelemetry.frontSuspMaxTravel = savedFrontSuspMaxTravel;
    m_bikeTelemetry.rearSuspMaxTravel = savedRearSuspMaxTravel;
    m_bikeTelemetry.maxFuel = savedMaxFuel;
    m_bikeTelemetry.numberOfGears = savedNumberOfGears;

    m_inputTelemetry = InputTelemetryData();
    m_historyBuffers.clear();
    notifyHudManager(DataChangeType::InputTelemetry);
    DEBUG_INFO("Telemetry data cleared (bike config preserved)");
}

void PluginData::clear() {
    m_sessionData.clear();
    m_raceEntries.clear();
    m_standings.clear();
    m_classificationOrder.clear();
    m_lastLeaderRaceNum = -1;
    m_positionCache.clear();
    m_bPositionCacheDirty = true;
    m_filteredClassificationOrder.clear();
    m_filteredPositionCache.clear();
    m_bFilteredOrderDirty = true;
    m_trackPositions.clear();
    m_activeTrackPosRiders.clear();
    m_blueFlagsDirty = true;
    m_riderCurrentLap.clear();
    m_riderIdealLap.clear();
    m_riderLapLog.clear();
    m_riderBestLap.clear();
    clearOverallBestLap();

    // Position-reference maps (race-start snapshot, "Since S/F" / "Since split"
    // rolling references). Erased per-rider in removeRaceEntry() and reset at
    // every new session in handleRaceSession(); also reset here so a full event
    // exit can't leave a reused race number inheriting a departed rider's state.
    m_raceStartPositions.clear();
    m_lastSfPositions.clear();
    m_lastSplitPositions.clear();

    // Reset the lap timer AND the grid-start gate-drop watch it drives (isInGridStartGrace keys
    // on m_awaitingGateDrop, so a stale watch surviving clear() would suppress wrong-way/hazards).
    // resetAllLapTimers() clears all three in one place, matching the reset-in-clear() discipline.
    resetAllLapTimers();

    // Clear leader timing points
    m_leaderTimingPoints.clear();
    m_lastValidOfficialGap.clear();

    // Clear telemetry data
    m_bikeTelemetry = BikeTelemetryData();
    m_inputTelemetry = InputTelemetryData();
    m_historyBuffers.clear();

    m_hazardsDirty = true;
    m_hazardTypesDirty = true;
    m_cachedHazardRaceNums.clear();
    m_blueFlagsDirty = true;
    m_cachedBlueFlaggedSet.clear();
    m_cachedLapperToLapped.clear();
    m_cachedPlayerBlueFlagged = false;
    m_currentSessionTime = 0;
    m_playerRaceNum = -1;
    m_bPlayerRaceNumValid = false;
    m_bPlayerNotFoundWarned = false;
    m_bWaitingForPlayerEntry = false;
    m_iPendingPlayerRaceNum = -1;
    m_bPlayerIsRunning = false;
    m_drawState = 0;  // Reset to ON_TRACK
    m_spectatedRaceNum = -1;  // Reset spectated rider

    // Clear PB notification flags
    m_newSessionPB = false;
    m_newFastestLap = false;
    m_newAllTimePB = false;
    m_newDefaultSetup = false;

    // Reset the player's PB live gap — otherwise a stale valid flag survives an
    // event exit and the gap bar can briefly show the previous event's delta.
    m_liveGapMs = 0;
    m_liveGapValid = false;

    // Reset the segment timer (points are track-specific; drop on session change).
    // Keep m_splitPositions: they're track-specific and only re-delivered on track
    // load, so dropping them here would silently disable snap-to-split for the rest
    // of a same-track session (the MapHud likewise keeps its split markers).
    resetSegmentTimer();

    // Reset benchmark metrics (preserves active flag, clears all registrations)
    bool bmWasActive = m_benchmarkMetrics.active;
    m_benchmarkMetrics = BenchmarkMetrics{};
    m_benchmarkMetrics.active = bmWasActive;

    // Clear event log
    m_eventLog.clear();

    // Notify listeners so HTTP server pushes empty standings/events
    notifyHudManager(DataChangeType::SessionData);

    DEBUG_INFO("Plugin data cleared");
}

// ========================================================================
// Event Log
// ========================================================================
void PluginData::addEventLogEntry(EventLogType type, const char* message, const char* detail, int iconColorSlot) {
    EventLogEntry entry;
    entry.type = type;
    entry.sessionTimeMs = m_currentSessionTime;
    entry.steadyTime = std::chrono::steady_clock::now();
    entry.systemTime = std::chrono::system_clock::now();
    entry.iconColorSlot = iconColorSlot;
    strncpy_s(entry.message, sizeof(entry.message), message, _TRUNCATE);
    if (detail) {
        strncpy_s(entry.detail, sizeof(entry.detail), detail, _TRUNCATE);
    }

    m_eventLog.push_back(entry);
    if (static_cast<int>(m_eventLog.size()) > PluginConstants::HudLimits::MAX_EVENT_LOG_CAPACITY) {
        m_eventLog.pop_front();
    }

    notifyHudManager(DataChangeType::EventLog);
}

// Direct call to HudManager and DiscordManager, no callback/observer overhead
void PluginData::notifyHudManager(DataChangeType changeType) {
    if (!HudManager::getInstance().isInitialized()) return;
    HudManager::getInstance().onDataChanged(changeType);
#if GAME_HAS_DISCORD
    DiscordManager::getInstance().onDataChanged(changeType);
#endif
#if GAME_HAS_STEAM_FRIENDS
    SteamFriendsManager::getInstance().onDataChanged(changeType);
#endif
#if GAME_HAS_HTTP_SERVER
    HttpServer::getInstance().onDataChanged(changeType);
#endif
}

const XInputReader& PluginData::getXInputReader() const {
    return XInputReader::getInstance();
}

void PluginData::notifyTrackedRidersChanged() {
    notifyHudManager(DataChangeType::TrackedRiders);
}

void PluginData::updatePlayerRaceNum() const {

    const char* playerName = m_sessionData.riderName;

    if (playerName[0] == '\0') {
        m_playerRaceNum = -1;
        m_bPlayerRaceNumValid = false;
        return;
    }

    // Linear search through race entries
    // Handles exact match and server-forced rating prefixes (e.g., "B1 | Thomas" matches "Thomas")
    for (const auto& entry : m_raceEntries) {
        if (PluginUtils::matchRiderName(entry.second.name, playerName,
                                         PluginConstants::GameLimits::RACE_ENTRY_NAME_MAX)) {
            m_playerRaceNum = entry.second.raceNum;
            m_bPlayerRaceNumValid = true;
            m_bPlayerNotFoundWarned = false;  // Reset so we can warn again in future sessions
            m_bWaitingForPlayerEntry = false;  // Prevent primary path from overwriting with wrong player
            DEBUG_INFO_F("Player race number cached: %d", m_playerRaceNum);
            return;
        }
    }

    m_playerRaceNum = -1;
    m_bPlayerRaceNumValid = false;

    // Warn once if player not found - helps debug server-forced name prefix issues
    if (!m_raceEntries.empty() && !m_bPlayerNotFoundWarned) {
        DEBUG_WARN_F("Local player '%s' not found in %zu race entries", playerName, m_raceEntries.size());
        m_bPlayerNotFoundWarned = true;
    }
}

bool PluginData::isRaceSession() const {
    // Convert game-specific raw session integer to canonical via the active
    // game's adapter, then check for race-style sessions in one place.
    Unified::Session canonical = Game::Adapter::toCanonicalSession(
        m_sessionData.session, m_sessionData.eventType);
    return canonical == Unified::Session::Race ||
           canonical == Unified::Session::Race1 ||
           canonical == Unified::Session::Race2 ||
           canonical == Unified::Session::SR_Round ||
           canonical == Unified::Session::SR_QuarterFinals ||
           canonical == Unified::Session::SR_SemiFinals ||
           canonical == Unified::Session::SR_Final ||
           canonical == Unified::Session::KRP_QualifyHeat ||
           canonical == Unified::Session::KRP_SecondChanceHeat ||
           canonical == Unified::Session::KRP_PreFinal ||
           canonical == Unified::Session::KRP_Final;
}

bool PluginData::isQualifySession() const {
    Unified::Session canonical = Game::Adapter::toCanonicalSession(
        m_sessionData.session, m_sessionData.eventType);
    return canonical == Unified::Session::PreQualify ||
           canonical == Unified::Session::QualifyPractice ||
           canonical == Unified::Session::Qualify;
}
