// ============================================================================
// handlers/race_lap_handler.h
// Processes race lap timing data for all riders
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceLapHandler {
public:
    static RaceLapHandler& getInstance();

    void handleRaceLap(SPluginsRaceLap_t* psRaceLap);

private:
    RaceLapHandler() {}
    ~RaceLapHandler() {}
    RaceLapHandler(const RaceLapHandler&) = delete;
    RaceLapHandler& operator=(const RaceLapHandler&) = delete;
};
