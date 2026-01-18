// ============================================================================
// handlers/run_lap_handler.h
// Processes player lap timing data (RunLap events are player-only)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RunLapHandler {
public:
    static RunLapHandler& getInstance();

    void handleRunLap(Unified::PlayerLapData* psLapData);

private:
    RunLapHandler() {}
    ~RunLapHandler() {}
    RunLapHandler(const RunLapHandler&) = delete;
    RunLapHandler& operator=(const RunLapHandler&) = delete;
};
