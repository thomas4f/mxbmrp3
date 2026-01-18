// ============================================================================
// handlers/race_track_position_handler.h
// Processes race track position data for all riders
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceTrackPositionHandler {
public:
    static RaceTrackPositionHandler& getInstance();

    void handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition);

private:
    RaceTrackPositionHandler() {}
    ~RaceTrackPositionHandler() {}
    RaceTrackPositionHandler(const RaceTrackPositionHandler&) = delete;
    RaceTrackPositionHandler& operator=(const RaceTrackPositionHandler&) = delete;
};
