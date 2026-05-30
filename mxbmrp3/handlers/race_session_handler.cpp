// ============================================================================
// handlers/race_session_handler.cpp
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#include "race_session_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/fmx_manager.h"

DEFINE_HANDLER_SINGLETON(RaceSessionHandler)

// Log "Session started" event with optional format detail (e.g., "03:00 + 2L")
static void logSessionStarted(PluginData& data, const char* sessionStr, int sessionLength, int sessionNumLaps) {
    char eventMsg[64];
    snprintf(eventMsg, sizeof(eventMsg), "%s started", sessionStr);

    bool hasTime = (sessionLength > 0);
    bool hasLaps = (sessionNumLaps > 0);

    if (hasTime || hasLaps) {
        char detail[20];
        if (hasTime && hasLaps) {
            char timeBuf[16];
            PluginUtils::formatTimeMinutesSeconds(sessionLength, timeBuf, sizeof(timeBuf));
            snprintf(detail, sizeof(detail), "%s + %dL", timeBuf, sessionNumLaps);
        } else if (hasTime) {
            PluginUtils::formatTimeMinutesSeconds(sessionLength, detail, sizeof(detail));
        } else {
            snprintf(detail, sizeof(detail), "%d %s", sessionNumLaps,
                     sessionNumLaps == 1 ? "lap" : "laps");
        }
        data.addEventLogEntry(EventLogType::SessionStarted, eventMsg, detail);
    } else {
        data.addEventLogEntry(EventLogType::SessionStarted, eventMsg);
    }
}

void RaceSessionHandler::handleRaceSession(Unified::RaceSessionData* psRaceSession) {
    HANDLER_NULL_CHECK(psRaceSession);

    int eventType = PluginData::getInstance().getSessionData().eventType;
    DEBUG_INFO_F("RaceSession changed: session=%d (%s), state=0x%X (%s), length=%d, numLaps=%d",
        psRaceSession->session, PluginUtils::getSessionString(eventType, psRaceSession->session),
        psRaceSession->sessionState, PluginUtils::getSessionStateString(psRaceSession->sessionState),
        psRaceSession->sessionLength, psRaceSession->sessionNumLaps);

    // Log race format interpretation for debugging
    if (psRaceSession->sessionLength > 0 && psRaceSession->sessionNumLaps > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Timed+Laps race: %d ms + %d extra laps after timer",
            psRaceSession->sessionLength, psRaceSession->sessionNumLaps);
    } else if (psRaceSession->sessionLength > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Pure timed race: %d ms", psRaceSession->sessionLength);
    } else if (psRaceSession->sessionNumLaps > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Pure lap race: %d laps", psRaceSession->sessionNumLaps);
    }

    // Clear session-specific data when a new session starts
    // Note: event log persists across sessions within a race weekend (practice -> qualifying -> race).
    // It is only cleared on full event exit via PluginData::clear().
#if GAME_HAS_FMX
    FmxManager::getInstance().reset();
#endif
    PluginData::getInstance().clearAllIdealLap();
    PluginData::getInstance().clearAllLapLog();
    PluginData::getInstance().clearLiveGapTimingPoints();
    PluginData::getInstance().resetAllLapTimers();

    // Reset race finish tracking (overtime and leader finish time)
    PluginData::getInstance().setOvertimeStarted(false);
    PluginData::getInstance().setFinishLap(-1);
    PluginData::getInstance().setLastSessionTime(0);
    PluginData::getInstance().setLeaderFinishTime(-1);
    PluginData::getInstance().setSessionTimeExpired(false);
    PluginData::getInstance().clearSessionFinished();

    // Update plugin data store
    PluginData::getInstance().setSession(psRaceSession->session);
    PluginData::getInstance().setSessionState(psRaceSession->sessionState);
    PluginData::getInstance().setSessionLength(psRaceSession->sessionLength);
    PluginData::getInstance().setSessionNumLaps(psRaceSession->sessionNumLaps);
    PluginData::getInstance().setConditions(static_cast<int>(psRaceSession->conditions));
    PluginData::getInstance().setAirTemperature(psRaceSession->airTemperature);
    PluginData::getInstance().setTrackTemperature(psRaceSession->trackTemperature);

    // Increment session generation so HUDs can reliably detect new sessions
    // even when session type doesn't change (e.g. Practice→Practice on different track/bike)
    PluginData::getInstance().incrementSessionGeneration();

    // Event log: log initial session state
    // Practice/qualifying arrive directly with state=IN_PROGRESS (16), skipping
    // handleRaceSessionState entirely. Races arrive with PRE_START (256) and later
    // transition to IN_PROGRESS via handleRaceSessionState.
    // eventType persists across sessions within a race event (set by handleEventInit or
    // defaults to Race=2). Safe to use here since RaceSession fires within an active event.
    const char* sessionStr = PluginUtils::getSessionString(
        PluginData::getInstance().getSessionData().eventType, psRaceSession->session);
    if (sessionStr) {
        auto& data = PluginData::getInstance();
        int state = psRaceSession->sessionState;

        if (state & PluginConstants::SessionState::IN_PROGRESS) {
            // Session starts directly in progress (practice/qualifying)
            logSessionStarted(data, sessionStr, psRaceSession->sessionLength, psRaceSession->sessionNumLaps);
        } else if (state & PluginConstants::SessionState::PRE_START) {
            char eventMsg[64];
            snprintf(eventMsg, sizeof(eventMsg), "%s: Pre-Start", sessionStr);
            data.addEventLogEntry(EventLogType::SessionPreStart, eventMsg);
        }
    }
}

void RaceSessionHandler::handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState) {
    HANDLER_NULL_CHECK(psRaceSessionState);

    int eventType = PluginData::getInstance().getSessionData().eventType;
    DEBUG_INFO_F("RaceSessionState changed: session=%d (%s), state=0x%X (%s)",
        psRaceSessionState->session, PluginUtils::getSessionString(eventType, psRaceSessionState->session),
        psRaceSessionState->sessionState, PluginUtils::getSessionStateString(psRaceSessionState->sessionState));

    // When race transitions to "in progress" (state 16), reset timing state
    // This prevents false overtime detection when transitioning from pre-start (256)
    // where sessionTime values during countdown could falsely trigger the positive→negative transition
    // Also clear live gap timing points to prevent stale RTG values from pre-start
    // (track position updates during pre-start would otherwise contaminate RTG calculations)
    //
    // Guard: only reset on initial transition TO state 16, not on re-fires.
    // The game re-sends state 16 when overtime starts, which would wipe out
    // accumulated RTG data mid-race.
    // IMPORTANT: This check must happen BEFORE setSessionState() below,
    // since we compare against the cached (old) state to detect the transition.
    if ((psRaceSessionState->sessionState & PluginConstants::SessionState::IN_PROGRESS) &&
        !(PluginData::getInstance().getSessionData().sessionState & PluginConstants::SessionState::IN_PROGRESS)) {
        PluginData::getInstance().setLastSessionTime(0);
        PluginData::getInstance().clearLiveGapTimingPoints();
    }

    // Event log: session state changes
    {
        auto& data = PluginData::getInstance();
        int oldState = data.getSessionData().sessionState;

        // Only log meaningful transitions (not re-fires of the same state)
        if (psRaceSessionState->sessionState != oldState) {
            const SessionData& sessionData = data.getSessionData();
            const char* sessionStr = PluginUtils::getSessionString(sessionData.eventType, psRaceSessionState->session);
            const char* stateStr = PluginUtils::getSessionStateString(psRaceSessionState->sessionState);

            if (sessionStr && stateStr) {
                // "In Progress" means race/session started (e.g., "Race 1 started (03:00 + 2L)")
                if (psRaceSessionState->sessionState & PluginConstants::SessionState::IN_PROGRESS) {
                    logSessionStarted(data, sessionStr, sessionData.sessionLength, sessionData.sessionNumLaps);
                } else {
                    // Log other state transitions with natural phrasing
                    char eventMsg[64];
                    int state = psRaceSessionState->sessionState;
                    bool isComplete = (state & PluginConstants::SessionState::RACE_OVER) ||
                                      (state & PluginConstants::SessionState::FINISHED);
                    bool isPreStart = (state & PluginConstants::SessionState::PRE_START) ||
                                      (state & PluginConstants::SessionState::SIGHTING_LAP);
                    if (state & PluginConstants::SessionState::RACE_OVER) {
                        snprintf(eventMsg, sizeof(eventMsg), "%s ended", sessionStr);
                    } else if (state & PluginConstants::SessionState::FINISHED) {
                        snprintf(eventMsg, sizeof(eventMsg), "%s complete", sessionStr);
                    } else if (state & PluginConstants::SessionState::CANCELLED) {
                        snprintf(eventMsg, sizeof(eventMsg), "%s cancelled", sessionStr);
                    } else {
                        snprintf(eventMsg, sizeof(eventMsg), "%s: %s", sessionStr, stateStr);
                    }
                    EventLogType eventType = isComplete  ? EventLogType::SessionComplete :
                                             isPreStart  ? EventLogType::SessionPreStart :
                                                           EventLogType::SessionStateChange;
                    data.addEventLogEntry(eventType, eventMsg);
                }
            }
        }
    }

    // Update plugin data store
    // Note: Do NOT update sessionLength here - it changes during the race to countdown/other values
    // We keep the initial sessionLength from RaceSession for race format display
    PluginData::getInstance().setSession(psRaceSessionState->session);
    PluginData::getInstance().setSessionState(psRaceSessionState->sessionState);
}
