// ============================================================================
// handlers/race_lap_handler.cpp
// Processes race lap timing data for all riders
// ============================================================================
#include "race_lap_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/personal_best_manager.h"
#include "../core/hud_manager.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include <ctime>

DEFINE_HANDLER_SINGLETON(RaceLapHandler)

void RaceLapHandler::handleRaceLap(Unified::RaceLapData* psRaceLap) {
    HANDLER_NULL_CHECK(psRaceLap);

    // =========================================================================
    // API Lap Numbering Convention:
    // lapNum is 1-indexed and represents the lap just completed.
    // For a 3-lap race, RaceLap fires 3 times with lapNum = 1, 2, 3.
    //
    // Usage in this handler:
    // - Finish detection (line ~62): use lapNum directly (1-indexed count)
    // - Lap log storage (line ~105): subtract 1 for 0-indexed internal storage
    // - New lap setup (lines ~158, ~162): lapNum is the new lap starting
    // =========================================================================

    // Debug logging: log all RaceLap data
    // DEBUG_INFO_F("RaceLap: session=%d, raceNum=%d, lapNum=%d, invalid=%d, lapTime=%d, split[0]=%d, split[1]=%d, best=%d",
    //              psRaceLap->session,
    //              psRaceLap->raceNum,
    //              psRaceLap->lapNum,
    //              psRaceLap->invalid ? 1 : 0,
    //              psRaceLap->lapTime,
    //              psRaceLap->splits[0],
    //              psRaceLap->splits[1],
    //              psRaceLap->bestFlag);

    // RaceLap events fire for ALL riders (includes spectated players)
    // Process lap data for all riders to support spectate mode
    PluginData& data = PluginData::getInstance();
    const SessionData& sessionData = data.getSessionData();

    // Filter out historical lap events from previous sessions
    // When joining mid-race, the game sends RaceLap events from earlier sessions
    // which would pollute our lap log with stale data
    int currentSession = sessionData.session;
    if (psRaceLap->session != currentSession) {
        DEBUG_INFO_F("RaceLap: Ignoring event from session %d (current session is %d)",
                     psRaceLap->session, currentSession);
        return;
    }

    // Log lap completion in context of race finish for lap-based races
    if (sessionData.sessionNumLaps > 0) {
        // lapNum is the lap just COMPLETED (not the lap we're starting)
        // This is clear from pure lap races: 3 RaceLap events for a 3-lap race
        int completedLapNum = psRaceLap->lapNum;

        // Determine finish status based on race type
        // For timed+laps: finishLap set during overtime, finish when completed > finishLap
        // For pure lap: finish when completedLap >= sessionNumLaps
        bool isFinished = false;
        bool isLastLap = false;
        if (sessionData.sessionLength > 0) {
            // Timed+laps race: use finishLap (set during overtime)
            isFinished = sessionData.finishLap > 0 && completedLapNum > sessionData.finishLap;
            isLastLap = sessionData.finishLap > 0 && completedLapNum == sessionData.finishLap;
        } else {
            // Pure lap race: completedLap = laps finished so far
            isFinished = completedLapNum >= sessionData.sessionNumLaps;
            isLastLap = completedLapNum == sessionData.sessionNumLaps - 1;
        }

        DEBUG_INFO_F("[LAP COMPLETE] raceNum=%d completed lap %d of %d, finishLap=%d, overtimeStarted=%d, isLastLap=%s, isFinished=%s",
            psRaceLap->raceNum,
            completedLapNum,
            sessionData.sessionNumLaps,
            sessionData.finishLap,
            sessionData.overtimeStarted ? 1 : 0,
            isLastLap ? "YES" : "NO",
            isFinished ? "YES" : "NO");
    }

    int raceNum = psRaceLap->raceNum;

    // Lap log only uses RaceLap event data (splits) - no RaceSplit integration
    // This keeps it simple and consistent with historical lap data when joining mid-race
    int lapTime = psRaceLap->lapTime;
    int splitCount = psRaceLap->splitCount;  // Use dynamic split count (2 for MX Bikes, 3 for GP Bikes)

    // Extract splits based on actual count
    int split1 = splitCount >= 1 ? psRaceLap->splits[0] : 0;
    int split2 = splitCount >= 2 ? psRaceLap->splits[1] : 0;
    int split3 = splitCount >= 3 ? psRaceLap->splits[2] : 0;

    // Check if timing data is present and consistent (splits monotonically increasing)
    // For 2-split games: s1 < s2 < lapTime
    // For 3-split games: s1 < s2 < s3 < lapTime
    bool hasValidTimingData = (lapTime > 0) && (split1 > 0);
    if (hasValidTimingData && splitCount >= 2) {
        hasValidTimingData = (split2 > split1);
    }
    if (hasValidTimingData && splitCount >= 3) {
        hasValidTimingData = (split3 > split2) && (lapTime > split3);
    } else if (hasValidTimingData) {
        hasValidTimingData = (lapTime > split2);
    }

    // Convert accumulated split times to sector times
    // Only zero out if timing data is actually malformed/missing
    // For 2-split games (MX Bikes): S1, S2, S3 (final sector)
    // For 3-split games (GP Bikes): S1, S2, S3, S4 (final sector)
    int sector1 = hasValidTimingData ? split1 : 0;
    int sector2 = hasValidTimingData && splitCount >= 2 ? (split2 - split1) : 0;
    int sector3 = 0;
    int sector4 = -1;  // Only valid for 4-sector games (GP Bikes)
    if (hasValidTimingData) {
        if (splitCount >= 3) {
            sector3 = split3 - split2;  // Third sector for GP Bikes
            sector4 = lapTime - split3; // Fourth/final sector for GP Bikes
        } else if (splitCount >= 2) {
            sector3 = lapTime - split2;  // Third/final sector for MX Bikes
        }
    }
    lapTime = hasValidTimingData ? lapTime : 0;

    // Determine if lap is valid for session best / PB purposes
    // API behavior differs by session type:
    //   - Non-race (practice/warmup): invalid laps have lapTime=0, invalid is always false
    //   - Race: invalid laps have invalid=true but timing data is preserved
    // A lap is valid only if timing data exists AND invalid flag is not set
    bool isLapValid = hasValidTimingData && !psRaceLap->invalid;

    // Convert to 0-indexed for internal storage (see lap numbering convention above)
    int completedLapNumZeroIndexed = psRaceLap->lapNum - 1;

    // Update ideal lap data for ALL completed laps (so TimingHud can detect them)
    // Best sectors only updated for valid laps; invalid laps still trigger detection
    data.updateIdealLap(raceNum, completedLapNumZeroIndexed, lapTime, sector1, sector2, sector3, sector4, isLapValid);

    // Add completed lap to log (both valid and invalid laps)
    // Invalid laps in non-race: show placeholders (no timing data)
    // Invalid laps in race: show muted times (timing data preserved)
    LapLogEntry completedLap(
        completedLapNumZeroIndexed,
        sector1,
        sector2,
        sector3,
        sector4,
        lapTime,
        isLapValid,
        true                        // isComplete
    );

    data.updateLapLog(raceNum, completedLap);

    // If this was a new best lap, also store it separately for easy access
    // bestFlag: 1 = personal best, 2 = overall best (either way, update our personal best)
    if (psRaceLap->bestFlag > 0) {
        data.setBestLapEntry(raceNum, completedLap);

        // If this is the overall best lap (bestFlag == 2), store it with splits
        // for gap comparison at splits (not just lap completion)
        if (psRaceLap->bestFlag == 2) {
            data.setOverallBestLap(completedLap);
        }

        // Check if this is the local player and potentially update all-time PB
        // Only valid laps from the local player are candidates for all-time PB
        int playerRaceNum = data.getPlayerRaceNum();
        if (raceNum == playerRaceNum && isLapValid) {
            PersonalBestEntry pbEntry;
            pbEntry.trackId = sessionData.trackId;
            pbEntry.bikeName = sessionData.bikeName;
            pbEntry.lapTime = lapTime;
            pbEntry.sector1 = sector1;
            pbEntry.sector2 = sector2;
            pbEntry.sector3 = sector3;
            pbEntry.sector4 = sector4;
            pbEntry.setupName = sessionData.setupFileName;
            pbEntry.conditions = sessionData.conditions;
            pbEntry.timestamp = std::time(nullptr);

            // updatePersonalBest only saves if this beats the existing all-time PB
            bool newPB = PersonalBestManager::getInstance().updatePersonalBest(pbEntry);
#if GAME_HAS_RECORDS_PROVIDER
            if (newPB) {
                // Notify RecordsHud to refresh player's position in the leaderboard
                HudManager::getInstance().getRecordsHud().setDataDirty();
            }
#else
            (void)newPB;  // Suppress unused warning when records provider not available
#endif
        }
    }

    // Initialize tracking for the next lap (clears splits, sets lap number)
    // After completing lap N, we're now on lap N+1, but the API gives us N
    // So setCurrentLapNumber(N) sets us to be tracking lap N (the one just starting)
    data.setCurrentLapNumber(raceNum, psRaceLap->lapNum);

    // Reset centralized lap timer for new lap (start timing from 0)
    // lapNum is the new lap number we're starting
    data.resetLapTimerForNewLap(raceNum, psRaceLap->lapNum);
}
