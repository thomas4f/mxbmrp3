// ============================================================================
// core/plugin_data.cpp
// Central data store for all game state received from MX Bikes API
// ============================================================================
#include "plugin_data.h"
#include "plugin_utils.h"
#include "xinput_reader.h"
#include "../vendor/piboso/mxb_api.h"
#include "hud_manager.h"  // Direct include for notification
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include <algorithm>
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

void PluginData::setMaxFuel(float maxFuel) {
    m_bikeTelemetry.maxFuel = maxFuel;
}

void PluginData::setNumberOfGears(int numberOfGears) {
    m_bikeTelemetry.numberOfGears = numberOfGears;
}

void PluginData::setSession(int session) {
    if (setValue(m_sessionData.session, session)) {
        notifyHudManager(DataChangeType::SessionData);
    }
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

void PluginData::setSetupFileName(const char* setupFileName) {
    if (setStringValue(m_sessionData.setupFileName, sizeof(m_sessionData.setupFileName), setupFileName)) {
        notifyHudManager(DataChangeType::SessionData);
    }
}

void PluginData::addRaceEntry(int raceNum, const char* name, const char* bikeName) {
    // Validate input strings (defensive check for public API)
    if (!name || !bikeName) return;

    // Compute bike abbreviation and color once when entry is added
    const char* bikeAbbr;
    unsigned long bikeBrandColor;
    {
        bikeAbbr = PluginUtils::getBikeAbbreviationPtr(bikeName);
    }
    {
        bikeBrandColor = PluginUtils::getBikeBrandColor(bikeName);
    }

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
        m_raceEntries.emplace(raceNum, RaceEntryData(raceNum, name, bikeName, bikeAbbr, bikeBrandColor));

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

        // Clean up all per-rider data structures to prevent memory leaks
        m_riderCurrentLap.erase(raceNum);
        m_riderIdealLap.erase(raceNum);
        m_riderLapLog.erase(raceNum);
        m_riderBestLap.erase(raceNum);
        m_trackPositions.erase(raceNum);

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
    // When spectating or in replay, show the spectated rider's data
    // Otherwise show player's data
    if ((m_drawState == PluginConstants::ViewState::SPECTATE ||
         m_drawState == PluginConstants::ViewState::REPLAY) && m_spectatedRaceNum > 0) {
        return m_spectatedRaceNum;
    }
    return getPlayerRaceNum();
}

bool PluginData::isDisplayRiderFinished() const {
    int displayRaceNum = getDisplayRaceNum();
    const StandingsData* standing = getStanding(displayRaceNum);
    return standing && m_sessionData.isRiderFinished(standing->numLaps);
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

void PluginData::updateIdealLap(int raceNum, int completedLapNum, int lapTime, int sector1, int sector2, int sector3, bool isValid) {
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

        // Keep only recent laps (best lap is stored separately via setBestLapEntry)
        // Limit enforced by shared constant to match display capacity
        if (lapLog.size() > static_cast<size_t>(PluginConstants::HudLimits::MAX_LAP_LOG_CAPACITY)) {
            lapLog.resize(static_cast<size_t>(PluginConstants::HudLimits::MAX_LAP_LOG_CAPACITY));
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
        // Crossed S/F line - set anchor if we don't have one or lap changed
        if (!timer.anchorValid || lapNum != timer.lastLapNum) {
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
    // Keep track monitor initialized - we don't want to lose position tracking

    DEBUG_INFO_F("LapTimer: Reset for new lap, lap=%d", lapNum);
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
    DEBUG_INFO("LapTimer: Timer reset");
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

void PluginData::updateStandings(int raceNum, int state, int bestLap, int bestLapNum,
    int numLaps, int gap, int gapLaps, int penalty, int pit, bool notify) {
    auto it = m_standings.find(raceNum);

    if (it != m_standings.end()) {
        // Entry exists - check if data changed
        // PERFORMANCE: Order comparisons by likelihood of change (gap/numLaps change most frequently)
        if (it->second.gap != gap || it->second.numLaps != numLaps ||
            it->second.state != state || it->second.bestLap != bestLap ||
            it->second.gapLaps != gapLaps || it->second.penalty != penalty ||
            it->second.bestLapNum != bestLapNum || it->second.pit != pit) {

            it->second.state = state;
            it->second.bestLap = bestLap;
            it->second.bestLapNum = bestLapNum;
            it->second.numLaps = numLaps;
            it->second.gap = gap;
            it->second.gapLaps = gapLaps;
            it->second.penalty = penalty;
            it->second.pit = pit;
        }
        else {
            // No change, skip notification
            return;
        }
    }
    else {
        // New entry
        m_standings.emplace(raceNum, StandingsData(raceNum, state, bestLap, bestLapNum,
            numLaps, gap, gapLaps, penalty, pit));
    }

    // Notify HUD manager if requested
    if (notify) {
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::batchUpdateStandings(SPluginsRaceClassificationEntry_t* entries, int numEntries) {
    // Batch update all standings AND build classification order in single pass
    // Eliminates duplicate iteration of the same array

    bool anyChanged = false;

    // Reserve space for classification order (avoid reallocations)
    m_classificationOrder.clear();
    m_classificationOrder.reserve(numEntries);

    for (int i = 0; i < numEntries; ++i) {
        const SPluginsRaceClassificationEntry_t& entry = entries[i];

        // Build classification order (game already sorted by position)
        m_classificationOrder.push_back(entry.m_iRaceNum);

        // Update standings data
        auto it = m_standings.find(entry.m_iRaceNum);

        if (it != m_standings.end()) {
            // Entry exists - check if data changed
            StandingsData& standing = it->second;

            // Handle official gap with caching to prevent flicker
            // The API temporarily clears gaps (sends 0) when leader crosses line
            // We cache the last valid gap and use it when API sends 0
            // Exception: leader (i==0) should always have gap=0, clear their cache
            int effectiveGap = entry.m_iGap;
            if (i == 0) {
                // Leader's gap is always 0 - clear any stale cached gap
                m_lastValidOfficialGap.erase(entry.m_iRaceNum);
            } else if (entry.m_iGap > 0) {
                // Valid gap from API - cache it
                m_lastValidOfficialGap[entry.m_iRaceNum] = entry.m_iGap;
            } else if (entry.m_iGap == 0 && entry.m_iGapLaps == 0) {
                // API sent zero gap - check if we have cached value
                auto cachedIt = m_lastValidOfficialGap.find(entry.m_iRaceNum);
                if (cachedIt != m_lastValidOfficialGap.end()) {
                    effectiveGap = cachedIt->second;
                }
            }

            if (standing.state != entry.m_iState ||
                standing.bestLap != entry.m_iBestLap ||
                standing.bestLapNum != entry.m_iBestLapNum ||
                standing.numLaps != entry.m_iNumLaps ||
                standing.gap != effectiveGap ||
                standing.gapLaps != entry.m_iGapLaps ||
                standing.penalty != entry.m_iPenalty ||
                standing.pit != entry.m_iPit) {

                standing.state = entry.m_iState;
                standing.bestLap = entry.m_iBestLap;
                standing.bestLapNum = entry.m_iBestLapNum;
                standing.numLaps = entry.m_iNumLaps;
                standing.gap = effectiveGap;
                standing.gapLaps = entry.m_iGapLaps;
                standing.penalty = entry.m_iPenalty;
                standing.pit = entry.m_iPit;

                anyChanged = true;
            }
        }
        else {
            // New entry
            int effectiveGap = entry.m_iGap;
            // Only cache gap for non-leaders (leader gap should always be 0)
            if (i > 0 && effectiveGap > 0) {
                m_lastValidOfficialGap[entry.m_iRaceNum] = effectiveGap;
            }
            m_standings.emplace(entry.m_iRaceNum,
                StandingsData(entry.m_iRaceNum, entry.m_iState, entry.m_iBestLap,
                    entry.m_iBestLapNum, entry.m_iNumLaps, effectiveGap,
                    entry.m_iGapLaps, entry.m_iPenalty, entry.m_iPit));
            anyChanged = true;
        }
    }

    // Capture finish time for each rider when they finish
    // Calculate elapsed time based on race type (same formula for all riders)
    auto calculateElapsedTime = [&]() -> int {
        if (m_sessionData.sessionLength > 0) {
            // Timed race: elapsed = sessionLength - sessionTime
            return m_sessionData.sessionLength - m_currentSessionTime;
        } else {
            // Lap-based race: sessionTime is elapsed time
            return m_currentSessionTime > 0 ? m_currentSessionTime : 0;
        }
    };

    // Check each rider for finish
    for (auto& [raceNum, standing] : m_standings) {
        // Only capture once (when finishTime transitions from -1)
        if (standing.finishTime < 0 && m_sessionData.isRiderFinished(standing.numLaps)) {
            standing.finishTime = calculateElapsedTime();
            DEBUG_INFO_F("[RIDER FINISHED] Rider #%d finished race in %d ms", raceNum, standing.finishTime);
            anyChanged = true;

            // Also update leader finish time if this is the leader
            if (!m_classificationOrder.empty() && raceNum == m_classificationOrder[0] && m_sessionData.leaderFinishTime < 0) {
                m_sessionData.leaderFinishTime = standing.finishTime;
                DEBUG_INFO_F("[LEADER FINISHED] Leader #%d finished race in %d ms", raceNum, standing.finishTime);
            }
        }
    }

    // Notify once if anything changed
    if (anyChanged) {
        m_bPositionCacheDirty = true;  // Mark position cache dirty when standings change
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::clearStandings() {
    if (!m_standings.empty()) {
        m_standings.clear();
        DEBUG_INFO("Standings data cleared");
        notifyHudManager(DataChangeType::Standings);
    }
}

const StandingsData* PluginData::getStanding(int raceNum) const {
    auto it = m_standings.find(raceNum);
    return (it != m_standings.end()) ? &it->second : nullptr;
}

void PluginData::setClassificationOrder(const std::vector<int>& order) {
    m_classificationOrder = order;
    m_bPositionCacheDirty = true;  // Mark position cache dirty when classification changes
    // Note: We don't notify HudManager here because this is called as part of
    // the standings update, which already triggers a notification
}

int PluginData::getPositionForRaceNum(int raceNum) const {
    // Rebuild cache if dirty (only happens when classification changes)
    if (m_bPositionCacheDirty) {
        m_positionCache.clear();

        // Build position cache from classification order
        // Position is simply the index in classification order (1-based)
        // This matches how StandingsHud calculates positions
        for (size_t i = 0; i < m_classificationOrder.size(); ++i) {
            m_positionCache[m_classificationOrder[i]] = static_cast<int>(i) + 1;
        }
        m_bPositionCacheDirty = false;
    }

    // Lookup position in cache (O(1) operation)
    auto it = m_positionCache.find(raceNum);
    if (it != m_positionCache.end()) {
        return it->second;
    }
    return -1;  // Not found in standings
}

void PluginData::updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime) {
    auto it = m_trackPositions.find(raceNum);

    if (it != m_trackPositions.end()) {
        TrackPositionData& data = it->second;

        // Add current position to circular buffer
        data.positionHistory[data.historyIndex] = trackPos;
        data.historyIndex = (data.historyIndex + 1) % TrackPositionData::POSITION_HISTORY_SIZE;
        if (data.historyCount < TrackPositionData::POSITION_HISTORY_SIZE) {
            data.historyCount++;
        }

        // Detect wrong-way by comparing oldest and newest positions in buffer
        bool wrongWay = false;
        if (data.historyCount >= TrackPositionData::POSITION_HISTORY_SIZE) {
            // Get oldest position in buffer (the one we're about to overwrite)
            int oldestIndex = data.historyIndex;  // Points to oldest after increment
            float oldestPos = data.positionHistory[oldestIndex];
            float newestPos = trackPos;

            // Calculate position change over the time window
            float posChange = newestPos - oldestPos;

            // Handle wraparound at start/finish line
            if (posChange > TrackPositionData::WRAPAROUND_THRESHOLD) {
                // Wrapped backwards through start line (0.05 -> 0.95) = wrong way
                wrongWay = true;
            } else if (posChange < -TrackPositionData::WRAPAROUND_THRESHOLD) {
                // Wrapped forward through finish line (0.95 -> 0.05) = correct way
                wrongWay = false;
            } else if (posChange <= TrackPositionData::WRONG_WAY_THRESHOLD) {
                // Consistently moving backwards (not wrapping) = wrong way
                wrongWay = true;
            } else {
                // Moving forward or stationary = correct way
                wrongWay = false;
            }
        }

        // Update position data
        data.trackPos = trackPos;
        data.numLaps = numLaps;
        data.sessionTime = sessionTime;
        data.crashed = crashed;
        data.wrongWay = wrongWay;
    } else {
        // Create new position entry
        TrackPositionData posData;
        posData.trackPos = trackPos;
        posData.numLaps = numLaps;
        posData.sessionTime = sessionTime;
        posData.crashed = crashed;
        posData.wrongWay = false;  // Can't detect wrong way until we have history

        // Initialize history with current position
        posData.positionHistory[0] = trackPos;
        posData.historyIndex = 1;
        posData.historyCount = 1;

        m_trackPositions[raceNum] = posData;
    }

    // Store current session time
    m_currentSessionTime = sessionTime;
}

bool PluginData::isPlayerGoingWrongWay() const {
    int displayRaceNum = getDisplayRaceNum();
    auto it = m_trackPositions.find(displayRaceNum);
    if (it != m_trackPositions.end()) {
        return it->second.wrongWay;
    }
    return false;  // No position data = assume not wrong way
}

const TrackPositionData* PluginData::getPlayerTrackPosition() const {
    int displayRaceNum = getDisplayRaceNum();
    auto it = m_trackPositions.find(displayRaceNum);
    if (it != m_trackPositions.end()) {
        return &it->second;
    }
    return nullptr;  // No position data available
}

std::vector<int> PluginData::getBlueFlagRaceNums() const {
    std::vector<int> blueFlagRiders;

    // Only check for blue flags in race sessions
    if (!isRaceSession()) {
        return blueFlagRiders;
    }

    // Get player's race number and data
    int playerRaceNum = getDisplayRaceNum();
    if (playerRaceNum <= 0) {
        return blueFlagRiders;  // No player data
    }

    // Early exit if player is leading - leader can't be blue flagged
    int playerPosition = getPositionForRaceNum(playerRaceNum);
    if (playerPosition == 1) {
        return blueFlagRiders;
    }

    // Get player's position and lap data
    auto playerPosIt = m_trackPositions.find(playerRaceNum);
    auto playerStandingIt = m_standings.find(playerRaceNum);

    if (playerPosIt == m_trackPositions.end() || playerStandingIt == m_standings.end()) {
        return blueFlagRiders;  // Missing player data
    }

    const TrackPositionData& playerPos = playerPosIt->second;
    const StandingsData& playerStanding = playerStandingIt->second;
    int playerLaps = playerStanding.numLaps;
    float playerTrackPos = playerPos.trackPos;

    // Early exit if no one is 1+ lap ahead
    bool anyoneAhead = false;
    for (const auto& [raceNum, standing] : m_standings) {
        if (standing.numLaps >= playerLaps + 1) {
            anyoneAhead = true;
            break;
        }
    }
    if (!anyoneAhead) {
        return blueFlagRiders;
    }

    // Distance threshold for "approaching from behind" (6% of track)
    constexpr float APPROACH_THRESHOLD = 0.06f;

    // Check all other riders in the classification
    for (int otherRaceNum : m_classificationOrder) {
        if (otherRaceNum == playerRaceNum) {
            continue;  // Skip the player
        }

        // Get other rider's position and lap data
        auto otherPosIt = m_trackPositions.find(otherRaceNum);
        auto otherStandingIt = m_standings.find(otherRaceNum);

        if (otherPosIt == m_trackPositions.end() || otherStandingIt == m_standings.end()) {
            continue;  // Missing data for this rider
        }

        const TrackPositionData& otherPos = otherPosIt->second;
        const StandingsData& otherStanding = otherStandingIt->second;
        int otherLaps = otherStanding.numLaps;
        float otherTrackPos = otherPos.trackPos;

        // Check if other rider is 1+ laps ahead
        if (otherLaps < playerLaps + 1) {
            continue;  // Not lapping the player
        }

        // Check if other rider is behind on track (approaching from behind)
        // We need to account for wraparound at the finish line
        float distanceBehind;

        if (otherTrackPos < playerTrackPos) {
            // Other rider is behind on the same lap (direct distance)
            distanceBehind = playerTrackPos - otherTrackPos;
        } else {
            // Other rider is ahead on track but behind in laps
            // This means they crossed finish line and are approaching from behind
            distanceBehind = (1.0f - otherTrackPos) + playerTrackPos;
        }

        // Check if within approach threshold
        if (distanceBehind <= APPROACH_THRESHOLD) {
            blueFlagRiders.push_back(otherRaceNum);
        }
    }

    return blueFlagRiders;
}

void PluginData::updateRealTimeGaps() {
    // Only calculate gaps if we have classification order
    if (m_classificationOrder.empty()) {
        return;
    }

    // Find the leader (first in classification order)
    int leaderRaceNum = m_classificationOrder[0];
    auto leaderPosIt = m_trackPositions.find(leaderRaceNum);
    auto leaderStandingIt = m_standings.find(leaderRaceNum);

    if (leaderPosIt == m_trackPositions.end() || leaderStandingIt == m_standings.end()) {
        return;  // Leader position not available
    }

    const TrackPositionData& leaderPos = leaderPosIt->second;
    int leaderLaps = leaderStandingIt->second.numLaps;

    // Store leader's timing point at current position for current lap
    // trackPos is [0.0, 1.0], map to indices [0, NUM_TIMING_POINTS-1]
    // Clamp handles edge case where trackPos = 1.0 exactly (at finish line before lap increments)
    int positionIndex = static_cast<int>(leaderPos.trackPos * static_cast<float>(NUM_TIMING_POINTS));
    positionIndex = std::max(0, std::min(positionIndex, static_cast<int>(NUM_TIMING_POINTS - 1)));

    // Ensure lap entry exists in map
    if (m_leaderTimingPoints.find(leaderLaps) == m_leaderTimingPoints.end()) {
        m_leaderTimingPoints[leaderLaps] = std::array<LeaderTimingPoint, NUM_TIMING_POINTS>();
    }

    // Store when the current leader passed this position
    // Always update - we want the timestamp of when THE LEADER was here, regardless of who it was
    m_leaderTimingPoints[leaderLaps][positionIndex] = LeaderTimingPoint(
        leaderPos.sessionTime,
        leaderLaps
    );

    // Calculate gaps for all other riders
    bool anyUpdated = false;
    int minLapNeeded = leaderLaps;  // Track oldest lap we need to keep

    for (int raceNum : m_classificationOrder) {
        if (raceNum == leaderRaceNum) {
            // Explicitly set leader's gap to 0 (prevents stale data after lead changes)
            leaderStandingIt->second.realTimeGap = 0;
            continue;
        }

        auto posIt = m_trackPositions.find(raceNum);
        auto standingIt = m_standings.find(raceNum);

        if (posIt == m_trackPositions.end() || standingIt == m_standings.end()) {
            continue;  // Position data not available
        }

        const TrackPositionData& riderPos = posIt->second;
        StandingsData& standing = standingIt->second;
        int riderLap = standing.numLaps;

        // If rider finished, freeze their gap by skipping calculation
        if (m_sessionData.isRiderFinished(riderLap)) {
            continue;  // Gap is frozen at last calculated value
        }

        // Track the minimum lap we need to keep timing data for
        if (riderLap < minLapNeeded) {
            minLapNeeded = riderLap;
        }

        // Find rider's position index
        // trackPos is [0.0, 1.0], map to indices [0, NUM_TIMING_POINTS-1]
        int riderPosIndex = static_cast<int>(riderPos.trackPos * static_cast<float>(NUM_TIMING_POINTS));
        riderPosIndex = std::max(0, std::min(riderPosIndex, static_cast<int>(NUM_TIMING_POINTS - 1)));

        // Look up leader's timing point for the SAME lap the rider is on
        auto lapIt = m_leaderTimingPoints.find(riderLap);
        if (lapIt == m_leaderTimingPoints.end()) {
            continue;  // No timing data for this lap yet
        }

        const LeaderTimingPoint& leaderTiming = lapIt->second[riderPosIndex];

        // Verify timing point is valid
        // Note: sessionTime can be negative during overtime in time+lap races, but lapNum won't be -1
        if (leaderTiming.lapNum >= 0) {
            // Calculate gap based on race format
            // For time+lap races (countdown timer), smaller sessionTime = later in time
            // For lap races (counting-up timer), larger sessionTime = later in time
            int newGap;
            if (m_sessionData.sessionLength > 0) {
                // Time-based race: timer counts DOWN (300 → 0 → -100)
                // Leader has HIGHER sessionTime, rider has LOWER sessionTime
                newGap = leaderTiming.sessionTime - riderPos.sessionTime;
            } else {
                // Lap-based race: timer counts UP (0 → 100 → 200)
                // Leader has LOWER sessionTime, rider has HIGHER sessionTime
                newGap = riderPos.sessionTime - leaderTiming.sessionTime;
            }

            // Sanity check: gap should be positive (negative would indicate calculation error)
            if (newGap > 0) {
                // Only mark dirty if gap changed by threshold amount
                // This reduces HUD rebuild frequency while maintaining useful precision
                int oldGap = standing.realTimeGap;
                int gapChange = (newGap > oldGap) ? (newGap - oldGap) : (oldGap - newGap);

                standing.realTimeGap = newGap;  // Always update the stored value

                if (gapChange >= GAP_UPDATE_THRESHOLD_MS) {
                    anyUpdated = true;
                }
            }
        }
    }

    // Prune old laps that no rider needs anymore (keep at least 1 lap buffer)
    int oldestLapToKeep = minLapNeeded - 1;
    auto it = m_leaderTimingPoints.begin();
    while (it != m_leaderTimingPoints.end()) {
        if (it->first < oldestLapToKeep) {
            it = m_leaderTimingPoints.erase(it);
        } else {
            ++it;
        }
    }

    // Safety check: prevent excessive memory usage
    if (m_leaderTimingPoints.size() > MAX_LAPS_TO_KEEP) {
        // Erase oldest laps beyond MAX_LAPS_TO_KEEP
        while (m_leaderTimingPoints.size() > MAX_LAPS_TO_KEEP) {
            m_leaderTimingPoints.erase(m_leaderTimingPoints.begin());
        }
    }

    // Only notify if something actually changed
    if (anyUpdated) {
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::clearLiveGapTimingPoints() {
    // Clear all timing points when a new session starts
    m_leaderTimingPoints.clear();

    // Reset session time
    m_currentSessionTime = 0;

    // Clear track positions
    m_trackPositions.clear();

    // Clear cached official gaps for new session
    m_lastValidOfficialGap.clear();

    // Clear realTimeGap values from standings (prevent old session data from persisting)
    for (auto& pair : m_standings) {
        pair.second.realTimeGap = 0;
    }

    DEBUG_INFO("Live gap timing points cleared for new session");
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
    m_positionCache.clear();
    m_bPositionCacheDirty = true;
    m_trackPositions.clear();
    m_riderCurrentLap.clear();
    m_riderIdealLap.clear();
    m_riderLapLog.clear();
    m_riderBestLap.clear();
    clearOverallBestLap();

    // Reset single lap timer
    m_displayLapTimer.reset();
    m_displayLapTimerRaceNum = -1;

    // Clear leader timing points
    m_leaderTimingPoints.clear();
    m_lastValidOfficialGap.clear();

    // Clear telemetry data
    m_bikeTelemetry = BikeTelemetryData();
    m_inputTelemetry = InputTelemetryData();
    m_historyBuffers.clear();

    m_currentSessionTime = 0;
    m_playerRaceNum = -1;
    m_bPlayerRaceNumValid = false;
    m_bPlayerNotFoundWarned = false;
    m_bWaitingForPlayerEntry = false;
    m_iPendingPlayerRaceNum = -1;
    m_bPlayerIsRunning = false;
    m_drawState = 0;  // Reset to ON_TRACK
    m_spectatedRaceNum = -1;  // Reset spectated rider
    DEBUG_INFO("Plugin data cleared");
}

// Direct call to HudManager, no callback/observer overhead
void PluginData::notifyHudManager(DataChangeType changeType) {
    HudManager::getInstance().onDataChanged(changeType);
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

void PluginData::updateDebugMetrics(float fps, float pluginTimeMs, float pluginPercent) {
    m_debugMetrics.currentFps = fps;
    m_debugMetrics.pluginTimeMs = pluginTimeMs;
    m_debugMetrics.pluginPercent = pluginPercent;

    // Notify HudManager that debug metrics changed
    notifyHudManager(DataChangeType::DebugMetrics);
}

void PluginData::updateSpeedometer(float speedometer, int gear, int rpm, float fuel) {
    m_bikeTelemetry.speedometer = speedometer;
    m_bikeTelemetry.gear = gear;
    m_bikeTelemetry.rpm = rpm;
    m_bikeTelemetry.fuel = fuel;
    m_bikeTelemetry.isValid = true;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    // This saves ~200 deque operations/second at 100Hz physics rate
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        // Add RPM to history (normalize to 0-1 range using limiterRPM as max, clamp to non-negative)
        // Safety: Only normalize if limiterRPM is valid to avoid division by zero
        float normalizedRpm = 0.0f;
        if (m_sessionData.limiterRPM > 0) {
            normalizedRpm = static_cast<float>(std::max(0, rpm)) / static_cast<float>(m_sessionData.limiterRPM);
        }
        m_historyBuffers.addSample(m_historyBuffers.rpm, normalizedRpm);

        // Add gear to history (normalize to 0-1 range using numberOfGears as max)
        // Safety: Only normalize if numberOfGears is valid to avoid division by zero
        float normalizedGear = 0.0f;
        if (m_bikeTelemetry.numberOfGears > 0) {
            normalizedGear = static_cast<float>(std::max(0, gear)) / static_cast<float>(m_bikeTelemetry.numberOfGears);
        }
        m_historyBuffers.addSample(m_historyBuffers.gear, normalizedGear);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::invalidateSpeedometer() {
    m_bikeTelemetry.isValid = false;
    // Notify HudManager so widgets can update to show placeholder
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateRoll(float roll) {
    m_bikeTelemetry.roll = roll;
    // No separate notification - roll updates at same frequency as speedometer
    // which already notifies with InputTelemetry
}

void PluginData::updateSuspensionMaxTravel(float frontMaxTravel, float rearMaxTravel) {
    m_bikeTelemetry.frontSuspMaxTravel = frontMaxTravel;
    m_bikeTelemetry.rearSuspMaxTravel = rearMaxTravel;
    // No notification needed - max travel is set once during bike initialization
}

void PluginData::updateSuspensionLength(float frontLength, float rearLength) {
    m_bikeTelemetry.frontSuspLength = frontLength;
    m_bikeTelemetry.rearSuspLength = rearLength;

    // Calculate compression percentages and add to history
    // Compression = (maxTravel - currentLength) / maxTravel
    // 0% = fully extended, 100% = fully compressed
    float frontCompression = 0.0f;
    float rearCompression = 0.0f;

    if (m_bikeTelemetry.frontSuspMaxTravel > 0) {
        frontCompression = (m_bikeTelemetry.frontSuspMaxTravel - frontLength) / m_bikeTelemetry.frontSuspMaxTravel;
        frontCompression = std::max(0.0f, std::min(1.0f, frontCompression));  // Clamp to 0-1
    }

    if (m_bikeTelemetry.rearSuspMaxTravel > 0) {
        rearCompression = (m_bikeTelemetry.rearSuspMaxTravel - rearLength) / m_bikeTelemetry.rearSuspMaxTravel;
        rearCompression = std::max(0.0f, std::min(1.0f, rearCompression));  // Clamp to 0-1
    }

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        m_historyBuffers.addSample(m_historyBuffers.frontSusp, frontCompression);
        m_historyBuffers.addSample(m_historyBuffers.rearSusp, rearCompression);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateInputTelemetry(float steer, float throttle, float frontBrake, float rearBrake, float clutch) {
    // Update telemetry data (processed bike inputs)
    m_inputTelemetry.steer = steer;
    m_inputTelemetry.throttle = throttle;
    m_inputTelemetry.frontBrake = frontBrake;
    m_inputTelemetry.rearBrake = rearBrake;
    m_inputTelemetry.clutch = clutch;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        m_historyBuffers.addSample(m_historyBuffers.throttle, throttle);
        m_historyBuffers.addSample(m_historyBuffers.frontBrake, frontBrake);
        m_historyBuffers.addSample(m_historyBuffers.rearBrake, rearBrake);
        m_historyBuffers.addSample(m_historyBuffers.clutch, clutch);
        m_historyBuffers.addSample(m_historyBuffers.steer, steer);
    }

    // Notify HudManager that input telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateRaceVehicleTelemetry(float speedometer, int gear, int rpm, float throttle, float frontBrake, float lean) {
    // Update current values (for widgets that display latest value)
    m_bikeTelemetry.speedometer = speedometer;
    m_bikeTelemetry.gear = gear;
    m_bikeTelemetry.rpm = rpm;
    m_bikeTelemetry.roll = lean;  // Lean angle available in RaceVehicleData
    m_bikeTelemetry.isValid = true;

    m_inputTelemetry.throttle = throttle;
    m_inputTelemetry.frontBrake = frontBrake;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    // Only add to history for data that's actually available in SPluginsRaceVehicleData_t
    // Other buffers (rearBrake, clutch, steer, fuel, suspension) are not updated
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        float normalizedRpm = 0.0f;
        if (m_sessionData.limiterRPM > 0) {
            normalizedRpm = static_cast<float>(std::max(0, rpm)) / static_cast<float>(m_sessionData.limiterRPM);
        }
        m_historyBuffers.addSample(m_historyBuffers.rpm, normalizedRpm);

        float normalizedGear = 0.0f;
        if (m_bikeTelemetry.numberOfGears > 0) {
            normalizedGear = static_cast<float>(std::max(0, gear)) / static_cast<float>(m_bikeTelemetry.numberOfGears);
        }
        m_historyBuffers.addSample(m_historyBuffers.gear, normalizedGear);

        m_historyBuffers.addSample(m_historyBuffers.throttle, throttle);
        m_historyBuffers.addSample(m_historyBuffers.frontBrake, frontBrake);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateXInputData(const XInputData& xinputData) {
    // Update XInput data (raw controller inputs)
    m_inputTelemetry.leftStickX = xinputData.leftStickX;
    m_inputTelemetry.leftStickY = xinputData.leftStickY;
    m_inputTelemetry.rightStickX = xinputData.rightStickX;
    m_inputTelemetry.rightStickY = xinputData.rightStickY;
    m_inputTelemetry.leftTrigger = xinputData.leftTrigger;
    m_inputTelemetry.rightTrigger = xinputData.rightTrigger;
    m_inputTelemetry.xinputConnected = xinputData.isConnected;

    // Add both sticks to history
    m_historyBuffers.addStickSample(m_historyBuffers.leftStick, xinputData.leftStickX, xinputData.leftStickY);
    m_historyBuffers.addStickSample(m_historyBuffers.rightStick, xinputData.rightStickX, xinputData.rightStickY);

    // Notify HudManager that input telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

bool PluginData::isRaceSession() const {
    using namespace PluginConstants::Session;
    using namespace PluginConstants::EventType;

    int eventType = m_sessionData.eventType;
    int session = m_sessionData.session;

    // Straight Rhythm events use different session values
    // WARMUP (5) conflicts with SR_FINAL (5), so check event type first
    if (eventType == STRAIGHT_RHYTHM) {
        // Straight Rhythm race sessions
        return (session == SR_ROUND || session == SR_QUARTER_FINALS ||
                session == SR_SEMI_FINALS || session == SR_FINAL);
    } else {
        // Regular race sessions (not practice, qualify, or warmup)
        return (session == RACE_1 || session == RACE_2);
    }
}

bool PluginData::isQualifySession() const {
    using namespace PluginConstants::Session;
    using namespace PluginConstants::EventType;

    int eventType = m_sessionData.eventType;
    int session = m_sessionData.session;

    // Straight Rhythm doesn't have qualifying sessions
    if (eventType == STRAIGHT_RHYTHM) {
        return false;
    }

    // Regular qualifying sessions
    return (session == PRE_QUALIFY || session == QUALIFY_PRACTICE || session == QUALIFY);
}
