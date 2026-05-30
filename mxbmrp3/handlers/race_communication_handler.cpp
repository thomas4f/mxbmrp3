// ============================================================================
// handlers/race_communication_handler.cpp
// Processes race communication messages (penalties, warnings)
// ============================================================================
#include "race_communication_handler.h"
#include "../core/handler_singleton.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/stats_manager.h"

using namespace PluginConstants;

DEFINE_HANDLER_SINGLETON(RaceCommunicationHandler)

void RaceCommunicationHandler::handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication) {
    if (!psRaceCommunication) {
        DEBUG_WARN("handleRaceCommunication called with NULL pointer");
        return;
    }

    // ============================================================================
    // RaceCommunication API Event Types
    // ============================================================================
    // 1. State Changes (commType == StateChange):
    //    - DNS (state=1), Retired (state=3), DSQ (state=4)
    //    - Updates rider state in standings immediately
    //
    // 2. Penalties (commType == Penalty):
    //    - offence indicates type: 1=jump start, 2=cutting
    //    - penaltyTime is the penalty amount in milliseconds
    //
    // 3/4. Penalty Clear / Change (GP Bikes, WRS, KRP only):
    //    - Logged to event log; penalty totals refresh from RaceClassification
    // ============================================================================

    auto& pluginData = PluginData::getInstance();
    const StandingsData* currentStanding = pluginData.getStanding(psRaceCommunication->raceNum);

    if (!currentStanding) {
        // No standing found - rider may not have been classified yet
        return;
    }

    // Handle state change (DNS, Retired, DSQ)
    if (psRaceCommunication->commType == Unified::CommunicationType::StateChange) {
        const char* stateStr = "Unknown";
        switch (psRaceCommunication->state) {
            case Unified::EntryState::DNS:     stateStr = "DNS"; break;
            case Unified::EntryState::Retired: stateStr = "Retired"; break;
            case Unified::EntryState::DSQ:     stateStr = "DSQ"; break;
            default: break;
        }
        DEBUG_INFO_F("Updating rider #%d state to %d (%s)",
            psRaceCommunication->raceNum, static_cast<int>(psRaceCommunication->state), stateStr);

        // Event log: rider state change (log before updateStandings to match handler convention)
        const RaceEntryData* entry = pluginData.getRaceEntry(psRaceCommunication->raceNum);
        const char* riderLabel = entry ? entry->formattedRaceNum : "???";
        char eventMsg[64];
        switch (psRaceCommunication->state) {
        case Unified::EntryState::DNS:
            snprintf(eventMsg, sizeof(eventMsg), "%s did not start", riderLabel);
            pluginData.addEventLogEntry(EventLogType::RiderDNS, eventMsg);
            break;
        case Unified::EntryState::Retired:
            snprintf(eventMsg, sizeof(eventMsg), "%s retired", riderLabel);
            pluginData.addEventLogEntry(EventLogType::RiderRetired, eventMsg);
            break;
        case Unified::EntryState::DSQ: {
            snprintf(eventMsg, sizeof(eventMsg), "%s disqualified", riderLabel);
            pluginData.addEventLogEntry(EventLogType::RiderDSQ, eventMsg);
            break;
        }
        default:
            break;
        }

        // Update standings with new state (notify immediately for individual state changes)
        pluginData.updateStandings(
            currentStanding->raceNum,
            static_cast<int>(psRaceCommunication->state),  // New state
            currentStanding->bestLap,
            currentStanding->bestLapNum,
            currentStanding->numLaps,
            currentStanding->gap,
            currentStanding->gapLaps,
            currentStanding->penalty,
            currentStanding->pit,
            true  // Notify immediately
        );
    }
    // Handle penalty - record amount and log to event log
    else if (psRaceCommunication->commType == Unified::CommunicationType::Penalty) {
        const char* offenceStr = PluginUtils::getOffenceString(psRaceCommunication->offence);
        int penaltyMs = psRaceCommunication->penaltyTime;
        DEBUG_INFO_F("Penalty given to rider #%d for %s (%dms)",
            psRaceCommunication->raceNum, offenceStr, penaltyMs);

        // Regression detector: time penalties used to ship with penaltyTime==0
        // (the API bug we worked around). If a TimePenalty event arrives with
        // a zero amount on a current build, that's a signal the API may have
        // regressed. Warn once per process to flag it without spamming.
        if (psRaceCommunication->penaltyType == Unified::PenaltyType::TimePenalty &&
            penaltyMs == 0) {
            static bool s_zeroPenaltyWarned = false;
            if (!s_zeroPenaltyWarned) {
                s_zeroPenaltyWarned = true;
                DEBUG_WARN("RaceCommunication: TimePenalty event with penaltyTime=0 - "
                           "API may have regressed (was historically always 0). "
                           "This warning fires once per process.");
            }
        }

        // Event log: e.g., "#4 5s penalty (Cutting)" or "#4 penalty (Cutting)" when no time
        const RaceEntryData* entry = pluginData.getRaceEntry(psRaceCommunication->raceNum);
        const char* riderLabel = entry ? entry->formattedRaceNum : "???";
        char eventMsg[64];
        if (penaltyMs > 0) {
            int penaltySeconds = (penaltyMs + 500) / 1000;  // Round to nearest second
            snprintf(eventMsg, sizeof(eventMsg), "%s %ds penalty (%s)",
                     riderLabel, penaltySeconds, offenceStr);
        } else {
            snprintf(eventMsg, sizeof(eventMsg), "%s penalty (%s)", riderLabel, offenceStr);
        }
        pluginData.addEventLogEntry(EventLogType::Penalty, eventMsg);

        // Record penalty in stats (player only) - count + time in one call
        if (psRaceCommunication->raceNum == pluginData.getPlayerRaceNum()) {
            StatsManager::getInstance().recordPenalty(penaltyMs, pluginData.isRaceSession());
        }
    }
    // Handle penalty clear (GP Bikes, WRS, KRP only)
    else if (psRaceCommunication->commType == Unified::CommunicationType::PenaltyClear) {
        const RaceEntryData* entry = pluginData.getRaceEntry(psRaceCommunication->raceNum);
        const char* riderLabel = entry ? entry->formattedRaceNum : "???";
        char eventMsg[64];
        snprintf(eventMsg, sizeof(eventMsg), "%s penalty cleared", riderLabel);
        pluginData.addEventLogEntry(EventLogType::PenaltyClear, eventMsg);
    }
    // Handle penalty change (GP Bikes, WRS, KRP only)
    else if (psRaceCommunication->commType == Unified::CommunicationType::PenaltyChange) {
        const RaceEntryData* entry = pluginData.getRaceEntry(psRaceCommunication->raceNum);
        const char* riderLabel = entry ? entry->formattedRaceNum : "???";
        char eventMsg[64];
        snprintf(eventMsg, sizeof(eventMsg), "%s penalty changed", riderLabel);
        pluginData.addEventLogEntry(EventLogType::PenaltyChange, eventMsg);
    }
}
