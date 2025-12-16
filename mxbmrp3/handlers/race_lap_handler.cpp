// ============================================================================
// handlers/race_lap_handler.cpp
// Processes race lap timing data for all riders
// ============================================================================
#include "race_lap_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceLapHandler)

void RaceLapHandler::handleRaceLap(SPluginsRaceLap_t* psRaceLap) {
    HANDLER_NULL_CHECK(psRaceLap);

    // Debug logging: log all RaceLap data
    DEBUG_INFO_F("RaceLap: session=%d, raceNum=%d, lapNum=%d, invalid=%d, lapTime=%d, split[0]=%d, split[1]=%d, best=%d",
                 psRaceLap->m_iSession,
                 psRaceLap->m_iRaceNum,
                 psRaceLap->m_iLapNum,
                 psRaceLap->m_iInvalid,
                 psRaceLap->m_iLapTime,
                 psRaceLap->m_aiSplit[0],
                 psRaceLap->m_aiSplit[1],
                 psRaceLap->m_iBest);

    // RaceLap events fire for ALL riders (includes spectated players)
    // Process lap data for all riders to support spectate mode
    PluginData& data = PluginData::getInstance();

    // Filter out historical lap events from previous sessions
    // When joining mid-race, the game sends RaceLap events from earlier sessions
    // which would pollute our lap log with stale data
    int currentSession = data.getSessionData().session;
    if (psRaceLap->m_iSession != currentSession) {
        DEBUG_INFO_F("RaceLap: Ignoring event from session %d (current session is %d)",
                     psRaceLap->m_iSession, currentSession);
        return;
    }

    int raceNum = psRaceLap->m_iRaceNum;

    // Lap log only uses RaceLap event data (m_aiSplit) - no RaceSplit integration
    // This keeps it simple and consistent with historical lap data when joining mid-race
    int split1 = psRaceLap->m_aiSplit[0];
    int split2 = psRaceLap->m_aiSplit[1];
    int lapTime = psRaceLap->m_iLapTime;

    // Check if timing data is present and consistent (splits monotonically increasing)
    bool hasValidTimingData = (lapTime > 0) &&
                              (split1 > 0) &&
                              (split2 > split1) &&
                              (lapTime > split2);

    // Convert accumulated split times to sector times
    // Only zero out if timing data is actually malformed/missing
    int sector1 = hasValidTimingData ? split1 : 0;
    int sector2 = hasValidTimingData ? (split2 - split1) : 0;
    int sector3 = hasValidTimingData ? (lapTime - split2) : 0;
    lapTime = hasValidTimingData ? lapTime : 0;

    // Determine if lap is valid for session best / PB purposes
    // API behavior differs by session type:
    //   - Non-race (practice/warmup): invalid laps have lapTime=0, m_iInvalid is always 0
    //   - Race: invalid laps have m_iInvalid=1 but timing data is preserved
    // A lap is valid only if timing data exists AND m_iInvalid flag is not set
    bool isLapValid = hasValidTimingData && !psRaceLap->m_iInvalid;

    // Lap number that just finished (API gives lap we're NOW on, so subtract 1)
    int completedLapNum = psRaceLap->m_iLapNum - 1;

    // Update session data for ALL completed laps (so TimingHud can detect them)
    // Best sectors only updated for valid laps; invalid laps still trigger detection
    data.updateSessionBest(raceNum, completedLapNum, lapTime, sector1, sector2, sector3, isLapValid);

    // Add completed lap to log (both valid and invalid laps)
    // Invalid laps in non-race: show placeholders (no timing data)
    // Invalid laps in race: show muted times (timing data preserved)
    LapLogEntry completedLap(
        completedLapNum,
        sector1,
        sector2,
        sector3,
        lapTime,
        isLapValid,
        true                        // isComplete
    );

    data.updateLapLog(raceNum, completedLap);

    // If this was a new best lap, also store it separately for easy access
    // m_iBest: 1 = personal best, 2 = overall best (either way, update our personal best)
    if (psRaceLap->m_iBest > 0) {
        data.setBestLapEntry(raceNum, completedLap);
    }

    // Initialize the next lap (clears splits but sets lap number for upcoming splits)
    // m_iLapNum is the lap we just started (after completing the previous lap)
    // This must happen for ALL laps (valid or invalid) to keep lap numbering in sync
    data.setCurrentLapNumber(raceNum, psRaceLap->m_iLapNum);

    // Reset centralized lap timer for new lap (start timing from 0)
    // m_iLapNum is the new lap number we're starting
    data.resetLapTimerForNewLap(raceNum, psRaceLap->m_iLapNum);
}
