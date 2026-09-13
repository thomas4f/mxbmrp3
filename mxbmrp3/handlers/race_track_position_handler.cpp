// ============================================================================
// handlers/race_track_position_handler.cpp
// Processes race track position data for all riders
// ============================================================================
#include "race_track_position_handler.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/hud_manager.h"
#include "../core/spotter_manager.h"

void Handlers::handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition) {
    // Defensive null check and bounds validation
    if (!pasRaceTrackPosition || iNumVehicles <= 0) return;

    // Forward rider positions to map HUD (fast path - no processing)
    HudManager::getInstance().updateRiderPositions(iNumVehicles, pasRaceTrackPosition);

    PluginData& pluginData = PluginData::getInstance();
    int sessionTime = pluginData.getSessionTime();

    // Track which riders are in this batch (for stale gap detection)
    pluginData.updateActiveTrackPosRiders(iNumVehicles, pasRaceTrackPosition);

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

    // Spotter proximity/hazard detectors tick on this batch. BEFORE the
    // race-session early-return below: the spotter also calls practice and
    // qualifying (blue flags, hazards, riders closing). One bool test when
    // the spotter is disabled.
    SpotterManager::getInstance().onTrackPositions(iNumVehicles,
                                                   pasRaceTrackPosition);

    const SessionData& sessionData = pluginData.getSessionData();
    const bool inProgress =
        (sessionData.sessionState & PluginConstants::SessionState::IN_PROGRESS) != 0;

    // Roost. ANY session in progress, not races only: following someone round a
    // practice session is the same riding, and the row's sentence never said
    // race - the only thing that implied it was the page it sits on. What the
    // gate still keeps out is a grid: a race's pre-start is not IN_PROGRESS.
    //
    // And only while the player is RIDING: a replay and a spectated race deliver
    // the same batches with the player's own bike among them, and a bike that is
    // not moving is not riding (that half is in recordProximity, which is where
    // the speed is). Ended rather than paused, like every other gap here.
    if (inProgress && pluginData.isPlayerRunning()) {
        pluginData.updateProximity(iNumVehicles, pasRaceTrackPosition);
    } else {
        pluginData.endProximity();
    }

    // Live gaps stay RACE-only: they are the standings' running order, and there
    // is no order to keep in a practice session.
    if (!pluginData.isRaceSession() || !inProgress) return;
    pluginData.updateRealTimeGaps();
}
