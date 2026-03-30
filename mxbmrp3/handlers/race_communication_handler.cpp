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
    // This handler processes two types of events:
    //
    // 1. State Changes (communicationType == 1):
    //    - DNS (state=1), Retired (state=3), DSQ (state=4)
    //    - Updates rider state in standings immediately
    //
    // 2. Penalties (communicationType == CommunicationType::PENALTY):
    //    - offence indicates type: 1=jump start, 2=cutting
    //    - penaltyTime is ALWAYS 0 (API bug - do not use)
    //    - Logged for debugging only
    //    - Actual penalty totals come from RaceClassification event
    //
    // NOTE: We do not read penalty amounts from this event because:
    //   - penaltyTime field is broken (always 0)
    //   - RaceClassification provides accurate penalty totals
    //   - Reading undocumented fields would require unsafe pointer arithmetic
    // ============================================================================

    auto& pluginData = PluginData::getInstance();
    const StandingsData* currentStanding = pluginData.getStanding(psRaceCommunication->raceNum);

    if (!currentStanding) {
        // No standing found - rider may not have been classified yet
        return;
    }

    // Handle state change (DNS, Retired, DSQ)
    if (psRaceCommunication->commType == Unified::CommunicationType::StateChange) {
        DEBUG_INFO_F("Updating rider #%d state to %d", psRaceCommunication->raceNum, psRaceCommunication->state);

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
    // Handle penalty - just log it (RaceClassification already provides total penalties)
    else if (psRaceCommunication->commType == Unified::CommunicationType::Penalty) {
        const char* offenceStr = PluginUtils::getOffenceString(psRaceCommunication->offence);
        DEBUG_INFO_F("Penalty given to rider #%d for %s (penalty amount will be updated by RaceClassification)",
            psRaceCommunication->raceNum, offenceStr);

        // Event log: penalty (offence is always "cutting" in MX Bikes, so just log the event)
        const RaceEntryData* entry = pluginData.getRaceEntry(psRaceCommunication->raceNum);
        const char* riderLabel = entry ? entry->formattedRaceNum : "???";
        char eventMsg[64];
        snprintf(eventMsg, sizeof(eventMsg), "%s received penalty", riderLabel);
        pluginData.addEventLogEntry(EventLogType::Penalty, eventMsg);

        // Record penalty count in stats (player only)
        // NOTE: penaltyTime from this event is always 0 (API bug).
        // Penalty time is tracked via updatePenaltyFromStandings() in batchUpdateStandings.
        if (psRaceCommunication->raceNum == pluginData.getPlayerRaceNum()) {
            StatsManager::getInstance().recordPenalty();
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
