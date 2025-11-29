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

using namespace PluginConstants;

DEFINE_HANDLER_SINGLETON(RaceCommunicationHandler)

void RaceCommunicationHandler::handleRaceCommunication(SPluginsRaceCommunication_t* psRaceCommunication, int dataSize) {
    if (!psRaceCommunication) {
        DEBUG_WARN("handleRaceCommunication called with NULL pointer");
        return;
    }

    // ============================================================================
    // RaceCommunication API Event Types
    // ============================================================================
    // This handler processes two types of events:
    //
    // 1. State Changes (m_iCommunication == 1):
    //    - DNS (m_iState=1), Retired (m_iState=3), DSQ (m_iState=4)
    //    - Updates rider state in standings immediately
    //
    // 2. Penalties (m_iCommunication == CommunicationType::PENALTY):
    //    - m_iOffence indicates type: 1=jump start, 2=cutting
    //    - m_iTime is ALWAYS 0 (API bug - do not use)
    //    - Logged for debugging only
    //    - Actual penalty totals come from RaceClassification event
    //
    // NOTE: We do not read penalty amounts from this event because:
    //   - m_iTime field is broken (always 0)
    //   - RaceClassification provides accurate penalty totals (m_iPenalty)
    //   - Reading undocumented fields would require unsafe pointer arithmetic
    // ============================================================================

    auto& pluginData = PluginData::getInstance();
    const StandingsData* currentStanding = pluginData.getStanding(psRaceCommunication->m_iRaceNum);

    if (!currentStanding) {
        // No standing found - rider may not have been classified yet
        return;
    }

    // Handle state change (DNS, Retired)
    if (psRaceCommunication->m_iCommunication == CommunicationType::STATE_CHANGE) {
        DEBUG_INFO_F("Updating rider #%d state to %d", psRaceCommunication->m_iRaceNum, psRaceCommunication->m_iState);

        // Update standings with new state (notify immediately for individual state changes)
        pluginData.updateStandings(
            currentStanding->raceNum,
            psRaceCommunication->m_iState,  // New state
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
    else if (psRaceCommunication->m_iCommunication == CommunicationType::PENALTY) {
        const char* offenceStr = PluginUtils::getOffenceString(psRaceCommunication->m_iOffence);
        DEBUG_INFO_F("Penalty given to rider #%d for %s (penalty amount will be updated by RaceClassification)",
            psRaceCommunication->m_iRaceNum, offenceStr);
    }
}
