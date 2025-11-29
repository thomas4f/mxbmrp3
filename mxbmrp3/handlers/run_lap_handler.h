// ============================================================================
// handlers/run_lap_handler.h
// Processes player lap timing data (RunLap events are player-only)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RunLapHandler {
public:
    static RunLapHandler& getInstance();

    void handleRunLap(SPluginsBikeLap_t* psLapData);

private:
    RunLapHandler() {}
    ~RunLapHandler() {}
    RunLapHandler(const RunLapHandler&) = delete;
    RunLapHandler& operator=(const RunLapHandler&) = delete;
};
