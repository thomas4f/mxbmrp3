// ============================================================================
// handlers/race_track_position_handler.cpp
// Processes race track position data for all riders
// ============================================================================
#include "race_track_position_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/hud_manager.h"

DEFINE_HANDLER_SINGLETON(RaceTrackPositionHandler)

void RaceTrackPositionHandler::handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition) {
    // Defensive null check and bounds validation
    if (!pasRaceTrackPosition || iNumVehicles <= 0) return;

    // Forward rider positions to map HUD (fast path - no processing)
    HudManager::getInstance().updateRiderPositions(iNumVehicles, pasRaceTrackPosition);

    PluginData& pluginData = PluginData::getInstance();
    int sessionTime = pluginData.getSessionTime();

    // Always update track positions (needed for wrong-way detection in all session types)
    for (int i = 0; i < iNumVehicles; ++i) {
        const Unified::TrackPositionData& pos = pasRaceTrackPosition[i];
        const StandingsData* standing = pluginData.getStanding(pos.raceNum);
        int numLaps = standing ? standing->numLaps : 0;

        pluginData.updateTrackPosition(
            pos.raceNum,
            pos.trackPos,
            numLaps,
            pos.crashed,
            sessionTime
        );
    }

    // Only calculate real-time gaps for race sessions in progress
    if (!pluginData.isRaceSession()) {
        return;
    }

    const SessionData& sessionData = pluginData.getSessionData();
    if (!(sessionData.sessionState & PluginConstants::SessionState::IN_PROGRESS)) {
        return;
    }

    pluginData.updateRealTimeGaps();
}
