// ============================================================================
// handlers/race_lap_handler.h
// Processes race lap timing data for all riders
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceLapHandler {
public:
    static RaceLapHandler& getInstance();

    void handleRaceLap(Unified::RaceLapData* psRaceLap);

private:
    RaceLapHandler() {}
    ~RaceLapHandler() {}
    RaceLapHandler(const RaceLapHandler&) = delete;
    RaceLapHandler& operator=(const RaceLapHandler&) = delete;
};
